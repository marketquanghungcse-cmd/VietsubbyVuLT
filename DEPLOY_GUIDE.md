# Hướng Dẫn Cài Đặt & Chạy VideoDubberPro Trên Máy Tính Mới

Tài liệu này dùng cho **Antigravity** hoặc người dùng để thiết lập toàn bộ môi trường và khởi chạy hệ thống trên máy tính mới.

---

## 🚀 Cách Cài Đặt Nhanh Nhất (Dành Cho Antigravity)

Nếu bạn đang mở thư mục này trong **Antigravity** trên máy tính mới, chỉ cần gõ yêu cầu sau cho Antigravity:

> *"Đọc file `DEPLOY_GUIDE.md`, kiểm tra và cài đặt toàn bộ môi trường (Python, thư viện pip, Playwright chromium, FFmpeg, VC++ Redist) rồi khởi động `host_web.bat` để lấy public link."*

Antigravity trên máy tính mới sẽ tự động chạy các lệnh bên dưới theo đúng thứ tự:

### 1. Cài đặt các công cụ hệ thống (nếu máy mới chưa có)
Mở PowerShell (quyền Admin nếu cần) hoặc để Antigravity chạy:
```powershell
# 1. Cài đặt Python 3.11 (nếu chưa có)
winget install Python.Python.3.11 --accept-package-agreements --accept-source-agreements

# 2. Cài đặt FFmpeg (bắt buộc để cắt/ghép âm thanh, video)
winget install Gyan.FFmpeg --accept-package-agreements --accept-source-agreements

# 3. Cài đặt Microsoft Visual C++ Redistributable (để chạy VideoDubberPro.exe)
winget install Microsoft.VCRedist.2015+.x64 --accept-package-agreements --accept-source-agreements
```

### 2. Cài đặt thư viện Python & Chromium Playwright
Tại thư mục gốc dự án:
```powershell
pip install -r requirements.txt
playwright install chromium
```

### 3. Khởi động Web Studio & Public Link Cloudflare
Chỉ cần chạy lệnh:
```powershell
python host_server.py
```
Hoặc nhấp đúp chuột vào file:
👉 **`host_web.bat`**

Hệ thống sẽ:
1. Khởi chạy máy chủ C++ backend `VideoDubberPro.exe` ở cổng `http://127.0.0.1:8765`.
2. Tự động kết nối đường truyền bảo mật **Cloudflare Tunnel**.
3. In ra màn hình và lưu vào file `active_web_urls.txt`:
   - **Local URL:** `http://127.0.0.1:8765`
   - **Public HTTPS URL:** `https://xxxxxx.trycloudflare.com`
4. Tự động mở trình duyệt lên giao diện Web Studio!

---

## 📁 Cấu Trúc Dự Án Đã Đóng Gói
* `VideoDubberPro.exe`: File thực thi chính (C++ High-Performance Server & Pipeline).
* `bin/`: Chứa các binary hỗ trợ (`cloudflared.exe`, `whisper-cli.exe`, DLLs xử lý âm thanh).
* `models/`: Chứa mô hình Whisper nhận diện giọng nói tiếng Trung (`ggml-small.bin`).
* `scripts/`: Chứa script cào link Douyin chống bot (`douyin_playwright_dl.py`), đăng nhập cookie.
* `config/`: Chứa cookie đăng nhập Douyin và cấu hình hệ thống.
* `config.json`: Cấu hình API keys, luồng xử lý, model AI.
* `ui/`: Giao diện Web Studio Dashboard hiện đại.
* `host_web.bat` & `host_server.py`: Bộ khởi chạy Localhost + Cloudflare Public HTTPS Link.
* `setup_environment.bat`: File tự động chạy cài đặt môi trường.
* `output/`: Thư mục chứa video đã render hoàn chỉnh.
* `temp/`: Thư mục tạm thời trong quá trình xử lý.

---

## 🔑 Cấu hình API Key (Nếu cần thay đổi)
Mở file `config.json` hoặc chỉnh trực tiếp trên giao diện Web Studio (Mục Cài Đặt):
* **Gemini API Key:** Dùng để dịch kịch bản AI (`gemini_api_keys`).
* **Vibi API Key:** Dùng nếu chọn giọng đọc nâng cao Vibi/ElevenLabs (`vibi_api_key`).
* **TTS Engine:** Mặc định `google` hoặc `edge` hoàn toàn miễn phí.
