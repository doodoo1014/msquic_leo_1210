// // // /*
// // //     CubicBoost V20: Cumulative Elasticity (Fixing Low E issue)
// // //     - Logic: Compare current metrics against 'Epoch Baseline' (start of congestion avoidance),
// // //              instead of the immediate previous round.
// // //     - Benefit: Accumulates small changes over time to detect meaningful elasticity.
// // // */

// // // #include "precomp.h"
// // // #include <stdio.h>
// // // #include "cubicprobe.h" 

// // // // =========================================================================
// // // // Constants
// // // // =========================================================================

// // // #define TEN_TIMES_BETA_CUBIC 7  
// // // #define TEN_TIMES_C_CUBIC 4     
// // // #define PROBE_SENSITIVITY_GAMMA 4       
// // // #define PROBE_MIN_NOISE_MARGIN_US 4000  

// // // // =========================================================================
// // // // Helper Functions
// // // // =========================================================================

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static uint32_t CubeRoot(uint32_t Radicand)
// // // {
// // //     int i;
// // //     uint32_t x = 0;
// // //     uint32_t y = 0;
// // //     for (i = 30; i >= 0; i -= 3) {
// // //         x = x * 8 + ((Radicand >> i) & 7);
// // //         if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
// // //             y = y * 2 + 1;
// // //         } else {
// // //             y = y * 2;
// // //         }
// // //     }
// // //     return y;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void CubicProbeResetPhysicsState(_In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe)
// // // {
// // //     CubicProbe->MinRttUs = UINT64_MAX;
    
// // //     // Round Tracking
// // //     CubicProbe->ProbeTargetPacketNumber = 0; 
// // //     CubicProbe->RoundInFlightBytes = 0;
// // //     CubicProbe->RoundStartTime = CxPlatTimeUs64();
    
// // //     // [New] Epoch Baselines (누적 계산을 위한 기준점)
// // //     CubicProbe->EpochStartBandwidth = 0;
// // //     CubicProbe->EpochStartCwnd = 0;
    
// // //     CubicProbe->CurrentElasticity = 0.0;
// // //     CubicProbe->IsQueueBuilding = FALSE;
// // //     CubicProbe->AckCountForGrowth = 0;
// // // }

