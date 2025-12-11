import re
import os
import matplotlib.pyplot as plt

def parse_server_log(file_path):
    """
    서버 로그(testserver.txt)에서 CWND와 RTT 데이터를 파싱합니다.
    [CubicProbe]와 [Cubic] 포맷을 모두 지원하며, RTT가 없으면 CWND만 추출합니다.
    """
    expanded_path = os.path.expanduser(file_path)
    flows_data = {}

    print(f"📂 서버 로그 분석 중: '{expanded_path}'")

    # 1. 헤더 패턴: [Tag][FlowID][Time]
    # Tag는 Cubic 또는 CubicProbe 모두 허용
    header_pattern = re.compile(r"\[(Cubic|CubicProbe)\]\[(0x[\da-fA-F]+)\]\[([\d\.]+)ms\]\s+(.*)")
    
    # 2. CWND 패턴 (업데이트 및 Loss 처리)
    # Case A: "CWND Update ... -> 12345"
    # Case B: "LOSS EVENT: CWnd=12345" (Old)
    # Case C: "LOSS: CWnd=12345" (New)
    cwnd_pattern = re.compile(r"(?:CWND Update.*->|LOSS(?: EVENT)?: CWnd=)\s*(\d+)")
    
    # 3. RTT 패턴 (있을 수도 있고 없을 수도 있음)
    rtt_pattern = re.compile(r"RTT Update: Curr=([\d\.]+)ms")

    try:
        with open(expanded_path, 'r', encoding='utf-8') as f:
            for line in f:
                header_match = header_pattern.search(line)
                if not header_match:
                    continue

                # group(1)은 Tag이므로 건너뛰고, group(2)부터 ID
                flow_id = header_match.group(2)
                time_ms = float(header_match.group(3))
                message = header_match.group(4)

                if flow_id not in flows_data:
                    flows_data[flow_id] = {'cwnd': [], 'rtt': []}

                # --- CWND 추출 ---
                cwnd_match = cwnd_pattern.search(message)
                if cwnd_match:
                    cwnd_bytes = int(cwnd_match.group(1))
                    flows_data[flow_id]['cwnd'].append((time_ms, cwnd_bytes))

                # --- RTT 추출 (데이터가 있는 경우에만) ---
                rtt_match = rtt_pattern.search(message)
                if rtt_match:
                    rtt_ms = float(rtt_match.group(1))
                    flows_data[flow_id]['rtt'].append((time_ms, rtt_ms))

    except FileNotFoundError:
        print(f"❌ 오류: 서버 로그 파일 '{expanded_path}'을(를) 찾을 수 없습니다.")
        return {}

    return flows_data

def parse_client_log(file_path):
    """
    클라이언트 로그(testclient.txt)에서 Throughput 데이터를 파싱합니다.
    """
    expanded_path = os.path.expanduser(file_path)
    throughput_data = []

    print(f"📂 클라이언트 로그 분석 중: '{expanded_path}'")

    # 패턴: [CLIENT] Time: 14006942788.828ms | Throughput: 31.97 Mbps
    client_pattern = re.compile(r"\[CLIENT\] Time:\s*([\d\.]+)ms\s*\|\s*Throughput:\s*([\d\.]+)\s*Mbps")

    try:
        with open(expanded_path, 'r', encoding='utf-8') as f:
            for line in f:
                match = client_pattern.search(line)
                if match:
                    time_ms = float(match.group(1))
                    mbps = float(match.group(2))
                    throughput_data.append((time_ms, mbps))

    except FileNotFoundError:
        print(f"❌ 오류: 클라이언트 로그 파일 '{expanded_path}'을(를) 찾을 수 없습니다.")
        return []

    return throughput_data

