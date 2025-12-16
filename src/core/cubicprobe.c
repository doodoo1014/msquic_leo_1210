// /*
//     CubicBoost V20: Cumulative Elasticity (Fixing Low E issue)
//     - Logic: Compare current metrics against 'Epoch Baseline' (start of congestion avoidance),
//              instead of the immediate previous round.
//     - Benefit: Accumulates small changes over time to detect meaningful elasticity.
// */

// #include "precomp.h"
// #include <stdio.h>
// #include "cubicprobe.h" 

// // =========================================================================
// // Constants
// // =========================================================================

// #define TEN_TIMES_BETA_CUBIC 7  
// #define TEN_TIMES_C_CUBIC 4     
// #define PROBE_SENSITIVITY_GAMMA 4       
// #define PROBE_MIN_NOISE_MARGIN_US 4000  

// // =========================================================================
// // Helper Functions
// // =========================================================================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static uint32_t CubeRoot(uint32_t Radicand)
// {
//     int i;
//     uint32_t x = 0;
//     uint32_t y = 0;
//     for (i = 30; i >= 0; i -= 3) {
//         x = x * 8 + ((Radicand >> i) & 7);
//         if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
//             y = y * 2 + 1;
//         } else {
//             y = y * 2;
//         }
//     }
//     return y;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void CubicProbeResetPhysicsState(_In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe)
// {
//     CubicProbe->MinRttUs = UINT64_MAX;
    
//     // Round Tracking
//     CubicProbe->ProbeTargetPacketNumber = 0; 
//     CubicProbe->RoundInFlightBytes = 0;
//     CubicProbe->RoundStartTime = CxPlatTimeUs64();
    
//     // [New] Epoch Baselines (누적 계산을 위한 기준점)
//     CubicProbe->EpochStartBandwidth = 0;
//     CubicProbe->EpochStartCwnd = 0;
    
//     CubicProbe->CurrentElasticity = 0.0;
//     CubicProbe->IsQueueBuilding = FALSE;
//     CubicProbe->AckCountForGrowth = 0;
// }

// // =========================================================================
// // Logic 1: Safety Check & RTT (Per ACK)
// // =========================================================================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeCheckSafety(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];

//     if (AckEvent->MinRttValid) {
//         if (CubicProbe->MinRttUs == UINT64_MAX || AckEvent->MinRtt < CubicProbe->MinRttUs) {
//             CubicProbe->MinRttUs = AckEvent->MinRtt;
//         }
//     }

//     CubicProbe->RttVariance = Path->RttVariance; 
//     uint64_t NoiseMargin = PROBE_SENSITIVITY_GAMMA * CubicProbe->RttVariance;
//     if (NoiseMargin < PROBE_MIN_NOISE_MARGIN_US) NoiseMargin = PROBE_MIN_NOISE_MARGIN_US;

//     uint64_t BaselineRtt = (Path->SmoothedRtt > 0) ? Path->SmoothedRtt : CubicProbe->MinRttUs;
//     uint64_t Threshold = BaselineRtt + NoiseMargin;

//     if (AckEvent->MinRtt > Threshold) {
//         CubicProbe->IsQueueBuilding = TRUE; 
        
//         // [Important] If queue is building, reset the Epoch Baseline.
//         // We want to measure elasticity only during "clean" intervals.
//         CubicProbe->EpochStartBandwidth = 0; 
//     } else {
//         CubicProbe->IsQueueBuilding = FALSE; 
//     }

//     CubicProbe->RoundInFlightBytes += AckEvent->NumRetransmittableBytes;
// }

// // =========================================================================
// // Logic 2: Cumulative Elasticity Check (Per Round)
// // =========================================================================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeCheckElasticity(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

//     if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
        
//         uint64_t TimeNow = AckEvent->TimeNow;
//         uint64_t TimeDelta = CxPlatTimeDiff64(CubicProbe->RoundStartTime, TimeNow);
        
//         uint64_t CurrentBW = 0;
//         if (TimeDelta > 0) {
//             CurrentBW = CubicProbe->RoundInFlightBytes * 1000000 / TimeDelta;
//         }
//         uint32_t CurrentCwnd = Cubic->CongestionWindow;