// // // // =========================================================================
// // // // Logic 1: Safety Check & RTT (Per ACK)
// // // // =========================================================================

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeCheckSafety(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ACK_EVENT* AckEvent
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const QUIC_PATH* Path = &Connection->Paths[0];

// // //     if (AckEvent->MinRttValid) {
// // //         if (CubicProbe->MinRttUs == UINT64_MAX || AckEvent->MinRtt < CubicProbe->MinRttUs) {
// // //             CubicProbe->MinRttUs = AckEvent->MinRtt;
// // //         }
// // //     }

// // //     CubicProbe->RttVariance = Path->RttVariance; 
// // //     uint64_t NoiseMargin = PROBE_SENSITIVITY_GAMMA * CubicProbe->RttVariance;
// // //     if (NoiseMargin < PROBE_MIN_NOISE_MARGIN_US) NoiseMargin = PROBE_MIN_NOISE_MARGIN_US;

// // //     uint64_t BaselineRtt = (Path->SmoothedRtt > 0) ? Path->SmoothedRtt : CubicProbe->MinRttUs;
// // //     uint64_t Threshold = BaselineRtt + NoiseMargin;

// // //     if (AckEvent->MinRtt > Threshold) {
// // //         CubicProbe->IsQueueBuilding = TRUE; 
        
// // //         // [Important] If queue is building, reset the Epoch Baseline.
// // //         // We want to measure elasticity only during "clean" intervals.
// // //         CubicProbe->EpochStartBandwidth = 0; 
// // //     } else {
// // //         CubicProbe->IsQueueBuilding = FALSE; 
// // //     }

// // //     CubicProbe->RoundInFlightBytes += AckEvent->NumRetransmittableBytes;
// // // }

// // // // =========================================================================
// // // // Logic 2: Cumulative Elasticity Check (Per Round)
// // // // =========================================================================

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeCheckElasticity(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ACK_EVENT* AckEvent
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

// // //     if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
        
// // //         uint64_t TimeNow = AckEvent->TimeNow;
// // //         uint64_t TimeDelta = CxPlatTimeDiff64(CubicProbe->RoundStartTime, TimeNow);
        
// // //         uint64_t CurrentBW = 0;
// // //         if (TimeDelta > 0) {
// // //             CurrentBW = CubicProbe->RoundInFlightBytes * 1000000 / TimeDelta;
// // //         }
// // //         uint32_t CurrentCwnd = Cubic->CongestionWindow;

// // //         // [Epoch Initialization]
// // //         // If we don't have a baseline yet (start of connection or after congestion), set it.
// // //         if (CubicProbe->EpochStartBandwidth == 0 || CubicProbe->EpochStartCwnd == 0) {
// // //             CubicProbe->EpochStartBandwidth = CurrentBW;
// // //             CubicProbe->EpochStartCwnd = CurrentCwnd;
// // //             CubicProbe->CurrentElasticity = 0.0;
// // //         } 
// // //         else {
// // //             // [Cumulative Calculation]
// // //             // Calculate growth relative to the START of the epoch, not just previous round.
            
// // //             // Prevent division by zero
// // //             if (CubicProbe->EpochStartBandwidth > 0 && CubicProbe->EpochStartCwnd > 0) {
                
// // //                 double BwGrowth = (double)((int64_t)CurrentBW - (int64_t)CubicProbe->EpochStartBandwidth) / (double)CubicProbe->EpochStartBandwidth;
// // //                 double CwndGrowth = (double)((int64_t)CurrentCwnd - (int64_t)CubicProbe->EpochStartCwnd) / (double)CubicProbe->EpochStartCwnd;

// // //                 // Only update E if we have pushed CWND enough (> 2%) to measure a reaction
// // //                 if (CwndGrowth > 0.02) {
// // //                     double NewE = BwGrowth / CwndGrowth;
                    
// // //                     // Allow E to go slightly above 1.0 due to noise, but clamp for logic
// // //                     if (NewE > 1.0) NewE = 1.0;
// // //                     if (NewE < 0.0) NewE = 0.0;
                    
// // //                     CubicProbe->CurrentElasticity = NewE;
// // //                 }
// // //             }
// // //         }
        
// // //         // [Baseline Reset Condition]
// // //         // If BW dropped significantly below baseline, reset the epoch (re-calibration).
// // //         if (CurrentBW < CubicProbe->EpochStartBandwidth * 0.9) {
// // //             CubicProbe->EpochStartBandwidth = CurrentBW;
// // //             CubicProbe->EpochStartCwnd = CurrentCwnd;
// // //         }

// // //         if (CubicProbe->CurrentElasticity > 0.1) {
// // //             printf("[Round] E=%.2f, CurrBW=%lu (BaseBW=%lu)\n", 
// // //                 CubicProbe->CurrentElasticity, CurrentBW, CubicProbe->EpochStartBandwidth);
// // //         }

// // //         // Reset for Next Round
// // //         CubicProbe->RoundInFlightBytes = 0;
// // //         CubicProbe->RoundStartTime = TimeNow;
// // //         CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; 
// // //     }
// // // }

// // // // =========================================================================
// // // // Logic 3: Target Calculation
// // // // =========================================================================

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeUpdate(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ACK_EVENT* AckEvent,
// // //     _In_ uint16_t DatagramPayloadLength,
// // //     _Out_ uint32_t* AckTarget
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;

// // //     // --- Standard CUBIC ---
// // //     if (Cubic->TimeOfCongAvoidStart == 0) {
// // //         Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
// // //         if (Cubic->CongestionWindow < Cubic->WindowMax) {
// // //             if (DatagramPayloadLength > 0) {
// // //                 uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
// // //                 uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
// // //                 Cubic->KCubic = CubeRoot(radicand);
// // //                 Cubic->KCubic = S_TO_MS(Cubic->KCubic);
// // //                 Cubic->KCubic >>= 3; 
// // //             } else {
// // //                 Cubic->KCubic = 0;
// // //             }
// // //         } else {
// // //             Cubic->KCubic = 0;
// // //             Cubic->WindowMax = Cubic->CongestionWindow;
// // //         }
// // //     }

// // //     const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
// // //     int64_t TimeDeltaMs = (int64_t)(t_us / 1000) - (int64_t)Cubic->KCubic;
// // //     int64_t CubicTerm = ((((TimeDeltaMs * TimeDeltaMs) >> 10) * TimeDeltaMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

// // //     uint32_t W_cubic_bytes;
// // //     if (TimeDeltaMs < 0) {
// // //         W_cubic_bytes = Cubic->WindowMax - (uint32_t)(-CubicTerm);
// // //     } else {
// // //         W_cubic_bytes = Cubic->WindowMax + (uint32_t)CubicTerm;
// // //     }

// // //     uint32_t N_cubic;
// // //     if (W_cubic_bytes > Cubic->CongestionWindow) {
// // //         uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
// // //         uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
// // //         uint32_t DiffSegments = (TargetSegments > CwndSegments) ? (TargetSegments - CwndSegments) : 1;
// // //         N_cubic = CwndSegments / DiffSegments;
// // //     } else {
// // //         N_cubic = 100 * (Cubic->CongestionWindow / DatagramPayloadLength); 
// // //     }

// // //     // --- Boost Logic ---
// // //     if (CubicProbe->IsQueueBuilding) {
// // //         *AckTarget = N_cubic; // Safety First
// // //     }
// // //     else {
// // //         double E = CubicProbe->CurrentElasticity;
        
// // //         // [Deterministic] If Elasticity is unknown (0), perform minimum linear growth (Reno)
// // //         // to accumulate enough CWND change for the next Epoch calculation.
// // //         if (E < 0.1) {
// // //             uint32_t N_reno = Cubic->CongestionWindow / DatagramPayloadLength;
// // //             if (N_reno < 1) N_reno = 1;
            
// // //             // Use Linear Growth (Reno) as the baseline for probing
// // //             // This is faster than CUBIC plateau, ensuring we get dCWND > 0.02 soon.
// // //             *AckTarget = N_reno; 
// // //         } 
// // //         else {
// // //             // Linear Interpolation: 
// // //             // Target = (1-E)*N_cubic + E*1(SlowStart)
// // //             // But since N_cubic can be huge, we cap the baseline at Reno to ensure responsiveness.
            
// // //             uint32_t N_baseline = Cubic->CongestionWindow / DatagramPayloadLength; // Reno
// // //             if (N_baseline > N_cubic) N_baseline = N_cubic; // Respect CUBIC if it's faster

// // //             double TargetDouble = ((1.0 - E) * (double)N_baseline) + (E * 1.0);
// // //             *AckTarget = (uint32_t)TargetDouble;
// // //         }
        
// // //         if (*AckTarget < 1) *AckTarget = 1;
// // //     }

// // //     if (*AckTarget < N_cubic && !CubicProbe->IsQueueBuilding) {
// // //         // printf("[Boost] N=%u -> Tgt=%u (E=%.2f)\n", N_cubic, *AckTarget, CubicProbe->CurrentElasticity);
// // //     }
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void
// // // CubicProbeIncreaseWindow(
// // //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// // //     _In_ const QUIC_ACK_EVENT* AckEvent,
// // //     _In_ uint32_t AckTarget,
// // //     _In_ uint16_t DatagramPayloadLength
// // //     )
// // // {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

// // //     uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
// // //     CubicProbe->AckCountForGrowth += AckedSegments;

// // //     if (CubicProbe->AckCountForGrowth >= AckTarget) {
        
// // //         uint32_t GrowthSegments = CubicProbe->AckCountForGrowth / AckTarget;
// // //         uint32_t PrevCwnd = Cubic->CongestionWindow;
        
// // //         Cubic->CongestionWindow += (GrowthSegments * DatagramPayloadLength);
// // //         CubicProbe->AckCountForGrowth %= AckTarget;

// // //         printf("[CubicProbe][%p][%.3fms] CWND+: %u -> %u (Tgt=%u, Grow=%u, E=%.2f)\n",
// // //             (void*)Connection, 
// // //             (double)AckEvent->TimeNow / 1000.0, 
// // //             PrevCwnd, 
// // //             Cubic->CongestionWindow, 
// // //             AckTarget,
// // //             GrowthSegments,
// // //             CubicProbe->CurrentElasticity);
// // //     }
// // // }

// // // // =========================================================================
// // // // Interface Implementation
// // // // =========================================================================

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets) {
// // //     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlReset(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN FullReset) {
// // //     UNREFERENCED_PARAMETER(FullReset);

// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const QUIC_SETTINGS_INTERNAL* Settings = &Connection->Settings;
// // //     const QUIC_PATH* Path = &Connection->Paths[0];
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// // //     Cubic->SlowStartThreshold = UINT32_MAX;
// // //     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
// // //     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
// // //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// // //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// // //     if (FullReset) Cubic->BytesInFlight = 0;
// // //     Cubic->WindowMax = 0; 
    
// // //     CubicProbe->MinRttUs = UINT64_MAX;
// // //     CubicProbeResetPhysicsState(CubicProbe);
    
// // //     printf("[Init] CubicBoost V20 (Cumulative). CWND=%u\n", Cubic->CongestionWindow);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     uint32_t SendAllowance;

// // //     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
// // //         SendAllowance = 0;
// // //     } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample) {
// // //         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// // //     } else {
// // //         uint64_t EstimatedWnd = (Cubic->CongestionWindow < Cubic->SlowStartThreshold) ? ((uint64_t)Cubic->CongestionWindow << 1) : (Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2));
// // //         if (EstimatedWnd > Cubic->SlowStartThreshold && Cubic->CongestionWindow < Cubic->SlowStartThreshold) EstimatedWnd = Cubic->SlowStartThreshold;
        
// // //         SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
// // //         if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
// // //             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// // //         }
// // //         Cubic->LastSendAllowance = SendAllowance;
// // //     }
// // //     return SendAllowance;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState) {
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
// // //         if (PreviousCanSendState) {
// // //             QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// // //         } else {
// // //             QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// // //             Connection->Send.LastFlushTime = CxPlatTimeUs64();
// // //             return TRUE;
// // //         }
// // //     }
// // //     return FALSE;
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN CubicProbeCongestionControlOnDataAcknowledged(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent) {
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

// // //     if (Cubic->IsInRecovery) {
// // //         if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
// // //             Cubic->IsInRecovery = FALSE;
            
// // //             // [Fix] Start new Epoch on Recovery Exit
// // //             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
// // //             CubicProbe->RoundStartTime = AckEvent->TimeNow;
// // //             CubicProbe->EpochStartBandwidth = 0; // Reset Epoch
// // //             CubicProbe->EpochStartCwnd = 0;
            
// // //             printf("[Recovery] Exit. CWND=%u\n", Cubic->CongestionWindow);
// // //         }
// // //         goto Exit;
// // //     }
// // //     if (AckEvent->NumRetransmittableBytes == 0) goto Exit;

// // //     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
// // //         // Slow Start Phase
// // //         uint32_t PrevCwnd = Cubic->CongestionWindow;
// // //         Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

// // //         printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
// // //             (void*)Connection, (double)AckEvent->TimeNow / 1000.0, PrevCwnd, Cubic->CongestionWindow);

// // //         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
// // //             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            
// // //             // Initialize Tracking on Exit SS
// // //             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
// // //             CubicProbe->RoundStartTime = AckEvent->TimeNow;
// // //             CubicProbe->RoundInFlightBytes = 0;
// // //             CubicProbe->EpochStartBandwidth = 0;
// // //         }
// // //     } else {
// // //         // Congestion Avoidance Phase
// // //         const QUIC_PATH* Path = &Connection->Paths[0];
// // //         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
// // //         if (DatagramPayloadLength == 0) goto Exit;

// // //         CubicProbeCheckSafety(Cc, AckEvent);
// // //         CubicProbeCheckElasticity(Cc, AckEvent); // Cumulative Check

// // //         uint32_t AckTarget = 0;
// // //         CubicProbeUpdate(Cc, AckEvent, DatagramPayloadLength, &AckTarget);
// // //         CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget, DatagramPayloadLength);
// // //     }

// // // Exit:
// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// // //     Cubic->BytesInFlight += NumRetransmittableBytes;
// // //     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
// // //         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
// // //         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
// // //     }
// // //     if (NumRetransmittableBytes > Cubic->LastSendAllowance) Cubic->LastSendAllowance = 0;
// // //     else Cubic->LastSendAllowance -= NumRetransmittableBytes;
// // //     if (Cubic->Exemptions > 0) --Cubic->Exemptions;

// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta) {
// // //     UNREFERENCED_PARAMETER(IsPersistentCongestion);
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const QUIC_PATH* Path = &Connection->Paths[0];
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    
// // //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// // //     CubicProbeResetPhysicsState(CubicProbe);
// // //     CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber; // Reset Round

// // //     if (!Cubic->IsInRecovery) Cubic->IsInRecovery = TRUE;
// // //     Cubic->HasHadCongestionEvent = TRUE;

// // //     if (!Ecn) Cubic->PrevCongestionWindow = Cubic->CongestionWindow;

// // //     Cubic->WindowLastMax = Cubic->WindowMax;
// // //     Cubic->WindowMax = Cubic->CongestionWindow;
// // //     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
// // //         Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
// // //     }

// // //     uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
// // //     Cubic->SlowStartThreshold = Cubic->CongestionWindow = CXPLAT_MAX(MinCongestionWindow, (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));
// // //     Cubic->TimeOfCongAvoidStart = 0;

// // //     printf("[LOSS] Event. CWND: %u -> %u\n", PrevCwnd, Cubic->CongestionWindow);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     // printf("[CubicProbe][%p][%.3fms] LOSS EVENT: CWnd=%u, InFlight=%u, LostBytes=%u\n",
// //     //     (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

// // //     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
// // //         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
// // //         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
// // //     }
// // //     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;
// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     // printf("[CubicProbe][%p][%.3fms] ECN EVENT: CWnd=%u, InFlight=%u\n",
// //     //     (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

// // //     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
// // //         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
// // //         Connection->Stats.Send.EcnCongestionCount++;
// // //         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
// // //     }
// // //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN CubicProbeCongestionControlOnDataInvalidated(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
// // //     Cubic->BytesInFlight -= NumRetransmittableBytes;
// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc) {
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

// // //     if (!Cubic->IsInRecovery) return FALSE;
// // //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);
// // //     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
// // //     Cubic->IsInRecovery = FALSE;
// // //     Cubic->HasHadCongestionEvent = FALSE;
    
// //     // printf("[CubicProbe][%p][%.3fms] SPURIOUS Revert: CWND -> %u\n", 
// //     //     (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);

// // //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // // }

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }
// // // uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.BytesInFlightMax; }
// // // uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.Exemptions; }
// // // uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.CongestionWindow; }
// // // BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); return FALSE; }
// // // void CubicProbeCongestionControlSetAppLimited(_In_ struct QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }

// // // void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics) {
// // //     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// // //     const QUIC_PATH* Path = &Connection->Paths[0];
// // //     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
// // //     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
// // //     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
// // //     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
// // //     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
// // //     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// // // }

// // // static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
// // //     .Name = "CubicBoost",
// // //     .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
// // //     .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
// // //     .QuicCongestionControlReset = CubicProbeCongestionControlReset,
// // //     .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
// // //     .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
// // //     .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
// // //     .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
// // //     .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
// // //     .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
// // //     .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
// // //     .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
// // //     .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
// // //     .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
// // //     .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
// // //     .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
// // //     .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
// // //     .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
// // // };

// // // _IRQL_requires_max_(DISPATCH_LEVEL)
// // // void CubicProbeCongestionControlInitialize(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_SETTINGS_INTERNAL* Settings) {
// // //     *Cc = QuicCongestionControlCubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// // //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// // //     // [Fix] Declare Connection here
// // //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// // //     const QUIC_PATH* Path = &Connection->Paths[0];
// // //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// // //     Cubic->SlowStartThreshold = UINT32_MAX;
// // //     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
// // //     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
// // //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// // //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// // //     Cubic->BytesInFlight = 0; 
// // //     Cubic->WindowMax = 0; 
    
// // //     CubicProbe->MinRttUs = UINT64_MAX;
// // //     CubicProbeResetPhysicsState(CubicProbe);
    
// // //     printf("[Init] CubicBoost V20 (Cumulative). CWND=%u\n", Cubic->CongestionWindow);
// // // }



// // /*++

// // Copyright (c) Microsoft Corporation.
// // Licensed under the MIT License.

// // Module Name:

// //     cubicprobe.c

// // Abstract:

// //     Implementation of CubicBoost v3 with Comprehensive Logging.
// //     - Standard Slow Start.
// //     - CubicBoost Logic applies only in Congestion Avoidance.
// //     - Logs every CWND change.

// // --*/

// // #include "precomp.h"
// // #include <stdio.h>
// // #include <math.h> 
// // #include "cubicprobe.h"

// // // Constants from RFC8312
// // #define TEN_TIMES_BETA_CUBIC 7 
// // #define TEN_TIMES_C_CUBIC 4
// // #define CUBIC_BOOST_FACTOR 1.5

// // // [Safety] Minimum ACK Target to prevent Microburst (1 = Grow every ACK)
// // #define MIN_ACK_TARGET 1 

// // // Forward declarations
// // static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta);
// // static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget);
// // static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

// // // CanSend declaration
// // BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);


// // // =========================================================================
// // // Helper Functions
// // // =========================================================================

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // static uint32_t
// // CubeRoot(uint32_t Radicand)
// // {
// //     int i;
// //     uint32_t x = 0;
// //     uint32_t y = 0;
// //     for (i = 30; i >= 0; i -= 3) {
// //         x = x * 8 + ((Radicand >> i) & 7);
// //         if ((y * 2 + 1) * (y * 2 + 1) * (y * 2 + 1) <= x) {
// //             y = y * 2 + 1;
// //         } else {
// //             y = y * 2;
// //         }
// //     }
// //     return y;
// // }

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // static void
// // CubicProbeResetStats(
// //     _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe,
// //     _In_ const QUIC_CONNECTION* Connection
// //     )
// // {
// //     // [CubicBoost] Reset Acceleration
// //     CubicProbe->StableRoundCount = 0;
// //     CubicProbe->IsRoundClean = TRUE;
    
// //     // Reset Round Trigger
// //     CubicProbe->EndOfRoundSeq = Connection->Send.NextPacketNumber;
    
// //     // Initial Threshold (Infinite until first RTT sample)
// //     CubicProbe->RoundRttThresh = UINT64_MAX;
    
// //     // Reset Accumulator
// //     CubicProbe->BytesAckedAccumulator = 0;
// // }

// // // =========================================================================
// // // Reset Function
// // // =========================================================================

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // void
// // CubicProbeCongestionControlReset(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ BOOLEAN FullReset
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     const QUIC_PATH* Path = &Connection->Paths[0];
// //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// //     Cubic->SlowStartThreshold = UINT32_MAX;
// //     Cubic->IsInRecovery = FALSE;
// //     Cubic->HasHadCongestionEvent = FALSE;
// //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// //     Cubic->LastSendAllowance = 0;
// //     if (FullReset) {
// //         Cubic->BytesInFlight = 0;
// //     }
// //     Cubic->WindowMax = 0;
// //     Cubic->WindowLastMax = 0;
// //     Cubic->TimeOfCongAvoidStart = 0;
// //     Cubic->KCubic = 0;

// //     CubicProbe->MinRttUs = UINT64_MAX;
    
// //     CubicProbeResetStats(CubicProbe, Connection);

// //     printf("[CubicProbe][%p][%.3fms] RESET: Full=%d, CWND: %u -> %u\n",
// //         (void*)Connection, (double)CxPlatTimeUs64()/1000.0, FullReset, PrevCwnd, Cubic->CongestionWindow);
// // }


// // // =========================================================================
// // // Core Logic: CubicBoost v3 (Only used in Congestion Avoidance)
// // // =========================================================================

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // static void
// // CubicProbeUpdate(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_ACK_EVENT* AckEvent,
// //     _Out_ uint32_t* AckTarget
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     const QUIC_PATH* Path = &Connection->Paths[0];
// //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// //     // 1. K 계산 (기존 로직 유지)
// //     if (Cubic->TimeOfCongAvoidStart == 0) {
// //         Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
// //         if (Cubic->CongestionWindow < Cubic->WindowMax) {
// //             if (DatagramPayloadLength > 0) {
// //                 uint32_t W_max_in_mss = (Cubic->WindowMax - Cubic->CongestionWindow) / DatagramPayloadLength;
// //                 uint32_t radicand = (W_max_in_mss * (10) << 9) / TEN_TIMES_C_CUBIC;
// //                 Cubic->KCubic = CubeRoot(radicand);
// //                 Cubic->KCubic = S_TO_MS(Cubic->KCubic);
// //                 Cubic->KCubic >>= 3;
// //             } else {
// //                 Cubic->KCubic = 0;
// //             }
// //         } else {
// //             Cubic->KCubic = 0;
// //             Cubic->WindowMax = Cubic->CongestionWindow;
// //         }
// //     }

// //     // 2. [Time-Shift] 가상 시간 계산 (Boost 적용)
// //     uint64_t t_us_real = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
// //     uint64_t TimeShiftUs = 0;

// //     if (CubicProbe->StableRoundCount > 0 && Path->SmoothedRtt > 0) {
// //         // BoostFactor를 통해 시간을 미래로 당김
// //         TimeShiftUs = (uint64_t)((double)CubicProbe->StableRoundCount * (double)Path->SmoothedRtt * CUBIC_BOOST_FACTOR);
// //     }
    
// //     uint64_t t_us_virtual = t_us_real + TimeShiftUs;

// //     // 3. Target Window (W_target) 계산 - [중요] Byte 단위 실수 연산
// //     const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;
// //     int64_t TimeDeltaUs = (int64_t)t_us_virtual - (int64_t)K_us;
// //     int64_t OffsetMs = (TimeDeltaUs / 1000);

// //     // CUBIC Term: C * (t - K)^3 (Byte 단위 근사값)
// //     int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);
    
// //     // W_target을 double(Bytes)로 유지 -> 소수점 손실 방지
// //     double W_target_bytes;
// //     if (TimeDeltaUs < 0) W_target_bytes = (double)Cubic->WindowMax - (double)(-CubicTerm);
// //     else W_target_bytes = (double)Cubic->WindowMax + (double)CubicTerm;

// //     // 4. AckTarget 계산
// //     // "현재 CWND(Byte) 대비 목표(Byte)가 얼마나 큰가?"를 비율로 계산
// //     double CwndBytes = (double)Cubic->CongestionWindow;

// //     if (W_target_bytes > CwndBytes) {
// //         // DeltaBytes: 목표치와 현재치의 차이 (예: 2190 bytes = 1.5 MSS)
// //         double DeltaBytes = W_target_bytes - CwndBytes;
        
// //         // 0으로 나누기 방지 (아주 작은 값이라도 설정)
// //         if (DeltaBytes < 1.0) DeltaBytes = 1.0;

// //         // AckTarget 공식: Cwnd / Delta
// //         // 예: Cwnd=146000, Delta=2190 (1.5 MSS)
// //         // AckTarget = 146000 / 2190 = 66.6 -> 66
// //         // (ACK 66개 받으면 1 MSS 증가 -> 100개보다 빠름 -> 곡선 형성)
        
// //         // MSS 단위로 변환해서 계산하는 것보다 Byte 자체 비율이 가장 정확함
// //         // (Cwnd / DeltaBytes) * MSS_Size 로 접근하는 것이 아님.
// //         // 표준 공식: cnt = cwnd / (w_cubic - cwnd)  <-- 여기서 cwnd, w_cubic은 패킷 단위
// //         // 이를 Byte 단위로 치환하면:
// //         // cnt = (CwndBytes / MSS) / ((TargetBytes - CwndBytes) / MSS)
// //         //     = CwndBytes / (TargetBytes - CwndBytes)
        
// //         *AckTarget = (uint32_t)(CwndBytes / DeltaBytes);

// //     } else {
// //         // Standard CUBIC (TCP Friendly Region or Plateau)
// //         // 여기서는 안전하게 기존 로직 사용 (너무 느린 구간)
// //         *AckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
// //     }

// //     // [Safety] Microburst 방지
// //     if (*AckTarget < MIN_ACK_TARGET) {
// //         *AckTarget = MIN_ACK_TARGET;
// //     }
// // }


// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // BOOLEAN
// // CubicProbeCongestionControlOnDataAcknowledged(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_ACK_EVENT* AckEvent
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     const QUIC_PATH* Path = &Connection->Paths[0];
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     // [Log Checkpoint] Save Previous CWND
// //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// //     // Update BytesInFlight
// //     Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

// //     // Handle Recovery
// //     if (Cubic->IsInRecovery) {
// //         if (AckEvent->LargestSentPacketNumber > Cubic->RecoverySentPacketNumber) {
// //             Cubic->IsInRecovery = FALSE;
// //             CubicProbeResetStats(CubicProbe, Connection);
            
// //             printf("[CubicProbe][%p][%.3fms] RECOVERY EXIT: CWND=%u\n",
// //                 (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->CongestionWindow);
// //         }
// //         goto Exit;
// //     }

// //     if (AckEvent->NumRetransmittableBytes == 0) goto Exit;


// //     // ---------------------------------------------------------------------
// //     // [CubicBoost] Round-based Statistics (Always Active for Tracking)
// //     // ---------------------------------------------------------------------

// //     uint64_t CurrentRtt = (AckEvent->MinRttValid) ? AckEvent->MinRtt : Path->LatestRttSample;
    
// //     if (CubicProbe->RoundRttThresh != UINT64_MAX) {
// //         if (CurrentRtt > CubicProbe->RoundRttThresh) {
// //             if (CubicProbe->IsRoundClean) {
// //                 printf("[CubicProbe][%p][%.3fms] ROUND DIRTY: RTT(%llu) > Thresh(%llu)\n",
// //                     (void*)Connection, (double)AckEvent->TimeNow/1000.0, 
// //                     (unsigned long long)CurrentRtt, (unsigned long long)CubicProbe->RoundRttThresh);
// //             }
// //             CubicProbe->IsRoundClean = FALSE;
// //         }
// //     }

// //     // Check End of Round
// //     if (AckEvent->LargestSentPacketNumber >= CubicProbe->EndOfRoundSeq) {
// //         // --- Round Boundary ---
// //         uint32_t PrevS = CubicProbe->StableRoundCount;

// //         if (CubicProbe->IsRoundClean) {
// //             CubicProbe->StableRoundCount++;
// //         } else {
// //             CubicProbe->StableRoundCount = 0;
// //         }

// //         // Snapshot Threshold for the NEXT round
// //         if (Path->SmoothedRtt > 0) {
// //             CubicProbe->RoundRttThresh = Path->SmoothedRtt + (CUBIC_BOOST_GAMMA * Path->RttVariance);
// //         } else {
// //             CubicProbe->RoundRttThresh = UINT64_MAX;
// //         }

// //         // Only log round end if momentum changed or just periodically useful?
// //         // Let's keep logging to see stability
// //         printf("[CubicProbe][%p][%.3fms] ROUND END: Clean=%d, Momentum=%u->%u, NextThresh=%llu\n",
// //              (void*)Connection, (double)AckEvent->TimeNow/1000.0, 
// //              CubicProbe->IsRoundClean, PrevS, CubicProbe->StableRoundCount, 
// //              (unsigned long long)CubicProbe->RoundRttThresh);


// //         // Set trigger for next round end
// //         CubicProbe->EndOfRoundSeq = Connection->Send.NextPacketNumber;
// //         CubicProbe->IsRoundClean = TRUE;
// //     }


// //     // ---------------------------------------------------------------------
// //     // CWND Update
// //     // ---------------------------------------------------------------------

// //     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
// //         // =================================================================
// //         // [SLOW START] Standard RFC Implementation (No Boost)
// //         // =================================================================
        
// //         Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;
        
// //         // Transition to CA
// //         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
// //             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
// //             printf("[CubicProbe][%p][%.3fms] SS EXIT -> CA START: SST=%u\n",
// //                  (void*)Connection, (double)AckEvent->TimeNow/1000.0, Cubic->SlowStartThreshold);
// //         }

// //     } else {
// //         // =================================================================
// //         // [CONGESTION AVOIDANCE] CubicBoost Logic Applied Here
// //         // =================================================================
        
// //         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
// //         if (DatagramPayloadLength == 0) goto Exit;

// //         if (CubicProbe->MinRttUs == UINT64_MAX || CubicProbe->MinRttUs > CurrentRtt) {
// //             CubicProbe->MinRttUs = CurrentRtt;
// //         }

// //         uint32_t AckTarget = 0;
// //         CubicProbeUpdate(Cc, AckEvent, &AckTarget); // Calculate Boosted Target

// //         CubicProbe->BytesAckedAccumulator += AckEvent->NumRetransmittableBytes;
// //         uint64_t BytesRequired = (uint64_t)AckTarget * DatagramPayloadLength;

// //         if (CubicProbe->BytesAckedAccumulator >= BytesRequired) {
// //             Cubic->CongestionWindow += DatagramPayloadLength;
// //             CubicProbe->BytesAckedAccumulator -= BytesRequired;
// //         }
// //     }

// // Exit:
// //     // [Log Requirement] Check for CWND Change
// //     if (Cubic->CongestionWindow != PrevCwnd) {
// //         printf("[CubicProbe][%p][%.3fms] CWND UPDATE: %u -> %u\n",
// //             (void*)Connection, (double)AckEvent->TimeNow/1000.0, PrevCwnd, Cubic->CongestionWindow);
// //     }

// //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }


// // // =========================================================================
// // // Congestion Event Handlers (Loss / ECN)
// // // =========================================================================

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // static void
// // CubicProbeCongestionControlOnCongestionEvent(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ BOOLEAN IsPersistentCongestion,
// //     _In_ BOOLEAN Ecn,
// //     _In_ uint32_t TenTimesBeta
// //     )
// // {
// //     UNREFERENCED_PARAMETER(IsPersistentCongestion);

// //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     const QUIC_PATH* Path = &Connection->Paths[0];
// //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// //     // Save PrevCWND for logging
// //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// //     // [CubicBoost] Full reset of boost state on congestion
// //     CubicProbeResetStats(CubicProbe, Connection);

// //     if (!Cubic->IsInRecovery) {
// //          Cubic->IsInRecovery = TRUE;
// //     }
// //     Cubic->HasHadCongestionEvent = TRUE;

// //     if (!Ecn) {
// //         Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
// //     }

// //     Cubic->WindowLastMax = Cubic->WindowMax;
// //     Cubic->WindowMax = Cubic->CongestionWindow;

// //     // Fast Convergence
// //     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
// //         Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
// //     }

// //     uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    
// //     Cubic->SlowStartThreshold =
// //     Cubic->CongestionWindow =
// //         CXPLAT_MAX(
// //             MinCongestionWindow,
// //             (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));

// //     Cubic->TimeOfCongAvoidStart = 0;

// //     // [Log Requirement] CWND Reduced
// //     printf("[CubicProbe][%p][%.3fms] CWND UPDATE (LOSS/ECN): %u -> %u (SST=%u)\n",
// //         (void*)Connection, (double)CxPlatTimeUs64()/1000.0, 
// //         PrevCwnd, Cubic->CongestionWindow, Cubic->SlowStartThreshold);
// // }

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // void
// // CubicProbeCongestionControlOnDataLost(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_LOSS_EVENT* LossEvent
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     printf("[CubicProbe][%p][%.3fms] LOSS DETECTED: InFlight=%u, LostBytes=%u\n",
// //         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, 
// //         Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

// //     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
// //         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
// //         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
// //     }

// //     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
// //     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

// //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // void
// // CubicProbeCongestionControlOnEcn(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_ECN_EVENT* EcnEvent
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     printf("[CubicProbe][%p][%.3fms] ECN DETECTED: InFlight=%u\n",
// //         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->BytesInFlight);

// //     // [Note] Using LargestPacketNumberAcked (Standard MsQuic)
// //     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
// //         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
// //         Connection->Stats.Send.EcnCongestionCount++;
// //         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
// //     }

// //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // BOOLEAN
// // CubicProbeCongestionControlOnSpuriousCongestionEvent(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    
// //     if (!Cubic->IsInRecovery) return FALSE;
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     uint32_t PrevCwnd = Cubic->CongestionWindow;

// //     // Revert CWND
// //     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
// //     Cubic->IsInRecovery = FALSE;
// //     Cubic->HasHadCongestionEvent = FALSE;

// //     Cc->CubicProbe.EndOfRoundSeq = Connection->Send.NextPacketNumber;
// //     Cc->CubicProbe.IsRoundClean = TRUE;

// //     // [Log Requirement] Spurious Revert
// //     printf("[CubicProbe][%p][%.3fms] CWND UPDATE (SPURIOUS): %u -> %u\n", 
// //         (void*)Connection, (double)CxPlatTimeUs64()/1000.0, PrevCwnd, Cubic->CongestionWindow);

// //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }

// // // =========================================================================
// // // Standard MsQuic Interface Functions (Boilerplate)
// // // =========================================================================

// // BOOLEAN
// // CubicProbeCongestionControlCanSend(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// // }

// // void
// // CubicProbeCongestionControlSetExemption(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ uint8_t NumPackets
// //     )
// // {
// //     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// // }

// // uint32_t
// // CubicProbeCongestionControlGetSendAllowance(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ uint64_t TimeSinceLastSend,
// //     _In_ BOOLEAN TimeSinceLastSendValid
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     uint32_t SendAllowance;

// //     // Pacing logic (Standard CUBIC)
// //     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
// //         SendAllowance = 0;
// //     } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample || Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
// //         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// //     } else {
// //         uint64_t EstimatedWnd;
// //         if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
// //             EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
// //             if (EstimatedWnd > Cubic->SlowStartThreshold) {
// //                 EstimatedWnd = Cubic->SlowStartThreshold;
// //             }
// //         } else {
// //             EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
// //         }
// //         SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
// //         if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
// //             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
// //         }
// //         Cubic->LastSendAllowance = SendAllowance;
// //     }
// //     return SendAllowance;
// // }

// // void
// // CubicProbeCongestionControlOnDataSent(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ uint32_t NumRetransmittableBytes
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     Cubic->BytesInFlight += NumRetransmittableBytes;
// //     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
// //         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
// //         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
// //     }
// //     if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
// //         Cubic->LastSendAllowance = 0;
// //     } else {
// //         Cubic->LastSendAllowance -= NumRetransmittableBytes;
// //     }
// //     if (Cubic->Exemptions > 0) {
// //         --Cubic->Exemptions;
// //     }

// //     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }

// // BOOLEAN
// // CubicProbeCongestionControlOnDataInvalidated(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ uint32_t NumRetransmittableBytes
// //     )
// // {
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

// //     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
// //     Cubic->BytesInFlight -= NumRetransmittableBytes;

// //     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// // }

// // void
// // CubicProbeCongestionControlLogOutFlowStatus(
// //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     UNREFERENCED_PARAMETER(Cc);
// // }

// // uint32_t
// // CubicProbeCongestionControlGetBytesInFlightMax(
// //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     return Cc->CubicProbe.Cubic.BytesInFlightMax;
// // }

// // uint8_t
// // CubicProbeCongestionControlGetExemptions(
// //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     return Cc->CubicProbe.Cubic.Exemptions;
// // }

// // uint32_t
// // CubicProbeCongestionControlGetCongestionWindow(
// //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     return Cc->CubicProbe.Cubic.CongestionWindow;
// // }

// // BOOLEAN
// // CubicProbeCongestionControlIsAppLimited(
// //     _In_ const QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     UNREFERENCED_PARAMETER(Cc);
// //     return FALSE;
// // }

// // void
// // CubicProbeCongestionControlSetAppLimited(
// //     _In_ struct QUIC_CONGESTION_CONTROL* Cc
// //     )
// // {
// //     UNREFERENCED_PARAMETER(Cc);
// // }

// // void
// // CubicProbeCongestionControlGetNetworkStatistics(
// //     _In_ const QUIC_CONNECTION* const Connection,
// //     _In_ const QUIC_CONGESTION_CONTROL* const Cc,
// //     _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
// //     )
// // {
// //     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
// //     const QUIC_PATH* Path = &Connection->Paths[0];

// //     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
// //     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
// //     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
// //     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
// //     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
// //     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// // }

// // static BOOLEAN
// // CubicProbeCongestionControlUpdateBlockedState(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ BOOLEAN PreviousCanSendState
// //     )
// // {
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     if (PreviousCanSendState != CubicProbeCongestionControlCanSend(Cc)) {
// //         if (PreviousCanSendState) {
// //             QuicConnAddOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// //         } else {
// //             QuicConnRemoveOutFlowBlockedReason(Connection, QUIC_FLOW_BLOCKED_CONGESTION_CONTROL);
// //             Connection->Send.LastFlushTime = CxPlatTimeUs64();
// //             return TRUE;
// //         }
// //     }
// //     return FALSE;
// // }

// // // =========================================================================
// // // Initialization and VTable
// // // =========================================================================

// // static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
// //     .Name = "CubicBoost",
// //     .QuicCongestionControlCanSend = CubicProbeCongestionControlCanSend,
// //     .QuicCongestionControlSetExemption = CubicProbeCongestionControlSetExemption,
// //     .QuicCongestionControlReset = CubicProbeCongestionControlReset,
// //     .QuicCongestionControlGetSendAllowance = CubicProbeCongestionControlGetSendAllowance,
// //     .QuicCongestionControlOnDataSent = CubicProbeCongestionControlOnDataSent,
// //     .QuicCongestionControlOnDataInvalidated = CubicProbeCongestionControlOnDataInvalidated,
// //     .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
// //     .QuicCongestionControlOnDataLost = CubicProbeCongestionControlOnDataLost,
// //     .QuicCongestionControlOnEcn = CubicProbeCongestionControlOnEcn,
// //     .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeCongestionControlOnSpuriousCongestionEvent,
// //     .QuicCongestionControlLogOutFlowStatus = CubicProbeCongestionControlLogOutFlowStatus,
// //     .QuicCongestionControlGetExemptions = CubicProbeCongestionControlGetExemptions,
// //     .QuicCongestionControlGetBytesInFlightMax = CubicProbeCongestionControlGetBytesInFlightMax,
// //     .QuicCongestionControlIsAppLimited = CubicProbeCongestionControlIsAppLimited,
// //     .QuicCongestionControlSetAppLimited = CubicProbeCongestionControlSetAppLimited,
// //     .QuicCongestionControlGetCongestionWindow = CubicProbeCongestionControlGetCongestionWindow,
// //     .QuicCongestionControlGetNetworkStatistics = CubicProbeCongestionControlGetNetworkStatistics
// // };

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // void
// // CubicProbeCongestionControlInitialize(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_SETTINGS_INTERNAL* Settings
// //     )
// // {
// //     // Copy VTable
// //     *Cc = QuicCongestionControlCubicProbe;
    
// //     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
// //     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    
// //     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
// //     const QUIC_PATH* Path = &Connection->Paths[0];
// //     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

// //     Cubic->SlowStartThreshold = UINT32_MAX;
// //     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
// //     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
    
// //     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
// //     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
// //     Cubic->WindowMax = 0;
// //     Cubic->WindowLastMax = 0;

// //     CubicProbe->MinRttUs = UINT64_MAX;
    
// //     // Initialize Boost State
// //     CubicProbeResetStats(CubicProbe, Connection);

// //     printf("[CubicProbe][%p][%.3fms] INIT: CWND=%u\n",
// //         (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);
// // }







// #include "precomp.h"
// #include <stdio.h>
// #include "cubicprobe.h"

// // Constants from RFC8312 (for CUBIC)
// #define TEN_TIMES_BETA_CUBIC 7 // [유지] 일반 손실/ECN은 0.7배 감소
// #define TEN_TIMES_C_CUBIC 4

// // Constants for CubicProbe logic
// #define TEN_TIMES_BETA_PROBE 7 // [신규] 프로브 실패(RTT 스파이크) 시 0.5배 감소
// #define PROBE_RTT_INTERVAL 2
// #define PROBE_RTT_INCREASE_NUMERATOR 11   // 1.1x RTT threshold (수정 제안)
// #define PROBE_RTT_INCREASE_DENOMINATOR 10

// // Forward declarations
// // [수정] 혼잡 이벤트 함수가 감소 계수(beta)를 인자로 받도록 변경
// static void CubicProbeCongestionControlOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ uint32_t TenTimesBeta);
// static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget);
// static void CubicProbeIncreaseWindow(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _In_ uint32_t AckTarget);
// static void CubicProbePktsAcked(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent);

// BOOLEAN CubicProbeCongestionControlCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);
// static BOOLEAN CubicProbeCongestionControlUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static uint32_t
// CubeRoot(
//     uint32_t Radicand
//     )
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
// static void
// CubicProbeResetProbeState(
//     _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe
//     )
// {
//     CubicProbe->ProbeState = PROBE_INACTIVE;
//     CubicProbe->CumulativeSuccessLevel = 0;
//     CubicProbe->RttAtProbeStartUs = 0;
//     CubicProbe->ProbeTargetPacketNumber = 0;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void
// CubicProbeCongestionControlReset(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN FullReset
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->IsInRecovery = FALSE;
//     Cubic->HasHadCongestionEvent = FALSE;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     Cubic->LastSendAllowance = 0;
//     if (FullReset) {
//         Cubic->BytesInFlight = 0;
//     }
//     Cubic->WindowMax = 0;
//     Cubic->WindowLastMax = 0;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;
//     CubicProbe->MinRttUs = UINT64_MAX;
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbePktsAcked(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];

//     if (!AckEvent->MinRttValid) return;

//     if (CubicProbe->MinRttUs == UINT64_MAX || CubicProbe->MinRttUs > AckEvent->MinRtt) {
//         CubicProbe->MinRttUs = AckEvent->MinRtt;
//     }

//     // 1. Convex Entry Detection (Re-Anchor)
//     if (Cubic->WindowMax > 0 && !CubicProbe->HasCrossedWmax) {
//         if (Cubic->CongestionWindow >= Cubic->WindowMax) {
//             CubicProbe->HasCrossedWmax = TRUE;
//             CubicProbe->CumulativeSuccessLevel = 0;
//             CubicProbe->HasGrownInThisRound = FALSE; // Reset growth flag

//             CubicProbe->RttAtProbeStartUs = AckEvent->MinRtt;
//             uint64_t MaxVar = CubicProbe->RttAtProbeStartUs / 2;
//             CubicProbe->RttVarAtProbeStartUs = Path->RttVariance;
//             if (CubicProbe->RttVarAtProbeStartUs > MaxVar) CubicProbe->RttVarAtProbeStartUs = MaxVar;
//             if (CubicProbe->RttVarAtProbeStartUs < 1333) CubicProbe->RttVarAtProbeStartUs = 1333;

//             CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;

//             printf("[CubicProbe] Entered CONVEX Region! Reset Level=0, Re-Anchor RTT=%.3fms\n",
//                 (double)CubicProbe->RttAtProbeStartUs/1000.0);
//         }
//     }

//     uint64_t FixedThreshold = CubicProbe->RttAtProbeStartUs + (3 * CubicProbe->RttVarAtProbeStartUs);

//     switch (CubicProbe->ProbeState)
//     {
//         case PROBE_INACTIVE:
//         {
//             if (Cubic->WindowMax > 0 && !Cubic->IsInRecovery) {
//                 CubicProbe->ProbeState = PROBE_TEST;
//                 CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
//                 CubicProbe->HasGrownInThisRound = FALSE; // Init flag

//                 if (!CubicProbe->HasCrossedWmax) {
//                     CubicProbe->RttAtProbeStartUs = AckEvent->MinRtt; 
//                     uint64_t MaxVar = CubicProbe->RttAtProbeStartUs / 2;
//                     CubicProbe->RttVarAtProbeStartUs = Path->RttVariance;
//                     if (CubicProbe->RttVarAtProbeStartUs > MaxVar) CubicProbe->RttVarAtProbeStartUs = MaxVar;
//                     if (CubicProbe->RttVarAtProbeStartUs < 1333) CubicProbe->RttVarAtProbeStartUs = 1333;
                    
//                     printf("[CubicProbe] Start Probing (Concave). Anchor=%.3fms\n", 
//                         (double)CubicProbe->RttAtProbeStartUs/1000.0);
//                 }
//             }
//             break;
//         }

//         case PROBE_TEST:
//         case PROBE_WAITING:
//         case PROBE_JUDGMENT:
//         {
//             // End of Round Check
//             if (AckEvent->LargestAck >= CubicProbe->ProbeTargetPacketNumber) {
                
//                 // RTT Validation
//                 if (AckEvent->MinRtt <= FixedThreshold) {
                    
//                     // [CRITICAL FIX] Only increase Level if we actually grew
//                     if (CubicProbe->HasGrownInThisRound) {
//                         if (CubicProbe->CumulativeSuccessLevel < 1000) {
//                             CubicProbe->CumulativeSuccessLevel++;
//                         }
                        
//                         printf("[CubicProbe] Round Success (Lvl %u). RTT=%.3fms <= Threshold. Growth Occurred.\n", 
//                             CubicProbe->CumulativeSuccessLevel, (double)AckEvent->MinRtt / 1000.0);

//                         // Reset flag for the next round
//                         CubicProbe->HasGrownInThisRound = FALSE;

//                     } else {
//                         // Round passed, RTT is good, but we haven't grown yet.
//                         // Do NOT increase level. Just extend the round.
//                         // printf("[CubicProbe] Round Stable (Lvl %u). RTT=%.3fms. No Growth yet.\n", 
//                         //    CubicProbe->CumulativeSuccessLevel, (double)AckEvent->MinRtt / 1000.0);
//                     }

//                     // Start Next Round
//                     CubicProbe->ProbeTargetPacketNumber = Connection->Send.NextPacketNumber;
//                     CubicProbe->ProbeState = PROBE_TEST;

//                 } else {
//                     // RTT Spike -> Fail
//                     printf("[CubicProbe] Round FAILED. RTT=%.3fms > Threshold(%.3fms).\n", 
//                         (double)AckEvent->MinRtt / 1000.0, (double)FixedThreshold / 1000.0);
                    
//                     CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, FALSE, TEN_TIMES_BETA_PROBE);
//                 }
//             }
//             break;
//         }

//         default:
//             break;
//     }
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeUpdate(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _Out_ uint32_t* AckTarget
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     // 1. Standard CUBIC Calculation
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

//     const uint32_t W_max_bytes = Cubic->WindowMax;
//     const uint64_t t_us = CxPlatTimeDiff64(Cubic->TimeOfCongAvoidStart, AckEvent->TimeNow);
//     const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;

//     int64_t TimeDeltaUs = (int64_t)t_us - (int64_t)K_us;
//     int64_t OffsetMs = (TimeDeltaUs / 1000);
//     int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

//     uint32_t W_cubic_bytes;
//     if (TimeDeltaUs < 0) {
//         W_cubic_bytes = W_max_bytes - (uint32_t)(-CubicTerm);
//     } else {
//         W_cubic_bytes = W_max_bytes + (uint32_t)CubicTerm;
//     }

//     uint32_t BaseAckTarget;
//     if (W_cubic_bytes > Cubic->CongestionWindow) {
//         uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
//         uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
//         uint32_t DiffSegments = TargetSegments > CwndSegments ? TargetSegments - CwndSegments : 1;
//         BaseAckTarget = CwndSegments / DiffSegments;
//     } else {
//         BaseAckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
//     }

//     // 2. AckTarget Scaling (Frequency Control)
//     *AckTarget = BaseAckTarget;

//     if (CubicProbe->CumulativeSuccessLevel > 0) {
        
//         // 레벨에 비례하여 빈도를 높임 (Target 숫자를 줄임)
//         // Level 1: 1.5배, Level 2: 2.0배 ...
//         uint32_t ScaleNumerator = 2 + CubicProbe->CumulativeSuccessLevel; 
//         uint32_t ScaleDenominator = 2;

//         *AckTarget = (BaseAckTarget * ScaleDenominator) / ScaleNumerator;
        
//         // 최소값 안전장치
//         if (*AckTarget < 2) *AckTarget = 2;
//     }
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeIncreaseWindow(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent,
//     _In_ uint32_t AckTarget
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
//     CubicProbe->AckCountForGrowth += AckedSegments;

//     // Check if we reached the target to grow
//     if (CubicProbe->AckCountForGrowth >= AckTarget) {
        
//         uint32_t PrevCwnd = Cubic->CongestionWindow; 
//         uint32_t GrowthInSegments = 1;

//         // Strategy: Increase growth step only in Convex region
//         if (CubicProbe->HasCrossedWmax && CubicProbe->CumulativeSuccessLevel > 0) {
//             GrowthInSegments = CubicProbe->CumulativeSuccessLevel;
//             if (GrowthInSegments < 1) GrowthInSegments = 1;
//         }

//         Cubic->CongestionWindow += (GrowthInSegments * DatagramPayloadLength);
//         CubicProbe->AckCountForGrowth -= AckTarget;

//         // [NEW] Mark that we have grown in this round
//         CubicProbe->HasGrownInThisRound = TRUE;

//         // Log output
//         if (CubicProbe->ProbeState != PROBE_INACTIVE) {
//             printf("[CubicProbe][%p][%.3fms] CWND Update (Lvl %u): %u -> %u (Target=%u, Growth=%u, W_max=%u, %s)\n",
//                 (void*)Connection, 
//                 (double)AckEvent->TimeNow / 1000.0, 
//                 CubicProbe->CumulativeSuccessLevel, 
//                 PrevCwnd, 
//                 Cubic->CongestionWindow, 
//                 AckTarget, 
//                 GrowthInSegments,
//                 Cubic->WindowMax,
//                 CubicProbe->HasCrossedWmax ? "Convex" : "Concave");
//         } else {
//              printf("[CubicProbe][%p][%.3fms] CWND Update (CUBIC): %u -> %u (Target=%u)\n",
//                 (void*)Connection, 
//                 (double)AckEvent->TimeNow / 1000.0, 
//                 PrevCwnd, 
//                 Cubic->CongestionWindow, 
//                 AckTarget);
//         }
//     }
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// BOOLEAN
// CubicProbeCongestionControlOnDataAcknowledged(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ACK_EVENT* AckEvent
//     )실패 시 (Congested): 가속 레벨 초기화 혹은 CWND 축소(기존 CUBIC 배
// {
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

//     if (Cubic->IsInRecovery) {
//         if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
//             Cubic->IsInRecovery = FALSE;
//         }
//         goto Exit;
//     }
//     if (AckEvent->NumRetransmittableBytes == 0) {
//         goto Exit;
//     }

//     if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
//         uint32_t PrevCwnd = Cubic->CongestionWindow;

//         Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;

//         printf("[CubicProbe][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
//             (void*)Connection,
//             (double)AckEvent->TimeNow / 1000.0,
//             PrevCwnd,
//             Cubic->CongestionWindow);

//         if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
//             Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;

//             printf("[CubicProbe-SS][%p][%.3fms] SlowStart -> CongestionAvoidance (Ssthresh: %u)\n",
//                 (void*)Connection,
//                 (double)AckEvent->TimeNow / 1000.0,
//                 Cubic->SlowStartThreshold);
//         }

//     } else {
//         const QUIC_PATH* Path = &Connection->Paths[0];
//         const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
//         if (DatagramPayloadLength == 0) goto Exit;

//         CubicProbePktsAcked(Cc, AckEvent);

//         uint32_t AckTarget = 0;
//         CubicProbeUpdate(Cc, AckEvent, &AckTarget);

//         CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget);
//     }

// Exit:
//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// static void
// CubicProbeCongestionControlOnCongestionEvent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN IsPersistentCongestion,
//     _In_ BOOLEAN Ecn,
//     _In_ uint32_t TenTimesBeta
//     )
// {
//     UNREFERENCED_PARAMETER(IsPersistentCongestion);
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     const QUIC_PATH* Path = &Connection->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
//     uint32_t PrevCwnd = Cubic->CongestionWindow;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;

//     // [신규] 혼잡 발생 -> W_max 아래로 떨어짐 -> 플래그 리셋
//     CubicProbe->HasCrossedWmax = FALSE;

//     if (!Cubic->IsInRecovery) {
//          Cubic->IsInRecovery = TRUE;
//     }
//     Cubic->HasHadCongestionEvent = TRUE;

//     if (!Ecn) {
//         Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
//     }

//     Cubic->WindowLastMax = Cubic->WindowMax;
//     Cubic->WindowMax = Cubic->CongestionWindow;

//     if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
//         Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TenTimesBeta) / 20.0);
//     }

//     uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    
//     Cubic->SlowStartThreshold =
//     Cubic->CongestionWindow =
//         CXPLAT_MAX(
//             MinCongestionWindow,
//             (uint32_t)(Cubic->CongestionWindow * ((double)TenTimesBeta / 10.0)));

//     Cubic->TimeOfCongAvoidStart = 0;

//     printf("[Cubic][%p][%.3fms] CWND Update (Congestion Event, Beta=%.1f): %u -> %u (Reset Wmax Flag)\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, (double)TenTimesBeta / 10.0, PrevCwnd, Cubic->CongestionWindow);
// }

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void CubicProbeCongestionControlSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets);
// uint32_t CubicProbeCongestionControlGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid);
// void CubicProbeCongestionControlOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// BOOLEAN CubicProbeCongestionControlOnDataInvalidated( _In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
// void CubicProbeCongestionControlOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent);
// void CubicProbeCongestionControlOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent);
// BOOLEAN CubicProbeCongestionControlOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// uint8_t CubicProbeCongestionControlGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// uint32_t CubicProbeCongestionControlGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// BOOLEAN CubicProbeCongestionControlIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlSetAppLimited(_In_ QUIC_CONGESTION_CONTROL* Cc);
// uint32_t CubicProbeCongestionControlGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc);
// void CubicProbeCongestionControlGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics);

// BOOLEAN
// CubicProbeCongestionControlCanSend(
//     _In_ QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
// }

// void
// CubicProbeCongestionControlSetExemption(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint8_t NumPackets
//     )
// {
//     Cc->CubicProbe.Cubic.Exemptions = NumPackets;
// }

// uint32_t
// CubicProbeCongestionControlGetSendAllowance(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint64_t TimeSinceLastSend,
//     _In_ BOOLEAN TimeSinceLastSendValid
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     uint32_t SendAllowance;

//     if (Cubic->BytesInFlight >= Cubic->CongestionWindow) {
//         SendAllowance = 0;
//     } else if (!TimeSinceLastSendValid || !Connection->Settings.PacingEnabled || !Connection->Paths[0].GotFirstRttSample || Connection->Paths[0].SmoothedRtt < QUIC_MIN_PACING_RTT) {
//         SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//     } else {
//         uint64_t EstimatedWnd;
//         if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
//             EstimatedWnd = (uint64_t)Cubic->CongestionWindow << 1;
//             if (EstimatedWnd > Cubic->SlowStartThreshold) {
//                 EstimatedWnd = Cubic->SlowStartThreshold;
//             }
//         } else {
//             EstimatedWnd = Cubic->CongestionWindow + (Cubic->CongestionWindow >> 2);
//         }
//         SendAllowance = Cubic->LastSendAllowance + (uint32_t)((EstimatedWnd * TimeSinceLastSend) / Connection->Paths[0].SmoothedRtt);
//         if (SendAllowance < Cubic->LastSendAllowance || SendAllowance > (Cubic->CongestionWindow - Cubic->BytesInFlight)) {
//             SendAllowance = Cubic->CongestionWindow - Cubic->BytesInFlight;
//         }
//         Cubic->LastSendAllowance = SendAllowance;
//     }
//     return SendAllowance;
// }

// void
// CubicProbeCongestionControlOnDataSent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint32_t NumRetransmittableBytes
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->BytesInFlight += NumRetransmittableBytes;
//     if (Cubic->BytesInFlightMax < Cubic->BytesInFlight) {
//         Cubic->BytesInFlightMax = Cubic->BytesInFlight;
//         QuicSendBufferConnectionAdjust(QuicCongestionControlGetConnection(Cc));
//     }
//     if (NumRetransmittableBytes > Cubic->LastSendAllowance) {
//         Cubic->LastSendAllowance = 0;
//     } else {
//         Cubic->LastSendAllowance -= NumRetransmittableBytes;
//     }
//     if (Cubic->Exemptions > 0) {
//         --Cubic->Exemptions;
//     }

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// BOOLEAN
// CubicProbeCongestionControlOnDataInvalidated(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ uint32_t NumRetransmittableBytes
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
//     Cubic->BytesInFlight -= NumRetransmittableBytes;

//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlOnDataLost(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_LOSS_EVENT* LossEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     printf("[Cubic][%p][%.3fms] LOSS EVENT (0.7x): CWnd=%u, InFlight=%u, LostBytes=%u\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight, LossEvent->NumRetransmittableBytes);

//     if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
//         // [수정] 일반 손실 시 0.7배 감소(TEN_TIMES_BETA_CUBIC)를 사용하도록 호출
//         CubicProbeCongestionControlOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, TEN_TIMES_BETA_CUBIC);
//     }

//     CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
//     Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlOnEcn(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_ECN_EVENT* EcnEvent
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     printf("[Cubic][%p][%.3fms] ECN EVENT (0.7x): CWnd=%u, InFlight=%u\n",
//         (void*)Connection, (double)CxPlatTimeUs64() / 1000.0, Cubic->CongestionWindow, Cubic->BytesInFlight);

//     if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
//         Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
//         Connection->Stats.Send.EcnCongestionCount++;
//         // [수정] ECN 시 0.7배 감소(TEN_TIMES_BETA_CUBIC)를 사용하도록 호출
//         CubicProbeCongestionControlOnCongestionEvent(Cc, FALSE, TRUE, TEN_TIMES_BETA_CUBIC);
//     }

//     CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// BOOLEAN
// CubicProbeCongestionControlOnSpuriousCongestionEvent(
//     _In_ QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     if (!Cubic->IsInRecovery) return FALSE;
//     BOOLEAN PreviousCanSendState = CubicProbeCongestionControlCanSend(Cc);

//     Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
//     Cubic->IsInRecovery = FALSE;
//     Cubic->HasHadCongestionEvent = FALSE;

//     return CubicProbeCongestionControlUpdateBlockedState(Cc, PreviousCanSendState);
// }

// void
// CubicProbeCongestionControlLogOutFlowStatus(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
// }

// uint32_t
// CubicProbeCongestionControlGetBytesInFlightMax(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.BytesInFlightMax;
// }

// uint8_t
// CubicProbeCongestionControlGetExemptions(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.Exemptions;
// }

// uint32_t
// CubicProbeCongestionControlGetCongestionWindow(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     return Cc->CubicProbe.Cubic.CongestionWindow;
// }

// BOOLEAN
// CubicProbeCongestionControlIsAppLimited(
//     _In_ const QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
//     return FALSE;
// }

// void
// CubicProbeCongestionControlSetAppLimited(
//     _In_ struct QUIC_CONGESTION_CONTROL* Cc
//     )
// {
//     UNREFERENCED_PARAMETER(Cc);
// }

// void
// CubicProbeCongestionControlGetNetworkStatistics(
//     _In_ const QUIC_CONNECTION* const Connection,
//     _In_ const QUIC_CONGESTION_CONTROL* const Cc,
//     _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics
//     )
// {
//     const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
//     const QUIC_PATH* Path = &Connection->Paths[0];

//     NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
//     NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
//     NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
//     NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
//     NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
//     NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
// }

// static BOOLEAN
// CubicProbeCongestionControlUpdateBlockedState(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ BOOLEAN PreviousCanSendState
//     )
// {
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

// static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
//     .Name = "CubicProbe",
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
// void
// CubicProbeCongestionControlInitialize(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_SETTINGS_INTERNAL* Settings
//     )
// {
//     *Cc = QuicCongestionControlCubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
//     QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
//     const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
//     const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

//     Cubic->SlowStartThreshold = UINT32_MAX;
//     Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
//     Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
//     Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
//     Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
//     Cubic->WindowMax = 0;
//     Cubic->WindowLastMax = 0;

//     CubicProbeResetProbeState(CubicProbe);
//     CubicProbe->AckCountForGrowth = 0;
//     CubicProbe->MinRttUs = UINT64_MAX;
// }
//20251216










/*++

Copyright (c) Microsoft Corporation.
Licensed under the MIT License.

Module Name:

    cubicprobe.c

Abstract:

    CubicBoost (v2.1 + Debug Logging) Implementation.
    
    [Debug Feature]
    - Prints 'BaseTarget' (Standard CUBIC) vs 'Target' (Boosted) in log.
    - Helps verify if the boost logic is effectively reducing the Ack requirement.

--*/

#include "precomp.h"
#include <stdio.h>
#include "cubicprobe.h"

// Constants
#define TEN_TIMES_BETA_CUBIC 7
#define TEN_TIMES_C_CUBIC 4

// Forward declarations
static void CubicProbeOnCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN IsPersistentCongestion, _In_ BOOLEAN Ecn, _In_ const QUIC_ACK_EVENT* AckEvent);
// [Modified] Added BaseAckTarget output
static void CubicProbeUpdate(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _Out_ uint32_t* AckTarget, _Out_ uint32_t* BaseAckTarget);
// [Modified] Added BaseAckTarget input
static void CubicProbeIncreaseWindow(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent, _In_ uint32_t AckTarget, _In_ uint32_t BaseAckTarget);
static void CubicProbeCheckRoundState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ACK_EVENT* AckEvent);

BOOLEAN CubicProbeCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc);
static BOOLEAN CubicProbeUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState);

//
// CubeRoot Helper
//
_IRQL_requires_max_(DISPATCH_LEVEL)
static uint32_t
CubeRoot(
    uint32_t Radicand
    )
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

//
// State Reset
//
_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicBoostResetState(
    _In_ QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe,
    _In_ uint64_t NextPacketNumber
    )
{
    CubicProbe->BoostLevel = 0;
    CubicProbe->RoundEndPacketNumber = NextPacketNumber;
    CubicProbe->AnchorRttUs = 0; 
    CubicProbe->AnchorRttVarUs = 0;
    CubicProbe->MinRttInCurrentRoundUs = UINT64_MAX;
    CubicProbe->CurrentGrowthMagnitude = 1; 
    CubicProbe->IsConcave = TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlReset(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN FullReset
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

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

    CubicBoostResetState(CubicProbe, Connection->Send.NextPacketNumber);
    CubicProbe->AckCountForGrowth = 0;
}

//
// Round Check & Stat Logic
//
_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeCheckRoundState(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];

    if (!AckEvent->MinRttValid) return;

    if (AckEvent->MinRtt < CubicProbe->MinRttInCurrentRoundUs) {
        CubicProbe->MinRttInCurrentRoundUs = AckEvent->MinRtt;
    }

    if (AckEvent->LargestAck >= CubicProbe->RoundEndPacketNumber) {
        
        if (CubicProbe->AnchorRttUs > 0) {
            
            uint64_t Threshold = CubicProbe->AnchorRttUs + (3 * CubicProbe->AnchorRttVarUs);
            if (CubicProbe->AnchorRttVarUs < 1000) { 
                Threshold = CubicProbe->AnchorRttUs + 3000; 
            }

            if (CubicProbe->MinRttInCurrentRoundUs <= Threshold) {
                // Success
                uint32_t PrevLevel = CubicProbe->BoostLevel;
                CubicProbe->BoostLevel++;
                
                // Cap increased to 50
                if (CubicProbe->BoostLevel > 50) CubicProbe->BoostLevel = 50;

                printf("[CubicBoost][%p][%.3fms] Round Success! Level: %u->%u (Rtt: %llu <= %llu + 3*%llu)\n", 
                    (void*)Connection, 
                    (double)AckEvent->TimeNow / 1000.0,
                    PrevLevel, CubicProbe->BoostLevel,
                    (unsigned long long)CubicProbe->MinRttInCurrentRoundUs, 
                    (unsigned long long)CubicProbe->AnchorRttUs, 
                    (unsigned long long)CubicProbe->AnchorRttVarUs);

            } else {
                // Fail
                printf("[CubicBoost][%p][%.3fms] Round Failed. Level Reset 0. (Rtt: %llu > %llu + 3*%llu)\n",
                    (void*)Connection, 
                    (double)AckEvent->TimeNow / 1000.0,
                    (unsigned long long)CubicProbe->MinRttInCurrentRoundUs, 
                    (unsigned long long)CubicProbe->AnchorRttUs, 
                    (unsigned long long)CubicProbe->AnchorRttVarUs);
                
                CubicProbe->BoostLevel = 0;
            }
        } else {
            printf("[CubicBoost][%p][%.3fms] First Round Started.\n",
                (void*)Connection, (double)AckEvent->TimeNow / 1000.0);
        }

        CubicProbe->AnchorRttUs = CubicProbe->MinRttInCurrentRoundUs;
        if (CubicProbe->AnchorRttUs == UINT64_MAX) CubicProbe->AnchorRttUs = Path->MinRtt; 
        CubicProbe->AnchorRttVarUs = Path->RttVariance;

        CubicProbe->RoundEndPacketNumber = Connection->Send.NextPacketNumber;
        CubicProbe->MinRttInCurrentRoundUs = UINT64_MAX;
    }
}

//
// [Update Logic] 
//
_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeUpdate(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _Out_ uint32_t* AckTarget,
    _Out_ uint32_t* BaseAckTargetOut // [Debug] Export Base Target
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    uint32_t BaseAckTarget = 0;

    // --- 1. Standard CUBIC Calculation ---
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
    const uint64_t K_us = (uint64_t)Cubic->KCubic * 1000;

    int64_t TimeDeltaUs = (int64_t)t_us - (int64_t)K_us;
    int64_t OffsetMs = (TimeDeltaUs / 1000);
    int64_t CubicTerm = ((((OffsetMs * OffsetMs) >> 10) * OffsetMs * (int64_t)(DatagramPayloadLength * TEN_TIMES_C_CUBIC / 10)) >> 20);

    uint32_t W_cubic_bytes;
    BOOLEAN CurrentIsConcave = FALSE;

    if (TimeDeltaUs < 0) {
        W_cubic_bytes = W_max_bytes - (uint32_t)(-CubicTerm); 
        CurrentIsConcave = TRUE;
    } else {
        W_cubic_bytes = W_max_bytes + (uint32_t)CubicTerm;
        CurrentIsConcave = FALSE;
    }

    if (CubicProbe->IsConcave != CurrentIsConcave) {
        printf("[CubicBoost][%p][%.3fms] Region Change: %s -> %s (W_max=%u)\n",
            (void*)Connection,
            (double)AckEvent->TimeNow / 1000.0,
            CubicProbe->IsConcave ? "Concave" : "Convex",
            CurrentIsConcave ? "Concave" : "Convex",
            W_max_bytes);
        CubicProbe->IsConcave = CurrentIsConcave;
        CubicProbe->BoostLevel = 0;
    }

    if (W_cubic_bytes > Cubic->CongestionWindow) {
        uint32_t CwndSegments = Cubic->CongestionWindow / DatagramPayloadLength;
        uint32_t TargetSegments = W_cubic_bytes / DatagramPayloadLength;
        uint32_t DiffSegments = TargetSegments > CwndSegments ? TargetSegments - CwndSegments : 1;
        BaseAckTarget = CwndSegments / DiffSegments;
    } 
    else {
        if (CubicProbe->BoostLevel > 0) BaseAckTarget = Cubic->CongestionWindow / DatagramPayloadLength;
        else BaseAckTarget = 100 * (Cubic->CongestionWindow / DatagramPayloadLength);
    }
    
    if (BaseAckTarget == 0) BaseAckTarget = 1;
    
    // [Debug] Export BaseAckTarget for Logging
    *BaseAckTargetOut = BaseAckTarget;

    // --- 2. Boost Logic ---    
    uint32_t BoostFactor = 1 + CubicProbe->BoostLevel;
    uint32_t Divisor = BoostFactor * BoostFactor;

    
    uint32_t CalculatedTarget = BaseAckTarget / BoostFactor;
    if (!CurrentIsConcave) CalculatedTarget /= BoostFactor;

    if (CurrentIsConcave || CalculatedTarget >= 2) {
        // [Mode 1: Pacing]
        *AckTarget = CalculatedTarget;
        CubicProbe->CurrentGrowthMagnitude = 1;
    } else {
        // [Mode 2: Burst / Aggressive Growth]
        *AckTarget = 2; // Fixed Pacing
        
        uint64_t DesiredGrowth = ((uint64_t)2 * Divisor) / BaseAckTarget;
        
        if (DesiredGrowth < 1) DesiredGrowth = 1;
        
        CubicProbe->CurrentGrowthMagnitude = (uint32_t)DesiredGrowth;
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeIncreaseWindow(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_ACK_EVENT* AckEvent,
    _In_ uint32_t AckTarget,
    _In_ uint32_t BaseAckTarget // [Debug] Input for logging
    )
{
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);

    uint32_t AckedSegments = (AckEvent->NumRetransmittableBytes + DatagramPayloadLength - 1) / DatagramPayloadLength;
    CubicProbe->AckCountForGrowth += AckedSegments;

    if (CubicProbe->AckCountForGrowth >= AckTarget) {
        
        uint32_t PrevCwnd = Cubic->CongestionWindow;
        uint32_t GrowthInSegments = CubicProbe->CurrentGrowthMagnitude;

        Cubic->CongestionWindow += (GrowthInSegments * DatagramPayloadLength);
        CubicProbe->AckCountForGrowth -= AckTarget;

        // [Debug Log] Added Base=%u to compare original CUBIC speed vs Boosted speed
        printf("[CubicBoost][%p][%.3fms] CWND Update (CUBIC-%s): %u -> %u (Target=%u (Base=%u), Growth=%u, Level=%u)\n",
            (void*)Connection, 
            (double)AckEvent->TimeNow / 1000.0,
            CubicProbe->IsConcave ? "Concave" : "Convex",
            PrevCwnd, 
            Cubic->CongestionWindow, 
            AckTarget, 
            BaseAckTarget,
            GrowthInSegments,
            CubicProbe->BoostLevel);
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
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);

    Cubic->BytesInFlight -= AckEvent->NumRetransmittableBytes;

    if (Cubic->IsInRecovery) {
        if (AckEvent->LargestAck > Cubic->RecoverySentPacketNumber) {
            Cubic->IsInRecovery = FALSE;
            printf("[CubicBoost][%p][%.3fms] Exiting Recovery.\n", (void*)Connection, (double)AckEvent->TimeNow / 1000.0);
        }
        goto Exit;
    }
    if (AckEvent->NumRetransmittableBytes == 0) {
        goto Exit;
    }

    if (Cubic->CongestionWindow < Cubic->SlowStartThreshold) {
        // Slow Start
        uint32_t PrevCwnd = Cubic->CongestionWindow;
        Cubic->CongestionWindow += AckEvent->NumRetransmittableBytes;
        
        printf("[CubicBoost][%p][%.3fms] CWND Update (SlowStart): %u -> %u\n",
            (void*)Connection,
            (double)AckEvent->TimeNow / 1000.0,
            PrevCwnd,
            Cubic->CongestionWindow);

        if (Cubic->CongestionWindow >= Cubic->SlowStartThreshold) {
            Cubic->TimeOfCongAvoidStart = AckEvent->TimeNow;
            printf("[CubicBoost][%p][%.3fms] State -> Congestion Avoidance (Ssthresh=%u)\n",
                 (void*)Connection, (double)AckEvent->TimeNow / 1000.0, Cubic->SlowStartThreshold);
        }
    } else {
        // Congestion Avoidance
        const QUIC_PATH* Path = &Connection->Paths[0];
        const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
        if (DatagramPayloadLength == 0) goto Exit;

        CubicProbeCheckRoundState(Cc, AckEvent);

        uint32_t AckTarget = 0;
        uint32_t BaseAckTarget = 0; // Holder for Original CUBIC Target
        
        CubicProbeUpdate(Cc, AckEvent, &AckTarget, &BaseAckTarget);
        
        CubicProbeIncreaseWindow(Cc, AckEvent, AckTarget, BaseAckTarget);
    }

Exit:
    return CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static void
CubicProbeOnCongestionEvent(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ BOOLEAN IsPersistentCongestion,
    _In_ BOOLEAN Ecn,
    _In_ const QUIC_ACK_EVENT* AckEvent
    )
{
    UNREFERENCED_PARAMETER(IsPersistentCongestion);
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    const QUIC_PATH* Path = &Connection->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    
    uint32_t PrevCwnd = Cubic->CongestionWindow;
    printf("[CubicBoost][%p][%.3fms] LOSS EVENT: CWnd=%u, InFlight=%u, ECN=%d\n",
        (void*)Connection, 
        AckEvent ? (double)AckEvent->TimeNow / 1000.0 : (double)CxPlatTimeUs64() / 1000.0,
        Cubic->CongestionWindow, 
        Cubic->BytesInFlight, 
        Ecn);

    CubicBoostResetState(CubicProbe, Connection->Send.NextPacketNumber);
    CubicProbe->AckCountForGrowth = 0;

    if (!Cubic->IsInRecovery) {
         Cubic->IsInRecovery = TRUE;
    }
    Cubic->HasHadCongestionEvent = TRUE;

    if (!Ecn) {
        Cubic->PrevCongestionWindow = Cubic->CongestionWindow;
    }

    Cubic->WindowLastMax = Cubic->WindowMax;
    Cubic->WindowMax = Cubic->CongestionWindow;

    if (Cubic->WindowLastMax > 0 && Cubic->CongestionWindow < Cubic->WindowLastMax) {
        Cubic->WindowMax = (uint32_t)(Cubic->CongestionWindow * (10.0 + TEN_TIMES_BETA_CUBIC) / 20.0);
    }

    uint32_t MinCongestionWindow = 2 * DatagramPayloadLength;
    Cubic->SlowStartThreshold =
    Cubic->CongestionWindow =
        CXPLAT_MAX(
            MinCongestionWindow,
            (uint32_t)(Cubic->CongestionWindow * ((double)TEN_TIMES_BETA_CUBIC / 10.0)));

    Cubic->TimeOfCongAvoidStart = 0;

    printf("[CubicBoost][%p][%.3fms] CWND Update (Congestion Event): %u -> %u (Beta=0.7)\n",
        (void*)Connection,
        AckEvent ? (double)AckEvent->TimeNow / 1000.0 : (double)CxPlatTimeUs64() / 1000.0,
        PrevCwnd,
        Cubic->CongestionWindow);
}

//
// Wrappers
//
void CubicProbeSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets);
uint32_t CubicProbeGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid);
void CubicProbeOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
BOOLEAN CubicProbeOnDataInvalidated( _In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes);
void CubicProbeOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent);
void CubicProbeOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent);
BOOLEAN CubicProbeOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc);
uint8_t CubicProbeGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc);
uint32_t CubicProbeGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc);
BOOLEAN CubicProbeIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeSetAppLimited(_In_ struct QUIC_CONGESTION_CONTROL* Cc);
uint32_t CubicProbeGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc);
void CubicProbeGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics);