def plot_network_metrics(server_data, client_data, output_filename="network_analysis.png"):
    """
    서버 데이터(CWND, RTT)와 클라이언트 데이터(Throughput)를 시각화합니다.
    RTT 데이터가 없으면 해당 그래프에 'No Data'를 표시합니다.
    """
    if not server_data and not client_data:
        print("⚠️ 시각화할 데이터가 없습니다.")
        return

    # 1. 시간 동기화 (가장 빠른 시간을 0초로 설정)
    all_start_times = []
    
    for fid in server_data:
        if server_data[fid]['cwnd']: all_start_times.append(server_data[fid]['cwnd'][0][0])
        if server_data[fid]['rtt']: all_start_times.append(server_data[fid]['rtt'][0][0])
    
    if client_data:
        all_start_times.append(client_data[0][0])

    if not all_start_times:
        print("데이터에 유효한 타임스탬프가 없습니다.")
        return

    min_start_time = min(all_start_times)
    print(f"⏱️ 시작 시간(t=0) 기준: {min_start_time}ms")

    # 스타일 설정
    plt.style.use('seaborn-v0_8-whitegrid')
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 12), sharex=True)

    # --- 1. CWND 그래프 (서버 데이터) ---
    has_cwnd = False
    for flow_id, metrics in server_data.items():
        if metrics['cwnd']:
            has_cwnd = True
            times = [(t - min_start_time)/1000.0 for t, v in metrics['cwnd']]
            values = [v for t, v in metrics['cwnd']]
            short_id = flow_id[-4:]
            ax1.plot(times, values, label=f'Flow {short_id}', drawstyle='steps-post')
    
    ax1.set_title("1. Congestion Window (Server Side)", fontsize=14, fontweight='bold')
    ax1.set_ylabel("CWND (Bytes)", fontsize=12)
    if has_cwnd: ax1.legend(loc='upper right')
    ax1.grid(True, linestyle='--', alpha=0.7)

    # --- 2. RTT 그래프 (서버 데이터 - 없을 수 있음) ---
    has_rtt = False
    for flow_id, metrics in server_data.items():
        if metrics['rtt']:
            has_rtt = True
            times = [(t - min_start_time)/1000.0 for t, v in metrics['rtt']]
            values = [v for t, v in metrics['rtt']]
            short_id = flow_id[-4:]
            ax2.plot(times, values, label=f'Flow {short_id}', color='tab:orange', alpha=0.8)

    ax2.set_title("2. Round Trip Time (Server Side)", fontsize=14, fontweight='bold')
    ax2.set_ylabel("RTT (ms)", fontsize=12)
    ax2.grid(True, linestyle='--', alpha=0.7)
    
    if has_rtt:
        ax2.legend(loc='upper right')
    else:
        # RTT 데이터가 없을 경우 텍스트 표시
        ax2.text(0.5, 0.5, "No RTT Data Available (Cubic Log)", 
                 horizontalalignment='center', verticalalignment='center', 
                 transform=ax2.transAxes, fontsize=14, color='gray')

    # --- 3. Throughput 그래프 (클라이언트 데이터) ---
    if client_data:
        times = [(t - min_start_time)/1000.0 for t, v in client_data]
        values = [v for t, v in client_data]
        ax3.plot(times, values, label='Client Throughput', color='tab:red', marker='.', linestyle='-')
        ax3.legend(loc='upper right')
    else:
        ax3.text(0.5, 0.5, "No Client Data", 
                 horizontalalignment='center', verticalalignment='center', 
                 transform=ax3.transAxes, fontsize=14, color='gray')
    
    ax3.set_title("3. Throughput (Client Side)", fontsize=14, fontweight='bold')
    ax3.set_ylabel("Throughput (Mbps)", fontsize=12)
    ax3.set_xlabel("Time (seconds)", fontsize=12)
    ax3.grid(True, linestyle='--', alpha=0.7)

    plt.tight_layout()
    plt.savefig(output_filename, dpi=150)
    print(f"✅ 그래프가 '{output_filename}' 파일로 저장되었습니다.")

if __name__ == "__main__":
    server_log = "./build/bin/Release/testserver.txt"
    client_log = "./build/bin/Release/testclient.txt"
    
    s_data = parse_server_log(server_log)
    c_data = parse_client_log(client_log)
    
    plot_network_metrics(s_data, c_data)