//         // [Epoch Initialization]
//         // If we don't have a baseline yet (start of connection or after congestion), set it.
//         if (CubicProbe->EpochStartBandwidth == 0 || CubicProbe->EpochStartCwnd == 0) {
//             CubicProbe->EpochStartBandwidth = CurrentBW;
//             CubicProbe->EpochStartCwnd = CurrentCwnd;
//             CubicProbe->CurrentElasticity = 0.0;
//         } 
//         else {
//             // [Cumulative Calculation]
//             // Calculate growth relative to the START of the epoch, not just previous round.
            
//             // Prevent division by zero
//             if (CubicProbe->EpochStartBandwidth > 0 && CubicProbe->EpochStartCwnd > 0) {
                
//                 double BwGrowth = (double)((int64_t)CurrentBW - (int64_t)CubicProbe->EpochStartBandwidth) / (double)CubicProbe->EpochStartBandwidth;
//                 double CwndGrowth = (double)((int64_t)CurrentCwnd - (int64_t)CubicProbe->EpochStartCwnd) / (double)CubicProbe->EpochStartCwnd;

//                 // Only update E if we have pushed CWND enough (> 2%) to measure a reaction
//                 if (CwndGrowth > 0.02) {
//                     double NewE = BwGrowth / CwndGrowth;
                    
//                     // Allow E to go slightly above 1.0 due to noise, but clamp for logic
//                     if (NewE > 1.0) NewE = 1.0;
//                     if (NewE < 0.0) NewE = 0.0;
                    
//                     CubicProbe->CurrentElasticity = NewE;
//                 }
//             }
//         }
        
//         // [Baseline Reset Condition]
//         // If BW dropped significantly below baseline, reset the epoch (re-calibration).
//         if (CurrentBW < CubicProbe->EpochStartBandwidth * 0.9) {
//             CubicProbe->EpochStartBandwidth = CurrentBW;
//             CubicProbe->EpochStartCwnd = CurrentCwnd;
//         }

//         if (CubicProbe->CurrentElasticity > 0.1) {
//             printf("[Round] E=%.2f, CurrBW=%lu (BaseBW=%lu)\n", 
//                 CubicProbe->CurrentElasticity, CurrentBW, CubicProbe->EpochStartBandwidth);
//         }

//         // Reset for Next Round
//         CubicProbe->RoundInFlightBytes = 0;
//         CubicProbe->RoundStartTime = TimeNow;
//         CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; 
//     }
// }

// // =========================================================================
// // Logic 3: Target Calculation
// // =========================================================================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeUpdate(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _In_ uint16_t DatagramPayloadLength,
//     _Out_ uint32_t* AckTarget
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;

//     // --- Standard CUBIC ---
//     if (Cubic->TimeOfCongAvoidStart == 0) {
//         Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
//         if (Cubic->CongestionWindow < Cubic->WindowMax) {
//             if (DatagramPayloadLength > 0) {
//                 uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
//                 uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
//                 Cubic->KCubic = CubeRoot(radicand);
//                 Cubic->KCubic = S_TO_MS(Cubic->KCubic);
//                 Cubic->KCubic >>= 3; 
//             } else {
//                 Cubic->KCubic = 0;
//             }
//         } else {
//             Cubic->KCubic = 0;
//             Cubic->WindowMax = Cubic->CongestionWindow;
//         }
//     }

//     const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
//     int64_t TimeDeltaMs = (int64_t)(t_us / 1000) - (int64_t)Cubic->KCubic;
//     int64_t CubicTerm = ((((TimeDeltaMs * TimeDeltaMs) >> 10) * TimeDeltaMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

//     uint32_t W_cubic_bytes;
//     if (TimeDeltaMs < 0) {
//         W_cubic_bytes = Cubic->WindowMax - (uint32_t)(-CubicTerm);
//     } else {
//         W_cubic_bytes = Cubic->WindowMax + (uint32_t)CubicTerm;
//     }

//     uint32_t N_cubic;
//     if (W_cubic_bytes > Cubic->CongestionWindow) {
//         uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
//         uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
//         uint32_t DiffSegments = (TargetSegments > CwndSegments) ? (TargetSegments - CwndSegments) : 1;
//         N_cubic = CwndSegments / DiffSegments;
//     } else {
//         N_cubic = 100 * (Cubic->CongestionWindow / DatagramPayloadLength); 
//     }

//     // --- Boost Logic ---
//     if (CubicProbe->IsQueueBuilding) {
//         *AckTarget = N_cubic; // Safety First
//     }
//     else {
//         double E = CubicProbe->CurrentElasticity;
        
