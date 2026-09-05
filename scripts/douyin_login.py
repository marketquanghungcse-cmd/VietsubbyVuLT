"""
Douyin Account Login Helper
Mở trình duyệt trực quan để người dùng quét mã QR đăng nhập Douyin.
Tự động xuất cookies ra 'config/douyin_cookies.json' và 'temp/douyin_cookies.txt' để hệ thống tái sử dụng.
"""
import os, sys, json, time
from playwright.sync_api import sync_playwright

if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COOKIE_JSON = os.path.join(BASE_DIR, "config", "douyin_cookies.json")
COOKIE_TXT = os.path.join(BASE_DIR, "temp", "douyin_cookies.txt")
os.makedirs(os.path.join(BASE_DIR, "config"), exist_ok=True)
os.makedirs(os.path.join(BASE_DIR, "temp"), exist_ok=True)

def export_netscape_cookies(cookies, txt_path):
    with open(txt_path, 'w', encoding='utf-8') as f:
        f.write("# Netscape HTTP Cookie File\n")
        for c in cookies:
            domain = c.get('domain', '')
            flag = "TRUE" if domain.startswith('.') else "FALSE"
            path = c.get('path', '/')
            secure = "TRUE" if c.get('secure') else "FALSE"
            expiry = str(int(c.get('expires', time.time() + 365*86400)))
            name = c.get('name', '')
            val = c.get('value', '')
            f.write(f"{domain}\t{flag}\t{path}\t{secure}\t{expiry}\t{name}\t{val}\n")

def login():
    print("="*65)
    print("        DOUYIN LOGIN HELPER — DANG NHAP TAI KHOAN DOUYIN")
    print("="*65)
    print("1. Đang mở trình duyệt Chrome...")
    print("2. Vui lòng mở app Douyin trên điện thoại để QUÉT MÃ QR đăng nhập,")
    print("   hoặc đăng nhập bằng số điện thoại / mật khẩu trên cửa sổ vừa mở.")
    print("3. Hệ thống sẽ tự động phát hiện khi bạn đăng nhập thành công!")
    print("="*65)
    
    with sync_playwright() as p:
        browser = p.chromium.launch(
            headless=False,
            args=[
                '--disable-blink-features=AutomationControlled',
                '--start-maximized'
            ]
        )
        context = browser.new_context(
            user_agent='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36',
            no_viewport=True,
            locale='zh-CN'
        )
        page = context.new_page()
        page.goto("https://www.douyin.com", wait_until='domcontentloaded')
        
        # Poll up to 5 minutes (300s)
        logged_in = False
        start_time = time.time()
        while time.time() - start_time < 300:
            try:
                cookies = context.cookies()
                c_names = {c['name'] for c in cookies}
                # Check for Douyin session indicators
                if 'sessionid' in c_names or 'sessionid_ss' in c_names or 'passport_auth_status' in c_names:
                    logged_in = True
                    break
            except Exception:
                pass
            time.sleep(1)
            
        if logged_in:
            print("\n[OK] ĐĂNG NHẬP THÀNH CÔNG! Đang trích xuất và lưu trữ cookies...")
            all_cookies = context.cookies()
            with open(COOKIE_JSON, 'w', encoding='utf-8') as f:
                json.dump(all_cookies, f, indent=2, ensure_ascii=False)
            export_netscape_cookies(all_cookies, COOKIE_TXT)
            export_netscape_cookies(all_cookies, os.path.join(BASE_DIR, "temp", "douyin_cookies.txt"))
            print(f"[OK] Đã lưu cookie JSON tại: {COOKIE_JSON}")
            print(f"[OK] Đã lưu cookie TXT tại: {COOKIE_TXT}")
            print("[INFO] Từ giờ hệ thống VideoDubberPro sẽ tự động sử dụng tài khoản của bạn để tải video chất lượng cao nhất!")
            print("Cửa sổ sẽ tự động đóng sau 3 giây.")
            time.sleep(3)
        else:
            print("\n[HẾT GIỜ] Không phát hiện phiên đăng nhập sau 5 phút. Vui lòng thử lại khi cần.")
            
        browser.close()

if __name__ == "__main__":
    login()
