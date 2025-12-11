/*
    CubicBoost V11: Adaptive Threshold Fix
    - Problem Fix: Veto was too sensitive to standing queues.
    - Solution: Uses SmoothedRTT instead of MinRTT for local congestion detection.
*/

#include "precomp.h"
#include <stdio.h>
#include "cubicprobe.h" 

// =========================================================================
// Constants & Definitions
// =========================================================================

#define TEN_TIMES_BETA_CUBIC 7  
#define TEN_TIMES_C_CUBIC 4     

#define PROBE_SENSITIVITY_GAMMA 4       
// [수정] 노이즈 마진을 2ms -> 5ms로 완화하여 민감도 낮춤
#define PROBE_MIN_NOISE_MARGIN_US 5000  

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
    CubicProbe->PrevTime = CxPlatTimeUs64();
    CubicProbe->PrevCwnd = CubicProbe->Cubic.CongestionWindow;
    CubicProbe->PrevBandwidth = 0;
    CubicProbe->BatchBytesAcked = 0;
    
    CubicProbe->CurrentElasticity = 0.0;
    CubicProbe->IsQueueBuilding = FALSE;
    CubicProbe->AckCountForGrowth = 0;
}

// =========================================================================
// Core Logic: Elasticity & Veto Calculation (Per ACK)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbePktsAcked(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _In_ uint16_t DatagramPayloadLength
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];

    // 1. Update MinRTT
    if (AckEvent->MinRttValid) {
        if (CubicProbe->MinRttUs == UINT64_MAX || AckEvent->MinRtt < CubicProbe->MinRttUs) {
            CubicProbe->MinRttUs = AckEvent->MinRtt;
        }
    }
    
    // 2. Statistical Veto Logic (Improved for LEO)
    // 기존 문제: MinRTT는 너무 오래된(낮은) 값이라, 큐가 조금만 차도 Veto가 걸림.
    // 해결: Path->SmoothedRtt(SRTT)를 사용하여 "최근 평균"보다 튀는지를 감지함.
    
    CubicProbe->RttVariance = Path->RttVariance; 

    uint64_t NoiseMargin = PROBE_SENSITIVITY_GAMMA * CubicProbe->RttVariance;
    if (NoiseMargin < PROBE_MIN_NOISE_MARGIN_US) {
        NoiseMargin = PROBE_MIN_NOISE_MARGIN_US;
    }

    // [핵심 변경] 기준점을 MinRttUs 대신 SmoothedRtt로 변경
    // SRTT가 없으면 MinRtt 사용
    uint64_t BaselineRtt = (Path->SmoothedRtt > 0) ? Path->SmoothedRtt : CubicProbe->MinRttUs;
    uint64_t CongestionThreshold = BaselineRtt + NoiseMargin;

    // Check if queue is building
    if (AckEvent->MinRtt > CongestionThreshold) {
        CubicProbe->IsQueueBuilding = TRUE;
    } else {
        CubicProbe->IsQueueBuilding = FALSE;
    }

    // [LOG] RTT Debug
    // SRTT와 Threshold를 함께 출력하여 Veto 원인 파악
    printf("[CubicProbe][%p][%.3fms] RTT Check: Curr=%.3fms, SRTT=%.3fms, Thresh=%.3fms, Veto=%d\n",
        (void*)Connection,
        (double)AckEvent->TimeNow / 1000.0,
        (double)AckEvent->MinRtt / 1000.0,
        (double)BaselineRtt / 1000.0,
        (double)CongestionThreshold / 1000.0,
        CubicProbe->IsQueueBuilding);

    // 3. Elasticity Calculation
    CubicProbe->BatchBytesAcked += AckEvent->NumRetransmittableBytes;
    
    if (CubicProbe->BatchBytesAcked >= DatagramPayloadLength) {
        
        uint64_t TimeNow = AckEvent->TimeNow;
        uint64_t TimeDelta = CxPlatTimeDiff64(CubicProbe->PrevTime, TimeNow);

        uint64_t CurrentBandwidth = (TimeDelta > 0) ? (CubicProbe->BatchBytesAcked * 1000000 / TimeDelta) : 0;
        uint32_t CurrentCwnd = Cubic->CongestionWindow;

        if (CubicProbe->PrevBandwidth > 0 && CubicProbe->PrevCwnd > 0) {
            
            double DeltaBwPct = (double)((int64_t)CurrentBandwidth - (int64_t)CubicProbe->PrevBandwidth) / (double)CubicProbe->PrevBandwidth;
            double DeltaCwndPct = (double)((int64_t)CurrentCwnd - (int64_t)CubicProbe->PrevCwnd) / (double)CubicProbe->PrevCwnd;

            if (DeltaCwndPct > 0.001) { 
                double NewElasticity = DeltaBwPct / DeltaCwndPct;
                if (NewElasticity > 1.0) NewElasticity = 1.0;
                if (NewElasticity < 0.0) NewElasticity = 0.0;

                // EWMA
                CubicProbe->CurrentElasticity = (0.75 * CubicProbe->CurrentElasticity) + (0.25 * NewElasticity);

                // [LOG] Elasticity Update
                if (CubicProbe->CurrentElasticity > 0.1) {
                    printf("[CubicProbe][%p][%.3fms] E-Update: E=%.2f (Raw=%.2f)\n",
                        (void*)Connection, (double)TimeNow/1000.0, CubicProbe->CurrentElasticity, NewElasticity);
                }
            }
        }

        CubicProbe->PrevBandwidth = CurrentBandwidth;
        CubicProbe->PrevCwnd = CurrentCwnd;
        CubicProbe->PrevTime = TimeNow;
        CubicProbe->BatchBytesAcked = 0;
    }
}