BOOLEAN CubicProbeCanSend(_In_ QUIC_CONGESTION_CONTROL* Cc) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    return Cubic->BytesInFlight < Cubic->CongestionWindow || Cubic->Exemptions > 0;
}

void CubicProbeSetExemption(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint8_t NumPackets) {
    Cc->CubicProbe.Cubic.Exemptions = NumPackets;
}

uint32_t CubicProbeGetSendAllowance(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint64_t TimeSinceLastSend, _In_ BOOLEAN TimeSinceLastSendValid) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    uint32_t SendAllowance;

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

void CubicProbeOnDataSent(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);

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
    CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

BOOLEAN CubicProbeOnDataInvalidated(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ uint32_t NumRetransmittableBytes) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);
    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= NumRetransmittableBytes);
    Cubic->BytesInFlight -= NumRetransmittableBytes;
    return CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

void CubicProbeOnDataLost(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_LOSS_EVENT* LossEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);
    if (!Cubic->HasHadCongestionEvent || LossEvent->LargestPacketNumberLost > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = LossEvent->LargestSentPacketNumber;
        CubicProbeOnCongestionEvent(Cc, LossEvent->PersistentCongestion, FALSE, NULL);
    }
    CXPLAT_DBG_ASSERT(Cubic->BytesInFlight >= LossEvent->NumRetransmittableBytes);
    Cubic->BytesInFlight -= LossEvent->NumRetransmittableBytes;
    CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

