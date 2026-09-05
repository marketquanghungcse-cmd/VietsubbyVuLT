import os, sys, json, math, subprocess, wave
import numpy as np

def parse_time(t_str):
    t_str = t_str.replace(',', '.')
    parts = t_str.split(':')
    h = float(parts[0])
    m = float(parts[1])
    s = float(parts[2])
    return h * 3600 + m * 60 + s

def get_duration(fpath):
    cmd = f'ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "{fpath}"'
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    try:
        return float(res.stdout.strip())
    except:
        return 0.0

def align_timeline(json_file, segs_dir, out_wav, video_path=""):
    with open(json_file, "r", encoding="utf-8") as f:
        items = json.load(f)

    if not items:
        print("Empty items")
        return

    # Determine total video duration
    if video_path and os.path.exists(video_path):
        total_dur = get_duration(video_path)
    else:
        last_end = parse_time(items[-1]['end_time'])
        total_dur = last_end + 3.0

    SAMPLE_RATE = 24000
    total_samples = int(math.ceil(total_dur * SAMPLE_RATE))
    master_buffer = np.zeros(total_samples, dtype=np.int16)

    for i, it in enumerate(items):
        start_sec = parse_time(it['start_time'])
        end_sec = parse_time(it['end_time'])
        slot_dur = max(0.5, end_sec - start_sec)

        # Check next start time to prevent clipping into next sentence
        next_start_sec = parse_time(items[i+1]['start_time']) if i + 1 < len(items) else end_sec + 2.0
        max_allowed_dur = max(slot_dur, next_start_sec - start_sec - 0.1)

        raw_mp3 = os.path.join(segs_dir, f"seg_{i}.mp3")
        if not os.path.exists(raw_mp3):
            continue

        seg_dur = get_duration(raw_mp3)
        if seg_dur <= 0.01:
            continue

        # Dynamic tempo speedup if TTS exceeds allowed duration
        norm_wav = os.path.join(segs_dir, f"seg_{i}_aligned.wav")
        speed = 1.0
        if seg_dur > max_allowed_dur:
            speed = min(1.4, seg_dur / max_allowed_dur)

        if speed > 1.03:
            cmd = f'ffmpeg -y -i "{raw_mp3}" -filter:a "atempo={speed:.2f}" -ar {SAMPLE_RATE} -ac 1 -c:a pcm_s16le "{norm_wav}"'
        else:
            cmd = f'ffmpeg -y -i "{raw_mp3}" -ar {SAMPLE_RATE} -ac 1 -c:a pcm_s16le "{norm_wav}"'

        subprocess.run(cmd, shell=True, capture_output=True)

        if not os.path.exists(norm_wav):
            continue

        try:
            with wave.open(norm_wav, 'rb') as wf:
                data = wf.readframes(wf.getnframes())
                samples = np.frombuffer(data, dtype=np.int16)

            start_sample = int(start_sec * SAMPLE_RATE)
            end_sample = min(total_samples, start_sample + len(samples))
            copy_len = end_sample - start_sample

            if copy_len > 0 and start_sample < total_samples:
                master_buffer[start_sample:start_sample + copy_len] = np.clip(
                    master_buffer[start_sample:start_sample + copy_len].astype(np.int32) + samples[:copy_len].astype(np.int32),
                    -32768, 32767
                ).astype(np.int16)
        except Exception as e:
            print(f"Error reading segment {i}: {e}")

    os.makedirs(os.path.dirname(os.path.abspath(out_wav)), exist_ok=True)
    with wave.open(out_wav, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(master_buffer.tobytes())

    print(f"TIMELINE_ALIGN_SUCCESS: {out_wav} (Duration: {get_duration(out_wav):.2f}s)")

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: timeline_aligner.py <json_file> <segs_dir> <out_wav> [video_path]")
    else:
        v_path = sys.argv[4] if len(sys.argv) > 4 else ""
        align_timeline(sys.argv[1], sys.argv[2], sys.argv[3], v_path)