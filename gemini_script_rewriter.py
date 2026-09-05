import os, sys, json, urllib.request, urllib.parse, re, time

# Force UTF-8 on Windows
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

def load_keys_from_config(proj_dir):
    cfg_path = os.path.join(proj_dir, "config.json")
    gemini_keys = []
    deepseek_keys = []
    if os.path.exists(cfg_path):
        try:
            with open(cfg_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                gemini_keys = [k.strip() for k in cfg.get("gemini_api_keys", []) if k.strip() and not k.startswith("AIzaSyTestKey")]
                deepseek_keys = [k.strip() for k in cfg.get("deepseek_api_keys", []) if k.strip()]
        except:
            pass
    return gemini_keys, deepseek_keys

def call_gemini_api(prompt, api_key, model="gemini-2.5-flash", timeout=45):
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={api_key}"
    payload = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
            "temperature": 0.3,
            "topP": 0.95,
            "responseMimeType": "application/json"
        }
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        res_json = json.loads(resp.read().decode("utf-8"))
        cand = res_json.get("candidates", [])[0]
        return cand.get("content", {}).get("parts", [])[0].get("text", "")

def call_deepseek_api(prompt, api_key, model="deepseek-chat", timeout=45):
    url = "https://api.deepseek.com/chat/completions"
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": "Bạn là chuyên gia dịch thuật và biên kịch lồng tiếng video. Luôn trả lời ở định dạng JSON array hợp lệ."},
            {"role": "user", "content": prompt}
        ],
        "temperature": 0.3,
        "response_format": {"type": "json_object"}
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}"
    })
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        res_json = json.loads(resp.read().decode("utf-8"))
        raw_text = res_json["choices"][0]["message"]["content"]
        # DeepSeek might wrap in a key or directly as json
        parsed = json.loads(raw_text)
        if isinstance(parsed, dict):
            for k in ["subtitles", "data", "result", "translations", "items"]:
                if k in parsed and isinstance(parsed[k], list):
                    return json.dumps(parsed[k])
            # If values contains a list
            for v in parsed.values():
                if isinstance(v, list):
                    return json.dumps(v)
        return raw_text

def fallback_translate_google(texts, target_lang="vi"):
    if not texts:
        return []
    results = []
    for t in texts:
        if not t.strip():
            results.append("")
            continue
        try:
            encoded = urllib.parse.quote(t.strip())
            url = f"https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl={target_lang}&dt=t&q={encoded}"
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'})
            with urllib.request.urlopen(req, timeout=8) as resp:
                j = json.loads(resp.read().decode('utf-8'))
                trans = "".join([part[0] for part in j[0] if part and part[0]])
                results.append(trans if trans.strip() else t)
        except Exception:
            results.append(t)
    return results

def build_prompt(chunk, target_lang="vi", context_items=None):
    lang_name = "Tiếng Việt" if target_lang == "vi" else ("English" if target_lang == "en" else target_lang)
    formatted = []
    for it in chunk:
        formatted.append({
            "id": it["id"],
            "start": it.get("start_time", ""),
            "end": it.get("end_time", ""),
            "text": it.get("text", "")
        })

    context_str = ""
    if context_items:
        context_str = "\nBỐI CẢNH 2-3 CÂU LIỀN TRƯỚC ĐÓ ĐÃ DỊCH (để giữ nhất quán xưng hô/ngôi xưng/tên riêng/thuật ngữ xuyên suốt):\n"
        for ctx in context_items:
            ctx_orig = ctx.get("text", "")
            ctx_trans = ctx.get("translated_text") or ctx.get("vietnamese") or ""
            context_str += f"- Gốc: \"{ctx_orig}\" ➜ Đã dịch: \"{ctx_trans}\"\n"

    return f"""Bạn là Biên tập viên & Đạo diễn Lồng tiếng chuyên nghiệp cho video ngắn (Douyin, TikTok, Vlog, Đời sống, Gia dụng, Hoạt hình, Phim ảnh).
Nhiệm vụ: Chuyển thể danh sách phụ đề gốc dưới đây thành kịch bản lời thoại {lang_name} tự nhiên, cuốn hút để diễn viên đọc lồng tiếng (TTS).
{context_str}
YÊU CẦU BẮT BUỘC:
1. LỜI THOẠI TỰ NHIÊN 100%: Dùng ngôn ngữ đời sống hiện đại, hấp dẫn, trôi chảy, không dịch thô kiểu máy móc.
2. KHỚP NHỊP THỜI LƯỢNG: Câu thoại ngắn (1-2s) viết ngắn gọn, xúc tích (3-7 từ). Câu dài viết rõ nghĩa, không dài dòng tránh nói không kịp.
3. TUYỆT ĐỐI KHÔNG CHỨA CHỮ HÁN: 100% câu dịch phải là {lang_name} hoàn chỉnh.
4. GẮN NHÃN GIỌNG ĐỌC (speaker): "female" (giọng nữ) hoặc "male" (giọng nam).
5. TRẢ VỀ ĐÚNG ĐỊNH DẠNG JSON ARRAY:
[
  {{
    "id": 1,
    "speaker": "female",
    "vietnamese": "Lời thoại lồng tiếng tự nhiên"
  }}
]

DANH SÁCH THOẠI GỐC:
{json.dumps(formatted, ensure_ascii=False, indent=2)}
"""