void CubicProbeOnEcn(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_ECN_EVENT* EcnEvent) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);
    if (!Cubic->HasHadCongestionEvent || EcnEvent->LargestPacketNumberAcked > Cubic->RecoverySentPacketNumber) {
        Cubic->RecoverySentPacketNumber = EcnEvent->LargestSentPacketNumber;
        Connection->Stats.Send.EcnCongestionCount++;
        CubicProbeOnCongestionEvent(Cc, FALSE, TRUE, NULL);
    }
    CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

BOOLEAN CubicProbeOnSpuriousCongestionEvent(_In_ QUIC_CONGESTION_CONTROL* Cc) {
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    if (!Cubic->IsInRecovery) return FALSE;
    BOOLEAN PreviousCanSendState = CubicProbeCanSend(Cc);
    Cubic->CongestionWindow = Cubic->PrevCongestionWindow;
    Cubic->IsInRecovery = FALSE;
    Cubic->HasHadCongestionEvent = FALSE;
    printf("[CubicBoost][%p][%.3fms] Spurious Congestion Event. Reverted CWND to %u\n",
        (void*)Connection, (double)CxPlatTimeUs64()/1000.0, Cubic->CongestionWindow);
    return CubicProbeUpdateBlockedState(Cc, PreviousCanSendState);
}

