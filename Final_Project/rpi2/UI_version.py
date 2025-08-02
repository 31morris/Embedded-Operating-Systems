import pygame, sys, glob, os, math
import socket
import threading
from collections import deque
import random

# ────────────── Enhanced Parameters ──────────────
SCREEN_W, SCREEN_H = 1500, 900

# Connected gate system - both gates on 4he left side
ENTRY_X, ENTRY_Y   = 120, 250   # Upper gate for entry
ENTRY2_X, ENTRY2_Y = 120, 700   # Upper gate2 for entry
EXIT_X , EXIT_Y    = 120, 390   # Lower gate for exit

# Adjusted positioning to prevent overlap
Y_TOP, Y_BOTTOM  = 230, 700
LANE_RED_Y       = 350        # Middle upper lane →
LANE_GREEN_Y     = 550        # Middle lower lane ←
LANE_WIDTH       = 70         # Lane width

FPS              = 60
ASSETS           = "assets/"
WAIT_FRAMES      = int(1.0*FPS)        # Wait 1 second
OUTSIDE_WAIT     = int(1*FPS)          # Wait 1 second outside (reduced)

ARROW_LEN, ARROW_WID = 40, 20
TURN_R           = 75
FLOOR_OFF   = 820
NUM_LEVELS  = 2
GATE_HOLD_FRAMES = 2 * FPS   # 2 seconds

# Enhanced visual parameters
SLOT_WIDTH, SLOT_HEIGHT = 80, 120
GATE_WIDTH, GATE_HEIGHT = 20, 100
# ─────────────────────────────────────

def bez(p0,p1,p2,p3,t):
    u = 1-t
    return (u**3*p0[0]+3*u*u*t*p1[0]+3*u*t*t*p2[0]+t**3*p3[0],
            u**3*p0[1]+3*u*u*t*p1[1]+3*u*t*t*p2[1]+t**3*p3[1])

