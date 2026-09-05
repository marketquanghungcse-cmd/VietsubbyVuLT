"""
Douyin Video Downloader via Playwright (Dual-Engine: API Intercept + Stream Capture)
1. Intercepts /aweme/v1/web/aweme/detail/ API when unblocked.
2. Falls back to direct CDN media-stream capture + FFmpeg muxing when login/captcha blocks JSON API.
Usage: python scripts/douyin_playwright_dl.py <URL> <OUTPUT_DIR>
Output: JSON on stdout with {success, video_path, title, error}
"""
import sys, os, json, re, time, urllib.request, subprocess, concurrent.futures

# Force UTF-8 stdout on Windows
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

def extract_video_id(url):
    m = re.search(r'/video/(\d+)', url)
    if m: return m.group(1)
    m = re.search(r'modal_id=(\d+)', url)
    if m: return m.group(1)
    return None

def resolve_short_url(url):
    try:
        req = urllib.request.Request(url, headers={
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        })
        resp = urllib.request.urlopen(req, timeout=15)
        vid = extract_video_id(resp.url)
        if vid: return vid
        html = resp.read().decode('utf-8', errors='ignore')
        m = re.search(r'video/(\d+)', html)
        if m: return m.group(1)
    except:
        pass
    return None

def single_stream_download(url, out_path, headers=None, timeout=60):
    hdrs = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36',
        'Referer': 'https://www.douyin.com/',
        'Accept': '*/*'
    }
    if headers: hdrs.update(headers)
    req = urllib.request.Request(url, headers=hdrs)
    with urllib.request.urlopen(req, timeout=timeout) as resp, open(out_path, 'wb') as f:
        total = 0
        while True:
            chunk = resp.read(131072)
            if not chunk: break
            f.write(chunk)
            total += len(chunk)
    return total

def fast_chunk_download(url, out_path, num_threads=8, headers=None, timeout=60):
    hdrs = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36',
        'Referer': 'https://www.douyin.com/',
        'Accept': '*/*'
    }
    if headers: hdrs.update(headers)

    total_size = None
    try:
        probe_hdrs = dict(hdrs)
        probe_hdrs['Range'] = 'bytes=0-0'
        probe_req = urllib.request.Request(url, headers=probe_hdrs)
        with urllib.request.urlopen(probe_req, timeout=15) as probe_resp:
            cr = probe_resp.headers.get('Content-Range', '')
            if cr and '/' in cr:
                total_size = int(cr.split('/')[-1])
            elif probe_resp.headers.get('Content-Length'):
                total_size = int(probe_resp.headers.get('Content-Length'))
    except Exception as e:
        sys.stderr.write(f"[DEBUG] Range probe note: {e}\n")

    if not total_size or total_size < 1024 * 1024:
        sys.stderr.write(f"[INFO] Using direct single stream download (size: {total_size})\n")
        return single_stream_download(url, out_path, hdrs, timeout=timeout)

    sys.stderr.write(f"[INFO] Fast Multi-Connection Download: {num_threads} threads for {total_size} bytes ({total_size / (1024 * 1024):.2f} MB)\n")

    with open(out_path, 'wb') as f:
        f.seek(total_size - 1)
        f.write(b'\0')

    chunk_size = total_size // num_threads
    ranges = []
    for i in range(num_threads):
        start = i * chunk_size
        end = total_size - 1 if i == num_threads - 1 else (i + 1) * chunk_size - 1
        ranges.append((start, end))

    def download_range(r_start, r_end):
        r_hdrs = dict(hdrs)
        r_hdrs['Range'] = f"bytes={r_start}-{r_end}"
        r_req = urllib.request.Request(url, headers=r_hdrs)
        with urllib.request.urlopen(r_req, timeout=timeout) as resp:
            pos = r_start
            with open(out_path, 'r+b') as f:
                f.seek(pos)
                while True:
                    buf = resp.read(65536)
                    if not buf: break
                    f.write(buf)
                    pos += len(buf)

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(download_range, s, e) for s, e in ranges]
            for fut in concurrent.futures.as_completed(futures):
                fut.result()
        return os.path.getsize(out_path)
    except Exception as e:
        sys.stderr.write(f"[WARN] Multi-connection download failed ({e}), falling back to single stream...\n")
        return single_stream_download(url, out_path, hdrs, timeout=timeout)

