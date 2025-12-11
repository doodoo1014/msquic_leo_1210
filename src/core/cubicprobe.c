/*
    CubicBoost V18: Deterministic Physics-based Logic (Fixes Compilation Errors)
    - Fix: Added missing struct members in header.
    - Fix: Removed unused variables.
*/

#include "precomp.h"
#include <stdio.h>
#include "cubicprobe.h" 

// =========================================================================
// Constants
// =========================================================================

#define TEN_TIMES_BETA_CUBIC 7  
#define TEN_TIMES_C_CUBIC 4     

#define PROBE_SENSITIVITY_GAMMA 4       
#define PROBE_MIN_NOISE_MARGIN_US 4000  

// =========================================================================
// Helper Functions
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static uint32_t CubeRoot(uint32_t Radicand)
{
    int i;
    uint32_t x = 0;
    uint32_t y = 0;
    for (i = 30; i >= 0; i -= 3) {
        x = x * 8 + ((Radicand >> i) & 7);
        if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
            y = y * 2 + 1;
        } else {
            y = y * 2;
        }
    }
    return y;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void CubicProbeResetPhysicsState(_In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe)
{
    CubicProbe->MinRttUs = UINT64_MAX;
    
    // Round Tracking Initialization
    CubicProbe->ProbeTargetPacketNumber = 0; 
    CubicProbe->RoundInFlightBytes = 0;
    CubicProbe->RoundStartTime = CxPlatTimeUs64();
    
    // Metrics
    CubicProbe->PrevBandwidth = 0;
    CubicProbe->CurrentElasticity = 0.0;
    
    // Control Flags
    CubicProbe->IsQueueBuilding = FALSE;
    CubicProbe->AckCountForGrowth = 0;
}

// =========================================================================
// Logic 1: Safety Check (Per ACK)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeCheckSafety(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];

    // 1. MinRTT Update
    if (AckEvent->MinRttValid) {
        if (CubicProbe->MinRttUs == UINT64_MAX || AckEvent->MinRtt < CubicProbe->MinRttUs) {
            CubicProbe->MinRttUs = AckEvent->MinRtt;
        }
    }

    // 2. Statistical Veto
    CubicProbe->RttVariance = Path->RttVariance; 
    uint64_t NoiseMargin = PROBE_SENSITIVITY_GAMMA * CubicProbe->RttVariance;
    if (NoiseMargin < PROBE_MIN_NOISE_MARGIN_US) NoiseMargin = PROBE_MIN_NOISE_MARGIN_US;

    uint64_t BaselineRtt = (Path->SmoothedRtt > 0) ? Path->SmoothedRtt : CubicProbe->MinRttUs;
    uint64_t Threshold = BaselineRtt + NoiseMargin;

    if (AckEvent->MinRtt > Threshold) {
        CubicProbe->IsQueueBuilding = TRUE; 
    } else {
        CubicProbe->IsQueueBuilding = FALSE; 
    }

    // Accumulate bytes for Round measurement
    CubicProbe->RoundInFlightBytes += AckEvent->NumRetransmittableBytes;
}

// =========================================================================
// Logic 2: Elasticity Check (Per Round)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeCheckElasticity(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

    // Check Round Completion
    if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
        
        uint64_t TimeNow = AckEvent->TimeNow;
        uint64_t TimeDelta = CxPlatTimeDiff64(CubicProbe->RoundStartTime, TimeNow);
        
        uint64_t CurrentBW = 0;
        if (TimeDelta > 0) {
            CurrentBW = CubicProbe->RoundInFlightBytes * 1000000 / TimeDelta;
        }

        double NewElasticity = 0.0;

        if (CubicProbe->PrevBandwidth > 0) {
            if (CurrentBW > CubicProbe->PrevBandwidth) {
                // Calculate Growth Ratio
                double Ratio = (double)(CurrentBW - CubicProbe->PrevBandwidth) / (double)CubicProbe->PrevBandwidth;
                
                // 10% increase => E=1.0
                NewElasticity = Ratio * 10.0;
                if (NewElasticity > 1.0) NewElasticity = 1.0;
            } else {
                NewElasticity = 0.0;
            }
        }

        CubicProbe->CurrentElasticity = NewElasticity;
        
        if (CubicProbe->CurrentElasticity > 0.01) {
            printf("[Round] E=%.2f, BW=%lu (Prev=%lu), Veto=%d\n", 
                CubicProbe->CurrentElasticity, CurrentBW, CubicProbe->PrevBandwidth, CubicProbe->IsQueueBuilding);
        }

        // Reset for Next Round
        CubicProbe->PrevBandwidth = CurrentBW;
        CubicProbe->RoundInFlightBytes = 0;
        CubicProbe->RoundStartTime = TimeNow;
        CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; 
    }
}

