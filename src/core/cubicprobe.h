/*
    cubicprobe.h - Physics-based CubicBoost Header
*/

#ifndef QUIC_CUBICPROBE_H
#define QUIC_CUBICPROBE_H

#include "cubic.h" // QUIC_CONGESTION_CONTROL_CUBIC 정의를 가져옴

// CubicProbe (CubicBoost) 구조체 정의
typedef struct QUIC_CONGESTION_CONTROL_CUBICPROBE {

    // 1. Base MsQuic CUBIC State (상속)
    QUIC_CONGESTION_CONTROL_CUBIC Cubic;

    // 2. Physics & Statistics (통계 및 물리 변수)
    uint64_t MinRttUs;          // 관측된 물리적 최소 RTT
    uint64_t RttVariance;       // RTT 분산 (msquic Path에서 가져옴)
    
    // 3. Elasticity Measurement (Event-Driven)
    uint64_t PrevTime;          // 이전 측정 시각
    uint32_t PrevCwnd;          // 이전 측정 시 CWND
    uint64_t PrevBandwidth;     // 이전 측정 대역폭
    
    uint64_t BatchBytesAcked;   // 현재 배치의 누적 ACK 바이트
    
    double   CurrentElasticity; // 현재 계산된 탄력성 (0.0 ~ 1.0)

    // 4. Control Flags
    BOOLEAN  IsQueueBuilding;   // Veto Flag (큐가 쌓이는 중인가?)
    uint32_t AckCountForGrowth; // CWND 성장을 위한 ACK 누적 카운터

} QUIC_CONGESTION_CONTROL_CUBICPROBE;

// 초기화 함수 선언
_IRQL_requires_max_(DISPATCH_LEVEL)
void
CubicProbeCongestionControlInitialize(
    _In_ QUIC_CONGESTION_CONTROL* Cc,
    _In_ const QUIC_SETTINGS_INTERNAL* Settings
    );

#endif // QUIC_CUBICPROBE_H