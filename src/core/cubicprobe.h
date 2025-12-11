/*
    cubicprobe.h - Physics-based CubicBoost Header
    - Updated for V20 (Cumulative Elasticity)
*/

#ifndef QUIC_CUBICPROBE_H
#define QUIC_CUBICPROBE_H

#include "cubic.h" 

typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

    // 1. Base MsQuic CUBIC State (Inheritance)
    QUIC_CONGESTION_CONTROL_CUBIC Cubic;

    // 2. Physics & Statistics 
    uint64_t MinRttUs;          
    uint64_t RttVariance;       
    
    // 3. Round-Trip Logic 
    uint64_t RoundStartTime;        
    uint64_t RoundInFlightBytes;    
    uint64_t ProbeTargetPacketNumber; 
    
    // 4. Elasticity Metrics (Cumulative)
    uint64_t PrevBandwidth;      // (구버전 호환용)
    uint32_t PrevCwnd;           // (구버전 호환용)
    uint64_t PrevTime;           // (구버전 호환용)
    
    // [New] Epoch Baselines for V20
    uint64_t EpochStartBandwidth; 
    uint32_t EpochStartCwnd;

    // Accumulator for Batch Processing
    uint64_t BatchBytesAcked;

    double   CurrentElasticity; 

    // 5. Control Flags
    BOOLEAN  IsQueueBuilding;   
    uint32_t AckCountForGrowth; 
    
    // Veto Counter (Optional if used)
    uint8_t VetoCounter;

} QUIC_CONGESTION_CONTROL_CUBICPROBE;

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );

#endif // QUIC_CUBICPROBE_H