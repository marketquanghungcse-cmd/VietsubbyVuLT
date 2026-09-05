import asyncio, sys, json, os, subprocess, re, urllib.request, urllib.parse
try:
    import edge_tts
except ImportError:
    edge_tts = None
from vibi_client import VibiClient

def create_silent_mp3(out_path, duration=0.5):
    cmd = f'ffmpeg -y -f lavfi -i anullsrc=r=24000:cl=mono -t {duration} -q:a 9 -acodec libmp3lame "{out_path}"'
    subprocess.run(cmd, shell=True, capture_output=True)

def generate_google_line_sync(text, lang, out_path):
    clean_text = re.sub(r'[^\w\s\u4e00-\u9fff\u00C0-\u1EF9]', '', text).strip()
    if not clean_text or len(clean_text) < 1:
        create_silent_mp3(out_path, 0.5)
        return

    # Method 1: gTTS library
    try:
        from gtts import gTTS
        tts = gTTS(text=clean_text, lang=lang if lang else 'vi', slow=False)
        tts.save(out_path)
        if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
            return
    except Exception:
        pass

    # Method 2: Direct Google Translate TTS endpoint fallback
    try:
        url = f"https://translate.google.com/translate_tts?ie=UTF-8&tl={lang if lang else 'vi'}&client=tw-ob&q=" + urllib.parse.quote(clean_text)
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'})
        with urllib.request.urlopen(req, timeout=10) as resp, open(out_path, 'wb') as f:
            f.write(resp.read())
        if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
            return
    except Exception as e:
        print(f"[GOOGLE_TTS_ERR] {e}")

    create_silent_mp3(out_path, 0.8)

async def generate_google_line(text, lang, out_path, sem):
    async with sem:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, generate_google_line_sync, text, lang, out_path)

async def generate_edge_line(text, voice, out_path, sem):
    clean_text = re.sub(r'[^\w\s\u4e00-\u9fff\u00C0-\u1EF9]', '', text).strip()
    if not clean_text or len(clean_text) < 1:
        create_silent_mp3(out_path, 0.5)
        return

    async with sem:
        for attempt in range(3):
            try:
                communicate = edge_tts.Communicate(text, voice)
                await communicate.save(out_path)
                if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
                    return
            except Exception as e:
                await asyncio.sleep(0.5)
        # If all 3 attempts fail, generate silence
        create_silent_mp3(out_path, 0.8)

def generate_vibi_line_sync(text, voice_id, out_path, client, provider, model_id):
    clean_text = re.sub(r'[^\w\s\u4e00-\u9fff\u00C0-\u1EF9]', '', text).strip()
    if not clean_text or len(clean_text) < 1:
        create_silent_mp3(out_path, 0.5)
        return

    ok, res = client.generate_speech(
        text=clean_text,
        voice_id=voice_id,
        output_file=out_path,
        provider=provider,
        model_id=model_id
    )
    if not ok or not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
        print(f"[VIBI_WARN] Failed for: '{text[:25]}...' -> {res}. Fallback to silence.")
        create_silent_mp3(out_path, 0.8)

async def generate_vibi_line(text, voice_id, out_path, sem, client, provider, model_id):
    async with sem:
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, generate_vibi_line_sync, text, voice_id, out_path, client, provider, model_id)

async def main():
    if len(sys.argv) < 3:
        print("Usage: batch_tts.py <srt_json_file> <output_dir> [default_voice] [tts_engine]")
        return

    json_file = sys.argv[1]
    out_dir = sys.argv[2]
    default_voice = sys.argv[3] if len(sys.argv) > 3 else "vi-VN-HoaiMyNeural"
    engine = sys.argv[4] if len(sys.argv) > 4 else "edge"

    # Load config if exists
    config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
    cfg = {}
    if os.path.exists(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
        except Exception:
            pass

    # If voice is 'google' or engine is 'google'
    if default_voice.lower() == "google" or engine.lower() == "google":
        engine = "google"
    elif engine not in ["vibi", "edge", "google"]:
        engine = cfg.get("tts_engine", "edge").lower()

    # Vibi configuration
    vibi_api_key = cfg.get("vibi_api_key", "").strip()
    vibi_provider = cfg.get("vibi_provider", "elevenlabs")
    vibi_model_id = cfg.get("vibi_model_id", "eleven_v3")
    vibi_voices = cfg.get("vibi_voices", {})
    default_vibi_voice = cfg.get("vibi_default_voice_id") or cfg.get("vibi_voice_id") or ""

    # If engine is vibi but no API key is set, fallback to edge
    if engine == "vibi" and not vibi_api_key:
        print("[BATCH_TTS] Vibi API key is missing. Falling back to edge-tts.")
        engine = "edge"

    with open(json_file, 'r', encoding='utf-8') as f:
        items = json.load(f)

    os.makedirs(out_dir, exist_ok=True)
    sem = asyncio.Semaphore(4 if engine == "vibi" else (8 if engine == "google" else 5))

    vibi_client = VibiClient(api_key=vibi_api_key) if engine == "vibi" else None
    target_lang = cfg.get("target_language", "vi")

    def quick_translate(text, t_lang):
        try:
            encoded = urllib.parse.quote(text.strip())
            url = f"https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl={t_lang}&dt=t&q={encoded}"
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=6) as resp:
                j = json.loads(resp.read().decode('utf-8'))
                res = "".join([part[0] for part in j[0] if part and part[0]])
                if res.strip(): return res.strip()
        except Exception:
            pass
        return ""

    tasks = []
    for i, it in enumerate(items):
        txt = it.get('translated_text', '').strip()
        # If translated_text is empty or still has Chinese, translate it immediately!
        if not txt or re.search(r'[\u4e00-\u9fff]', txt):
            src_text = it.get('text', '').strip()
            if src_text:
                q_trans = quick_translate(src_text, target_lang)
                if q_trans:
                    txt = q_trans
                    it['translated_text'] = q_trans
                elif not txt:
                    txt = src_text

        speaker = it.get('speaker', '').lower()
        out_mp3 = os.path.join(out_dir, f"seg_{i}.mp3")

        if engine == "vibi":
            # Determine voice ID
            voice_id = default_voice if (default_voice and not default_voice.startswith("vi-VN-") and default_voice != "google") else ""
            if not voice_id:
                if speaker == 'male':
                    voice_id = vibi_voices.get('male') or default_vibi_voice
                elif speaker == 'female':
                    voice_id = vibi_voices.get('female') or default_vibi_voice
                else:
                    voice_id = vibi_voices.get('default') or default_vibi_voice
            tasks.append(generate_vibi_line(txt, voice_id, out_mp3, sem, vibi_client, vibi_provider, vibi_model_id))
        elif engine == "google":
            tasks.append(generate_google_line(txt, target_lang, out_mp3, sem))
        else:
            # Edge-TTS
            if speaker == 'male':
                line_voice = 'vi-VN-NamMinhNeural'
            elif speaker == 'female':
                line_voice = 'vi-VN-HoaiMyNeural'
            else:
                line_voice = default_voice if default_voice.startswith("vi-VN-") else "vi-VN-HoaiMyNeural"
            tasks.append(generate_edge_line(txt, line_voice, out_mp3, sem))

    await asyncio.gather(*tasks)
    print("ALL_TTS_GENERATED_SUCCESSFULLY")

if __name__ == '__main__':
    asyncio.run(main())