void CubicProbeLogOutFlowStatus(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }
uint8_t CubicProbeGetExemptions(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.Exemptions; }
uint32_t CubicProbeGetBytesInFlightMax(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.BytesInFlightMax; }
BOOLEAN CubicProbeIsAppLimited(_In_ const QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); return FALSE; }
void CubicProbeSetAppLimited(_In_ struct QUIC_CONGESTION_CONTROL* Cc) { UNREFERENCED_PARAMETER(Cc); }
uint32_t CubicProbeGetCongestionWindow(_In_ const QUIC_CONGESTION_CONTROL* Cc) { return Cc->CubicProbe.Cubic.CongestionWindow; }
void CubicProbeGetNetworkStatistics(_In_ const QUIC_CONNECTION* const Connection, _In_ const QUIC_CONGESTION_CONTROL* const Cc, _Out_ QUIC_NETWORK_STATISTICS* NetworkStatistics) {
    const QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &Cc->CubicProbe.Cubic;
    const QUIC_PATH* Path = &Connection->Paths[0];
    NetworkStatistics->BytesInFlight = Cubic->BytesInFlight;
    NetworkStatistics->PostedBytes = Connection->SendBuffer.PostedBytes;
    NetworkStatistics->IdealBytes = Connection->SendBuffer.IdealBytes;
    NetworkStatistics->SmoothedRTT = Path->SmoothedRtt;
    NetworkStatistics->CongestionWindow = Cubic->CongestionWindow;
    NetworkStatistics->Bandwidth = Path->SmoothedRtt > 0 ? (uint64_t)Cubic->CongestionWindow * 1000000 / Path->SmoothedRtt : 0;
}