//         // [Deterministic] If Elasticity is unknown (0), perform minimum linear growth (Reno)
//         // to accumulate enough CWND change for the next Epoch calculation.
//         if (E < 0.1) {
//             uint32_t N_reno = Cubic->CongestionWindow / DatagramPayloadLength;
//             if (N_reno < 1) N_reno = 1;
            
//             // Use Linear Growth (Reno) as the baseline for probing
//             // This is faster than CUBIC plateau, ensuring we get dCWND > 0.02 soon.
//             *AckTarget = N_reno; 
//         } 
//         else {
//             // Linear Interpolation: 
//             // Target = (1-E)*N_cubic + E*1(SlowStart)
//             // But since N_cubic can be huge, we cap the baseline at Reno to ensure responsiveness.
            
//             uint32_t N_baseline = Cubic->CongestionWindow / DatagramPayloadLength; // Reno
//             if (N_baseline > N_cubic) N_baseline = N_cubic; // Respect CUBIC if it's faster

//             double TargetDouble = ((1.0 - E) * (double)N_baseline) + (E * 1.0);
//             *AckTarget = (uint32_t)TargetDouble;
//         }
        
//         if (*AckTarget < 1) *AckTarget = 1;
//     }

//     if (*AckTarget < N_cubic && !CubicProbe->IsQueueBuilding) {
//         // printf("[Boost] N=%u -> Tgt=%u (E=%.2f)\n", N_cubic, *AckTarget, CubicProbe->CurrentElasticity);
//     }
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeIncreaseWindow(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _In_ uint32_t AckTarget,
//     _In_ uint16_t DatagramPayloadLength
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

//     uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
//     CubicProbe->AckCountForGrowth += AckedSegments;

//     if (CubicProbe->AckCountForGrowth >= AckTarget) {
        
//         uint32_t GrowthSegments = CubicProbe->AckCountForGrowth / AckTarget;
//         uint32_t PrevCwnd = Cubic->CongestionWindow;
        
//         Cubic->CongestionWindow += (GrowthSegments * DatagramPayloadLength);
//         CubicProbe->AckCountForGrowth %= AckTarget;

//         printf("[CubicProbe][%p][%.3fms] CWND+: %u -> %u (Tgt=%u, Grow=%u, E=%.2f)\n",
//             (void*)Connection, 
//             (double)AckEvent->TimeNow / 1000.0, 
//             PrevCwnd, 
//             Cubic->CongestionWindow, 
//             AckTarget,
//             GrowthSegments,
//             CubicProbe->CurrentElasticity);
//     }
// }

// // =========================================================================
// // Interface Implementation
// // =========================================================================

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets) {
//     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlReset(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN FullReset) {
//     UNREFERENCED_PARAMETER(FullReset);

//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_SETTINGS_INTERNAL* Settings = &Connection->Settings;
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
//     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     if (FullReset) Cubic->BytesInFlight = 0;
//     Cubic->WindowMax = 0; 
    
//     CubicProbe->MinRttUs = UINT64_MAX;
//     CubicProbeResetPhysicsState(CubicProbe);
    
//     printf("[Init] CubicBoost V20 (Cumulative). CWND=%u\n", Cubic->CongestionWindow);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     uint32_t SendAllowance;

//     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
//         SendAllowance = 0;
//     } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample) {
//         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//     } else {
//         uint64_t EstimatedWnd = (Cubic->CongestionWindow < Cubic->SlowStartThreshold) ? ((uint64_t)Cubic->CongestionWindow << 1) : (Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2));
//         if (EstimatedWnd > Cubic->SlowStartThreshold && Cubic->CongestionWindow < Cubic->SlowStartThreshold) EstimatedWnd = Cubic->SlowStartThreshold;
        
//         SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
//         if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
//             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//         }
//         Cubic->LastSendAllowance = SendAllowance;
//     }
//     return SendAllowance;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState) {
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
//         if (PreviousCanSendState) {
//             QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
//         } else {
//             QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
//             Connection->Send.LastFlushTime = CxPlatTimeUs64();
//             return TRUE;
//         }
//     }
//     return FALSE;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN CubicProbeCongestionControlOnDataAcknowledged(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent) {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

//     if (Cubic->IsInRecovery) {
//         if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
//             Cubic->IsInRecovery = FALSE;
            
//             // [Fix] Start new Epoch on Recovery Exit
//             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
//             CubicProbe->RoundStartTime = AckEvent->TimeNow;
//             CubicProbe->EpochStartBandwidth = 0; // Reset Epoch
//             CubicProbe->EpochStartCwnd = 0;
            
//             printf("[Recovery] Exit. CWND=%u\n", Cubic->CongestionWindow);
//         }
//         goto Exit;
//     }
//     if (AckEvent->NumRetransmittableBytes == 0) goto Exit;

//     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
//         // Slow Start Phase
//         uint32_t PrevCwnd = Cubic->CongestionWindow;
//         Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

//         printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
//             (void*)Connection, (double)AckEvent->TimeNow / 1000.0, PrevCwnd, Cubic->CongestionWindow);

//         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
//             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            
//             // Initialize Tracking on Exit SS
//             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
//             CubicProbe->RoundStartTime = AckEvent->TimeNow;
//             CubicProbe->RoundInFlightBytes = 0;
//             CubicProbe->EpochStartBandwidth = 0;
//         }
//     } else {
//         // Congestion Avoidance Phase
//         const QUIC_PATH* Path = &Connection->Paths[0];
//         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
//         if (DatagramPayloadLength == 0) goto Exit;

//         CubicProbeCheckSafety(Cc, AckEvent);
//         CubicProbeCheckElasticity(Cc, AckEvent); // Cumulative Check

//         uint32_t AckTarget = 0;
//         CubicProbeUpdate(Cc, AckEvent, DatagramPayloadLength, &AckTarget);
//         CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget, DatagramPayloadLength);
//     }

// Exit:
//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight += NumRetransmittableBytes;
//     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
//         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
//         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
//     }
//     if (NumRetransmittableBytes > Cubic->LastSendAllowance) Cubic->LastSendAllowance = 0;
//     else Cubic->LastSendAllowance -= NumRetransmittableBytes;
//     if (Cubic->Exemptions > 0) --Cubic->Exemptions;

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta) {
//     UNREFERENCED_PARAMETER(IsPersistentCongestion);
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    
//     uint32_t PrevCwnd = Cubic->CongestionWindow;

//     CubicProbeResetPhysicsState(CubicProbe);
//     CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; // Reset Round

//     if (!Cubic->IsInRecovery) Cubic->IsInRecovery = TRUE;
//     Cubic->HasHadCongestionEvent = TRUE;

//     if (!Ecn) Cubic->PrevCongestionWindow = Cubic->CongestionWindow;

//     Cubic->WindowLastMax = Cubic->WindowMax;
//     Cubic->WindowMax = Cubic->CongestionWindow;
//     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
//         Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
//     }

//     uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
//     Cubic->SlowStartThreshold = Cubic->CongestionWindow = CXPLAT_MAX(MinCongestionWindow, (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));
//     Cubic->TimeOfCongAvoidStart = 0;

//     printf("[LOSS] Event. CWND: %u -> %u\n", PrevCwnd, Cubic->CongestionWindow);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    // printf("[CubicProbe][%p][%.3fms] LOSS EVENT: CWnd=%u, InFlight=%u, LostBytes=%u\n",
    //     (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

//     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
//         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
//     }
//     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;
//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    // printf("[CubicProbe][%p][%.3fms] ECN EVENT: CWnd=%u, InFlight=%u\n",
    //     (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

//     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
//         Connection->Stats.Send.EcnCongestionCount++;
//         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
//     }
//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN CubicProbeCongestionControlOnDataInvalidated(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
//     Cubic->BytesInFlight -= NumRetransmittableBytes;
//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc) {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

//     if (!Cubic->IsInRecovery) return FALSE;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
//     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
//     Cubic->IsInRecovery = FALSE;
//     Cubic->HasHadCongestionEvent = FALSE;
    
    // printf("[CubicProbe][%p][%.3fms] SPURIOUS Revert: CWND -> %u\n", 
    //     (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);

//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }
// uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.BytesInFlightMax; }
// uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.Exemptions; }
// uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.CongestionWindow; }
// BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); return FALSE; }
// void CubicProbeCongestionControlSetAppLimited(_In_ struct QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }

// void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics) {
//     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
//     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
//     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
//     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
//     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
//     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// }

// static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
//     .Name = "CubicBoost",
//     .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
//     .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
//     .QuicCongestionControlReset = CubicProbeCongestionControlReset,
//     .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
//     .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
//     .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
//     .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
//     .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
//     .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
//     .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
//     .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
//     .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
//     .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
//     .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
//     .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
//     .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
//     .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
// };

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlInitialize(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_SETTINGS_INTERNAL* Settings) {
//     *Cc = QuicCongestionControlCubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     // [Fix] Declare Connection here
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
//     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     Cubic->BytesInFlight = 0; 
//     Cubic->WindowMax = 0; 
    
//     CubicProbe->MinRttUs = UINT64_MAX;
//     CubicProbeResetPhysicsState(CubicProbe);
    
//     printf("[Init] CubicBoost V20 (Cumulative). CWND=%u\n", Cubic->CongestionWindow);
// }



/*++

Copyright (c) Microsoft Corporation.
Licensed under the MIT License.

Module Name:

    cubicprobe.c

Abstract:

    Implementation of CubicBoost v3 with Comprehensive Logging.
    - Standard Slow Start.
    - CubicBoost Logic applies only in Congestion Avoidance.
    - Logs every CWND change.

--*/

#include "precomp.h"
#include <stdio.h>
#include <math.h> 
#include "cubicprobe.h"

// Constants from RFC8312
#define TEN_TIMES_BETA_CUBIC 7 
#define TEN_TIMES_C_CUBIC 4
#define CUBIC_BOOST_FACTOR 1.5

// [Safety] Minimum ACK Target to prevent Microburst (1 = Grow every ACK)
#define MIN_ACK_TARGET 1 

// Forward declarations
static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta);
static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget);
static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