// =========================================================================
// Logic 3: Target Calculation (Mixing)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeUpdate(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _In_ uint16_t DatagramPayloadLength,
    _Out_ uint32_t* AckTarget
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;

    // --- 1. Standard CUBIC (N_cubic) ---
    if (Cubic->TimeOfCongAvoidStart == 0) {
        Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
        if (Cubic->CongestionWindow < Cubic->WindowMax) {
            if (DatagramPayloadLength > 0) {
                uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
                uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
                Cubic->KCubic = CubeRoot(radicand);
                Cubic->KCubic = S_TO_MS(Cubic->KCubic);
                Cubic->KCubic >>= 3; 
            } else {
                Cubic->KCubic = 0;
            }
        } else {
            Cubic->KCubic = 0;
            Cubic->WindowMax = Cubic->CongestionWindow;
        }
    }

    const uint32_t W_max_bytes = Cubic->WindowMax;
    const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
    int64_t TimeDeltaMs = (int64_t)(t_us / 1000) - (int64_t)Cubic->KCubic;
    int64_t CubicTerm = ((((TimeDeltaMs * TimeDeltaMs) >> 10) * TimeDeltaMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

    uint32_t W_cubic_bytes;
    if (TimeDeltaMs < 0) {
        W_cubic_bytes = W_max_bytes - (uint32_t)(-CubicTerm);
    } else {
        W_cubic_bytes = W_max_bytes + (uint32_t)CubicTerm;
    }

    uint32_t N_cubic;
    if (W_cubic_bytes > Cubic->CongestionWindow) {
        uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
        uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
        uint32_t DiffSegments = (TargetSegments > CwndSegments) ? (TargetSegments - CwndSegments) : 1;
        N_cubic = CwndSegments / DiffSegments;
    } else {
        N_cubic = 100 * (Cubic->CongestionWindow / DatagramPayloadLength); 
    }

    // --- 2. Protocol References ---
    uint32_t N_reno = Cubic->CongestionWindow / DatagramPayloadLength;
    if (N_reno < 1) N_reno = 1;
    uint32_t N_ss = 1;

    // --- 3. Scenario-based Logic ---
    if (CubicProbe->IsQueueBuilding) {
        *AckTarget = N_cubic; 
    }
    else {
        double E = CubicProbe->CurrentElasticity;
        
        // Target = (1-E)*N_reno + E*N_ss
        double TargetDouble = ((1.0 - E) * (double)N_reno) + (E * (double)N_ss);
        
        *AckTarget = (uint32_t)TargetDouble;
        
        if (*AckTarget > N_cubic) *AckTarget = N_cubic;
        if (*AckTarget < 1) *AckTarget = 1;
    }

    if (*AckTarget < N_cubic && !CubicProbe->IsQueueBuilding) {
        printf("[Boost] N_cubic=%u -> Target=%u (E=%.2f, Veto=0)\n", N_cubic, *AckTarget, CubicProbe->CurrentElasticity);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeIncreaseWindow(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _In_ uint32_t AckTarget,
    _In_ uint16_t DatagramPayloadLength
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

    uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
    CubicProbe->AckCountForGrowth += AckedSegments;

    if (CubicProbe->AckCountForGrowth >= AckTarget) {
        
        uint32_t GrowthSegments = CubicProbe->AckCountForGrowth / AckTarget;
        uint32_t PrevCwnd = Cubic->CongestionWindow;
        
        Cubic->CongestionWindow += (GrowthSegments * DatagramPayloadLength);
        CubicProbe->AckCountForGrowth %= AckTarget;

        printf("[CubicProbe][%p][%.3fms] CWND+: %u -> %u (Tgt=%u, Grow=%u)\n",
            (void*)Connection, (double)AckEvent->TimeNow / 1000.0, 
            PrevCwnd, Cubic->CongestionWindow, AckTarget, GrowthSegments);
    }
}

// =========================================================================
// Interface Implementation
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets) {
    Cc->CubicProbe.Cubic.Exemptions = NumPackets;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlReset(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN FullReset) {
    UNREFERENCED_PARAMETER(FullReset); // [수정] 경고 방지 매크로 추가

    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_SETTINGS_INTERNAL* Settings = &Connection->Settings;
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
    Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    
    // FullReset 로직은 제거되었으므로 변수는 무시됨 (경고 해결)
    Cubic->BytesInFlight = 0; 
    Cubic->WindowMax = 0; 
    
    CubicProbe->MinRttUs = UINT64_MAX;
    CubicProbeResetPhysicsState(CubicProbe);
    
    printf("[Init] CubicBoost V18. CWND=%u\n", Cubic->CongestionWindow);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    uint32_t SendAllowance;

    if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
        SendAllowance = 0;
    } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample) {
        SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
    } else {
        uint64_t EstimatedWnd = (Cubic->CongestionWindow < Cubic->SlowStartThreshold) ? ((uint64_t)Cubic->CongestionWindow << 1) : (Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2));
        if (EstimatedWnd > Cubic->SlowStartThreshold && Cubic->CongestionWindow < Cubic->SlowStartThreshold) EstimatedWnd = Cubic->SlowStartThreshold;
        
        SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
        if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
            SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
        }
        Cubic->LastSendAllowance = SendAllowance;
    }
    return SendAllowance;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState) {
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
        if (PreviousCanSendState) {
            QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
        } else {
            QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
            Connection->Send.LastFlushTime = CxPlatTimeUs64();
            return TRUE;
        }
    }
    return FALSE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN CubicProbeCongestionControlOnDataAcknowledged(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent) {
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

    if (Cubic->IsInRecovery) {
        if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
            Cubic->IsInRecovery = FALSE;
            printf("[Recovery] Exit. CWND=%u\n", Cubic->CongestionWindow);
            
            CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
            CubicProbe->RoundStartTime = AckEvent->TimeNow;
        }
        goto Exit;
    }
    if (AckEvent->NumRetransmittableBytes == 0) goto Exit;

    if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
        // Slow Start
        uint32_t PrevCwnd = Cubic->CongestionWindow; // [사용됨]
        Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

        // [수정] 주석 처리되었던 로그를 활성화하여 PrevCwnd 사용 (오류 해결)
        printf("[SS] CWND %u -> %u\n", PrevCwnd, Cubic->CongestionWindow);

        if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            
            CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
            CubicProbe->RoundStartTime = AckEvent->TimeNow;
            CubicProbe->RoundInFlightBytes = 0;
        }
    } else {
        // Congestion Avoidance
        const QUIC_PATH* Path = &Connection->Paths[0];
        const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
        if (DatagramPayloadLength == 0) goto Exit;

        CubicProbeCheckSafety(Cc, AckEvent);
        CubicProbeCheckElasticity(Cc, AckEvent);

        uint32_t AckTarget = 0;
        CubicProbeUpdate(Cc, AckEvent, DatagramPayloadLength, &AckTarget);
        CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget, DatagramPayloadLength);
    }

