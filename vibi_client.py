"""
ViBi.pro TTS Provider Client for VideoDubberPro
Supports ElevenLabs, MiniMax, and CapCut voices hosted on Vibi.pro.
API Endpoint: https://api.vibi.pro
"""

import os
import sys
import json
import time
import urllib.request
import urllib.error
import urllib.parse
from typing import Optional, Dict, Any, Tuple, List

BASE_URL = "https://api.vibi.pro"

class VibiClient:
    def __init__(self, api_key: Optional[str] = None, base_url: str = BASE_URL):
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key or self._load_key_from_config()

    def _load_key_from_config(self) -> str:
        config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
        if os.path.exists(config_path):
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    cfg = json.load(f)
                    return cfg.get("vibi_api_key", "").strip()
            except Exception:
                pass
        return ""

    def _get_headers(self) -> Dict[str, str]:
        headers = {
            "Content-Type": "application/json",
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) VideoDubberPro/2.0"
        }
        if self.api_key:
            headers["xi-api-key"] = self.api_key
            headers["Authorization"] = f"Bearer {self.api_key}"
        return headers

    def get_user_info(self) -> Tuple[bool, Dict[str, Any]]:
        """Get current user information and credit balance."""
        if not self.api_key:
            return False, {"error": "API Key is empty"}
        url = f"{self.base_url}/v1/auth/me"
        req = urllib.request.Request(url, headers=self._get_headers(), method="GET")
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return True, data
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="ignore")
            return False, {"status_code": e.code, "error": err_body}
        except Exception as e:
            return False, {"error": str(e)}

    def list_voices(self, provider: str = "minimax") -> Tuple[bool, List[Dict[str, Any]]]:
        """Fetch available voices for given provider (minimax, elevenlabs, capcut, default)."""
        if not self.api_key:
            return False, [{"error": "API Key is empty"}]
        
        endpoint = "/v1/default-voices"
        if provider == "minimax":
            endpoint = "/v1/minimax/system-voices"
        elif provider == "capcut":
            endpoint = "/v1/capcut/system-voices"
        elif provider == "favourite":
            endpoint = "/v1/favourite-voices"

        url = f"{self.base_url}{endpoint}"
        req = urllib.request.Request(url, headers=self._get_headers(), method="GET")
        try:
            with urllib.request.urlopen(req, timeout=20) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return True, data
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="ignore")
            return False, [{"status_code": e.code, "error": err_body}]
        except Exception as e:
            return False, [{"error": str(e)}]

    def generate_speech(
        self,
        text: str,
        voice_id: str,
        output_file: str,
        provider: str = "minimax",
        model_id: Optional[str] = None,
        language_code: Optional[str] = None,
        speed: float = 1.0,
        pitch: int = 0,
        vol: float = 1.0,
        stability: float = 0.5,
        similarity_boost: float = 0.75,
        timeout: int = 120,
        poll_interval: float = 1.5
    ) -> Tuple[bool, str]:
        """
        Synthesize text to speech using Vibi.pro API.
        Polls until task is complete and saves audio to output_file.
        """
        if not self.api_key:
            return False, "Vibi API Key is missing. Please configure 'vibi_api_key' in settings or config.json."
        if not voice_id:
            return False, "Vibi Voice ID is missing."
        if not text or not text.strip():
            return False, "Input text is empty."

        clean_text = text.strip()

        # Set default model and language based on provider
        if not model_id:
            if provider == "minimax":
                model_id = "speech-2.8-turbo"
            elif provider == "capcut":
                model_id = "capcut"
            else:
                model_id = "eleven_v3"

        if not language_code:
            if provider == "minimax":
                language_code = "Vietnamese"
            else:
                language_code = "vi"

        voice_settings = {
            "speed": max(0.5, min(2.0, speed)),
            "pitch": max(-12, min(12, int(pitch))),
            "vol": max(0.01, min(10.0, vol))
        }
        if provider == "elevenlabs":
            voice_settings["stability"] = max(0.0, min(1.0, stability))
            voice_settings["similarity_boost"] = max(0.0, min(1.0, similarity_boost))

        payload = {
            "text": clean_text,
            "provider": provider,
            "model_id": model_id,
            "language_code": language_code,
            "voice_settings": voice_settings
        }

        # Step 1: Submit TTS Task
        submit_url = f"{self.base_url}/v1/text-to-speech/{voice_id}"
        req_data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(submit_url, data=req_data, headers=self._get_headers(), method="POST")

        task_id = None
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                resp_data = json.loads(resp.read().decode("utf-8"))
                task_id = resp_data.get("id")
                if not task_id:
                    return False, f"Unexpected response from Vibi.pro: {resp_data}"
        except urllib.error.HTTPError as e:
            err_msg = e.read().decode("utf-8", errors="ignore")
            return False, f"HTTP Error {e.code} during TTS submission: {err_msg}"
        except Exception as e:
            return False, f"Failed to submit TTS task: {e}"

        # Step 2: Poll task status
        poll_url = f"{self.base_url}/v1/history/{task_id}"
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            time.sleep(poll_interval)
            try:
                poll_req = urllib.request.Request(poll_url, headers=self._get_headers(), method="GET")
                with urllib.request.urlopen(poll_req, timeout=15) as poll_resp:
                    task_info = json.loads(poll_resp.read().decode("utf-8"))
                    status = task_info.get("status", "").lower()
                    
                    if status == "completed":
                        result = task_info.get("result", {})
                        audio_url = result.get("audio_url")
                        if not audio_url:
                            return False, f"Task completed but audio_url is empty in: {task_info}"
                        
                        # Handle relative audio URLs
                        if audio_url.startswith("/"):
                            audio_url = f"{self.base_url}{audio_url}"
                        
                        # Step 3: Download audio file
                        dl_req = urllib.request.Request(audio_url, headers=self._get_headers())
                        with urllib.request.urlopen(dl_req, timeout=40) as dl_resp:
                            audio_bytes = dl_resp.read()
                            os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)
                            with open(output_file, "wb") as f_out:
                                f_out.write(audio_bytes)
                        
                        if os.path.exists(output_file) and os.path.getsize(output_file) > 0:
                            return True, output_file
                        else:
                            return False, "Downloaded audio file is empty"
                    
                    elif status == "failed":
                        err = task_info.get("error") or task_info.get("detail_error") or "Unknown error"
                        return False, f"Vibi TTS task failed: {err}"
                    
                    # status is 'pending' or 'processing' - continue polling
            except Exception as e:
                # transient network glitch during polling
                continue

        return False, f"Vibi TTS timed out after {timeout} seconds for task {task_id}"

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python vibi_client.py test <api_key>")
        print("  python vibi_client.py voices <api_key> [minimax|elevenlabs|capcut]")
        print("  python vibi_client.py tts <voice_id> <text> <output_path> [provider] [api_key]")
        return

    cmd = sys.argv[1].lower()
    if cmd in ["test", "test_json"]:
        api_key = sys.argv[2] if len(sys.argv) > 2 else ""
        client = VibiClient(api_key=api_key)
        ok, res = client.get_user_info()
        out = {"success": ok, "data": res}
        print(json.dumps(out, ensure_ascii=False))

    elif cmd in ["voices", "voices_json"]:
        api_key = sys.argv[2] if len(sys.argv) > 2 else ""
        provider = sys.argv[3] if len(sys.argv) > 3 else "minimax"
        client = VibiClient(api_key=api_key)
        ok, voices = client.list_voices(provider=provider)
        out = {"success": ok, "voices": voices}
        print(json.dumps(out, ensure_ascii=False))

    elif cmd == "tts":
        if len(sys.argv) < 5:
            print("Error: missing parameters for tts")
            return
        voice_id = sys.argv[2]
        text = sys.argv[3]
        out_path = sys.argv[4]
        provider = sys.argv[5] if len(sys.argv) > 5 else "minimax"
        api_key = sys.argv[6] if len(sys.argv) > 6 else ""
        
        client = VibiClient(api_key=api_key)
        ok, err = client.generate_speech(text, voice_id, out_path, provider=provider)
        if ok:
            print("TTS_SUCCESS:", out_path)
        else:
            print("TTS_FAILED:", err)

if __name__ == "__main__":
    main()