// CanSend declaration
BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);


// =========================================================================
// Helper Functions
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static uint32_t
CubeRoot(uint32_t Radicand)
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
static void
CubicProbeResetStats(
    _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe,
    _In_ const QUIC_CONNECTION* Connection
    )
{
    // [CubicBoost] Reset Acceleration
    CubicProbe->StableRoundCount = 0;
    CubicProbe->IsRoundClean = TRUE;
    
    // Reset Round Trigger
    CubicProbe->EndOfRoundSeq = Connection->Send.NextPacketNumber;
    
    // Initial Threshold (Infinite until first RTT sample)
    CubicProbe->RoundRttThresh = UINT64_MAX;
    
    // Reset Accumulator
    CubicProbe->BytesAckedAccumulator = 0;
}

// =========================================================================
// Reset Function
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlReset(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN FullReset
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    uint32_t PrevCwnd = Cubic->CongestionWindow;

    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    Cubic->LastSendAllowance = 0;
    if (FullReset) {
        Cubic->BytesInFlight = 0;
    }
    Cubic->WindowMax = 0;
    Cubic->WindowLastMax = 0;
    Cubic->TimeOfCongAvoidStart = 0;
    Cubic->KCubic = 0;

    CubicProbe->MinRttUs = UINT64_MAX;
    
    CubicProbeResetStats(CubicProbe, Connection);

    printf("[CubicProbe][%p][%.3fms] RESET: Full=%d, CWND: %u -> %u\n",
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, FullReset, PrevCwnd, Cubic->CongestionWindow);
}