class ParkingUI:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((SCREEN_W,SCREEN_H))
        pygame.display.set_caption("Enhanced Parking‑Lot Simulator")
        self.clock  = pygame.time.Clock()
        
        self._load_assets()
        self._build_slots()
        self.gate_img = self._make_gate()
        self.background = pygame.Surface((SCREEN_W, SCREEN_H))
        self._render_static(self.background)
        # Fixed gate system with proper pivot points
        self.entry_gate = {
            "rect": pygame.Rect(ENTRY_X-GATE_WIDTH//2, ENTRY_Y-GATE_HEIGHT//2, GATE_WIDTH, GATE_HEIGHT),
            "angle": 0, 
            "state": "CLOSE", 
            "pivot": (ENTRY_X-GATE_WIDTH//2, ENTRY_Y-GATE_HEIGHT//2)  # Top-left corner as pivot for upward rotation
        }
        self.entry_gate2 = {
            "rect": pygame.Rect(ENTRY2_X - GATE_WIDTH//2, ENTRY2_Y - GATE_HEIGHT//2,
                                GATE_WIDTH, GATE_HEIGHT),
            "angle": 0, "state": "CLOSE",
            "pivot": (ENTRY2_X-GATE_WIDTH//2, ENTRY2_Y-GATE_HEIGHT//2)
        }

        self.exit_gate = {
            "rect": pygame.Rect(EXIT_X-GATE_WIDTH//2, EXIT_Y-GATE_HEIGHT//2, GATE_WIDTH, GATE_HEIGHT),
            "angle": 0, 
            "state": "CLOSE", 
            "pivot": (EXIT_X-GATE_WIDTH//2, EXIT_Y+GATE_HEIGHT//2)  # Bottom-left corner as pivot for downward rotation
        }

        self.cars  = pygame.sprite.Group()
        self.cmd_q = deque()

        self.slots_f1   = self.slots         # slot list you built
        self.entry_f1   = (self.entry_gate, self.entry_gate2)
        self.exit_f1    = self.exit_gate
        self.cars_f1    = self.cars
        self.cmd_q_f1   = self.cmd_q

        # ────── BUILD FLOOR 2 ──────
        # reuse your same slot‐builder
        self._build_slots()
        self.slots_f2   = self.slots

        self.entry_f2 = ({
            "rect":  pygame.Rect(ENTRY2_X - GATE_WIDTH//2, ENTRY2_Y - GATE_HEIGHT//2,
                                 GATE_WIDTH, GATE_HEIGHT),
            "angle": 0, "state":"CLOSE",
            "pivot":(ENTRY2_X - GATE_WIDTH//2, ENTRY2_Y - GATE_HEIGHT//2)
        },)

        # exit gate for floor2
        self.exit_f2  = {
            "rect": pygame.Rect(EXIT_X - GATE_WIDTH//2, EXIT_Y - GATE_HEIGHT//2,
                                GATE_WIDTH, GATE_HEIGHT),
            "angle": 0, "state":"CLOSE",
            "pivot":(EXIT_X - GATE_WIDTH//2, EXIT_Y + GATE_HEIGHT//2)
        }

        self.cars_f2  = pygame.sprite.Group()
        self.cmd_q_f2 = deque()

        # start on floor 1
        self.active_floor = 1

        # ─── 新增：和 C‐Server 連線，並在背景執行緒不停收訊 ───
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            RASPBERRY_PI_IP = "192.168.7.2" 
            self.sock.connect((RASPBERRY_PI_IP, 8889)) 
        except Exception as e:
            print(f">>> [ERROR] 無法連到 Server ({RASPBERRY_PI_IP}:8889): {e}")
        else:
            print(f">>> [INFO] Connected to Server at {RASPBERRY_PI_IP}:8889")
            threading.Thread(target=self._recv_from_server, daemon=True).start()

    def _recv_from_server(self):
        """
        這個方法會在背景 thread 一直跑, recv 伺服器送來的車牌字串。
        收到「A111\n」之後, 就模擬一次 KEYDOWN (比如把 KEYDOWN:1 放到 Pygame 的事件佇列)。
        """
        buf = b""
        while True:
            try:
                data = self.sock.recv(1024)
                if not data:
                    # 連線關閉，直接跳出
                    print(">>> [INFO] Server 連線已斷開")
                    break
                buf += data
                # 伺服器有可能一次 send 不只一行，所以我們用 b'\n' 拆訊息
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode(errors="ignore").strip()
                    if not text:
                        continue
                    # text 可能是 "A111"、"E888"、"W123" 或其他格式
                    self._handle_server_message(text)
            except Exception as e:
                print(f">>> [ERROR] 接收 Server 訊息失敗: {e}")
                break

    def _handle_server_message(self, msg: str):
            print(f"[UI RECV] Server raw: {msg}")
            parts = msg.strip().split(':')
            cmd = parts[0].upper()

            if cmd == "ENTER" and len(parts) == 4:
                raw_plate, car_type_str, lot_char = parts[1], parts[2].upper(), parts[3].upper()
                
                use_gate2 = False
                plate = raw_plate

                if raw_plate.startswith('!'):
                    use_gate2 = True
                    plate = raw_plate[1:] # 獲取不帶 '!' 的實際車牌號

                print(f"[UI PARSED] ENTER Plate='{plate}', Type='{car_type_str}', Lot='{lot_char}', UseGate2={use_gate2}")

                # 根據 lot 切換樓層
                if lot_char == 'B':
                    self.active_floor = 2
                    print(">>> [UI INFO] Switched to Floor 2 for Lot B")
                elif lot_char == 'A':
                    self.active_floor = 1
                    print(">>> [UI INFO] Switched to Floor 1 for Lot A")

                key_to_post = None
                
                # ★★★ 修正點：根據車輛類型和閘門選擇來決定要模擬哪個按鍵 ★★★
                if car_type_str == "NORMAL":
                    # 一般車輛：如果 use_gate2 為真，按 '2'；否則按 '1'
                    key_to_post = pygame.K_2 if use_gate2 else pygame.K_1
                elif car_type_str == "EV":
                    # 電動車：如果 use_gate2 為真，按 '5'；否則按 '3'
                    key_to_post = pygame.K_5 if use_gate2 else pygame.K_3
                elif car_type_str == "HANDICAP":
                    # 殘障車：如果 use_gate2 為真，按 '6'；否則按 '4'
                    key_to_post = pygame.K_6 if use_gate2 else pygame.K_4

                if key_to_post:
                    # 這次的事件中，不再需要手動加入 car_type，因為按鍵本身已經隱含了類型
                    # 但為了保持 _handle_events 的統一性，我們仍然可以傳遞它
                    print(f">>> [UI INFO] Posting KEYDOWN event for car '{plate}' (Type: {car_type_str}, Gate2: {use_gate2}, Key: {pygame.key.name(key_to_post)})")
                    event = pygame.event.Event(pygame.KEYDOWN, {
                        "key": key_to_post, 
                        "plate": plate, 
                        "from_server": True,
                        "car_type": car_type_str # 保持傳遞 car_type 以支持統一的 _handle_events
                    })
                    pygame.event.post(event)
                else:
                    print(f">>> [UI WARN] Unknown car type '{car_type_str}'. Could not determine key_to_post for entry request.")

            # ... (EXIT_SLOT, EXIT, PARK_FAIL 等其他 elif 區塊保持不變) ...
            elif cmd == "EXIT_SLOT" and len(parts) == 2:
                # ... (你現有的 EXIT_SLOT 處理邏輯，無需修改) ...
                raw_slot_representation = parts[1]
                slot_letter_to_exit = ''
                if raw_slot_representation.startswith('+'):
                    if len(raw_slot_representation) > 1:
                        slot_letter_to_exit = raw_slot_representation[1].upper()
                        if self.active_floor != 2:
                            self.active_floor = 2
                            print(f">>> [UI INFO] Switched to Floor 2 for EXIT_SLOT '{raw_slot_representation}'.")
                            pygame.time.wait(50)
                    else:
                        print(f">>> [UI WARN] Invalid EXIT_SLOT format: '{raw_slot_representation}'")
                        return 
                else:
                    slot_letter_to_exit = raw_slot_representation.upper()
                    if self.active_floor != 1:
                        self.active_floor = 1
                        print(f">>> [UI INFO] Switched to Floor 1 for EXIT_SLOT '{raw_slot_representation}'.")
                        pygame.time.wait(50)
                
                if 'A' <= slot_letter_to_exit <= 'J':
                    key_attr_name = f"K_{slot_letter_to_exit.lower()}"
                    if hasattr(pygame, key_attr_name):
                        key_to_post = getattr(pygame, key_attr_name)
                        print(f">>> [UI INFO] Posting KEYDOWN event for exiting from slot {slot_letter_to_exit}...")
                        event = pygame.event.Event(pygame.KEYDOWN, {"key": key_to_post, "from_server_exit_slot_command": True})
                        pygame.event.post(event)
                    else:
                        print(f">>> [UI WARN] Pygame has no key attribute for {key_attr_name}")
                else:
                    print(f">>> [UI WARN] Invalid slot letter '{slot_letter_to_exit}' for EXIT_SLOT.")
            
            else:
                print(f">>> [UI INFO] Received unhandled/generic message from server: {msg}")

    def _load_assets(self):
        TARGET_CAR_W = 95
        TARGET_CAR_H = 95
        # Try to load car assets, create defaults if missing
        try:
            raw_surfaces = [
                pygame.image.load(p).convert_alpha()
                for p in glob.glob(os.path.join(ASSETS, "car_*.png"))
            ]
            
            # Rotate and then scale each one
            self.CAR_SKINS = []
            for surf in raw_surfaces:
                # 1) Rotate 90° (so “car” faces right/down as needed)
                rotated = pygame.transform.rotate(surf, 90)

                # 2) Now scale to exactly (TARGET_CAR_W, TARGET_CAR_H)
                #    You can choose smoothscale if you prefer less jagged edges;
                #    scale will be a little faster but possibly blockier.
                scaled = pygame.transform.smoothscale(rotated, (TARGET_CAR_W, TARGET_CAR_H))

                self.CAR_SKINS.append(scaled)
        except:
            self.CAR_SKINS = []
            
        if not self.CAR_SKINS:
            # Create default car images with better visibility
            self.CAR_SKINS = []
            colors = [(255,0,0), (0,255,0), (0,0,255), (255,255,0), (255,0,255), (0,255,255), (128,0,255)]
            for color in colors:
                surf = pygame.Surface((80, 40), pygame.SRCALPHA)
                # Main car body
                pygame.draw.rect(surf, color, (5, 5, 70, 30), border_radius=8)
                # Car outline
                pygame.draw.rect(surf, (0,0,0), (5, 5, 70, 30), 3, border_radius=8)
                # Windows
                pygame.draw.rect(surf, (200,200,255), (15, 10, 20, 20), border_radius=3)
                pygame.draw.rect(surf, (200,200,255), (45, 10, 20, 20), border_radius=3)
                # Wheels
                pygame.draw.circle(surf, (50,50,50), (15, 5), 8)
                pygame.draw.circle(surf, (50,50,50), (65, 5), 8)
                pygame.draw.circle(surf, (50,50,50), (15, 35), 8)
                pygame.draw.circle(surf, (50,50,50), (65, 35), 8)
                self.CAR_SKINS.append(surf)

        ev_path = os.path.join(ASSETS, "ev_clean.png")           ### ⇐ 新增

        try:
            ev_img = pygame.image.load(ev_path).convert_alpha()   ### ⇐ 新增
        except:
            raise FileNotFoundError(f"找不到 EV 圖示：{ev_path}") ### ⇐ 新增
        # 縮放到 40×40（可依需求改變）
        self.ev_icon = pygame.transform.smoothscale(ev_img, (40, 40))   ### ⇐ 新增

        # 2. 載入 WC（輪椅）圖示
        wc_path = os.path.join(ASSETS, "wc_clean.png")           ### ⇐ 新增

        try:
            wc_img = pygame.image.load(wc_path).convert_alpha()   ### ⇐ 新增
        except:
            raise FileNotFoundError(f"找不到 WC 圖示：{wc_path}")
        self.wc_icon = pygame.transform.smoothscale(wc_img, (85, 85))
        # Enhanced parking slot design with different layouts for top/bottom
        pay_path = os.path.join(ASSETS, "pay.png")
        try:
            raw_pay = pygame.image.load(pay_path).convert_alpha()
        except Exception as e:
            raise FileNotFoundError(f"找不到 pay.png：{pay_path}。請檢查檔案路徑。\n錯誤：{e}")
        # 將 pay 圖縮放到 60×60（你可以根據需求調整大小）
        self.pay_icon = pygame.transform.smoothscale(raw_pay, (90, 90))
        self._make_slot_images()

    def _make_slot_images(self):

        self.SLOT_IMG_TOP = pygame.Surface((SLOT_WIDTH, SLOT_HEIGHT), pygame.SRCALPHA)
        for i in range(SLOT_HEIGHT):
            alpha = int(255 * (1 - i / SLOT_HEIGHT * 0.3))
            color = (240, 240, 240, alpha)
            pygame.draw.line(self.SLOT_IMG_TOP, color[:3], (0, i), (SLOT_WIDTH, i))
        
        pygame.draw.rect(self.SLOT_IMG_TOP, (100, 100, 100), 
                         self.SLOT_IMG_TOP.get_rect(), 3, border_radius=10)
        
        # Yellow blocks at top for upper slots
        block_width, block_height = 20, 8
        pygame.draw.rect(self.SLOT_IMG_TOP, (255, 255, 0), 
                        (10, 5, block_width, block_height), border_radius=3)
        pygame.draw.rect(self.SLOT_IMG_TOP, (255, 255, 0), 
                        (SLOT_WIDTH-30, 5, block_width, block_height), border_radius=3)

        # Bottom slots (letter and LED at bottom, yellow blocks at bottom)
        self.SLOT_IMG_BOTTOM = pygame.Surface((SLOT_WIDTH, SLOT_HEIGHT), pygame.SRCALPHA)
        for i in range(SLOT_HEIGHT):
            alpha = int(255 * (1 - i / SLOT_HEIGHT * 0.3))
            color = (240, 240, 240, alpha)
            pygame.draw.line(self.SLOT_IMG_BOTTOM, color[:3], (0, i), (SLOT_WIDTH, i))
        
        pygame.draw.rect(self.SLOT_IMG_BOTTOM, (100, 100, 100), 
                         self.SLOT_IMG_BOTTOM.get_rect(), 3, border_radius=10)
        
        # Yellow blocks at bottom for lower slots
        pygame.draw.rect(self.SLOT_IMG_BOTTOM, (255, 255, 0), 
                        (10, SLOT_HEIGHT-13, block_width, block_height), border_radius=3)
        pygame.draw.rect(self.SLOT_IMG_BOTTOM, (255, 255, 0), 
                        (SLOT_WIDTH-30, SLOT_HEIGHT-13, block_width, block_height), border_radius=3)

    def _make_gate(self):
        surf = pygame.Surface((GATE_WIDTH, GATE_HEIGHT), pygame.SRCALPHA)
        num_stripes = 7
        stripe_height = GATE_HEIGHT // num_stripes
        for i in range(num_stripes):
            color = (255, 215, 0) if i % 2 == 0 else (0, 0, 0)
            pygame.draw.rect(surf, color, (0, i * stripe_height, GATE_WIDTH, stripe_height))
        
        pygame.draw.rect(surf, (150, 150, 150), (0, 0, GATE_WIDTH, GATE_HEIGHT), 2)
        return surf
    def _render_static(self, surf):
        # 1) background gradient
        for y in range(SCREEN_H):
            c = int(100 + 50 * (y / SCREEN_H))
            pygame.draw.line(surf, (c,c,c), (0,y), (SCREEN_W,y))

        # 2) main border
        border = pygame.Rect(50,50,SCREEN_W-100,SCREEN_H-100)
        pygame.draw.rect(surf, (180,180,180), border, 8, border_radius=15)
        pygame.draw.rect(surf, (120,120,120), border, 3, border_radius=15)

        # 3) gate-connection box
        conn = pygame.Rect(50, ENTRY_Y-50, 250, EXIT_Y-ENTRY_Y+100)
        pygame.draw.rect(surf, (160,160,160), conn, border_radius=10)
        pygame.draw.rect(surf, (100,100,100), conn, 2, border_radius=10)

        # 4) top green area + trees
        top = pygame.Rect(350, 80, SCREEN_W-450, 80)
        pygame.draw.rect(surf, (100,200,100), top, border_radius=10)
        pygame.draw.rect(surf, (80,150,80), top, 2, border_radius=10)
        # bottom green area
        bot = pygame.Rect(350, SCREEN_H-130, SCREEN_W-450, 80)
        pygame.draw.rect(surf, (100,200,100), bot, border_radius=10)
        pygame.draw.rect(surf, (80,150,80), bot, 2, border_radius=10)

        # trees in both
        for area in (top, bot):
            for i in range(4):
                tx = area.left + 50 + i*200
                ty = area.centery
                pygame.draw.circle(surf, (50,150,50), (tx,ty), 15)
                pygame.draw.rect(surf, (139,69,19), (tx-3, ty, 6, 20))

        # 5) static arrows on lanes
        for x in range(450, 1200, 200):
            self._draw_arrow(x, LANE_RED_Y, "right", target_surf=surf)
            self._draw_arrow(x, LANE_GREEN_Y, "left",  target_surf=surf)

        # 6) the single vertical arrow
        self._draw_arrow(
            SCREEN_W-150,
            (LANE_RED_Y+LANE_GREEN_Y)//2,
            "up",
            target_surf=surf
        )

    class Slot:
        def __init__(self, rect, letter, is_top_row=True):
            self.rect = rect
            self.letter = letter
            self.state = "EMPTY"
            self.is_top_row = is_top_row

    def _build_slots(self):
        self.slots = []
        letters = "ABCDEFGHIJ"
        start_x = 500
        spacing_x = 180
        n = 0
        
        # Upper row - moved up to avoid lane overlap
        for i in range(5):
            x = start_x + i * spacing_x
            r = pygame.Rect(0, 0, SLOT_WIDTH, SLOT_HEIGHT)
            r.center = (x, Y_TOP)
            self.slots.append(ParkingUI.Slot(r, letters[n], True))
            n += 1
        
        # Lower row - moved down to avoid lane overlap
        for i in range(5):
            x = start_x + i * spacing_x
            r = pygame.Rect(0, 0, SLOT_WIDTH, SLOT_HEIGHT)
            r.center = (x, Y_BOTTOM)
            self.slots.append(ParkingUI.Slot(r, letters[n], False))
            n += 1

    def _find_empty_slot_by_category(self, floor_slots, category: str):
        for s in floor_slots:
            if s.state != "EMPTY":
                continue
            if category == "normal":
                if s.letter not in ["F","G","H","I","J"]:
                    return s
            elif category == "ev":
                if s.letter in ["F","G","H"]:
                    return s
            elif category == "wc":
                if s.letter in ["I","J"]:
                    return s
        return None

    class Car(pygame.sprite.Sprite):
        def __init__(self, slot, skin, ui, entry_gate, exit_gate, plate_number=""):
            super().__init__()
            self.orig = skin
            self.rot_up = pygame.transform.rotate(skin, 90)
            self.rot_dn = pygame.transform.rotate(skin, -90)
            self.rot_left = pygame.transform.rotate(skin, 180)
            self.entry_gate = entry_gate
            self.exit_gate = exit_gate
            ex, ey = entry_gate["rect"].center
            self.image = self.orig
            self.slot = slot
            self.ui = ui
            self.rect = self.image.get_rect(center=(ex - 150, ey))


            self.phase = "WAIT_OUTSIDE"
            self.wait = OUTSIDE_WAIT
            self.idx = 0
            self.path = []
            self._mk_entry_path()
            self.visible = False  # Don't show car until it starts moving

        def _line(self, p0, p1, step=3):
            x0, y0 = p0; x1, y1 = p1
            dist = math.hypot(x1 - x0, y1 - y0)
            n = max(1, int(dist // step))
            for i in range(1, n + 1):
                t = i / n
                yield (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)

        def _mk_entry_path(self):
            p = []
            # From outside to gate waiting position
            ex, ey = self.entry_gate["rect"].center
            gate_pos = (ex - 40, ey)

            p += list(self._line(gate_pos, (250, ey), 4))
            p += list(self._line(p[-1], (250, LANE_RED_Y), 4))

            upper = self.slot.rect.centery < (Y_TOP + Y_BOTTOM) // 2
            if upper:
                # Red lane to slot
                p += list(self._line(p[-1], (self.slot.rect.centerx, LANE_RED_Y), 4))
                p += list(self._line(p[-1], self.slot.rect.center, 4))
            else:
                # Red lane to right, down to green lane, then to slot
                right_x = SCREEN_W - 150
                p += list(self._line(p[-1], (right_x, LANE_RED_Y), 4))
                p += list(self._line(p[-1], (right_x, LANE_GREEN_Y), 4))
                p += list(self._line(p[-1], (self.slot.rect.centerx, LANE_GREEN_Y), 4))
                p += list(self._line(p[-1], self.slot.rect.center, 4))

            self.path = p

        def _mk_exit_path(self):
            p = []
            cx, cy = self.slot.rect.center
            is_upper_slot = cy < (Y_TOP + Y_BOTTOM) // 2

            # 步驟 1: 從停車格開到對應的水平車道
            lane_y = LANE_RED_Y if is_upper_slot else LANE_GREEN_Y
            p += list(self._line((cx, cy), (cx, lane_y), step=3))
            
            # 步驟 2: 如果是上方車輛，需要開到右側，再開到下方的綠色離場車道
            if is_upper_slot:
                # 沿著紅色車道開到最右邊
                p += list(self._line(p[-1], (SCREEN_W - 150, LANE_RED_Y), step=4))
                # 從最右邊開到下方綠色車道
                p += list(self._line(p[-1], (SCREEN_W - 150, LANE_GREEN_Y), step=4))
            
            # ★★★ 統一的離場路徑 ★★★
            # 此時，所有車輛都位於下方的綠色車道 (LANE_GREEN_Y)
            
            # 步驟 3: 沿著綠色車道開到出口的準備區 (在出口右側)
            # 讓所有車輛都先開到一個靠近出口但還沒到閘門口的位置
            pre_exit_point = (EXIT_X + 150, LANE_GREEN_Y)
            p += list(self._line(p[-1], pre_exit_point, step=4))

            # 步驟 4: 從準備區開到閘門口的等待點 (車頭朝上)
            gate_wait_point = (EXIT_X + 150, EXIT_Y)
            p += list(self._line(p[-1], gate_wait_point, step=3))
            
            # ★★★ 設定等待點 ★★★
            # 在這個點，車輛會停下來，等待閘門打開
            self.exit_hold_idx = len(p) -1  # 減 1，停在到達目標點的那一刻

            # 步驟 5: 穿越閘門，開到閘門左側
            p += list(self._line(p[-1], (EXIT_X - 40, EXIT_Y), step=4))
            
            # 步驟 6: 開出畫面
            p += list(self._line(p[-1], (EXIT_X - 150, EXIT_Y), step=4))
            
            self.path = p


        def start_exit(self):
            self.slot.state = "EMPTY"
            self._mk_exit_path()
            self.idx       = 0
            self.image     = self.rot_up if self.slot.rect.centery < (Y_TOP+Y_BOTTOM)//2 else self.rot_dn
            self.phase     = "EXIT_DRIVE"
            self.visible   = True

# 在 ParkingUI.Car 類別中，用這個新版本替換掉舊的 update 函數

        def update(self):
            if self.phase == "WAIT_OUTSIDE":
                # ... (這部分不變) ...
                self.wait -= 1
                if self.wait <= 0:
                    self.phase = "APPROACH_GATE"
                    self.visible = True
                    ex, ey = self.entry_gate["rect"].center # 修正：字典取值
                    self.rect.center = (ex - 150, ey)

            elif self.phase == "APPROACH_GATE":
                # ... (這部分不變) ...
                ex, ey = self.entry_gate["rect"].center
                target = (ex - 40, ey)
                dx = target[0] - self.rect.centerx
                dy = target[1] - self.rect.centery
                if abs(dx) > 2 or abs(dy) > 2:
                    self.rect.centerx += 2 if dx > 0 else -2 if dx < 0 else 0
                    self.rect.centery += 2 if dy > 0 else -2 if dy < 0 else 0
                else:
                    self.phase = "WAIT_GATE"
                    self.wait = WAIT_FRAMES

            elif self.phase == "WAIT_GATE":
                # ... (這部分不變) ...
                self.wait -= 1
                if self.wait <= 0 and self.entry_gate["angle"] >= 85:
                    self.phase = "PATH"

            elif self.phase == "PATH":
                # ... (這部分不變) ...
                if self.idx < len(self.path):
                    self._goto(self.path[self.idx])
                    self.idx += 1
                else:
                    self.image = self.rot_dn if self.slot.rect.centery > LANE_RED_Y else self.rot_up
                    self.rect = self.image.get_rect(center=self.slot.rect.center)
                    self.slot.state = "OCCUPIED"
                    self.phase = "STOP"

            elif self.phase == "STOP":
                pass

            # ★★★ 修改後的離場狀態處理 ★★★
            elif self.phase == "EXIT_DRIVE":
                # 如果還沒到閘門口的等待點，就繼續前進
                if self.idx < self.exit_hold_idx:
                    self._goto(self.path[self.idx])
                    self.idx += 1
                # 如果剛好到達等待點
                else:
                    # 在等待點停下，確保車頭朝上
                    self.rect.center = self.path[self.exit_hold_idx]
                    self.image = self.rot_up # 修正：強制車頭朝上，準備進閘門
                    self.phase = "WAIT_EXIT_GATE" # 切換到等待閘門狀態

            elif self.phase == "WAIT_EXIT_GATE":
                # 等待閘門打開 (注意：這裡的 self.ui.exit_gate 應該是 self.exit_gate)
                # 假設在創建 Car 物件時傳入的 exit_gate 就是 floor 的 exit_gate
                if self.exit_gate["angle"] <= -85 or self.exit_gate["angle"] >= 85 : # 閘門向上或向下打開
                    # 閘門打開後，繼續前進
                    self.phase = "EXIT_DRIVE_AFTER"

            elif self.phase == "EXIT_DRIVE_AFTER":
                # 繼續走完剩下的路徑 (穿越閘門並離開畫面)
                if self.idx < len(self.path):
                    self._goto(self.path[self.idx])
                    self.idx += 1
                else:
                    self.kill() # 任務完成，從 sprite group 中移除

        def _goto(self, pos):
            nx, ny = pos
            cx, cy = self.rect.center
            dx, dy = nx - cx, ny - cy
            if dx == dy == 0:
                return

            if self.phase == "EXIT_PATH" and hasattr(self, "hold_vert_until") and self.idx < self.hold_vert_until:
                new_img = self.rot_up
            else:
                if abs(dx) >= abs(dy):
                    new_img = self.orig if dx > 0 else self.rot_left
                else:
                    new_img = self.rot_dn if dy > 0 else self.rot_up

            if new_img is not self.image:
                self.image = new_img
                self.rect = self.image.get_rect(center=pos)
            else:
                self.rect.center = pos

    def _handle_events(self):
            for e in pygame.event.get():
                # 1) 先处理窗口关闭事件
                if e.type == pygame.QUIT:
                    pygame.quit()
                    sys.exit()

                # 2) 再判断如果是键盘按下
                elif e.type == pygame.KEYDOWN:
                    k_name = pygame.key.name(e.key).upper() # 使用 k_name 避免與後續變數衝突

                    # —— 空白键：切换楼层 ——
                    if e.key == pygame.K_SPACE:
                        self.active_floor = 2 if self.active_floor == 1 else 1

                    # ★★★ 重構後的統一進場邏輯 (現在包含按鍵 1, 2, 3, 4, 5, 6) ★★★
                    elif e.key in [pygame.K_1, pygame.K_2, pygame.K_3, pygame.K_4, pygame.K_5, pygame.K_6]:

                        # 步驟 1: 準備數據（根據當前樓層）
                        if self.active_floor == 1:
                            floor_slots = self.slots_f1
                            entry_gates = self.entry_f1
                            exit_gate   = self.exit_f1
                            cars_group  = self.cars_f1
                        else:
                            floor_slots = self.slots_f2
                            entry_gates = self.entry_f2
                            exit_gate   = self.exit_f2
                            cars_group  = self.cars_f2

                        # 步驟 2: 根據按下的鍵或伺服器訊息，決定車輛類型
                        category_to_find = "normal" # 默認是一般車輛
                        
                        # 如果事件是由伺服器發來的，則以伺服器告知的 car_type 為準
                        if hasattr(e, 'car_type'):
                            if e.car_type == "EV":
                                category_to_find = "ev"
                            elif e.car_type == "HANDICAP":
                                category_to_find = "wc"
                        else: # 如果是使用者手動按鍵
                            if e.key in [pygame.K_3, pygame.K_5]: # 按鍵 3 和 5 都是電動車
                                category_to_find = "ev"
                            elif e.key in [pygame.K_4, pygame.K_6]: # 按鍵 4 和 6 都是殘障車
                                category_to_find = "wc"

                        # 步驟 3: 根據按下的鍵，決定使用哪個入口閘門
                        chosen_gate = entry_gates[0] # 默認使用 Gate 1
                        
                        # 按鍵 2, 5, 6 明確指定使用 Gate 2
                        if e.key in [pygame.K_2, pygame.K_5, pygame.K_6]:
                            if len(entry_gates) > 1:
                                chosen_gate = entry_gates[1]
                            else:
                                # 如果當前樓層只有一個閘門，則打印警告但仍然使用第一個閘門
                                print(f">> [UI WARN] Gate 2 requested by key '{k_name}', but only one gate available on Floor {self.active_floor}. Using Gate 1.")
                                
                        # 步驟 4: 尋找車位 (包含後備停車邏輯)
                        print(f">>> [UI INFO] Key '{k_name}' pressed. Searching for category: '{category_to_find}'...")
                        slot = self._find_empty_slot_by_category(floor_slots, category=category_to_find)
                        
                        # 如果是 EV 或 WC 且找不到專用車位，則嘗試尋找一般車位
                        if not slot and category_to_find in ("ev", "wc"):
                            print(f">> [UI INFO] Dedicated spots for '{category_to_find}' are full. Trying normal spots as fallback...")
                            slot = self._find_empty_slot_by_category(floor_slots, category="normal")
                        
                        # 步驟 5: 如果最終找到了車位，則創建車輛並開始動畫
                        if slot:
                            # 獲取車牌號碼 (如果來自伺服器) 或生成一個臨時的 (如果用戶手動按鍵)
                            plate_for_car = e.plate if hasattr(e, 'plate') else f"UI_CAR_{k_name}_{random.randint(100,999)}"
                            print(f">> [UI INFO] Found a spot for car '{plate_for_car}' at UI Slot {slot.letter}. Using Gate at {chosen_gate['rect'].center}")
                            
                            slot.state = "RESERVED"
                            skin = self.CAR_SKINS[len(cars_group) % len(self.CAR_SKINS)]
                            
                            # 創建 Car 物件
                            new_car = ParkingUI.Car(slot, skin, self, entry_gate=chosen_gate, exit_gate=exit_gate, plate_number=plate_for_car)
                            cars_group.add(new_car)
                            
                            # 開始進場動畫
                            new_car.visible = True
                            new_car.phase   = "APPROACH_GATE"
                            new_car.wait    = 0
                        else:
                            print(f">> [UI WARN] No available spots for this entry request (Category searched: {category_to_find}).")

                    # —— 按 A~J 字母：處理離場 (保持不變) —— 
                    elif k_name in "ABCDEFGHIJ": # 使用 k_name
                        slots = self.slots_f1 if self.active_floor == 1 else self.slots_f2
                        cars  = self.cars_f1  if self.active_floor == 1 else self.cars_f2
                        slot_to_exit = next((s for s in slots if s.letter == k_name), None)
                        if slot_to_exit and slot_to_exit.state == "OCCUPIED":
                            car_to_exit = next((c for c in cars if c.slot == slot_to_exit and c.phase == "STOP"), None)
                            if car_to_exit:
                                car_to_exit.start_exit()

    def _update(self):
        # ─── helper to process one floor ───
        def step_floor(slots, entry_gates, exit_gate, cars, cmd_q):
            # spawn new cars for this floor
            while cmd_q:
                cmd = cmd_q.popleft()
                slot = self._next_empty_in(slots)
                if not slot:
                    continue
                slot.state = "RESERVED"
                skin   = self.CAR_SKINS[len(cars) % len(self.CAR_SKINS)]
                # pick the gate point
                if cmd == "ENTER1":
                    eg = entry_gates[0]
                else:  # ENTER2 only exists on floor1
                    eg = entry_gates[1]
                # compute the gate’s actual center
                eg = entry_gates[0] if cmd=="ENTER1" else entry_gates[1]
                exit_dict = exit_gate
                # pass that into Car — its __init__ will offscreen it and mk_entry_path
                new_car = ParkingUI.Car(slot, skin, self, entry_gate=eg, exit_gate=exit_dict)
                new_car.visible = True
                new_car.phase   = "APPROACH_GATE"
                new_car.wait    = 0
                cars.add(new_car)

            # animate cars and gates for this floor
            cars.update()
            for g in (*entry_gates, exit_gate):
                self._gate_tick(g, cars)

        # ─── run floor 1 always ───
        step_floor(
            self.slots_f1,
            self.entry_f1,    # tuple of (entry_gate1, entry_gate2)
            self.exit_f1,
            self.cars_f1,
            self.cmd_q_f1
        )

        # ─── run floor 2 always ───
        step_floor(
            self.slots_f2,
            self.entry_f2,    # tuple of just (entry_gate2,)
            self.exit_f2,
            self.cars_f2,
            self.cmd_q_f2
        )

    def _gate_tick(self, g, cars_group):
            waiting = [] # 先初始化 waiting 列表

            # 判斷是否為入口閘門
            all_entry_gates = (*self.entry_f1, *self.entry_f2)
            if g in all_entry_gates:
                # 尋找正在等待這個特定入口閘門 g 的車輛
                waiting = [
                    c for c in cars_group
                    if c.phase == "WAIT_GATE" and c.entry_gate is g
                ]

            # ★★★ 修正點 1：正確識別第一層和第二層的出口閘門 ★★★
            elif g is self.exit_f1 or g is self.exit_f2:
                # 尋找正在等待這個特定出口閘門 g 的車輛
                waiting = [
                    c for c in cars_group
                    if c.phase == "WAIT_EXIT_GATE" and c.exit_gate is g
                ]
            
            # 後續的閘門狀態機邏輯
            if g["state"] == "CLOSE" and waiting:
                g["state"] = "OPENING"

            elif g["state"] == "OPENING":
                # ★★★ 修正點 2：所有閘門的角度都從 0 增加到 90 ★★★
                g["angle"] = min(90, g["angle"] + 4)
                if g["angle"] == 90:
                    g["state"] = "OPEN"
                    g["hold"]  = GATE_HOLD_FRAMES

            elif g["state"] == "OPEN":
                if g.get("hold", 0) > 0:
                    g["hold"] -= 1
                elif not waiting:
                    g["state"] = "CLOSING"

            elif g["state"] == "CLOSING":
                # ★★★ 修正點 3：所有閘門的角度都從 90 減少到 0 ★★★
                g["angle"] = max(0, g["angle"] - 4)
                if g["angle"] == 0:
                    g["state"] = "CLOSE"

    def _draw_arrow(self, x, y, direction="right", target_surf=None):
        surf = target_surf or self.screen
        w,h = ARROW_LEN, ARROW_WID
        base = [
            (x-w,y-h),(x+w,y-h),(x+w,y-2*h),(x+1.6*w,y),
            (x+w,y+2*h),(x+w,y+h),(x-w,y+h),
        ]
        if direction=="left":
            pts = [(2*x-px,py) for px,py in base]
        elif direction=="down":
            pts = [( x+(py-y),   y-(px-x)) for px,py in base]
        elif direction=="up":
            pts = [( x-(py-y),   y+(px-x)) for px,py in base]
        else:
            pts = base

        pygame.draw.polygon(surf, (255,255,255), pts)
        pygame.draw.polygon(surf, (200,200,200), pts, 2)


    def _draw(self):
        self.screen.blit(self.background, (0,0))

        self.screen.blit(self.pay_icon, (270, 82))

        # Draw parking slots with different designs for top/bottom
        font = pygame.font.SysFont('Arial', 28, bold=True)
        for s in self.slots:
            # Choose appropriate slot image
            slot_img = self.SLOT_IMG_TOP if s.is_top_row else self.SLOT_IMG_BOTTOM
            self.screen.blit(slot_img, s.rect.topleft)

            letter_y = s.rect.bottom - 25 if s.is_top_row else s.rect.bottom - 25            
            # Slot letter
            txt = font.render(s.letter, True, (50, 50, 50))
            txt_rect = txt.get_rect(center=(s.rect.centerx, letter_y))
            self.screen.blit(txt, txt_rect)
            status_colors = {
                "EMPTY":    (100,255,100),
                "RESERVED": (255,255,100),
                "OCCUPIED": (255,100,100)
            }
            status_color = status_colors.get(s.state, (128,128,128))
            # Status LED
            if (not s.is_top_row) and s.letter in ["F", "G", "H"]:
                # EV slot 的 LED 圓圈放到 s.rect 的右下角，y 座標往上 10px
                led_y = s.rect.bottom - 15

                pygame.draw.circle(self.screen, status_color, 
                                   (s.rect.right - 15, led_y), 8)
                pygame.draw.circle(self.screen, (0, 0, 0), 
                                   (s.rect.right - 15, led_y), 8, 2)

        # Modify part
        # （下面座標數值對應 Debug 印出的 (480,670)、(660,670)、(840,670)、(1020,670)、(1200,670)）

        # ─── FLOOR SELECTOR ───
        if self.active_floor == 1:
            slots, entry_gates, exit_gate, cars = (
                self.slots_f1, self.entry_f1, self.exit_f1, self.cars_f1
            )
        else:
            slots, entry_gates, exit_gate, cars = (
                self.slots_f2, self.entry_f2, self.exit_f2, self.cars_f2
            )

        # ─── draw only active floor’s slots ───
        font = pygame.font.SysFont('Arial', 28, bold=True)
        for s in slots:
            slot_img = self.SLOT_IMG_TOP if s.is_top_row else self.SLOT_IMG_BOTTOM
            self.screen.blit(slot_img, s.rect.topleft)

            letter_y = s.rect.bottom - 25

            status_colors = {
                "EMPTY":    (100,255,100),
                "RESERVED": (255,255,100),
                "OCCUPIED": (255,100,100)
            }
            status_color = status_colors.get(s.state, (128,128,128))
            if (not s.is_top_row) and s.letter in ["F", "G", "H"]:
                # EV slot 的 LED 圓圈放到 s.rect 的右下角，y 座標往上 10px
                led_y = s.rect.bottom - 15
                pygame.draw.circle(self.screen, status_color, 
                                   (s.rect.right - 15, led_y), 8)
                pygame.draw.circle(self.screen, (0, 0, 0), 
                                   (s.rect.right - 15, led_y), 8, 2)
        self.screen.blit(self.ev_icon, (480, 724))   # F slot 的 EV 圖示
        self.screen.blit(self.ev_icon, (660, 724))   # G slot 的 EV 圖示
        self.screen.blit(self.ev_icon, (840, 724))   # H slot 的 EV 圖示
        self.screen.blit(self.wc_icon, (1000, 670))  # I slot 的 WC 圖示
        self.screen.blit(self.wc_icon, (1180, 670))  # J slot 的 WC 圖示
        # ─── draw only active floor’s gates ───
        names = ["ENTRY1","ENTRY2"] if self.active_floor==1 else ["ENTRY"]
        for gate_name, g in zip(names + ["EXIT"], (*entry_gates, exit_gate)):
            if g["angle"] > 0:
                # rotate around pivot
                if g in entry_gates:
                    rot = pygame.transform.rotate(self.gate_img, g["angle"])
                else:
                    rot = pygame.transform.rotate(self.gate_img, -g["angle"])
                px, py = g["pivot"]
                new_rect = rot.get_rect()
                if g in entry_gates:
                    new_rect.topleft = (px, py)
                else:
                    new_rect.bottomleft = (px, py)
                self.screen.blit(rot, new_rect)
            else:
                self.screen.blit(self.gate_img, g["rect"])

            # label
            label = pygame.font.SysFont('Arial',20,bold=True).render(gate_name, True, (255,255,255))
            lx = g["rect"].centerx - label.get_width()//2
            ly = g["rect"].centery - 80
            pygame.draw.rect(self.screen, (0,0,0,128), (lx,ly,label.get_width(),label.get_height()))
            self.screen.blit(label, (lx,ly))

        # ─── draw only active floor’s cars ───
        for car in cars:
            if getattr(car, "visible", False):
                self.screen.blit(car.image, car.rect)
        info_font = pygame.font.SysFont('Arial', 20, bold=True)
        floor_text = f"Floor: {self.active_floor}"
        text_surf = info_font.render(floor_text, True, (255, 255, 255))
        # 設定區塊位置：在畫面右上，距離右邊 150px，距離上方 10px
        box_x = SCREEN_W - 1360
        box_y = 10
        box_w = 140
        box_h = 70

        # 先畫一個黑底半透明的矩形
        pygame.draw.rect(self.screen, (0, 0, 0, 200), (box_x, box_y, box_w, box_h), border_radius=5)
        # 再畫矩形的白色邊框（可選）
        pygame.draw.rect(self.screen, (255, 255, 255), (box_x, box_y, box_w, box_h), 2, border_radius=5)
        # 最後把「Floor: X」文字畫到正中央偏一點的位置
        text_rect = text_surf.get_rect(center=(box_x + box_w // 2, box_y + box_h // 2))
        self.screen.blit(text_surf, text_rect)
        # Instructions
        # inst_font = pygame.font.SysFont('Arial', 20)
        # instructions = [
        #     "1 - Add via ENTRY1 gate",
        #     "2 - Add via ENTRY2 gate",
        #     "A-J - Remove car from slot"
        # ]
        
        # for i, inst in enumerate(instructions):
        #     text = inst_font.render(inst, True, (255, 255, 255))
        #     bg_rect = pygame.Rect(SCREEN_W-300, 20 + i*30, 280, 25)
        #     pygame.draw.rect(self.screen, (0, 0, 0, 150), bg_rect, border_radius=5)
        #     self.screen.blit(text, (SCREEN_W-295, 22 + i*30))

    def _next_empty_in(self, slots):
        return next((s for s in slots if s.state=="EMPTY"), None)

    def run(self):
        while True:
            self._handle_events()
            self._update()
            self._draw()
            pygame.display.flip()
            self.clock.tick(FPS)

if __name__ == "__main__":
    ParkingUI().run()