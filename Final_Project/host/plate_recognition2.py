import socket
import cv2
import numpy as np
import pytesseract
import yolov5
import threading
from collections import deque, Counter
import time
import easyocr
import re

import warnings
warnings.filterwarnings("ignore", category=FutureWarning)

# sudo ufw disable  

# 載入 YOLOv5 車牌模型
model = yolov5.load('keremberke/yolov5m-license-plate').to('cuda')
model.conf = 0.25
model.iou = 0.45

# 儲存最近偵測結果
plate_history = deque(maxlen=6)

# TCP 傳送車牌至中央 server
def send_plate_to_server(plate, server_ip='192.168.7.2', server_port=8888):
    try:
        message_to_send = f"!{plate}\n"
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((server_ip, server_port))
            s.sendall(message_to_send.encode('utf-8'))
            print(f"✅ Sent to server: {message_to_send}")
            time.sleep(2)  # 確保資料完整發送
    except Exception as e:
        print(f"❌ TCP send error: {e}")

def detect_plate_and_ocr(image):
    results = model(image)
    plates = []

    for i, im in enumerate(results.ims):
        if not im.flags.writeable:
            results.ims[i] = im.copy()
    img = results.ims[0]

    for det in results.pred[0]:
        x1, y1, x2, y2, conf, cls = map(int, det[:6])
        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)

        crop = img[max(y1 - 15, 0):min(y2 + 15, img.shape[0]),
                   max(x1 - 15, 0):min(x2 + 15, img.shape[1])]
        gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
        enhanced = cv2.bilateralFilter(gray, 11, 17, 17)
        _, binary = cv2.threshold(enhanced, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)

        config = '--psm 7 -c tessedit_char_whitelist=0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
        text = pytesseract.image_to_string(binary, config=config, lang='eng').strip()
        text = re.sub(r'[^0-9A-Z-]', '', text)

        if text:
            cv2.putText(img, text, (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
            plates.append(text)

    return img, plates

def handle_udp_camera(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", port))
    print(f"📡 Listening on UDP port {port}...")

    while True:
        try:
            data, _ = sock.recvfrom(65536)
            frame = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                processed_img, plates = detect_plate_and_ocr(frame)

                for plate in plates:
                    plate_history.append(plate)
                    print(f"[Port {port}] 🪪 Plate: {plate}")

                if len(plate_history) == plate_history.maxlen:
                    most_common, count = Counter(plate_history).most_common(1)[0]
                    if count == plate_history.maxlen:
                        send_plate_to_server(most_common)
                        plate_history.clear()

                cv2.imshow(f"UDP Stream {port}", processed_img)

        except Exception as e:
            print(f"[Port {port}] ❌ Error: {e}")

        if cv2.waitKey(1) == ord('q'):
            break

# 只啟動其中一台攝影機
handle_udp_camera(5001)