// =========================================================================
// Core Logic: CubicBoost v3 (Only used in Congestion Avoidance)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeUpdate(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _Out_ uint32_t* AckTarget
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    // 1. K 계산 (기존 로직 유지)
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

    // 2. [Time-Shift] 가상 시간 계산 (Boost 적용)
    uint64_t t_us_real = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
    uint64_t TimeShiftUs = 0;

    if (CubicProbe->StableRoundCount > 0 && Path->SmoothedRtt > 0) {
        // BoostFactor를 통해 시간을 미래로 당김
        TimeShiftUs = (uint64_t)((double)CubicProbe->StableRoundCount * (double)Path->SmoothedRtt * CUBIC_BOOST_FACTOR);
    }
    
    uint64_t t_us_virtual = t_us_real + TimeShiftUs;

    // 3. Target Window (W_target) 계산 - [중요] Byte 단위 실수 연산
    const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;
    int64_t TimeDeltaUs = (int64_t)t_us_virtual - (int64_t)K_us;
    int64_t OffsetMs = (TimeDeltaUs / 1000);

    // CUBIC Term: C * (t - K)^3 (Byte 단위 근사값)
    int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);
    
    // W_target을 double(Bytes)로 유지 -> 소수점 손실 방지
    double W_target_bytes;
    if (TimeDeltaUs < 0) W_target_bytes = (double)Cubic->WindowMax - (double)(-CubicTerm);
    else W_target_bytes = (double)Cubic->WindowMax + (double)CubicTerm;

    // 4. AckTarget 계산
    // "현재 CWND(Byte) 대비 목표(Byte)가 얼마나 큰가?"를 비율로 계산
    double CwndBytes = (double)Cubic->CongestionWindow;

    if (W_target_bytes > CwndBytes) {
        // DeltaBytes: 목표치와 현재치의 차이 (예: 2190 bytes = 1.5 MSS)
        double DeltaBytes = W_target_bytes - CwndBytes;
        
        // 0으로 나누기 방지 (아주 작은 값이라도 설정)
        if (DeltaBytes < 1.0) DeltaBytes = 1.0;

        // AckTarget 공식: Cwnd / Delta
        // 예: Cwnd=146000, Delta=2190 (1.5 MSS)
        // AckTarget = 146000 / 2190 = 66.6 -> 66
        // (ACK 66개 받으면 1 MSS 증가 -> 100개보다 빠름 -> 곡선 형성)
        
        // MSS 단위로 변환해서 계산하는 것보다 Byte 자체 비율이 가장 정확함
        // (Cwnd / DeltaBytes) * MSS_Size 로 접근하는 것이 아님.
        // 표준 공식: cnt = cwnd / (w_cubic - cwnd)  <-- 여기서 cwnd, w_cubic은 패킷 단위
        // 이를 Byte 단위로 치환하면:
        // cnt = (CwndBytes / MSS) / ((TargetBytes - CwndBytes) / MSS)
        //     = CwndBytes / (TargetBytes - CwndBytes)
        
        *AckTarget = (uint32_t)(CwndBytes / DeltaBytes);

    } else {
        // Standard CUBIC (TCP Friendly Region or Plateau)
        // 여기서는 안전하게 기존 로직 사용 (너무 느린 구간)
        *AckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
    }

    // [Safety] Microburst 방지
    if (*AckTarget < MIN_ACK_TARGET) {
        *AckTarget = MIN_ACK_TARGET;
    }
}


