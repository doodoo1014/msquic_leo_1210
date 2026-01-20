# import re
# import os
# import matplotlib.pyplot as plt

# def get_start_time_from_log(file_path):
#     """
#     로그 파일 상단에서 'MonotonicStartTime'을 찾아 기준 시간(ms)으로 반환합니다.
#     못 찾을 경우 None을 반환합니다.
#     단위: us -> ms로 변환
#     """
#     # 패턴: MonotonicStartTime=14009206714390us
#     start_time_pattern = re.compile(r"MonotonicStartTime=(\d+)us")
    
#     try:
#         with open(file_path, 'r', encoding='utf-8') as f:
#             for line in f:
#                 match = start_time_pattern.search(line)
#                 if match:
#                     # 마이크로초(us)를 밀리초(ms)로 변환
#                     return float(match.group(1)) / 1000.0
#     except Exception:
#         pass
#     return None

# def parse_server_log(file_path):
#     """
#     서버 로그에서 CWND, RTT를 파싱합니다.
#     MonotonicStartTime을 기준으로 시간을 0초부터 시작하도록 보정합니다.
#     """
#     expanded_path = os.path.expanduser(file_path)
#     flows_data = {}

#     print(f"📂 서버 로그 분석 중: '{expanded_path}'")

#     # 1. 기준 시간 찾기
#     base_time_ms = get_start_time_from_log(expanded_path)
    
#     # 2. 정규식 준비
#     # 헤더: [Cubic] 또는 [CubicProbe] 허용
#     header_pattern = re.compile(r"\[(Cubic|CubicProbe)\]\[(0x[\da-fA-F]+)\]\[([\d\.]+)ms\]\s+(.*)")
#     # CWND: "CWND Update ... -> 12345" 또는 "LOSS: CWnd=12345"
#     cwnd_pattern = re.compile(r"(?:CWND Update.*->|LOSS(?: EVENT)?: CWnd=)\s*(\d+)")
#     # RTT: "RTT Update: Curr=..."
#     rtt_pattern = re.compile(r"RTT Update: Curr=([\d\.]+)ms")

#     first_packet_time = None

#     try:
#         with open(expanded_path, 'r', encoding='utf-8') as f:
#             for line in f:
#                 header_match = header_pattern.search(line)
#                 if not header_match:
#                     continue

#                 flow_id = header_match.group(2)
#                 log_time_ms = float(header_match.group(3))
#                 message = header_match.group(4)

#                 # 만약 MonotonicStartTime을 못 찾았다면, 첫 번째 로그 시간을 기준으로 삼음
#                 if base_time_ms is None:
#                     if first_packet_time is None:
#                         first_packet_time = log_time_ms
#                         print(f"⚠️ 서버 로그에 MonotonicStartTime이 없습니다. 첫 패킷 시간({first_packet_time}ms)을 0초로 설정합니다.")
#                     base_time_ms = first_packet_time

#                 # 상대 시간 계산 (초 단위)
#                 rel_time_sec = (log_time_ms - base_time_ms) / 1000.0

#                 if flow_id not in flows_data:
#                     flows_data[flow_id] = {'cwnd': [], 'rtt': []}

#                 # CWND 파싱
#                 cwnd_match = cwnd_pattern.search(message)
#                 if cwnd_match:
#                     cwnd = int(cwnd_match.group(1))
#                     flows_data[flow_id]['cwnd'].append((rel_time_sec, cwnd))

#                 # RTT 파싱 (서버 로그에 있을 경우)
#                 rtt_match = rtt_pattern.search(message)
#                 if rtt_match:
#                     rtt = float(rtt_match.group(1))
#                     flows_data[flow_id]['rtt'].append((rel_time_sec, rtt))

#     except FileNotFoundError:
#         print(f"❌ 오류: 서버 파일 '{expanded_path}' 없음")
#         return {}

#     if base_time_ms:
#         print(f"   ℹ️ 서버 기준 시간(t=0): {base_time_ms:.3f} ms")
    
#     return flows_data

# def parse_client_log(file_path):
#     """
#     클라이언트 로그에서 Throughput과 RTT를 파싱합니다.
#     MonotonicStartTime을 기준으로 시간을 보정합니다.
#     """
#     expanded_path = os.path.expanduser(file_path)
#     client_data = {'throughput': [], 'rtt': []}