static BOOLEAN CubicProbeUpdateBlockedState(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ BOOLEAN PreviousCanSendState) {
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    if (PreviousCanSendState != CubicProbeCanSend(Cc)) {
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

static const QUIC_CONGESTION_CONTROL QuicCongestionControlCubicProbe = {
    .Name = "CubicBoost",
    .QuicCongestionControlCanSend = CubicProbeCanSend,
    .QuicCongestionControlSetExemption = CubicProbeSetExemption,
    .QuicCongestionControlReset = CubicProbeCongestionControlReset,
    .QuicCongestionControlGetSendAllowance = CubicProbeGetSendAllowance,
    .QuicCongestionControlOnDataSent = CubicProbeOnDataSent,
    .QuicCongestionControlOnDataInvalidated = CubicProbeOnDataInvalidated,
    .QuicCongestionControlOnDataAcknowledged = CubicProbeCongestionControlOnDataAcknowledged,
    .QuicCongestionControlOnDataLost = CubicProbeOnDataLost,
    .QuicCongestionControlOnEcn = CubicProbeOnEcn,
    .QuicCongestionControlOnSpuriousCongestionEvent = CubicProbeOnSpuriousCongestionEvent,
    .QuicCongestionControlLogOutFlowStatus = CubicProbeLogOutFlowStatus,
    .QuicCongestionControlGetExemptions = CubicProbeGetExemptions,
    .QuicCongestionControlGetBytesInFlightMax = CubicProbeGetBytesInFlightMax,
    .QuicCongestionControlIsAppLimited = CubicProbeIsAppLimited,
    .QuicCongestionControlSetAppLimited = CubicProbeSetAppLimited,
    .QuicCongestionControlGetCongestionWindow = CubicProbeGetCongestionWindow,
    .QuicCongestionControlGetNetworkStatistics = CubicProbeGetNetworkStatistics
};

_IRQL_requires_max_(DISPATCH_LEVEL)
void CubicProbeCongestionControlInitialize(_In_ QUIC_CONGESTION_CONTROL* Cc, _In_ const QUIC_SETTINGS_INTERNAL* Settings) {
    *Cc = QuicCongestionControlCubicProbe;
    QUIC_CONGESTION_CONTROL_CUBICPROBE* CubicProbe = &Cc->CubicProbe;
    QUIC_CONGESTION_CONTROL_CUBIC* Cubic = &CubicProbe->Cubic;
    const QUIC_PATH* Path = &QuicCongestionControlGetConnection(Cc)->Paths[0];
    const uint16_t DatagramPayloadLength = QuicPathGetDatagramPayloadSize(Path);
    QUIC_CONNECTION* Connection = QuicCongestionControlGetConnection(Cc);
    Cubic->SlowStartThreshold = UINT32_MAX;
    Cubic->SendIdleTimeoutMs = Settings->SendIdleTimeoutMs;
    Cubic->InitialWindowPackets = Settings->InitialWindowPackets;
    Cubic->CongestionWindow = DatagramPayloadLength * Cubic->InitialWindowPackets;
    Cubic->BytesInFlightMax = Cubic->CongestionWindow / 2;
    Cubic->WindowMax = 0;
    Cubic->WindowLastMax = 0;
    CubicBoostResetState(CubicProbe, Connection->Send.NextPacketNumber);
    CubicProbe->AckCountForGrowth = 0;
}