class WindowsNamedMutex:
    def __init__(self, name="Local\\VideoDubber_Douyin_Lock", timeout_ms=240000):
        self.name = name
        self.timeout_ms = timeout_ms
        self.handle = None
        if sys.platform == 'win32':
            import ctypes
            self.kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        else:
            self.kernel32 = None

    def __enter__(self):
        if self.kernel32:
            sys.stderr.write(f"[INFO] Chờ lượt tải Douyin độc quyền (Mutex queue serialization)...\n")
            self.handle = self.kernel32.CreateMutexW(None, False, self.name)
            self.kernel32.WaitForSingleObject(self.handle, self.timeout_ms)
            sys.stderr.write(f"[INFO] Đã nhận quyền tải độc quyền, mở Playwright...\n")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.kernel32 and self.handle:
            time.sleep(1.2)  # Cooldown an toàn chống Douyin WAF chặn IP/Cookie
            self.kernel32.ReleaseMutex(self.handle)
            self.kernel32.CloseHandle(self.handle)
            self.handle = None

def download_with_playwright(video_id, output_dir):
    from playwright.sync_api import sync_playwright
    
    target_url = f"https://www.douyin.com/video/{video_id}"
    title = f"douyin_{video_id}"
    video_play_url = None
    video_desc = None
    
    # Stream fallback capture
    video_stream_url = None
    audio_stream_url = None
    
    with WindowsNamedMutex():
        with sync_playwright() as p:
            browser = p.chromium.launch(
                headless=True,
                args=[
                    '--autoplay-policy=no-user-gesture-required',
                    '--disable-blink-features=AutomationControlled',
                    '--no-sandbox'
                ]
            )
            context = browser.new_context(
                user_agent='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36',
                viewport={'width': 1920, 'height': 1080},
                locale='zh-CN'
            )
            context.add_init_script("""
                Object.defineProperty(navigator, 'webdriver', {
                    get: () => undefined
                });
            """)
            
            # Load user login cookies from cookie pool if available
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            import glob
            cookie_json_paths = sorted(glob.glob(os.path.join(base_dir, "config", "douyin_cookies*.json")))
            if not cookie_json_paths:
                cookie_json_paths = [os.path.join(base_dir, "temp", "douyin_cookies.json")]
            
            chosen_cookie_file = None
            for cjp in cookie_json_paths:
                if os.path.exists(cjp) and os.path.getsize(cjp) > 100:
                    chosen_cookie_file = cjp
                    break
            
            # If multiple accounts exist, rotate based on video_id
            valid_pool = [p for p in cookie_json_paths if os.path.exists(p) and os.path.getsize(p) > 100]
            if len(valid_pool) > 1:
                try:
                    chosen_cookie_file = valid_pool[abs(int(video_id)) % len(valid_pool)]
                except Exception:
                    chosen_cookie_file = valid_pool[0]

            if chosen_cookie_file and os.path.exists(chosen_cookie_file):
                try:
                    with open(chosen_cookie_file, 'r', encoding='utf-8') as f:
                        user_cookies = json.load(f)
                        context.add_cookies(user_cookies)
                        sys.stderr.write(f"[INFO] Loaded {len(user_cookies)} cookies from {os.path.basename(chosen_cookie_file)} (Pool: {len(valid_pool)} accounts)\n")
                except Exception as e:
                    sys.stderr.write(f"[WARN] Failed to load cookies from {chosen_cookie_file}: {e}\n")

            page = context.new_page()
            
            # Safe route filter: Only abort heavy font files, DO NOT abort any telemetry/security SDKs
            def filter_requests(route):
                req = route.request
                if req.resource_type in ['font']:
                    route.abort()
                else:
                    route.continue_()

            page.route('**/*', filter_requests)

            def on_response(response):
                nonlocal video_play_url, video_desc, video_stream_url, audio_stream_url
                rurl = response.url
                ct = response.headers.get('content-type', '')
                
                # Engine 1: Detail API intercept
                if ('/aweme/v1/web/aweme/detail/' in rurl or 'iteminfo' in rurl) and response.status == 200:
                    try:
                        body = response.text()
                        if 'play_addr' in body:
                            data = json.loads(body)
                            aweme = data.get('aweme_detail', {})
                            desc = aweme.get('desc', '')
                            if desc:
                                video_desc = desc
                            video_info = aweme.get('video', {})
                            play_addr = video_info.get('play_addr', {})
                            url_list = play_addr.get('url_list', [])
                            if url_list:
                                raw_url = url_list[0]
                                video_play_url = raw_url.replace('\\u0026', '&').replace('\\u002F', '/')
                                sys.stderr.write(f"[OK] Found play_addr URL via API\n")
                    except Exception as e:
                        sys.stderr.write(f"[WARN] Error parsing aweme detail: {e}\n")
                        
                # Engine 2: Direct media stream capture
                if 'video' in ct or 'media-video' in rurl or ('/video/tos/' in rurl and 'avc' in rurl):
                    if not video_stream_url and ('zjcdn.com' in rurl or 'douyinvod' in rurl):
                        video_stream_url = rurl
                        sys.stderr.write(f"[OK] Captured video stream URL\n")
                if 'audio' in ct or 'media-audio' in rurl or ('/video/tos/' in rurl and 'mp4a' in rurl):
                    if not audio_stream_url and ('zjcdn.com' in rurl or 'douyinvod' in rurl):
                        audio_stream_url = rurl
                        sys.stderr.write(f"[OK] Captured audio stream URL\n")
            
            page.on('response', on_response)
            
            # Navigate to page
            try:
                page.goto(target_url, wait_until='commit', timeout=35000)
            except Exception as e:
                sys.stderr.write(f"[WARN] Navigation note: {e}\n")
                
            if not video_play_url and not (video_stream_url and audio_stream_url):
                # Trigger realistic playback actions
                for step in range(25):
                    if video_play_url or (video_stream_url and audio_stream_url):
                        break
                    if step == 2:
                        try: page.mouse.click(960, 540)
                        except: pass
                    elif step == 4:
                        try: page.keyboard.press("Space")
                        except: pass
                    elif step == 7:
                        try: page.mouse.wheel(0, 300)
                        except: pass
                    elif step == 10:
                        try: page.keyboard.press("ArrowDown")
                        except: pass
                    page.wait_for_timeout(500)
                
            # Extract title from DOM if not found from API
            if not video_desc:
                try:
                    title_el = page.locator('h1, span[class*="title"], div[class*="title"]').first
                    if title_el:
                        txt = title_el.inner_text().strip()
                        if len(txt) > 2:
                            video_desc = txt
                except:
                    pass
            if not video_desc:
                pt = page.title()
                if pt and '抖音' in pt:
                    video_desc = pt.replace('- 抖音', '').strip()
                
            browser.close()
    
        if video_desc and len(video_desc) > 2:
            title = re.sub(r'[\\/:*?"<>|\r\n]', '_', video_desc)[:80]
            
        os.makedirs(output_dir, exist_ok=True)
        safe_title = re.sub(r'[^\w\u4e00-\u9fff\-]', '_', title)[:60]
        out_path = os.path.join(output_dir, f"{safe_title}.mp4")

        # Path A: Single direct URL from API (High-speed multi-threaded download)
        if video_play_url:
            try:
                total = fast_chunk_download(video_play_url, out_path, num_threads=8)
                if total >= 50000:
                    return {
                        "success": True,
                        "video_path": os.path.abspath(out_path).replace('\\', '/'),
                        "title": title,
                        "file_size": total
                    }
            except Exception as e:
                sys.stderr.write(f"[WARN] Direct fast download failed, falling back: {e}\n")

        # Path B: Stream capture + FFmpeg muxing
        if video_stream_url:
            sys.stderr.write(f"[INFO] High-speed parallel stream capture & merging via FFmpeg...\n")
            temp_v = os.path.join(output_dir, f"{safe_title}_temp_v.mp4")
            temp_a = os.path.join(output_dir, f"{safe_title}_temp_a.m4a") if audio_stream_url else None
            
            try:
                # Concurrently download video and audio streams via multi-threaded chunks
                def dl_v():
                    return fast_chunk_download(video_stream_url, temp_v, num_threads=8)
                def dl_a():
                    if audio_stream_url:
                        return fast_chunk_download(audio_stream_url, temp_a, num_threads=4)
                    return 0

                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
                    fv = executor.submit(dl_v)
                    fa = executor.submit(dl_a)
                    fv.result()
                    fa.result()

                cmd = ['ffmpeg', '-y', '-i', temp_v]
                if temp_a and os.path.exists(temp_a):
                    cmd += ['-i', temp_a]
                cmd += ['-c', 'copy', out_path]
                proc = subprocess.run(cmd, capture_output=True, text=True)

                # Cleanup temp files
                if os.path.exists(temp_v):
                    try: os.remove(temp_v)
                    except: pass
                if temp_a and os.path.exists(temp_a):
                    try: os.remove(temp_a)
                    except: pass

                if os.path.exists(out_path):
                    total = os.path.getsize(out_path)
                    if total >= 50000:
                        return {
                            "success": True,
                            "video_path": os.path.abspath(out_path).replace('\\', '/'),
                            "title": title,
                            "file_size": total
                        }
                sys.stderr.write(f"[ERROR] FFmpeg stream merge failed: {proc.stderr[-300:]}\n")
            except Exception as e:
                sys.stderr.write(f"[ERROR] Stream capture failed: {e}\n")
                # Fallback to direct ffmpeg network stream
                cmd = ['ffmpeg', '-y', '-headers', 'Referer: https://www.douyin.com/\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n', '-i', video_stream_url]
                if audio_stream_url:
                    cmd += ['-headers', 'Referer: https://www.douyin.com/\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n', '-i', audio_stream_url]
                cmd += ['-c', 'copy', out_path]
                subprocess.run(cmd, capture_output=True, text=True)
                if os.path.exists(out_path) and os.path.getsize(out_path) >= 50000:
                    return {
                        "success": True,
                        "video_path": os.path.abspath(out_path).replace('\\', '/'),
                        "title": title,
                        "file_size": os.path.getsize(out_path)
                    }

        return {"success": False, "error": f"Could not extract video stream for Douyin ID {video_id}"}

def main():
    if len(sys.argv) < 3:
        print(json.dumps({"success": False, "error": "Usage: douyin_playwright_dl.py <URL> <OUTPUT_DIR>"}))
        sys.exit(1)
    
    url = sys.argv[1]
    output_dir = sys.argv[2]
    
    video_id = extract_video_id(url)
    if not video_id:
        video_id = resolve_short_url(url)
    
    if not video_id:
        print(json.dumps({"success": False, "error": f"Cannot extract video ID from: {url}"}))
        sys.exit(1)
    
    sys.stderr.write(f"[INFO] Douyin Video ID: {video_id}\n")
    result = download_with_playwright(video_id, output_dir)
    print(json.dumps(result, ensure_ascii=False))
    sys.exit(0 if result.get("success") else 1)

if __name__ == "__main__":
    main()
