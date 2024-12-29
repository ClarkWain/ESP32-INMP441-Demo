import socket
import wave
import threading
import time
import struct

HOST = '0.0.0.0'        # 监听所有网络接口
PORT = 12345            # 监听端口
CHUNK_SIZE = 1024       # 每次接收的数据大小
CHANNELS = 1            # 单声道
SAMPLE_WIDTH = 2        # 16位
SAMPLE_RATE = 16000     # 16kHz

buffer_lock = threading.Lock()  # 缓冲区锁
buffer_data = bytearray()       # 缓冲区数据
running = True

def save_task():
    while running:
        print("Saving task running...")
        time.sleep(5)  # 每隔5秒保存一次
        print("Saving task triggered.")
        with buffer_lock:
            if buffer_data:
                filename = time.strftime("audio_%Y%m%d_%H%M%S.wav")
                print(f"Saving {filename} (size={len(buffer_data)} bytes)")
                with wave.open(filename, 'wb') as wf:
                    wf.setnchannels(CHANNELS)
                    wf.setsampwidth(SAMPLE_WIDTH)
                    wf.setframerate(SAMPLE_RATE)
                    wf.writeframes(buffer_data)
                buffer_data.clear()

def calculate_checksum(data):
    return sum(data) & 0xFFFFFFFF

def handle_client(conn, addr):
    print(f"Client connected: {addr}")
    buffer = bytearray()
    FRAME_SIZE = 2048   # 每帧期望的字节数
    HEADER_SIZE = 4     # 数据长度字段大小
    CHECKSUM_SIZE = 4   # 校验和字段大小

    while True:
        data = conn.recv(CHUNK_SIZE)
        if not data:
            print(f"Client {addr} disconnected.")
            break  # 正确退出循环
        with buffer_lock:
            # 将接收到的数据追加到缓冲区
            buffer.extend(data)
            print(f"Received {len(data)} bytes from {addr}")

            # 解析数据包
            while True:
                if len(buffer) < HEADER_SIZE:
                    break  # 不足以读取长度

                # 读取长度（四字节，网络字节序）
                length = struct.unpack('!I', buffer[:HEADER_SIZE])[0]
                if len(buffer) < HEADER_SIZE + length + CHECKSUM_SIZE:
                    break  # 不足以读取完整数据包

                # 读取数据
                data_start = HEADER_SIZE
                data_end = HEADER_SIZE + length
                frame = buffer[data_start:data_end]

                # 读取校验和
                checksum_expected = struct.unpack('!I', buffer[data_end:data_end + CHECKSUM_SIZE])[0]

                # 计算校验和
                checksum_calculated = calculate_checksum(frame)

                if checksum_calculated == checksum_expected:
                    # 校验通过，写入缓冲数据
                    buffer_data.extend(frame)
                else:
                    print(f"Checksum mismatch from {addr}. Expected {checksum_expected}, got {checksum_calculated}")

                # 移除已处理的数据包
                buffer = buffer[data_end + CHECKSUM_SIZE:]

    print(f"Closing connection with {addr}")
    conn.close()

def start_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind((HOST, PORT))
    s.listen(5)  # 增加监听队列长度
    print(f"Server listening on {HOST}:{PORT}")
    while True:
        conn, addr = s.accept()
        print(f"Connection from {addr}")
        handle_client(conn, addr)

if __name__ == "__main__":
    t_save = threading.Thread(target=save_task, daemon=True)
    t_save.start()
    start_server()
