// // /*
// //     cubicprobe.h - Physics-based CubicBoost Header
// //     - Updated for V20 (Cumulative Elasticity)
// // */

// // #ifndef QUIC_CUBICPROBE_H
// // #define QUIC_CUBICPROBE_H

// // #include "cubic.h" 

// // typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

// //     // 1. Base MsQuic CUBIC State (Inheritance)
// //     QUIC_CONGESTION_CONTROL_CUBIC Cubic;

// //     // 2. Physics & Statistics 
// //     uint64_t MinRttUs;          
// //     uint64_t RttVariance;       
    
// //     // 3. Round-Trip Logic 
// //     uint64_t RoundStartTime;        
// //     uint64_t RoundInFlightBytes;    
// //     uint64_t ProbeTargetPacketNumber; 
    
// //     // 4. Elasticity Metrics (Cumulative)
// //     uint64_t PrevBandwidth;      // (구버전 호환용)
// //     uint32_t PrevCwnd;           // (구버전 호환용)
// //     uint64_t PrevTime;           // (구버전 호환용)
    
// //     // [New] Epoch Baselines for V20
// //     uint64_t EpochStartBandwidth; 
// //     uint32_t EpochStartCwnd;

// //     // Accumulator for Batch Processing
// //     uint64_t BatchBytesAcked;

// //     double   CurrentElasticity; 

// //     // 5. Control Flags
// //     BOOLEAN  IsQueueBuilding;   
// //     uint32_t AckCountForGrowth; 
    
// //     // Veto Counter (Optional if used)
// //     uint8_t VetoCounter;

// // } QUIC_CONGESTION_CONTROL_CUBICPROBE;

// // _IRQL_requires_max_(DISPATCH_LEVEL)
// // void
// // CubicProbeCongestionControlInitialize(
// //     _In_ QUIC_CONGESTION_CONTROL* Cc,
// //     _In_ const QUIC_SETTINGS_INTERNAL* Settings
// //     );

// // #endif // QUIC_CUBICPROBE_H


// /*++

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// Module Name:

//     cubicprobe.h

// Abstract:

//     CubicBoost v3 Congestion Control Algorithm for MsQuic.
//     - Round-based Time-Shifting logic.
//     - Dynamic Threshold using Chebyshev inequality (No Magic Numbers).
//     - Anti-Microburst protection.

// --*/

// #ifndef QUIC_CUBICPROBE_H
// #define QUIC_CUBICPROBE_H

// #include "cubic.h"

// // [CubicBoost v3] Chebyshev Gamma Factor 
// // k=3 implies 99.7% confidence interval for normal distribution.
// #define CUBIC_BOOST_GAMMA 3 

// typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

//     //
//     // Base CUBIC State (MsQuic Standard)
//     //
//     QUIC_CONGESTION_CONTROL_CUBIC Cubic;

//     //
//     // [CubicBoost] Round Control State
//     //
    
//     // The packet number that marks the end of the current round.
//     uint64_t EndOfRoundSeq; 

//     // Snapshot of the RTT threshold for the current round.
//     // Calculated as: SRTT + (Gamma * RTTVAR) at the start of the round.
//     uint64_t RoundRttThresh;

//     // Flag indicating if the current round has been "clean" (no high delay).
//     BOOLEAN IsRoundClean;

//     //
//     // [CubicBoost] Acceleration State
//     //

//     // "Momentum": How many consecutive rounds have been stable.
//     // Used to calculate the virtual time shift.
//     uint32_t StableRoundCount; 

//     //
//     // Helper / Statistics
//     //
    
//     // Accumulator for congestion window growth (Bytes)
//     uint64_t BytesAckedAccumulator;
    
//     // Minimum RTT observed (Fallback statistic)
//     uint64_t MinRttUs;

// } QUIC_CONGESTION_CONTROL_CUBICPROBE;

// _IRQL_requires_max_(DISPATCH_LEVEL)
// void
// CubicProbeCongestionControlInitialize(
//     _In_ QUIC_CONGESTION_CONTROL* Cc,
//     _In_ const QUIC_SETTINGS_INTERNAL* Settings
//     );

// #endif // QUIC_CUBICPROBE_H






#ifndef QUIC_CUBICPROBE_H
#define QUIC_CUBICPROBE_H

#include "cubic.h" // cubic.h를 포함하여 필요한 타입들을 가져옵니다.

// CubicProbe의 탐색 상태 Enum
typedef enum QUIC_PROBE_STATE {
    PROBE_INACTIVE,
    PROBE_TEST,
    PROBE_WAITING,
    PROBE_JUDGMENT
} QUIC_PROBE_STATE;

// CubicProbe 알고리즘의 상태를 저장하는 구조체
typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

    // Base MsQuic CUBIC State.
    QUIC_CONGESTION_CONTROL_CUBIC Cubic;

    // CubicProbe-specific State
    QUIC_PROBE_STATE ProbeState;
    BOOLEAN HasCrossedWmax;
    BOOLEAN HasGrownInThisRound;
    uint32_t CumulativeSuccessLevel;
    uint64_t RttAtProbeStartUs;
    uint64_t RttVarAtProbeStartUs;

    // **ACK Counter for ns-3 style growth**
    // uint32_t AckCountSinceLastGrowth; // ns-3의 m_cWndCnt 역할 (세그먼트 단위)
    uint64_t MinRttUs;
    uint32_t AckCountForGrowth;
    uint64_t ProbeTargetPacketNumber;

} QUIC_CONGESTION_CONTROL_CUBICPROBE;

// CubicProbe 알고리즘을 초기화하는 함수
_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );

#endif // QUIC_CUBICPROBE_H

