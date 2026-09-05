#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys, os, json, re

def parse_srt(srt_file):
    if not os.path.exists(srt_file):
        return []
    with open(srt_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    blocks = re.split(r'\n\s*\n', content.strip())
    items = []
    for b in blocks:
        lines = [l.strip() for l in b.split('\n') if l.strip()]
        if len(lines) >= 2:
            m = re.search(r'(\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3})', lines[1] if len(lines) > 1 and '-->' in lines[1] else lines[0])
            if m:
                s_ms = int(m.group(1))*3600000 + int(m.group(2))*60000 + int(m.group(3))*1000 + int(m.group(4))
                e_ms = int(m.group(5))*3600000 + int(m.group(6))*60000 + int(m.group(7))*1000 + int(m.group(8))
                text = ' '.join(lines[2:]) if len(lines) > 2 else ''
                items.append({
                    'start': s_ms / 1000.0,
                    'end': e_ms / 1000.0,
                    'text': text
                })
    return items

def main():
    if len(sys.argv) < 4:
        print('Usage: python detect_sub_boxes.py <video_path> <srt_path> <out_json>')
        sys.exit(1)

    video_path = sys.argv[1]
    srt_path = sys.argv[2]
    out_json = sys.argv[3]

    if not os.path.exists(video_path):
        print(f'[OCR] Video file not found: {video_path}')
        sys.exit(1)

    srt_items = parse_srt(srt_path)
    if not srt_items:
        print('[OCR] No SRT items to scan. Exiting.')
        sys.exit(0)

    try:
        import cv2
        import numpy as np
        from rapidocr_onnxruntime import RapidOCR
        ocr = RapidOCR()
    except Exception as e:
        print(f'[OCR] Error importing RapidOCR or cv2: {e}')
        ocr = None

    cap = cv2.VideoCapture(video_path) if ocr else None
    if cap and not cap.isOpened():
        cap = None

    w, h, fps = 1080, 1920, 30.0
    if cap:
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)) or 1080
        h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)) or 1920
        fps = cap.get(cv2.CAP_PROP_FPS) or 30.0

    is_portrait = (h > w)
    if is_portrait:
        def_w = int(round(0.86 * w))
        def_x = (w - def_w) // 2
        def_y = int(round(0.720 * h))
        def_h = int(round(0.080 * h))
        scan_y1 = int(round(0.60 * h))
        scan_y2 = int(round(0.92 * h))
    else:
        def_w = int(round(0.75 * w))
        def_x = (w - def_w) // 2
        def_y = int(round(0.885 * h))
        def_h = int(round(0.088 * h))
        scan_y1 = int(round(0.75 * h))
        scan_y2 = int(round(0.98 * h))

    if def_w % 2 != 0: def_w -= 1
    if def_h % 2 != 0: def_h -= 1

    boxes = []

    for item in srt_items:
        st = max(0.0, item['start'] - 0.05)
        en = item['end'] + 0.05
        mid_sec = (st + en) / 2.0

        detected_box = None

        if cap and ocr:
            sample_times = [mid_sec]
            if en - st > 0.8:
                sample_times = [st + 0.25 * (en - st), mid_sec, st + 0.75 * (en - st)]

            candidates = []
            for t in sample_times:
                f_idx = int(round(t * fps))
                cap.set(cv2.CAP_PROP_POS_FRAMES, f_idx)
                ret, frame = cap.read()
                if not ret or frame is None:
                    continue

                crop = frame[scan_y1:scan_y2, 0:w]
                gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
                if np.sum(gray > 200) < 50:
                    continue

                try:
                    res, _ = ocr(crop)
                    if res:
                        for line in res:
                            box = line[0]
                            text = line[1].strip()
                            score = line[2]
                            if score > 0.50 and len(text) >= 1:
                                xs = [p[0] for p in box]
                                ys = [p[1] for p in box]
                                bx1 = int(min(xs))
                                bx2 = int(max(xs))
                                by1 = int(min(ys)) + scan_y1
                                by2 = int(max(ys)) + scan_y1
                                candidates.append((bx1, by1, bx2 - bx1, by2 - by1))
                except Exception:
                    pass

            if candidates:
                min_x = min(c[0] for c in candidates)
                min_y = min(c[1] for c in candidates)
                max_x = max(c[0] + c[2] for c in candidates)
                max_y = max(c[1] + c[3] for c in candidates)

                pad_x = 24
                pad_y = 12
                bx = max(0, min_x - pad_x)
                by = max(0, min_y - pad_y)
                bw = min(w - bx, (max_x - min_x) + 2 * pad_x)
                bh = min(h - by, (max_y - min_y) + 2 * pad_y)

                if bw % 2 != 0: bw -= 1
                if bh % 2 != 0: bh -= 1

                detected_box = {
                    'start': round(st, 2),
                    'end': round(en, 2),
                    'x': bx,
                    'y': by,
                    'w': bw,
                    'h': bh
                }

        if not detected_box:
            detected_box = {
                'start': round(st, 2),
                'end': round(en, 2),
                'x': def_x,
                'y': def_y,
                'w': def_w,
                'h': def_h
            }

        boxes.append(detected_box)

    if cap:
        cap.release()

    os.makedirs(os.path.dirname(os.path.abspath(out_json)), exist_ok=True)
    with open(out_json, 'w', encoding='utf-8') as f:
        json.dump(boxes, f, indent=2, ensure_ascii=False)

    print(f'[OCR] Done. Detected {len(boxes)} dynamic delogo subtitle boxes -> {out_json}')

if __name__ == '__main__':
    main()