_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
CubicProbeCongestionControlOnDataAcknowledged(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    // [Log Checkpoint] Save Previous CWND
    uint32_t PrevCwnd = Cubic->CongestionWindow;

    // Update BytesInFlight
    Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

    // Handle Recovery
    if (Cubic->IsInRecovery) {
        if (AckEvent->LargestSentPacketNumber > Cubic->RecoverySentPacketNumber) {
            Cubic->IsInRecovery = FALSE;
            CubicProbeResetStats(CubicProbe, Connection);
            
            printf("[CubicProbe][%p][%.3fms] RECOVERY EXIT: CWND=%u\n",
                (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->CongestionWindow);
        }
        goto Exit;
    }

    if (AckEvent->NumRetransmittableBytes == 0) goto Exit;


    // ---------------------------------------------------------------------
    // [CubicBoost] Round-based Statistics (Always Active for Tracking)
    // ---------------------------------------------------------------------

    uint64_t CurrentRtt = (AckEvent->MinRttValid) ? AckEvent->MinRtt : Path->LatestRttSample;
    
    if (CubicProbe->RoundRttThresh != UINT64_MAX) {
        if (CurrentRtt > CubicProbe->RoundRttThresh) {
            if (CubicProbe->IsRoundClean) {
                printf("[CubicProbe][%p][%.3fms] ROUND DIRTY: RTT(%llu) > Thresh(%llu)\n",
                    (void*)Connection, (double)AckEvent->TimeNow/1000.0, 
                    (unsigned long long)CurrentRtt, (unsigned long long)CubicProbe->RoundRttThresh);
            }
            CubicProbe->IsRoundClean = FALSE;
        }
    }

    // Check End of Round
    if (AckEvent->LargestSentPacketNumber >= CubicProbe->EndOfRoundSeq) {
        // --- Round Boundary ---
        uint32_t PrevS = CubicProbe->StableRoundCount;

        if (CubicProbe->IsRoundClean) {
            CubicProbe->StableRoundCount++;
        } else {
            CubicProbe->StableRoundCount = 0;
        }

        // Snapshot Threshold for the NEXT round
        if (Path->SmoothedRtt > 0) {
            CubicProbe->RoundRttThresh = Path->SmoothedRtt + (CUBIC_BOOST_GAMMA * Path->RttVariance);
        } else {
            CubicProbe->RoundRttThresh = UINT64_MAX;
        }

        // Only log round end if momentum changed or just periodically useful?
        // Let's keep logging to see stability
        printf("[CubicProbe][%p][%.3fms] ROUND END: Clean=%d, Momentum=%u->%u, NextThresh=%llu\n",
             (void*)Connection, (double)AckEvent->TimeNow/1000.0, 
             CubicProbe->IsRoundClean, PrevS, CubicProbe->StableRoundCount, 
             (unsigned long long)CubicProbe->RoundRttThresh);


        // Set trigger for next round end
        CubicProbe->EndOfRoundSeq = Connection->Send.NextPacketNumber;
        CubicProbe->IsRoundClean = TRUE;
    }


    // ---------------------------------------------------------------------
    // CWND Update
    // ---------------------------------------------------------------------

    if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
        // =================================================================
        // [SLOW START] Standard RFC Implementation (No Boost)
        // =================================================================
        
        Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;
        
        // Transition to CA
        if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            printf("[CubicProbe][%p][%.3fms] SS EXIT -> CA START: SST=%u\n",
                 (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->SlowStartThreshold);
        }

    } else {
        // =================================================================
        // [CONGESTION AVOIDANCE] CubicBoost Logic Applied Here
        // =================================================================
        
        const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
        if (DatagramPayloadLength == 0) goto Exit;

        if (CubicProbe->MinRttUs == UINT64_MAX || CubicProbe->MinRttUs > CurrentRtt) {
            CubicProbe->MinRttUs = CurrentRtt;
        }

        uint32_t AckTarget = 0;
        CubicProbeUpdate(Cc, AckEvent, &AckTarget); // Calculate Boosted Target

        CubicProbe->BytesAckedAccumulator += AckEvent->NumRetransmittableBytes;
        uint64_t BytesRequired = (uint64_t)AckTarget * DatagramPayloadLength;

        if (CubicProbe->BytesAckedAccumulator >= BytesRequired) {
            Cubic->CongestionWindow += DatagramPayloadLength;
            CubicProbe->BytesAckedAccumulator -= BytesRequired;
        }
    }

Exit:
    // [Log Requirement] Check for CWND Change
    if (Cubic->CongestionWindow != PrevCwnd) {
        printf("[CubicProbe][%p][%.3fms] CWND UPDATE: %u -> %u\n",
            (void*)Connection, (double)AckEvent->TimeNow/1000.0, PrevCwnd, Cubic->CongestionWindow);
    }

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}