// =========================================================================
// Core Logic: Target Calculation
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

    // --- Part 1: Standard CUBIC ---
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

    // --- Part 2: Physics Blending ---
    double E = CubicProbe->CurrentElasticity;
    
    // [수정] Veto가 걸려도 E가 높으면 0.1 정도는 남겨둬서 최소한의 가속 가능성 열어두기 (실험용)
    // 정석: if (CubicProbe->IsQueueBuilding) E = 0.0;
    // 완화: Veto 상태면 E를 절반으로 깎음
    if (CubicProbe->IsQueueBuilding) {
        E = 0.0; // 안전을 위해 우선 차단. 만약 그래도 안되면 여기를 주석 처리하세요.
    }

    double BlendedTarget = (1.0 - E) * (double)N_cubic + (E * 1.0);
    *AckTarget = (uint32_t)BlendedTarget;
    if (*AckTarget < 1) *AckTarget = 1;

    // [LOG] Acceleration Info
    if (*AckTarget < N_cubic) {
        printf("[Boost] N_cubic=%u -> Target=%u (E=%.2f, Veto=%d)\n", N_cubic, *AckTarget, E, CubicProbe->IsQueueBuilding);
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
        uint32_t PrevCwnd = Cubic->CongestionWindow;
        
        Cubic->CongestionWindow += DatagramPayloadLength;
        CubicProbe->AckCountForGrowth -= AckTarget;

        // [LOG] CWND Update
        printf("[CubicProbe][%p][%.3fms] CWND Update: %u -> %u (Target=%u, E=%.2f)\n",
            (void*)Connection, 
            (double)AckEvent->TimeNow / 1000.0, 
            PrevCwnd, 
            Cubic->CongestionWindow, 
            AckTarget,
            CubicProbe->CurrentElasticity);
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
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    if (FullReset) Cubic->BytesInFlight = 0;
    Cubic->WindowMax = 0; 
    
    CubicProbe->MinRttUs = UINT64_MAX;
    CubicProbeResetPhysicsState(CubicProbe);
    
    printf("[CubicProbe][%p][%.3fms] Initialized. InitialCWND=%u\n", 
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);
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
            printf("[CubicProbe][%p][%.3fms] RECOVERY EXIT. CWND=%u\n", 
                (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->CongestionWindow);
        }
        goto Exit;
    }
    if (AckEvent->NumRetransmittableBytes == 0) goto Exit;

    if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
        // Slow Start
        uint32_t PrevCwnd = Cubic->CongestionWindow;
        Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

        printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
            (void*)Connection, (double)AckEvent->TimeNow / 1000.0, PrevCwnd, Cubic->CongestionWindow);

        if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            printf("[CubicProbe][%p][%.3fms] SlowStart Exited. Threshold=%u\n", 
                (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->SlowStartThreshold);
        }
    } else {
        // Congestion Avoidance
        const QUIC_PATH* Path = &Connection->Paths[0];
        const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
        if (DatagramPayloadLength == 0) goto Exit;

        CubicProbePktsAcked(Cc, AckEvent, DatagramPayloadLength);

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

    printf("[CubicProbe][%p][%.3fms] CWND Update (Congestion Event): %u -> %u (Beta=%.1f)\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, PrevCwnd, Cubic->CongestionWindow, (double)TenTimesBeta/10.0);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[CubicProbe][%p][%.3fms] LOSS EVENT: CWnd=%u, InFlight=%u, LostBytes=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

    if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
        CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
    }
    Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;
    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[CubicProbe][%p][%.3fms] ECN EVENT: CWnd=%u, InFlight=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

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
    
    printf("[CubicProbe][%p][%.3fms] SPURIOUS Revert: CWND -> %u\n", 
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
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
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
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
    
    printf("[CubicProbe][%p][%.3fms] Initialized. InitialCWND=%u\n", 
        (void*)QuicCongestionControlGetConnection(Cc), (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);
}