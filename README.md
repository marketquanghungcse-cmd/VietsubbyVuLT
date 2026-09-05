# 🎬 VideoDubberPro (C++ Video Dubbing & Translation Suite)

Ứng dụng C++ GUI đa luồng chuyên nghiệp tự động hóa toàn bộ quy trình:
1. **Download video số lượng lớn** từ các nền tảng video ngắn (Douyin không watermark, TikTok, Facebook Reels, YouTube Shorts, Kuaishou, Instagram Reels) qua link đơn hoặc file `.txt` (1 dòng / 1 link).
2. **Bóc tách phụ đề tiếng Trung siêu tốc (STT)** offline bằng lõi C++ `whisper.cpp`.
3. **Dịch thuật AI thông minh (SRT)** hỗ trợ cả **Google Gemini 2.0 Flash (mặc định)** và **DeepSeek Chat/Reasoner** với cơ chế xoay vòng **Multi-API Key (Failover 429)**.
4. **Tạo giọng đọc tự nhiên (TTS)** bằng **Microsoft Edge TTS Neural (Hoài My, Nam Minh)** + **Google Translate TTS**, tự động co giãn tốc độ (`atempo 0.85x - 1.40x`) khớp chuẩn từng mili-giây với timeline câu thoại gốc.
5. **Tách nhạc nền AI (Vocal Separation)** giữ lại 100% BGM/SFX gốc và xóa sạch giọng nói tiếng Trung cũ.
6. **Tự động làm mờ phụ đề cũ (Gaussian Blur)** che sạch chữ phụ đề Trung Quốc và chèn giọng mới.
7. **Trình chỉnh sửa SRT tương tác (Pause/Resume Checkpoints):** Cho phép tạm dừng ở bất kỳ bước nào để kiểm tra, sửa câu từ/mốc thời gian trước khi render.
8. **Xuất video thành phẩm:** Tự động lưu video `.mp4` và file phụ đề `.srt` đã dịch vào thư mục `output/`.

---

## 🚀 Hướng Dẫn Sử Dụng

### Cách 1: Chạy trực tiếp
Nhấp đúp chuột vào file **`run.bat`** hoặc **`VideoDubberPro.exe`**.
Trình duyệt sẽ tự động mở giao diện Studio tại: `http://127.0.0.1:8765`.

### Cách 2: Biên dịch lại từ mã nguồn (Rebuild)
Nhấp đúp chuột vào file **`build.bat`**.

---

## 📁 Cấu Trúc Dự Án

- `VideoDubberPro.exe` : File thực thi chính của ứng dụng (C++ x64).
- `config.json` : File cấu hình hệ thống, danh sách API Keys, giọng đọc, ngôn ngữ.
- `ui/` : Giao diện đồ họa Dark Mode hiện đại (HTML5/CSS/JS).
- `src/` : Mã nguồn C++ theo kiến trúc hướng module:
  - `core/` : ConfigManager, ApiKeyPool (Multi-key failover), PipelineManager (Đa luồng).
  - `downloader/` : VideoDownloader (yt-dlp wrapper, hỗ trợ link đơn & batch .txt).
  - `stt/` : WhisperTranscriber (whisper.cpp engine), SrtParser (timestamp parser).
  - `translator/` : AiTranslator (Gemini 2.0 Flash + DeepSeek API client).
  - `tts/` : TtsEngine (Edge TTS + Google TTS + FFmpeg atempo time-stretcher).
  - `audio_separator/` : VocalSeparator (AI Demucs BGM separation & Ducking).
  - `video_processor/` : SubBlurrer (Gaussian Blur sub cũ & Muxer).
  - `ui/` : HttpGuiServer (Embedded Winsock HTTP/REST Server).
- `bin/` : Chứa bộ binary whisper.cpp x64.
- `models/` : Chứa mô hình `ggml-base.bin` nhận diện tiếng Trung.
- `output/` : Thư mục chứa video MP4 thành phẩm và file phụ đề SRT đã dịch.