// =========================================================================
// Congestion Event Handlers (Loss / ECN)
// =========================================================================

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeCongestionControlOnCongestionEvent(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN IsPersistentCongestion,
    _In_ BOOLEAN Ecn,
    _In_ uint32_t TenTimesBeta
    )
{
    UNREFERENCED_PARAMETER(IsPersistentCongestion);

    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    // Save PrevCWND for logging
    uint32_t PrevCwnd = Cubic->CongestionWindow;

    // [CubicBoost] Full reset of boost state on congestion
    CubicProbeResetStats(CubicProbe, Connection);

    if (!Cubic->IsInRecovery) {
         Cubic->IsInRecovery = TRUE;
    }
    Cubic->HasHadCongestionEvent = TRUE;

    if (!Ecn) {
        Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
    }

    Cubic->WindowLastMax = Cubic->WindowMax;
    Cubic->WindowMax = Cubic->CongestionWindow;

    // Fast Convergence
    if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
        Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
    }

    uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    
    Cubic->SlowStartThreshold =
    Cubic->CongestionWindow =
        CXPLAT_MAX(
            MinCongestionWindow,
            (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));

    Cubic->TimeOfCongAvoidStart = 0;

    // [Log Requirement] CWND Reduced
    printf("[CubicProbe][%p][%.3fms] CWND UPDATE (LOSS/ECN): %u -> %u (SST=%u)\n",
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, 
        PrevCwnd, Cubic->CongestionWindow, Cubic->SlowStartThreshold);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlOnDataLost(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_LOSS_EVENT* LossEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[CubicProbe][%p][%.3fms] LOSS DETECTED: InFlight=%u, LostBytes=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, 
        Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

    if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
        CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
    }

    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
    Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlOnEcn(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ECN_EVENT* EcnEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    printf("[CubicProbe][%p][%.3fms] ECN DETECTED: InFlight=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->BytesInFlight);

    // [Note] Using LargestPacketNumberAcked (Standard MsQuic)
    if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
        Connection->Stats.Send.EcnCongestionCount++;
        CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
    }

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
CubicProbeCongestionControlOnSpuriousCongestionEvent(
    _In_ QUIC_CONGESTION_CONTROL* Cc
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    
    if (!Cubic->IsInRecovery) return FALSE;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    uint32_t PrevCwnd = Cubic->CongestionWindow;

    // Revert CWND
    Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;

    Cc->CubicProbe.EndOfRoundSeq = Connection->Send.NextPacketNumber;
    Cc->CubicProbe.IsRoundClean = TRUE;

    // [Log Requirement] Spurious Revert
    printf("[CubicProbe][%p][%.3fms] CWND UPDATE (SPURIOUS): %u -> %u\n", 
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, PrevCwnd, Cubic->CongestionWindow);

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

// =========================================================================
// Standard MsQuic Interface Functions (Boilerplate)
// =========================================================================

BOOLEAN
CubicProbeCongestionControlCanSend(
    _In_ QUIC_CONGESTION_CONTROL* Cc
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
}

void
CubicProbeCongestionControlSetExemption(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint8_t NumPackets
    )
{
    Cc->CubicProbe.Cubic.Exemptions = NumPackets;
}

uint32_t
CubicProbeCongestionControlGetSendAllowance(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint64_t TimeSinceLastSend,
    _In_ BOOLEAN TimeSinceLastSendValid
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    uint32_t SendAllowance;

    // Pacing logic (Standard CUBIC)
    if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
        SendAllowance = 0;
    } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample || Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
        SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
    } else {
        uint64_t EstimatedWnd;
        if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
            EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
            if (EstimatedWnd > Cubic->SlowStartThreshold) {
                EstimatedWnd = Cubic->SlowStartThreshold;
            }
        } else {
            EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
        }
        SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
        if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
            SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
        }
        Cubic->LastSendAllowance = SendAllowance;
    }
    return SendAllowance;
}

