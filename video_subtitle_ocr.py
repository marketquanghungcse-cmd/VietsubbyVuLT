import cv2, os, sys, json, time
import numpy as np
from rapidocr_onnxruntime import RapidOCR

def format_time(sec):
    h = int(sec // 3600)
    m = int((sec % 3600) // 60)
    s = int(sec % 60)
    ms = int((sec - int(sec)) * 1000)
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"

def extract_video_subtitles(video_path, output_json, output_srt):
    print(f"Starting Video Subtitle OCR on: {video_path}")
    t0 = time.time()
    
    ocr = RapidOCR()
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print("Error: Could not open video file.")
        return []

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    duration = total_frames / fps
    print(f"Video duration: {duration:.2f}s, FPS: {fps}, Total frames: {total_frames}")

    # Sample at 3 fps (every 10 frames if 30fps)
    sample_step = max(1, int(round(fps / 3.0)))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))

    # Subtitle region: bottom 22% (between 78% and 94% height, avoiding TikTok watermark at bottom)
    crop_y1 = int(h * 0.76)
    crop_y2 = int(h * 0.94)

    raw_detections = []
    frame_idx = 0

    print("Scanning video frames...")
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break

        if frame_idx % sample_step == 0:
            current_sec = frame_idx / fps
            crop = frame[crop_y1:crop_y2, :]

            # Run OCR on cropped subtitle area
            res, _ = ocr(crop)
            text_found = ""
            if res:
                # Filter high confidence detections
                texts = [line[1].strip() for line in res if line[2] > 0.65]
                # Filter out channel watermark/advertisements if any
                valid_texts = [t for t in texts if len(t) >= 2 and not any(k in t for k in ["抖音", "星河浅夏", "无不良引导", "AI生成"])]
                if valid_texts:
                    text_found = " ".join(valid_texts)

            if text_found:
                raw_detections.append({
                    "time": current_sec,
                    "text": text_found
                })

        frame_idx += 1

    cap.release()
    print(f"Scan complete: found {len(raw_detections)} subtitle detections in {time.time() - t0:.1f}s")

    # Group continuous detections into subtitle segments
    segments = []
    if raw_detections:
        cur_text = raw_detections[0]["text"]
        start_t = raw_detections[0]["time"]
        end_t = start_t + 0.33

        for det in raw_detections[1:]:
            t = det["time"]
            txt = det["text"]

            # Check similarity with current segment
            if (txt == cur_text or (len(txt) > 3 and txt in cur_text) or (len(cur_text) > 3 and cur_text in txt)) and (t - end_t <= 0.8):
                end_t = t + 0.33
                if len(txt) > len(cur_text):
                    cur_text = txt
            else:
                # Finalize previous segment
                if end_t - start_t >= 0.4:
                    segments.append({
                        "id": len(segments) + 1,
                        "start_sec": round(start_t, 3),
                        "end_sec": round(end_t, 3),
                        "start_time": format_time(start_t),
                        "end_time": format_time(end_t),
                        "text": cur_text
                    })
                cur_text = txt
                start_t = t
                end_t = t + 0.33

        if end_t - start_t >= 0.4:
            segments.append({
                "id": len(segments) + 1,
                "start_sec": round(start_t, 3),
                "end_sec": round(end_t, 3),
                "start_time": format_time(start_t),
                "end_time": format_time(end_t),
                "text": cur_text
            })

    print(f"Grouped into {len(segments)} exact dialogue subtitle segments.")

    with open(output_json, "w", encoding="utf-8") as f:
        json.dump(segments, f, ensure_ascii=False, indent=2)

    with open(output_srt, "w", encoding="utf-8") as f:
        for seg in segments:
            f.write(f"{seg['id']}\n")
            f.write(f"{seg['start_time']} --> {seg['end_time']}\n")
            f.write(f"{seg['text']}\n\n")

    print(f"Saved OCR Subtitles to: {output_srt}")
    return segments

if __name__ == "__main__":
    vid = r"C:\Users\demie\.gemini\antigravity\scratch\VideoDubberPro\temp\vid_test.mp4"
    out_j = r"C:\Users\demie\.gemini\antigravity\scratch\VideoDubberPro\temp\ocr_subtitles.json"
    out_s = r"C:\Users\demie\.gemini\antigravity\scratch\VideoDubberPro\temp\ocr_subtitles.srt"
    extract_video_subtitles(vid, out_j, out_s)