/*
    cubicprobe.h - Physics-based CubicBoost Header
    - Updated for V17 (Round-Trip Logic)
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
    
    // 3. Round-Trip Logic (New Members for V17)
    uint64_t RoundStartTime;        // Start time of the current round
    uint64_t RoundInFlightBytes;    // Bytes accumulated in this round
    uint64_t ProbeTargetPacketNumber; // Packet number that marks end of round
    
    // 4. Elasticity Metrics
    uint64_t PrevBandwidth;     // Bandwidth of the previous round
    double   CurrentElasticity; // Calculated E (0.0 ~ 1.0)

    // 5. Control Flags
    BOOLEAN  IsQueueBuilding;   // Veto Flag
    uint32_t AckCountForGrowth; // Accumulator for CWND growth

} QUIC_CONGESTION_CONTROL_CUBICPROBE;

_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );

#endif // QUIC_CUBICPROBE_H