Exit:
    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->BytesInFlight += NumRetransmittableBytes;
    if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
        Cubic->BytesInFlightMax = Cubic->BytesInFlight;
        QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
    }
    if (NumRetransmittableBytes > Cubic->LastSendAllowance) Cubic->LastSendAllowance = 0;
    else Cubic->LastSendAllowance -= NumRetransmittableBytes;
    if (Cubic->Exemptions > 0) --Cubic->Exemptions;

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta) {
    UNREFERENCED_PARAMETER(IsPersistentCongestion);
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    
    uint32_t PrevCwnd = Cubic->CongestionWindow;

    CubicProbeResetPhysicsState(CubicProbe);
    CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; // Reset Round

    if (!Cubic->IsInRecovery) Cubic->IsInRecovery = TRUE;
    Cubic->HasHadCongestionEvent = TRUE;

    if (!Ecn) Cubic->PrevCongestionWindow = Cubic->CongestionWindow;

    Cubic->WindowLastMax = Cubic->WindowMax;
    Cubic->WindowMax = Cubic->CongestionWindow;
    if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
        Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
    }

    uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    Cubic->SlowStartThreshold = Cubic->CongestionWindow = CXPLAT_MAX(MinCongestionWindow, (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));
    Cubic->TimeOfCongAvoidStart = 0;

    printf("[LOSS] Event. CWND: %u -> %u\n", PrevCwnd, Cubic->CongestionWindow);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
        CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
    }
    Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;
    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
    
    // Unused parameter warning fix
    (void)Connection;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
        Connection->Stats.Send.EcnCongestionCount++;
        CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
    }
    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN CubicProbeCongestionControlOnDataInvalidated(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
    Cubic->BytesInFlight -= NumRetransmittableBytes;
    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

    if (!Cubic->IsInRecovery) return FALSE;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
    Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;
    
    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
    
    // Unused parameter warning fix
    (void)Connection;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }
uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.BytesInFlightMax; }
uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.Exemptions; }
uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.CongestionWindow; }
BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); return FALSE; }
void CubicProbeCongestionControlSetAppLimited(_In_ struct QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }

void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics) {
    const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    const QUIC_PATH* Path = &Connection->Paths[0];
    NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
    NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
    NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
    NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
    NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
    NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
}

static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
    .Name = "CubicBoost",
    .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
    .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
    .QuicCongestionControlReset = CubicProbeCongestionControlReset,
    .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
    .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
    .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
    .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
    .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
    .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
    .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
    .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
    .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
    .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
    .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
    .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
    .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
    .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
};

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlInitialize(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_SETTINGS_INTERNAL* Settings) {
    *Cc = QuicCongestionControlCubicProbe;
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
    Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    Cubic->BytesInFlight = 0;
    Cubic->WindowMax = 0; 
    
    CubicProbe->MinRttUs = UINT64_MAX;
    CubicProbeResetPhysicsState(CubicProbe);
    
    printf("[Init] CubicBoost V18 (Round+Safety). CWND=%u\n", Cubic->CongestionWindow);
}