#     print(f"📂 클라이언트 로그 분석 중: '{expanded_path}'")

#     # 1. 기준 시간 찾기
#     base_time_ms = get_start_time_from_log(expanded_path)
    
#     # 2. 정규식: [CLIENT] Time: 1400...ms | Throughput: ... | RTT: ...
#     # RTT가 없을 수도 있으니 선택적으로 매칭하도록 처리
#     line_pattern = re.compile(r"\[CLIENT\] Time:\s*([\d\.]+)ms\s*\|\s*Throughput:\s*([\d\.]+)\s*Mbps(?:.*RTT:\s*([\d\.]+)\s*ms)?")

#     first_packet_time = None

#     try:
#         with open(expanded_path, 'r', encoding='utf-8') as f:
#             for line in f:
#                 match = line_pattern.search(line)
#                 if match:
#                     log_time_ms = float(match.group(1))
#                     throughput = float(match.group(2))
                    
#                     # RTT는 있을 수도 있고 없을 수도 있음 (group 3)
#                     rtt_val = None
#                     if match.group(3):
#                         rtt_val = float(match.group(3))

#                     # 기준 시간 보정
#                     if base_time_ms is None:
#                         if first_packet_time is None:
#                             first_packet_time = log_time_ms
#                             print(f"⚠️ 클라이언트 로그에 MonotonicStartTime이 없습니다. 첫 로그 시간({first_packet_time}ms)을 0초로 설정합니다.")
#                         base_time_ms = first_packet_time
                    
#                     rel_time_sec = (log_time_ms - base_time_ms) / 1000.0
                    
#                     client_data['throughput'].append((rel_time_sec, throughput))
#                     if rtt_val is not None:
#                         client_data['rtt'].append((rel_time_sec, rtt_val))

#     except FileNotFoundError:
#         print(f"❌ 오류: 클라이언트 파일 '{expanded_path}' 없음")
#         return {}

#     if base_time_ms:
#         print(f"   ℹ️ 클라이언트 기준 시간(t=0): {base_time_ms:.3f} ms")

#     return client_data

# def plot_network_metrics(server_data, client_data, output_filename="network_analysis_synced.png"):
#     if not server_data and not client_data['throughput']:
#         print("⚠️ 그릴 데이터가 없습니다.")
#         return

#     plt.style.use('seaborn-v0_8-whitegrid')
#     fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 12), sharex=True)

#     # --- 1. CWND (Server) ---
#     has_cwnd = False
#     for flow_id, metrics in server_data.items():
#         if metrics['cwnd']:
#             has_cwnd = True
#             times, values = zip(*metrics['cwnd'])
#             short_id = flow_id[-4:]
#             ax1.plot(times, values, label=f'Flow {short_id}', drawstyle='steps-post', linewidth=1.5)
    
#     ax1.set_title("1. Congestion Window (Server Side)", fontsize=14, fontweight='bold')
#     ax1.set_ylabel("CWND (Bytes)", fontsize=12)
#     if has_cwnd: ax1.legend(loc='upper right')
#     ax1.grid(True, linestyle='--', alpha=0.7)
#     ax1.set_ylim(bottom=0, top=3000000)

#     # --- 2. RTT (Server & Client Mix) ---
#     # 서버측 RTT
#     server_rtt_exists = False
#     for flow_id, metrics in server_data.items():
#         if metrics['rtt']:
#             server_rtt_exists = True
#             times, values = zip(*metrics['rtt'])
#             short_id = flow_id[-4:]
#             ax2.plot(times, values, label=f'Server Measured (Flow {short_id})', alpha=0.7)
    
#     # 클라이언트측 RTT
#     client_rtt_exists = False
#     if client_data['rtt']:
#         client_rtt_exists = True
#         times, values = zip(*client_data['rtt'])
#         ax2.plot(times, values, label='Client Measured', color='purple', linestyle='--', marker='x', markersize=4, alpha=0.8)