void
CubicProbeCongestionControlOnDataSent(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint32_t NumRetransmittableBytes
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    Cubic->BytesInFlight += NumRetransmittableBytes;
    if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
        Cubic->BytesInFlightMax = Cubic->BytesInFlight;
        QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
    }
    if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
        Cubic->LastSendAllowance = 0;
    } else {
        Cubic->LastSendAllowance -= NumRetransmittableBytes;
    }
    if (Cubic->Exemptions > 0) {
        --Cubic->Exemptions;
    }

    CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

BOOLEAN
CubicProbeCongestionControlOnDataInvalidated(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ uint32_t NumRetransmittableBytes
    )
{
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
    Cubic->BytesInFlight -= NumRetransmittableBytes;

    return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
}

void
CubicProbeCongestionControlLogOutFlowStatus(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
}

uint32_t
CubicProbeCongestionControlGetBytesInFlightMax(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.BytesInFlightMax;
}

uint8_t
CubicProbeCongestionControlGetExemptions(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.Exemptions;
}

uint32_t
CubicProbeCongestionControlGetCongestionWindow(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    return Cc->CubicProbe.Cubic.CongestionWindow;
}

BOOLEAN
CubicProbeCongestionControlIsAppLimited(
    _In_ const QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
    return FALSE;
}

void
CubicProbeCongestionControlSetAppLimited(
    _In_ struct QUIC_CONGESTION_CONTROL* Cc
    )
{
    UNREFERENCED_PARAMETER(Cc);
}

void
CubicProbeCongestionControlGetNetworkStatistics(
    _In_ const QUIC_CONNECTION* const Connection,
    _In_ const QUIC_CONGESTION_CONTROL* const Cc,
    _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
    )
{
    const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    const QUIC_PATH* Path = &Connection->Paths[0];

    NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
    NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
    NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
    NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
    NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
    NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
}

static BOOLEAN
CubicProbeCongestionControlUpdateBlockedState(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN PreviousCanSendState
    )
{
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

// =========================================================================
// Initialization and VTable
// =========================================================================

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
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    )
{
    // Copy VTable
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
    Cubic->WindowMax = 0;
    Cubic->WindowLastMax = 0;

    CubicProbe->MinRttUs = UINT64_MAX;
    
    // Initialize Boost State
    CubicProbeResetStats(CubicProbe, Connection);

    printf("[CubicProbe][%p][%.3fms] INIT: CWND=%u\n",
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);
}