def contains_chinese(text):
    return bool(re.search(r'[\u4e00-\u9fff]', text))

def main():
    out_file = None
    context_file = None
    args = []
    idx_arg = 1
    while idx_arg < len(sys.argv):
        if sys.argv[idx_arg] == '--out-file' and idx_arg + 1 < len(sys.argv):
            out_file = sys.argv[idx_arg + 1]
            idx_arg += 2
        elif sys.argv[idx_arg] == '--context-file' and idx_arg + 1 < len(sys.argv):
            context_file = sys.argv[idx_arg + 1]
            idx_arg += 2
        else:
            args.append(sys.argv[idx_arg])
            idx_arg += 1

    if len(args) < 1:
        print("Usage: gemini_script_rewriter.py <input_json> [target_lang] [output_srt] [optional_key] [optional_provider] [--out-file path] [--context-file path]")
        return

    json_file = args[0]
    target_lang = args[1] if len(args) > 1 else "vi"
    out_srt = args[2] if len(args) > 2 else ""
    passed_key = args[3] if len(args) > 3 else ""
    passed_provider = args[4] if len(args) > 4 else ""

    proj_dir = os.path.dirname(os.path.abspath(json_file))
    if "temp" in proj_dir:
        proj_dir = os.path.dirname(proj_dir)

    with open(json_file, 'r', encoding='utf-8') as f:
        items = json.load(f)

    if not items:
        print("Empty items list.")
        return

    context_items = []
    if context_file and os.path.exists(context_file):
        try:
            with open(context_file, 'r', encoding='utf-8') as cf:
                context_items = json.load(cf)
        except Exception as ce:
            sys.stderr.write(f"[WARN] Failed to load context file: {ce}\n")

    gemini_keys, deepseek_keys = load_keys_from_config(proj_dir)
    provider = passed_provider.lower() if passed_provider else "gemini"

    # Select primary key
    api_key = passed_key
    if not api_key:
        if provider == "deepseek" and deepseek_keys:
            api_key = deepseek_keys[0]
        elif gemini_keys:
            api_key = gemini_keys[0]
        elif deepseek_keys:
            api_key = deepseek_keys[0]
            provider = "deepseek"

    chunk_size = 25
    success_all = True

    for i in range(0, len(items), chunk_size):
        chunk = items[i:i+chunk_size]
        curr_context = context_items if i == 0 else items[max(0, i-3):i]
        prompt = build_prompt(chunk, target_lang, curr_context)
        translated_batch = None

        # 1. Try Primary Provider (DeepSeek or Gemini)
        if api_key:
            try:
                if provider == "deepseek":
                    sys.stderr.write(f"[AI] Dịch batch {i+1}-{min(i+chunk_size, len(items))} bằng DeepSeek...\n")
                    raw = call_deepseek_api(prompt, api_key)
                else:
                    sys.stderr.write(f"[AI] Dịch batch {i+1}-{min(i+chunk_size, len(items))} bằng Gemini...\n")
                    raw = call_gemini_api(prompt, api_key)

                parsed = json.loads(raw)
                if isinstance(parsed, list):
                    translated_batch = parsed
            except Exception as e:
                sys.stderr.write(f"[WARN] Primary {provider} failed: {e}\n")

        # 2. Try Fallback Provider if primary failed
        if not translated_batch:
            if provider == "gemini" and deepseek_keys:
                try:
                    sys.stderr.write(f"[AI] Chuyển sang DeepSeek fallback...\n")
                    raw = call_deepseek_api(prompt, deepseek_keys[0])
                    parsed = json.loads(raw)
                    if isinstance(parsed, list):
                        translated_batch = parsed
                except Exception as e:
                    sys.stderr.write(f"[WARN] DeepSeek fallback failed: {e}\n")
            elif provider == "deepseek" and gemini_keys:
                try:
                    sys.stderr.write(f"[AI] Chuyển sang Gemini fallback...\n")
                    raw = call_gemini_api(prompt, gemini_keys[0])
                    parsed = json.loads(raw)
                    if isinstance(parsed, list):
                        translated_batch = parsed
                except Exception as e:
                    sys.stderr.write(f"[WARN] Gemini fallback failed: {e}\n")

        # 3. Apply translations to chunk
        if translated_batch:
            id_map = {it.get("id"): it for it in translated_batch if isinstance(it, dict) and "id" in it}
            for it in chunk:
                cid = it["id"]
                if cid in id_map:
                    t_val = id_map[cid].get("vietnamese") or id_map[cid].get("translated_text") or id_map[cid].get("translation") or ""
                    it["translated_text"] = t_val.strip()
                    it["speaker"] = id_map[cid].get("speaker", "female")

        # 4. Critical Post-translation Validation: NO CHINESE ALLOWED in translated_text
        untranslated = []
        untrans_indices = []
        for idx, it in enumerate(chunk):
            val = it.get("translated_text", "").strip()
            if not val or contains_chinese(val):
                untranslated.append(it.get("text", ""))
                untrans_indices.append(idx)

        if untranslated:
            sys.stderr.write(f"[AI] Phát hiện {len(untranslated)} câu còn tiếng Trung, dịch nhanh qua Google API...\n")
            g_trans = fallback_translate_google(untranslated, target_lang)
            for k, u_idx in enumerate(untrans_indices):
                if k < len(g_trans) and g_trans[k].strip():
                    chunk[u_idx]["translated_text"] = g_trans[k].strip()
                    chunk[u_idx]["speaker"] = "female"
                else:
                    # Final safety net: if still failing, cannot leave empty
                    chunk[u_idx]["translated_text"] = chunk[u_idx].get("text", "")

    # Save back to JSON
    target_out_json = out_file if out_file else json_file
    os.makedirs(os.path.dirname(os.path.abspath(target_out_json)), exist_ok=True)
    with open(target_out_json, 'w', encoding='utf-8') as f:
        json.dump(items, f, ensure_ascii=False, indent=2)

    if out_file and os.path.abspath(out_file) != os.path.abspath(json_file):
        try:
            with open(json_file, 'w', encoding='utf-8') as f:
                json.dump(items, f, ensure_ascii=False, indent=2)
        except:
            pass

    # Export SRT file
    if out_srt:
        os.makedirs(os.path.dirname(os.path.abspath(out_srt)), exist_ok=True)
        with open(out_srt, 'w', encoding='utf-8') as sf:
            for idx, it in enumerate(items):
                sf.write(f"{idx+1}\n")
                sf.write(f"{it.get('start_time', '00:00:00,000')} --> {it.get('end_time', '00:00:02,000')}\n")
                txt = it.get('translated_text') or it.get('text') or ''
                sf.write(f"{txt}\n\n")

    print("REWRITE_SCRIPT_SUCCESS")

if __name__ == '__main__':
    main()