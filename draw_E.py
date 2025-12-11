import re
import matplotlib.pyplot as plt

# 1. 데이터를 저장할 리스트 초기화
timestamps = []
e_values = []

# 2. 정규표현식 패턴 컴파일
# 타임스탬프 패턴: 대괄호 안의 숫자와 점, 그리고 ms로 끝나는 부분 추출
time_pattern = re.compile(r'\[(\d+\.\d+)ms\]')
# E 값 패턴: E= 뒤에 오는 숫자와 점 추출
e_pattern = re.compile(r'E=(\d+\.\d+)')

# 3. 파일 읽기 및 파싱
try:
    with open('./build/bin/Release/testserver.txt', 'r') as file:
        for line in file:
            # 각 줄에서 패턴 검색
            time_match = time_pattern.search(line)
            e_match = e_pattern.search(line)
            
            # 타임스탬프와 E 값이 모두 존재하는 경우에만 리스트에 추가
            if time_match and e_match:
                timestamps.append(float(time_match.group(1)))
                e_values.append(float(e_match.group(1)))

    # 4. 그래프 그리기
    if timestamps:
        # 첫 번째 로그 시간을 0으로 맞추어 상대 시간 계산 (선택 사항)
        start_time = timestamps[0]
        relative_timestamps = [t - start_time for t in timestamps]

        plt.figure(figsize=(10, 6))
        # X축: 상대 시간, Y축: E 값
        plt.plot(relative_timestamps, e_values, marker='o', linestyle='-', label='E Value')
        
        plt.title('Change of E Value Over Time')
        plt.xlabel('Time (ms, relative)')
        plt.ylabel('E Value')
        plt.grid(True)
        plt.legend()
        plt.ylim(0, 0.1)
        
        # 그래프 보여주기 (Jupyter 환경 등에서는 plt.show() 필요)
        plt.savefig('e_value_plot.png')
        plt.show()
    else:
        print("데이터를 추출하지 못했습니다. 파일 형식이나 정규표현식을 확인해주세요.")

except FileNotFoundError:
    print("testserver.txt 파일을 찾을 수 없습니다.")