#     ax2.set_title("2. Round Trip Time (RTT)", fontsize=14, fontweight='bold')
#     ax2.set_ylabel("RTT (ms)", fontsize=12)
#     ax2.grid(True, linestyle='--', alpha=0.7)
    
#     if server_rtt_exists or client_rtt_exists:
#         ax2.legend(loc='upper right')
#     else:
#         ax2.text(0.5, 0.5, "No RTT Data", transform=ax2.transAxes, ha='center', color='gray')

#     # --- 3. Throughput (Client) ---
#     if client_data['throughput']:
#         times, values = zip(*client_data['throughput'])
#         ax3.plot(times, values, label='Client Throughput', color='tab:red', marker='.', linestyle='-')
#         ax3.legend(loc='upper right')
#     else:
#         ax3.text(0.5, 0.5, "No Throughput Data", transform=ax3.transAxes, ha='center', color='gray')

#     ax3.set_title("3. Throughput (Client Side)", fontsize=14, fontweight='bold')
#     ax3.set_ylabel("Throughput (Mbps)", fontsize=12)
#     ax3.set_xlabel("Time (seconds) - Aligned to ListenerStart", fontsize=12)
#     ax3.grid(True, linestyle='--', alpha=0.7)

#     plt.tight_layout()
#     plt.savefig(output_filename, dpi=150)
#     print(f"✅ 그래프 저장 완료: {output_filename}")

# if __name__ == "__main__":
#     server_log = "./build/bin/Release/testserver.txt"
#     client_log = "./build/bin/Release/testclient.txt"
    
#     s_data = parse_server_log(server_log)
#     c_data = parse_client_log(client_log)
    
#     plot_network_metrics(s_data, c_data)








import re
import os
import matplotlib.pyplot as plt

def get_start_time_from_log(file_path):
    """
    로그 파일 상단에서 'MonotonicStartTime'을 찾아 기준 시간(ms)으로 반환합니다.
    """
    start_time_pattern = re.compile(r"MonotonicStartTime=(\d+)us")
    
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            for line in f:
                match = start_time_pattern.search(line)
                if match:
                    # 마이크로초(us)를 밀리초(ms)로 변환
                    return float(match.group(1)) / 1000.0
    except Exception as e:
        pass
    return None

def parse_server_log(file_path):
    """
    서버 로그에서 CWND, RTT를 파싱합니다.
    새로운 로그 포맷(ROUND DIRTY, CWND UPDATE 등)을 지원합니다.
    """
    if not os.path.exists(file_path):
        print(f"❌ 오류: 파일 '{file_path}'을 찾을 수 없습니다.")
        return {}

    flows_data = {}
    print(f"📂 서버 로그 분석 중: '{file_path}'")

    # 1. 기준 시간 찾기
    base_time_ms = get_start_time_from_log(file_path)
    
    # 2. 정규식 정의
    # 헤더: [CubicProbe][Address][Time_ms]
    header_pattern = re.compile(r"\[.*?\]\[(0x[\da-fA-F]+)\]\[([\d\.]+)ms\]\s+(.*)")
    
    # CWND 패턴: 
    # - CWND UPDATE: 1196057 -> 1197529
    # - CWND+: ...
    # - CWND Update ...
    cwnd_pattern = re.compile(r"(?:CWND UPDATE|CWND Update|CWND\+|CWND Aggregated Update).*?:\s*\d+\s*->\s*(\d+)")
    
    # RTT 패턴:
    # - ROUND DIRTY: RTT(46496) > Thresh(46089)  -> (값은 us 단위로 추정)
    # - RTT Update: Curr=...
    rtt_dirty_pattern = re.compile(r"RTT\((\d+)\)")
    rtt_update_pattern = re.compile(r"(?:RTT Update|RTT).*?[:=]\s*([\d\.]+)")

    first_packet_time = None

    with open(file_path, 'r', encoding='utf-8') as f:
        for line in f:
            header_match = header_pattern.search(line)
            if not header_match:
                continue

            flow_id = header_match.group(1)
            log_time_ms = float(header_match.group(2))
            message = header_match.group(3)

            # 기준 시간이 없으면 첫 로그 시간을 0초로 설정
            if base_time_ms is None:
                if first_packet_time is None:
                    first_packet_time = log_time_ms
                    print(f"⚠️ MonotonicStartTime 부재. 첫 로그 시간({first_packet_time}ms)을 0초로 설정합니다.")
                base_time_ms = first_packet_time

            # 상대 시간 계산 (초 단위)
            rel_time_sec = (log_time_ms - base_time_ms) / 1000.0

            if flow_id not in flows_data:
                flows_data[flow_id] = {'cwnd': [], 'rtt': []}

            # --- CWND 파싱 ---
            cwnd_match = cwnd_pattern.search(message)
            if cwnd_match:
                cwnd = int(cwnd_match.group(1))
                flows_data[flow_id]['cwnd'].append((rel_time_sec, cwnd))

            # --- RTT 파싱 ---
            # 1. ROUND DIRTY: RTT(xxxx) 형태 (보통 us 단위)
            rtt_dirty_match = rtt_dirty_pattern.search(message)
            if rtt_dirty_match:
                rtt_us = float(rtt_dirty_match.group(1))
                rtt_ms = rtt_us / 1000.0  # us -> ms 변환
                flows_data[flow_id]['rtt'].append((rel_time_sec, rtt_ms))
            
            # 2. 일반 RTT Update 형태 (보통 ms 단위)
            elif 'RTT' in message:
                rtt_update_match = rtt_update_pattern.search(message)
                if rtt_update_match:
                    # 값이 너무 크면 us로 간주하고 변환, 작으면 ms로 간주 (heuristic)
                    raw_val = float(rtt_update_match.group(1))
                    if raw_val > 10000: # 10초 이상일 리 없으므로 us로 가정
                        rtt_ms = raw_val / 1000.0
                    else:
                        rtt_ms = raw_val
                    flows_data[flow_id]['rtt'].append((rel_time_sec, rtt_ms))

    if base_time_ms:
        print(f"   ℹ️ 기준 시간(t=0): {base_time_ms:.3f} ms")
    
    return flows_data

