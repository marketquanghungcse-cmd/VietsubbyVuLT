# HƯỚNG DẪN CÀI ĐẶT & CHẠY VIDEODUBBERPRO TRÊN MÁY MỚI

Gói nén này là bản **Portable Full-Package** đã tích hợp sẵn:
- File thực thi lõi C++ tốc độ cao `VideoDubberPro.exe`
- Bộ công cụ `bin/` (`ffmpeg.exe`, `ffprobe.exe`, `cloudflared.exe`, `whisper.exe`, `yt-dlp.exe`)
- Mô hình AI Whisper gốc trong `models/`
- Trình điều khiển tải đa tài khoản Douyin, bóc tách sub, dịch thuật và che mờ phụ đề
- Giao diện Web Studio hiện đại trong `ui/`

---

## 🚀 3 BƯỚC KHỞI CHẠY TRÊN MÁY MỚI

### Bước 1: Giải nén
- Giải nén file zip vào bất kỳ thư mục nào trên ổ đĩa (Ví dụ: `D:\VideoDubberPro` hoặc `C:\VideoDubberPro`).
- *Lưu ý: Không đặt trong thư mục có dấu tiếng Việt đặc biệt.*

### Bước 2: Chạy cài đặt tự động (Chỉ làm 1 lần)
- Nhấp đúp mở file: **`1_CAI_DAT_MAY_MOI.bat`**
- Script sẽ tự động:
  1. Kiểm tra Python (tự cài nếu chưa có).
  2. Cài đặt các thư viện Python bổ trợ.
  3. Cài đặt Chromium cho Playwright để cào Douyin chống chặn.
  4. Mở port tường lửa 8765 cho máy chủ Web Studio.

### Bước 3: Khởi động sử dụng
- Nhấp đúp mở file: **`2_CHAY_WEB_STUDIO.bat`**
- Trình duyệt sẽ tự động bật lên địa chỉ `http://127.0.0.1:8765`.
- Bạn có thể bắt đầu dán link video Douyin, TikTok, Facebook, YouTube để dịch và lồng tiếng tự động!

---

## 🌐 NẾU MUỐN TRUY CẬP TỪ XA HOẶC ĐIỆN THOẠI (PUBLIC LINK)
- Nhấp đúp mở file: **`3_TAO_PUBLIC_LINK_CLOUDFLARE.bat`**
- Sau 3-5 giây, màn hình sẽ hiển thị 1 đường link dạng:
  `https://xxxx-xxxx-xxxx.trycloudflare.com`
- Bạn có thể mở link này trên điện thoại hoặc bất kỳ máy tính nào ở bất cứ đâu để điều khiển và tải video!

---

## 🔑 ĐĂNG NHẬP DOUYIN (COOKIE POOL ĐA TÀI KHOẢN)
- Mở file: **`dang_nhap_douyin.bat`** hoặc vào mục **`⚙️ Cài Đặt & Điểm Dừng`** trên Web Studio.
- Bạn có thể kết nối 2-3 tài khoản để hệ thống tự động xoay tua, chống bị Douyin chặn Rate-limit.