def plot_network_metrics(server_data, output_filename="server_analysis_v2.png"):
    if not server_data:
        print("⚠️ 그릴 데이터가 없습니다.")
        return

    # 스타일 설정
    plt.style.use('seaborn-v0_8-whitegrid')
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=True)

    # --- 1. CWND Plot ---
    has_cwnd = False
    for flow_id, metrics in server_data.items():
        if metrics['cwnd']:
            has_cwnd = True
            times, values = zip(*metrics['cwnd'])
            # Flow ID 뒷 4자리만 표시
            ax1.plot(times, values, label=f'Flow {flow_id[-4:]}', drawstyle='steps-post', linewidth=1.5, marker='.', markersize=4)
    
    ax1.set_title("TCP Congestion Window (CWND)", fontsize=14, fontweight='bold')
    ax1.set_ylabel("CWND (Bytes)", fontsize=12)
    ax1.grid(True, linestyle='--', alpha=0.7)
    ax1.set_ylim(bottom=0, top=3000000)
    if has_cwnd:
        ax1.legend(loc='lower right')

    # --- 2. RTT Plot ---
    has_rtt = False
    for flow_id, metrics in server_data.items():
        if metrics['rtt']:
            has_rtt = True
            times, values = zip(*metrics['rtt'])
            ax2.plot(times, values, label=f'Flow {flow_id[-4:]}', linestyle='-', marker='x', markersize=6, alpha=0.7)
    
    ax2.set_title("Round Trip Time (RTT)", fontsize=14, fontweight='bold')
    ax2.set_ylabel("RTT (ms)", fontsize=12)
    ax2.set_xlabel("Time (seconds)", fontsize=12)
    ax2.grid(True, linestyle='--', alpha=0.7)
    
    if has_rtt:
        ax2.legend(loc='upper right')
    else:
        ax2.text(0.5, 0.5, "No RTT data found", transform=ax2.transAxes, 
                 ha='center', va='center', color='gray', fontsize=12)

    plt.tight_layout()
    plt.savefig(output_filename, dpi=150)
    print(f"✅ 그래프 저장 완료: {output_filename}")

if __name__ == "__main__":
    server_log = "./build/bin/Release/testserver.txt"
    s_data = parse_server_log(server_log)
    plot_network_metrics(s_data)