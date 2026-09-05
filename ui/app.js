
let currentTab = 'queue';
let currentFilter = 'ALL';
let inspectorTaskId = null;
let currentInspectorTask = null;
let currentInspStep = 'tab-insp-source';
let inspSourceSrt = [];
let inspTransSrt = [];
let allTasks = [];

function toggleCustomNameInput() {
    const select = document.getElementById('task-naming-pattern');
    const wrap = document.getElementById('wrap-custom-name');
    if (select && wrap) {
        wrap.style.display = (select.value === 'custom_seq') ? 'block' : 'none';
    }
}
window.toggleCustomNameInput = toggleCustomNameInput;

document.addEventListener('DOMContentLoaded', () => {
    loadConfig();
    loadApiKeys();
    fetchTasks();
    fetchLogs();
    checkDouyinStatus();

    setInterval(fetchTasks, 2000);
    setInterval(fetchLogs, 3000);
    setInterval(checkDouyinStatus, 10000);

    const singleInput = document.getElementById('single-url');
    if (singleInput) {
        const handleAutoExtract = () => {
            const raw = singleInput.value;
            if (!raw) return;
            const cleaned = extractCleanUrl(raw);
            if (cleaned && cleaned !== raw && cleaned.startsWith('http')) {
                singleInput.value = cleaned;
                const hint = document.getElementById('url-extract-hint');
                const hintVal = document.getElementById('url-extract-val');
                if (hint && hintVal) {
                    hintVal.textContent = cleaned.length > 55 ? cleaned.substring(0, 52) + '...' : cleaned;
                    hint.style.display = 'block';
                    setTimeout(() => { if (hint) hint.style.display = 'none'; }, 4500);
                }
            }
        };
        singleInput.addEventListener('paste', () => setTimeout(handleAutoExtract, 10));
        singleInput.addEventListener('input', handleAutoExtract);
    }
});

// 1. SIDEBAR NAVIGATION
function switchTab(tabId) {
    currentTab = tabId;
    document.querySelectorAll('.tab-section').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));

    const targetSection = document.getElementById('sec-' + tabId);
    if (targetSection) targetSection.classList.add('active');

    const btns = document.querySelectorAll('.nav-btn');
    if (tabId === 'queue') {
        btns[0].classList.add('active');
        setHeaderTitle('⚡ Bảng Điều Khiển & Hàng Đợi Video', 'Tự động tải video, bóc sub tiếng Trung, dịch Gemini/DeepSeek, lồng voice & che sub cũ');
    } else if (tabId === 'keys') {
        btns[1].classList.add('active');
        setHeaderTitle('🔑 Quản Lý API Key Pool (Multi-Key Failover)', 'Lưu vĩnh viễn vào hệ thống (chỉ cần nhập 1 lần), tự động xoay vòng khi gặp hạn mức 429');
        loadApiKeys();
    } else if (tabId === 'settings') {
        btns[2].classList.add('active');
        setHeaderTitle('⚙️ Cài Đặt Hệ Thống & Điểm Dừng Can Thiệp', 'Cấu hình giọng đọc TTS, chế độ BGM, bộ lọc Gaussian Blur và các điểm dừng kiểm tra');
        loadConfig();
    } else if (tabId === 'logs') {
        btns[3].classList.add('active');
        setHeaderTitle('📜 Nhật Ký Hoạt Động Thời Gian Thực', 'Theo dõi chi tiết từng tiến trình C++, ffmpeg, whisper.cpp, AI Translation');
        fetchLogs();
    }
}
window.switchTab = switchTab;

function setHeaderTitle(title, sub) {
    const t = document.getElementById('top-title');
    const s = document.getElementById('top-sub');
    if (t) t.textContent = title;
    if (s) s.textContent = sub;
}

// 2. CONFIG & PRESETS
async function loadConfig() {
    try {
        const res = await fetch('/api/config');
        const cfg = await res.json();

        // Quick settings in queue tab
        if (document.getElementById('quick-target-lang')) document.getElementById('quick-target-lang').value = cfg.target_language || 'vi';
        if (document.getElementById('quick-tts-voice')) document.getElementById('quick-tts-voice').value = cfg.tts_voice || 'vi-VN-HoaiMyNeural';
        if (document.getElementById('quick-model')) document.getElementById('quick-model').value = cfg.default_model || 'gemini';
        if (document.getElementById('quick-bgm-mode')) document.getElementById('quick-bgm-mode').value = cfg.bgm_mode || 'keep_bgm';
        if (document.getElementById('quick-blur-sub-mode')) document.getElementById('quick-blur-sub-mode').value = cfg.blur_sub_mode || 'auto_ocr';

        // Settings tab
        if (document.getElementById('cfg-model')) document.getElementById('cfg-model').value = cfg.default_model || 'gemini';
        if (document.getElementById('cfg-target-lang')) document.getElementById('cfg-target-lang').value = cfg.target_language || 'vi';
        if (document.getElementById('cfg-voice')) document.getElementById('cfg-voice').value = cfg.tts_voice || 'vi-VN-HoaiMyNeural';
        if (document.getElementById('cfg-bgm')) document.getElementById('cfg-bgm').value = cfg.bgm_mode || 'keep_bgm';
        if (document.getElementById('cfg-blur-sub-mode')) document.getElementById('cfg-blur-sub-mode').value = cfg.blur_sub_mode || 'auto_ocr';

        // ViBi Settings
        if (document.getElementById('cfg-vibi-key')) document.getElementById('cfg-vibi-key').value = cfg.vibi_api_key || '';
        if (document.getElementById('cfg-vibi-provider')) document.getElementById('cfg-vibi-provider').value = cfg.vibi_provider || 'minimax';
        if (document.getElementById('cfg-vibi-model')) document.getElementById('cfg-vibi-model').value = cfg.vibi_model_id || 'speech-2.8-turbo';
        if (document.getElementById('cfg-vibi-voice-id')) document.getElementById('cfg-vibi-voice-id').value = cfg.vibi_default_voice_id || '';

        if (document.getElementById('cfg-pause-dl')) document.getElementById('cfg-pause-dl').checked = !!cfg.pause_after_download;
        if (document.getElementById('cfg-pause-stt')) document.getElementById('cfg-pause-stt').checked = !!cfg.pause_after_stt;
        if (document.getElementById('cfg-pause-trans')) document.getElementById('cfg-pause-trans').checked = !!cfg.pause_after_translate;

        if (document.getElementById('cfg-blur-kernel')) document.getElementById('cfg-blur-kernel').value = cfg.blur_kernel_size || 25;

        // Output Naming & Directory Settings
        const pattern = cfg.output_naming_pattern || 'seq_title';
        const prefix = cfg.custom_output_prefix || '';
        const outDir = cfg.output_dir || 'outputs';

        if (document.getElementById('task-naming-pattern')) document.getElementById('task-naming-pattern').value = pattern;
        if (document.getElementById('task-custom-name')) document.getElementById('task-custom-name').value = prefix;
        if (document.getElementById('cfg-output-naming')) document.getElementById('cfg-output-naming').value = pattern;
        if (document.getElementById('cfg-custom-prefix')) document.getElementById('cfg-custom-prefix').value = prefix;
        if (document.getElementById('cfg-output-dir')) document.getElementById('cfg-output-dir').value = outDir;
        toggleCustomNameInput();

        // Concurrency Settings
        const maxConcurrent = cfg.max_concurrent_tasks || 2;
        if (document.getElementById('quick-concurrency')) document.getElementById('quick-concurrency').value = maxConcurrent;
        if (document.getElementById('cfg-max-concurrent')) document.getElementById('cfg-max-concurrent').value = maxConcurrent;

        updateHeaderBadges(cfg.default_model, cfg.target_language);
    } catch (e) {}
}

async function changeConcurrency(val) {
    const max_concurrent_tasks = parseInt(val) || 2;
    if (document.getElementById('quick-concurrency')) document.getElementById('quick-concurrency').value = max_concurrent_tasks;
    if (document.getElementById('cfg-max-concurrent')) document.getElementById('cfg-max-concurrent').value = max_concurrent_tasks;

    await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ max_concurrent_tasks })
    });
    fetchTasks();
}
window.changeConcurrency = changeConcurrency;

function updateHeaderBadges(model, lang) {
    const m = document.getElementById('hdr-engine');
    const l = document.getElementById('hdr-lang');
    if (m) m.textContent = (model === 'deepseek') ? '🐋 DeepSeek Chat' : '✨ Gemini 2.0 Flash';
    let langTxt = 'Tiếng Việt';
    if (lang === 'en') langTxt = 'English';
    if (lang === 'zh') langTxt = 'Tiếng Trung';
    if (l) l.textContent = langTxt;
}

async function updateQuickPreset() {
    const target_language = document.getElementById('quick-target-lang').value;
    const tts_voice = document.getElementById('quick-tts-voice').value;
    const default_model = document.getElementById('quick-model').value;
    const bgm_mode = document.getElementById('quick-bgm-mode').value;
    const blur_sub_mode = document.getElementById('quick-blur-sub-mode') ? document.getElementById('quick-blur-sub-mode').value : 'auto_ocr';

    let tts_engine = 'edge';
    if (tts_voice === 'google') tts_engine = 'google';
    else if (tts_voice === 'vibi' || (!tts_voice.startsWith('vi-VN-') && !tts_voice.startsWith('en-US-') && !tts_voice.startsWith('zh-CN-'))) tts_engine = 'vibi';

    updateHeaderBadges(default_model, target_language);

    await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ target_language, tts_voice, tts_engine, default_model, bgm_mode, blur_sub_mode })
    });
}
window.updateQuickPreset = updateQuickPreset;

async function saveFullSettings() {
    const default_model = document.getElementById('cfg-model').value;
    const target_language = document.getElementById('cfg-target-lang').value;
    const tts_voice = document.getElementById('cfg-voice').value;
    const bgm_mode = document.getElementById('cfg-bgm').value;
    const blur_sub_mode = document.getElementById('cfg-blur-sub-mode') ? document.getElementById('cfg-blur-sub-mode').value : 'auto_ocr';

    let tts_engine = 'edge';
    if (tts_voice === 'google') tts_engine = 'google';
    else if (tts_voice === 'vibi' || (!tts_voice.startsWith('vi-VN-') && !tts_voice.startsWith('en-US-') && !tts_voice.startsWith('zh-CN-'))) tts_engine = 'vibi';

    const pause_after_download = document.getElementById('cfg-pause-dl').checked;
    const pause_after_stt = document.getElementById('cfg-pause-stt').checked;
    const pause_after_translate = document.getElementById('cfg-pause-trans').checked;
    const blur_kernel_size = parseInt(document.getElementById('cfg-blur-kernel').value) || 25;

    const max_concurrent_tasks = parseInt(document.getElementById('cfg-max-concurrent') ? document.getElementById('cfg-max-concurrent').value : '2') || 2;
    if (document.getElementById('quick-concurrency')) document.getElementById('quick-concurrency').value = max_concurrent_tasks;

    const output_dir = document.getElementById('cfg-output-dir') ? document.getElementById('cfg-output-dir').value.trim() : 'outputs';
    const output_naming_pattern = document.getElementById('cfg-output-naming') ? document.getElementById('cfg-output-naming').value : 'seq_title';
    const custom_output_prefix = document.getElementById('cfg-custom-prefix') ? document.getElementById('cfg-custom-prefix').value.trim() : '';

    // sync Tab 1 if present
    if (document.getElementById('task-naming-pattern')) document.getElementById('task-naming-pattern').value = output_naming_pattern;
    if (document.getElementById('task-custom-name')) document.getElementById('task-custom-name').value = custom_output_prefix;
    toggleCustomNameInput();

    updateHeaderBadges(default_model, target_language);

    await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            default_model, target_language, tts_voice, tts_engine, bgm_mode, blur_sub_mode,
            pause_after_download, pause_after_stt, pause_after_translate,
            blur_kernel_size, max_concurrent_tasks, output_dir, output_naming_pattern, custom_output_prefix
        })
    });
    alert('Đã lưu cấu hình cài đặt chung thành công!');
}
window.saveFullSettings = saveFullSettings;

// ViBi Functions
function toggleVibiKeyVisibility() {
    const input = document.getElementById('cfg-vibi-key');
    if (input) {
        input.type = input.type === 'password' ? 'text' : 'password';
    }
}
window.toggleVibiKeyVisibility = toggleVibiKeyVisibility;

function onVibiProviderChange() {
    const prov = document.getElementById('cfg-vibi-provider').value;
    const modelInput = document.getElementById('cfg-vibi-model');
    if (modelInput) {
        if (prov === 'minimax') modelInput.value = 'speech-2.8-turbo';
        else if (prov === 'elevenlabs') modelInput.value = 'eleven_multilingual_v2';
        else if (prov === 'capcut') modelInput.value = 'capcut';
    }
}
window.onVibiProviderChange = onVibiProviderChange;

async function testVibiConnection() {
    const key = document.getElementById('cfg-vibi-key').value.trim();
    const badge = document.getElementById('vibi-status-badge');
    badge.innerHTML = '<span style="color:#f59e0b;">⏳ Đang kết nối tới ViBi.pro...</span>';
    try {
        const res = await fetch('/api/vibi/test', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ api_key: key })
        });
        const data = await res.json();
        if (data.success) {
            const user = data.data || {};
            const credits = (user.credits !== undefined) ? Number(user.credits).toLocaleString() : ((user.balance !== undefined) ? Number(user.balance).toLocaleString() : 'Khả dụng');
            const email = user.email || user.username || 'Thành viên ViBi';
            badge.innerHTML = `<span style="color:#10b981;">✅ Kết nối thành công! Tài khoản: <b>${email}</b> | Số dư: <b>${credits} credits</b></span>`;
        } else {
            const err = data.error || (data.data && data.data.error) || 'Xác thực thất bại';
            badge.innerHTML = `<span style="color:#ef4444;">❌ Lỗi: ${typeof err === 'object' ? JSON.stringify(err) : err}</span>`;
        }
    } catch (e) {
        badge.innerHTML = `<span style="color:#ef4444;">❌ Lỗi: ${e.message}</span>`;
    }
}
window.testVibiConnection = testVibiConnection;

async function loadVibiVoices() {
    const key = document.getElementById('cfg-vibi-key').value.trim();
    const prov = document.getElementById('cfg-vibi-provider').value;
    const container = document.getElementById('vibi-voices-container');
    const list = document.getElementById('vibi-voices-list');
    list.innerHTML = '<em>Đang tải danh sách giọng đọc từ ViBi.pro...</em>';
    container.style.display = 'block';

    try {
        const res = await fetch('/api/vibi/voices', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ api_key: key, provider: prov })
        });
        const data = await res.json();
        const voices = data.voices || (Array.isArray(data) ? data : []);
        if (data.success && Array.isArray(voices) && voices.length > 0) {
            list.innerHTML = '';
            voices.forEach(v => {
                const vId = v.voice_id || v.id || v.uniq_id;
                const vName = v.name || v.voice_name || vId;
                const gender = v.gender || '';
                const btn = document.createElement('button');
                btn.type = 'button';
                btn.className = 'btn-outline btn-sm';
                btn.style.fontSize = '12px';
                btn.style.padding = '4px 8px';
                btn.innerHTML = `${gender === 'Female' ? '👩' : (gender === 'Male' ? '👨' : '🎙️')} ${vName} <code style="font-size:10px; color:#a1a1aa;">(${vId})</code>`;
                btn.onclick = () => {
                    document.getElementById('cfg-vibi-voice-id').value = vId;
                    alert(`Đã chọn Voice ID: ${vName} (${vId})`);
                };
                list.appendChild(btn);
            });
        } else {
            list.innerHTML = '<span style="color:#ef4444;">Không thể lấy danh sách giọng đọc. Vui lòng kiểm tra lại API Key.</span>';
        }
    } catch (e) {
        list.innerHTML = `<span style="color:#ef4444;">Lỗi: ${e.message}</span>`;
    }
}
window.loadVibiVoices = loadVibiVoices;

async function saveVibiSettings() {
    const vibi_api_key = document.getElementById('cfg-vibi-key').value.trim();
    const vibi_provider = document.getElementById('cfg-vibi-provider').value;
    const vibi_model_id = document.getElementById('cfg-vibi-model').value.trim();
    const vibi_default_voice_id = document.getElementById('cfg-vibi-voice-id').value.trim();

    await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            vibi_api_key,
            vibi_provider,
            vibi_model_id,
            vibi_default_voice_id
        })
    });
    alert('Đã lưu cấu hình ViBi.pro thành công!');
}
window.saveVibiSettings = saveVibiSettings;

// 3. QUEUE & TASK CRUD
function setInputMode(mode) {
    document.querySelectorAll('.sub-tab-btn').forEach(btn => btn.classList.remove('active'));
    document.getElementById('box-single').classList.add('hidden');
    document.getElementById('box-batch').classList.add('hidden');
    document.getElementById('box-file').classList.add('hidden');

    if (mode === 'single') {
        document.querySelectorAll('.sub-tab-btn')[0].classList.add('active');
        document.getElementById('box-single').classList.remove('hidden');
    } else if (mode === 'batch') {
        document.querySelectorAll('.sub-tab-btn')[1].classList.add('active');
        document.getElementById('box-batch').classList.remove('hidden');
    } else if (mode === 'file') {
        document.querySelectorAll('.sub-tab-btn')[2].classList.add('active');
        document.getElementById('box-file').classList.remove('hidden');
    }
}
window.setInputMode = setInputMode;

function extractCleanUrl(text) {
    if (!text) return '';
    const m = text.match(/https?:\/\/[^\s"'`\(\)\[\]<>，。！？\u4e00-\u9fa5]+/i);
    if (m) {
        let u = m[0];
        u = u.replace(/[),.;'"，。！？\]>]+$/, '');
        return u;
    }
    return text.trim();
}
window.extractCleanUrl = extractCleanUrl;

function extractAllUrls(text) {
    if (!text) return [];
    const regex = /https?:\/\/[^\s"'`\(\)\[\]<>，。！？\u4e00-\u9fa5]+/gi;
    const matches = text.match(regex) || [];
    return matches.map(u => u.replace(/[),.;'"，。！？\]>]+$/, '')).filter(u => u.length > 8 && u.startsWith('http'));
}
window.extractAllUrls = extractAllUrls;

async function addSingleUrl() {
    const input = document.getElementById('single-url');
    let url = extractCleanUrl(input.value);
    if (!url || !url.startsWith('http')) return alert('Vui lòng dán link video hoặc nội dung chia sẻ hợp lệ (Rednote, Douyin, TikTok, Facebook, YouTube,...)!');

    const lang = document.getElementById('quick-target-lang').value;
    const voice = document.getElementById('quick-tts-voice').value;
    const model = document.getElementById('quick-model') ? document.getElementById('quick-model').value : 'gemini';
    const bgm = document.getElementById('quick-bgm-mode').value;
    const blur_sub_mode = document.getElementById('quick-blur-sub-mode') ? document.getElementById('quick-blur-sub-mode').value : 'auto_ocr';
    const output_naming_pattern = document.getElementById('task-naming-pattern') ? document.getElementById('task-naming-pattern').value : 'seq_title';
    const custom_output_name = document.getElementById('task-custom-name') ? document.getElementById('task-custom-name').value.trim() : '';

    let tts_engine = 'edge';
    if (voice === 'google') tts_engine = 'google';
    else if (voice === 'vibi' || (!voice.startsWith('vi-VN-') && !voice.startsWith('en-US-') && !voice.startsWith('zh-CN-'))) tts_engine = 'vibi';

    await fetch('/api/tasks/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ input: url, type: 'url', lang, voice, tts_engine, model, bgm, blur_sub_mode, output_naming_pattern, custom_output_name })
    });
    input.value = '';
    const hint = document.getElementById('url-extract-hint');
    if (hint) hint.style.display = 'none';
    fetchTasks();
}
window.addSingleUrl = addSingleUrl;

async function addBatchUrls() {
    const raw = document.getElementById('batch-urls').value;
    const links = extractAllUrls(raw);
    if (links.length === 0) return alert('Không tìm thấy link video hợp lệ nào trong nội dung đã nhập!');

    const lang = document.getElementById('quick-target-lang').value;
    const voice = document.getElementById('quick-tts-voice').value;
    const model = document.getElementById('quick-model') ? document.getElementById('quick-model').value : 'gemini';
    const bgm = document.getElementById('quick-bgm-mode').value;
    const blur_sub_mode = document.getElementById('quick-blur-sub-mode') ? document.getElementById('quick-blur-sub-mode').value : 'auto_ocr';
    const output_naming_pattern = document.getElementById('task-naming-pattern') ? document.getElementById('task-naming-pattern').value : 'seq_title';
    const custom_output_name = document.getElementById('task-custom-name') ? document.getElementById('task-custom-name').value.trim() : '';

    let tts_engine = 'edge';
    if (voice === 'google') tts_engine = 'google';
    else if (voice === 'vibi' || (!voice.startsWith('vi-VN-') && !voice.startsWith('en-US-') && !voice.startsWith('zh-CN-'))) tts_engine = 'vibi';

    await fetch('/api/tasks/batch', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ links, lang, voice, tts_engine, model, bgm, blur_sub_mode, output_naming_pattern, custom_output_name })
    });
    document.getElementById('batch-urls').value = '';
    alert('Đã tự động bóc tách và thêm ' + links.length + ' video vào hàng đợi!');
    fetchTasks();
}
window.addBatchUrls = addBatchUrls;

function handleTxtUpload(event) {
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (e) => { document.getElementById('batch-urls').value = e.target.result; };
    reader.readAsText(file);
}
window.handleTxtUpload = handleTxtUpload;

async function addLocalFile() {
    const input = document.getElementById('local-file-path');
    const path = input.value.trim();
    if (!path) return alert('Vui lòng nhập đường dẫn file video MP4!');

    const lang = document.getElementById('quick-target-lang').value;
    const voice = document.getElementById('quick-tts-voice').value;
    const model = document.getElementById('quick-model') ? document.getElementById('quick-model').value : 'gemini';
    const bgm = document.getElementById('quick-bgm-mode').value;
    const blur_sub_mode = document.getElementById('quick-blur-sub-mode') ? document.getElementById('quick-blur-sub-mode').value : 'auto_ocr';
    const output_naming_pattern = document.getElementById('task-naming-pattern') ? document.getElementById('task-naming-pattern').value : 'seq_title';
    const custom_output_name = document.getElementById('task-custom-name') ? document.getElementById('task-custom-name').value.trim() : '';

    let tts_engine = 'edge';
    if (voice === 'google') tts_engine = 'google';
    else if (voice === 'vibi' || (!voice.startsWith('vi-VN-') && !voice.startsWith('en-US-') && !voice.startsWith('zh-CN-'))) tts_engine = 'vibi';

    await fetch('/api/tasks/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ input: path, type: 'file', lang, voice, tts_engine, model, bgm, blur_sub_mode, output_naming_pattern, custom_output_name })
    });
    input.value = '';
    fetchTasks();
}
window.addLocalFile = addLocalFile;

function setFilter(f) {
    currentFilter = f;
    document.querySelectorAll('.filter-pill').forEach(el => el.classList.remove('active'));
    if (event && event.target) event.target.classList.add('active');
    fetchTasks();
}
window.setFilter = setFilter;

async function fetchTasks() {
    try {
        const searchInput = document.getElementById('task-search');
        const query = searchInput ? searchInput.value.trim() : '';
        const res = await fetch('/api/tasks?filter=' + encodeURIComponent(currentFilter) + '&q=' + encodeURIComponent(query));
        const tasks = await res.json();
        allTasks = tasks;

        let running = 0, paused = 0, done = 0;
        tasks.forEach(t => {
            if (t.state.indexOf('PAUSED') !== -1) paused++;
            else if (t.state === 'COMPLETED') done++;
            else if (t.state !== 'FAILED' && t.state !== 'CANCELLED') running++;
        });

        const stTotal = document.getElementById('stat-total');
        const stRun = document.getElementById('stat-running');
        const stPause = document.getElementById('stat-paused');
        const stDone = document.getElementById('stat-done');
        if (stTotal) stTotal.textContent = tasks.length;
        if (stRun) stRun.textContent = running;
        if (stPause) stPause.textContent = paused;
        if (stDone) stDone.textContent = done;

        const tbody = document.getElementById('task-tbody');
        if (!tbody) return;
        if (tasks.length === 0) {
            tbody.innerHTML = '<tr><td colspan="7" style="text-align:center; padding:24px;" class="text-muted">Chưa có video nào trong hàng đợi. Hãy dán link hoặc nạp file .txt ở trên!</td></tr>';
            return;
        }

        let html = '';
        tasks.forEach((t, idx) => {
            const isPaused = t.state.indexOf('PAUSED') !== -1;
            const isDone = t.state === 'COMPLETED';
            const isFailed = t.state === 'FAILED';
            let platformClass = 'tag-Douyin';
            if (t.platform.indexOf('TikTok') !== -1) platformClass = 'tag-TikTok';
            else if (t.platform.indexOf('Facebook') !== -1) platformClass = 'tag-Facebook';
            else if (t.platform.indexOf('YouTube') !== -1) platformClass = 'tag-YouTube';
            else if (t.platform.indexOf('Rednote') !== -1) platformClass = 'tag-Rednote';
            else if (t.platform.indexOf('Kuaishou') !== -1) platformClass = 'tag-Kuaishou';

            html += '<tr>' +
                '<td><input type="checkbox" class="task-chk" value="' + t.id + '"></td>' +
                '<td><strong style="color:var(--text-sub)">#' + (idx + 1) + '</strong></td>' +
                '<td>' +
                    '<div style="font-weight:700; max-width:280px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; cursor:pointer;" onclick="openInspectorModal(\'' + t.id + '\')">' +
                        t.title +
                    '</div>' +
                    '<small class="text-muted" style="display:block; max-width:280px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">' + t.input + '</small>' +
                '</td>' +
                '<td><span class="platform-tag ' + platformClass + '">' + t.platform + '</span></td>' +
                '<td><span class="status-badge st-' + (isPaused ? 'PAUSED' : t.state) + '">' + t.status_msg + '</span></td>' +
                '<td>' +
                    '<div style="display:flex; justify-content:space-between; font-size:11px; margin-bottom:2px;"><span>' + t.progress + '%</span></div>' +
                    '<div class="prog-wrap"><div class="prog-bar" style="width: ' + t.progress + '%"></div></div>' +
                '</td>' +
                '<td style="text-align:right;">' +
                    '<div class="flex-gap" style="justify-content: flex-end;">' +
                        (isDone && t.output_video ? '<button class="btn-success btn-sm" onclick="downloadSingleVideo(\'' + encodeURIComponent(t.output_video) + '\')" title="Tải trực tiếp video MP4 này về máy">⬇️ Tải MP4</button>' : '') +
                        '<button class="btn-primary btn-sm" onclick="openInspectorModal(\'' + t.id + '\')" title="Kiểm tra chi tiết từng bước & Sửa phụ đề">🔍 Kiểm Tra</button>' +
                        (isPaused ? '<button class="btn-success btn-sm" onclick="resumeTask(\'' + t.id + '\')">▶️ Tiếp</button>' : '') +
                        (!isDone && !isFailed && !isPaused ? '<button class="btn-outline btn-sm" onclick="pauseTask(\'' + t.id + '\')">⏸️ Dừng</button>' : '') +
                        (isFailed ? '<button class="btn-warning btn-sm" onclick="retryTask(\'' + t.id + '\')">🔁 Thử Lại</button>' : '') +
                        '<button class="btn-danger btn-sm" onclick="deleteSingleTask(\'' + t.id + '\')" title="Xóa">✕</button>' +
                    '</div>' +
                '</td>' +
            '</tr>';
        });
        tbody.innerHTML = html;
    } catch (e) {}
}
window.fetchTasks = fetchTasks;

async function pauseTask(id) {
    await fetch('/api/tasks/pause', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ id }) });
    fetchTasks();
}
window.pauseTask = pauseTask;

async function resumeTask(id) {
    await fetch('/api/tasks/resume', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ id }) });
    fetchTasks();
}
window.resumeTask = resumeTask;

async function retryTask(id) {
    await fetch('/api/tasks/retry', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ id }) });
    fetchTasks();
}
window.retryTask = retryTask;

async function deleteSingleTask(id) {
    if (!confirm('Bạn có chắc muốn xóa tác vụ này?')) return;
    await fetch('/api/tasks/delete', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ id, delete_files: true }) });
    fetchTasks();
}
window.deleteSingleTask = deleteSingleTask;

async function deleteCompleted() {
    await fetch('/api/tasks/delete_completed', { method: 'POST' });
    fetchTasks();
}
window.deleteCompleted = deleteCompleted;

async function clearAllQueue() {
    if (!confirm('CẢNH BÁO: Bạn có chắc muốn xóa toàn bộ hàng đợi?')) return;
    await fetch('/api/tasks/clear_all', { method: 'POST' });
    fetchTasks();
}
window.clearAllQueue = clearAllQueue;

function toggleSelectAll(master) {
    document.querySelectorAll('.task-chk').forEach(c => c.checked = master.checked);
}
window.toggleSelectAll = toggleSelectAll;

// 4. API KEYS CRUD & REAL TEST
async function loadApiKeys() {
    try {
        const res = await fetch('/api/keys');
        const data = await res.json();
        const tbody = document.getElementById('keys-tbody');
        if (!tbody) return;

        const allKeys = [
            ...(data.gemini_keys || []).map(k => ({ ...k, provider: 'gemini', prov_name: 'Google Gemini' })),
            ...(data.deepseek_keys || []).map(k => ({ ...k, provider: 'deepseek', prov_name: 'DeepSeek' }))
        ];

        if (allKeys.length === 0) {
            tbody.innerHTML = '<tr><td colspan="7" style="text-align:center; padding:16px;" class="text-muted">Chưa có API Key. Hãy thêm Key bên cạnh (chỉ cần nhập 1 lần)!</td></tr>';
            return;
        }

        let html = '';
        allKeys.forEach(k => {
            html += '<tr>' +
                '<td><strong>' + k.prov_name + '</strong></td>' +
                '<td>' + k.label + '</td>' +
                '<td><code style="background:#070a12; padding:3px 6px; border-radius:4px; font-family:monospace;">' + k.masked_key + '</code></td>' +
                '<td style="color:#34d399; font-weight:700;">' + k.success_count + '</td>' +
                '<td style="color:#f87171; font-weight:700;">' + k.error_count + '</td>' +
                '<td><span style="font-size:11px; font-weight:600; color:' + (k.enabled ? '#34d399' : '#94a3b8') + ';">' + k.status + '</span></td>' +
                '<td style="text-align:right;">' +
                    '<div class="flex-gap" style="justify-content: flex-end;">' +
                        '<button class="btn-primary btn-sm" onclick="testApiKey(\'' + k.provider + '\', \'' + k.raw_key + '\')">⚡ Test Key Thực Tế</button>' +
                        '<button class="btn-secondary btn-sm" onclick="toggleApiKey(\'' + k.provider + '\', \'' + k.raw_key + '\', ' + (!k.enabled) + ')">' + (k.enabled ? 'Tắt' : 'Bật') + '</button>' +
                        '<button class="btn-danger btn-sm" onclick="deleteApiKey(\'' + k.provider + '\', \'' + k.raw_key + '\')">✕</button>' +
                    '</div>' +
                '</td>' +
            '</tr>';
        });
        tbody.innerHTML = html;
    } catch (e) {}
}
window.loadApiKeys = loadApiKeys;

async function addApiKey() {
    const provider = document.getElementById('new-key-provider').value;
    const label = document.getElementById('new-key-label').value.trim();
    const key = document.getElementById('new-key-value').value.trim();
    if (!key) return alert('Vui lòng nhập API Key!');

    const res = await fetch('/api/keys/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ provider, label, key })
    });
    const result = await res.json();
    if (result.success) {
        document.getElementById('new-key-value').value = '';
        document.getElementById('new-key-label').value = '';
        loadApiKeys();
        alert('Đã lưu API Key vĩnh viễn vào hệ thống! Bạn có thể bấm nút Test Key để kiểm tra hoạt động.');
    } else {
        alert(result.error || 'Lỗi thêm key');
    }
}
window.addApiKey = addApiKey;

async function deleteApiKey(provider, key) {
    if (!confirm('Bạn có chắc muốn xóa API Key này khỏi hệ thống?')) return;
    await fetch('/api/keys/delete', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ provider, key }) });
    loadApiKeys();
}
window.deleteApiKey = deleteApiKey;

async function toggleApiKey(provider, key, enabled) {
    await fetch('/api/keys/toggle', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ provider, key, enabled }) });
    loadApiKeys();
}
window.toggleApiKey = toggleApiKey;

async function testApiKey(provider, key) {
    const btn = event ? event.target : null;
    let oldTxt = '';
    if (btn) { oldTxt = btn.textContent; btn.textContent = '⏳ Đang kiểm tra...'; btn.disabled = true; }

    try {
        const res = await fetch('/api/keys/test', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ provider, key })
        });
        const data = await res.json();
        alert((data.success ? '✅ ' : '❌ ') + data.message);
    } catch (e) {
        alert('Lỗi kết nối máy chủ khi kiểm tra API key');
    } finally {
        if (btn) { btn.textContent = oldTxt; btn.disabled = false; }
        loadApiKeys();
    }
}
window.testApiKey = testApiKey;

// 5. INSPECTOR MODAL (TRÌNH KIỂM TRA QUY TRÌNH & SỬA PHỤ ĐỀ)
async function openInspectorModal(taskId) {
    inspectorTaskId = taskId;
    const res = await fetch('/api/tasks/detail?id=' + taskId);
    const detail = await res.json();
    currentInspectorTask = detail;

    document.getElementById('insp-task-title').textContent = detail.title || 'Video';
    document.getElementById('insp-task-status').textContent = detail.status_msg || 'Sẵn sàng';
    document.getElementById('insp-task-progress').textContent = (detail.progress || 0) + '%';

    inspSourceSrt = detail.source_srt || [];
    inspTransSrt = detail.translated_srt || [];

    const rawVid = document.getElementById('insp-raw-video');
    const outVid = document.getElementById('insp-out-video');
    const btnDlMp4 = document.getElementById('btn-modal-dl-mp4');
    const outActions = document.getElementById('insp-out-actions');
    const btnResume = document.getElementById('btn-modal-resume');

    if (detail.video_path) {
        rawVid.src = '/preview_file?path=' + encodeURIComponent(detail.video_path);
        rawVid.load();
        rawVid.classList.remove('hidden');
        document.getElementById('insp-no-raw-video').classList.add('hidden');
    } else {
        rawVid.classList.add('hidden');
        document.getElementById('insp-no-raw-video').classList.remove('hidden');
    }

    if (detail.output_video) {
        outVid.src = '/preview_file?path=' + encodeURIComponent(detail.output_video);
        outVid.load();
        outVid.classList.remove('hidden');
        document.getElementById('insp-no-out-video').classList.add('hidden');
        if (btnDlMp4) btnDlMp4.style.display = 'inline-block';
        if (outActions) outActions.style.display = 'flex';
    } else {
        outVid.classList.add('hidden');
        document.getElementById('insp-no-out-video').classList.remove('hidden');
        if (btnDlMp4) btnDlMp4.style.display = 'none';
        if (outActions) outActions.style.display = 'none';
    }

    if (btnResume) {
        if (detail.state === 'COMPLETED') {
            btnResume.textContent = '🔄 Render Lại Video';
            btnResume.title = 'Render lại video với phụ đề mới chỉnh sửa';
        } else {
            btnResume.textContent = '▶️ Tiếp Tục Xử Lý';
            btnResume.title = 'Lưu phụ đề và tiếp tục quy trình tự động';
        }
    }

    renderInspectorSourceSrt();
    renderInspectorTransSrt();
    renderInspectorTtsList();

    switchInspectorTab('tab-insp-source');
    document.getElementById('modal-inspector').classList.remove('hidden');
}
window.openInspectorModal = openInspectorModal;

function switchInspectorTab(tabId) {
    currentInspStep = tabId;
    document.querySelectorAll('.insp-step-content').forEach(el => el.classList.add('hidden'));
    document.querySelectorAll('.insp-tab-btn').forEach(btn => {
        btn.classList.remove('active');
        const onc = btn.getAttribute('onclick') || '';
        if (onc.includes(tabId)) {
            btn.classList.add('active');
        }
    });

    const content = document.getElementById(tabId);
    if (content) content.classList.remove('hidden');

    if (tabId === 'tab-insp-outvid') {
        const outVid = document.getElementById('insp-out-video');
        if (outVid && outVid.src && outVid.src.indexOf('/preview_file?path=') !== -1) {
            outVid.load();
        }
    } else if (tabId === 'tab-insp-rawvid') {
        const rawVid = document.getElementById('insp-raw-video');
        if (rawVid && rawVid.src && rawVid.src.indexOf('/preview_file?path=') !== -1) {
            rawVid.load();
        }
    }
}
window.switchInspectorTab = switchInspectorTab;

function renderInspectorSourceSrt() {
    const tbody = document.getElementById('insp-source-tbody');
    if (!tbody) return;
    if (inspSourceSrt.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" style="text-align:center; padding:20px;" class="text-muted">Chưa có phụ đề gốc. Hãy để tiến trình hoàn tất bóc sub Whisper!</td></tr>';
        return;
    }
    let rows = '';
    inspSourceSrt.forEach((it, idx) => {
        rows += '<tr>' +
            '<td><strong>#' + it.id + '</strong></td>' +
            '<td>' +
                '<input type="text" value="' + it.start_time + '" onchange="inspSourceSrt[' + idx + '].start_time = this.value" style="width:80px; font-size:11px; margin-bottom:3px;"/><br>' +
                '<input type="text" value="' + it.end_time + '" onchange="inspSourceSrt[' + idx + '].end_time = this.value" style="width:80px; font-size:11px;"/>' +
            '</td>' +
            '<td><textarea rows="2" onchange="inspSourceSrt[' + idx + '].text = this.value" style="font-size:12px;">' + (it.text || '') + '</textarea></td>' +
            '<td style="text-align:right;"><button class="btn-danger btn-sm" onclick="deleteInspSourceRow(' + idx + ')">✕</button></td>' +
        '</tr>';
    });
    tbody.innerHTML = rows;
}

function addInspSourceRow() {
    const last = inspSourceSrt[inspSourceSrt.length - 1];
    let start = "00:00:00,000", end = "00:00:02,000";
    if (last) { start = last.end_time; end = "00:00:05,000"; }
    inspSourceSrt.push({ id: inspSourceSrt.length + 1, start_time: start, end_time: end, text: "新字幕", translated_text: "" });
    renderInspectorSourceSrt();
}
window.addInspSourceRow = addInspSourceRow;

function deleteInspSourceRow(idx) {
    inspSourceSrt.splice(idx, 1);
    inspSourceSrt.forEach((it, i) => it.id = i + 1);
    renderInspectorSourceSrt();
}
window.deleteInspSourceRow = deleteInspSourceRow;

function renderInspectorTransSrt() {
    const tbody = document.getElementById('insp-trans-tbody');
    if (!tbody) return;
    const count = Math.max(inspSourceSrt.length, inspTransSrt.length);
    if (count === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; padding:20px;" class="text-muted">Chưa có dữ liệu bản dịch. Hãy đợi tiến trình bóc sub và dịch AI!</td></tr>';
        return;
    }
    let rows = '';
    for (let i = 0; i < count; i++) {
        const src = inspSourceSrt[i] || { id: i + 1, start_time: '00:00:00,000', end_time: '00:00:02,000', text: '' };
        const trn = inspTransSrt[i] || { id: i + 1, start_time: src.start_time, end_time: src.end_time, text: src.text, translated_text: '' };

        rows += '<tr>' +
            '<td><strong>#' + src.id + '</strong></td>' +
            '<td><span style="font-size:11px; color:var(--text-sub);">' + src.start_time + '<br>➜ ' + src.end_time + '</span></td>' +
            '<td><div style="font-size:12px; color:#cbd5e1; background:var(--bg-input); padding:5px 8px; border-radius:6px; border:1px solid var(--border);">' + (src.text || '') + '</div></td>' +
            '<td><textarea rows="2" onchange="inspTransSrt[' + i + '].translated_text = this.value" style="font-size:12px;">' + (trn.translated_text || '') + '</textarea></td>' +
            '<td style="text-align:right;"><button class="btn-purple btn-sm" onclick="retranslateInspRow(' + i + ')">✨ AI Dịch Lại</button></td>' +
        '</tr>';
    }
    tbody.innerHTML = rows;
}

async function retranslateInspRow(idx) {
    const src = inspSourceSrt[idx];
    if (!src) return;
    const target_lang = document.getElementById('quick-target-lang').value || 'vi';

    const res = await fetch('/api/tasks/retranslate_line', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ id: inspectorTaskId, line_id: src.id, target_lang })
    });
    const data = await res.json();
    if (data.success) {
        if (!inspTransSrt[idx]) inspTransSrt[idx] = { ...src, translated_text: data.translation };
        else inspTransSrt[idx].translated_text = data.translation;
        renderInspectorTransSrt();
        renderInspectorTtsList();
    } else {
        alert('Lỗi khi AI dịch lại. Vui lòng kiểm tra API Key tại tab Quản Lý API Key.');
    }
}
window.retranslateInspRow = retranslateInspRow;

function renderInspectorTtsList() {
    const tbody = document.getElementById('insp-tts-tbody');
    if (!tbody) return;
    if (inspTransSrt.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; padding:20px;" class="text-muted">Chưa có câu thoại đã dịch để lồng tiếng.</td></tr>';
        return;
    }
    let rows = '';
    inspTransSrt.forEach((it) => {
        const lineText = it.translated_text || it.text || '';
        const encText = encodeURIComponent(lineText);
        rows += '<tr>' +
            '<td><strong>#' + it.id + '</strong></td>' +
            '<td><span style="font-size:11px; color:var(--text-sub);">' + it.start_time + ' ➜ ' + it.end_time + '</span></td>' +
            '<td><div style="font-size:12.5px; font-weight:600;">' + lineText + '</div></td>' +
            '<td><span class="badge-pill" style="font-size:10px;">atempo auto</span></td>' +
            '<td style="text-align:right; white-space:nowrap;">' +
                '<button class="btn-outline btn-sm" onclick="previewInspTts(\'' + encText + '\')">🔊 Nghe Thử</button>' +
                '<button class="btn-primary btn-sm" id="btn-regen-voice-' + it.id + '" style="margin-left:6px;" onclick="regenerateInspVoiceLine(' + it.id + ', \'' + encText + '\')">🎙️ Tạo Lại Giọng</button>' +
            '</td>' +
        '</tr>';
    });
    tbody.innerHTML = rows;
}

async function previewInspTts(encodedText) {
    const text = decodeURIComponent(encodedText);
    const voice = (document.getElementById('quick-tts-voice') ? document.getElementById('quick-tts-voice').value : '') || 'vi-VN-HoaiMyNeural';
    let engine = 'edge';
    if (voice === 'google') engine = 'google';
    else if (voice === 'vibi' || (!voice.startsWith('vi-VN-') && !voice.startsWith('en-US-') && !voice.startsWith('zh-CN-'))) engine = 'vibi';

    const res = await fetch('/api/tts_preview_line', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ text, voice, engine })
    });
    const data = await res.json();
    if (data.success) {
        const audio = new Audio(data.audio_url + '&t=' + Date.now());
        audio.play();
    } else {
        alert('Lỗi tạo âm thanh voice preview');
    }
}
window.previewInspTts = previewInspTts;

async function regenerateInspVoiceLine(lineId, encodedText) {
    if (!inspectorTaskId) {
        alert('Không tìm thấy ID tác vụ.');
        return;
    }
    const text = decodeURIComponent(encodedText);
    const btn = document.getElementById(`btn-regen-voice-${lineId}`);
    const originalHtml = btn ? btn.innerHTML : '';
    if (btn) {
        btn.disabled = true;
        btn.innerHTML = '⏳ Đang tạo...';
    }

    try {
        const voice = (document.getElementById('quick-tts-voice') ? document.getElementById('quick-tts-voice').value : '') || '';
        let engine = 'edge';
        if (voice === 'google') engine = 'google';
        else if (voice === 'vibi' || (!voice.startsWith('vi-VN-') && !voice.startsWith('en-US-') && !voice.startsWith('zh-CN-') && voice !== '')) engine = 'vibi';

        const res = await fetch('/api/tasks/regenerate_voice_line', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                task_id: inspectorTaskId,
                line_id: lineId,
                text: text,
                voice: voice,
                engine: engine
            })
        });
        const data = await res.json();
        if (data.success) {
            if (data.audio_url) {
                const audio = new Audio(data.audio_url + '&t=' + Date.now());
                audio.play();
            }
            if (btn) {
                btn.innerHTML = '✅ Đã tạo & ghép!';
                setTimeout(() => {
                    btn.disabled = false;
                    btn.innerHTML = originalHtml;
                }, 2500);
            }
        } else {
            alert('Lỗi tạo lại giọng: ' + (data.error || 'Vui lòng kiểm tra lại cấu hình voice/TTS.'));
            if (btn) {
                btn.disabled = false;
                btn.innerHTML = originalHtml;
            }
        }
    } catch (err) {
        alert('Lỗi mạng hoặc server: ' + err.message);
        if (btn) {
            btn.disabled = false;
            btn.innerHTML = originalHtml;
        }
    }
}
window.regenerateInspVoiceLine = regenerateInspVoiceLine;

function downloadCurrentSrt(type) {
    const list = (type === 'source') ? inspSourceSrt : inspTransSrt;
    if (!list || list.length === 0) {
        alert('Chưa có phụ đề để tải về!');
        return;
    }

    const formatSrtTime = (t) => {
        if (!t) return "00:00:00,000";
        return t.replace('.', ',');
    };

    let content = "";
    list.forEach((item, index) => {
        const idx = index + 1;
        const start = formatSrtTime(item.start_time);
        const end = formatSrtTime(item.end_time);
        const txt = (type === 'source') ? (item.text || '') : (item.translated_text || item.text || '');
        content += `${idx}\r\n${start} --> ${end}\r\n${txt}\r\n\r\n`;
    });

    const blob = new Blob(["\uFEFF" + content], { type: 'text/srt;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    const title = (currentInspectorTask && currentInspectorTask.title) ? currentInspectorTask.title : (document.getElementById('insp-task-title').textContent || 'subtitle');
    const safeTitle = title.replace(/[/\\?%*:|"<>]/g, '_').trim();
    a.download = `${safeTitle}_${type === 'source' ? 'source_zh' : 'trans_vi'}.srt`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}
window.downloadCurrentSrt = downloadCurrentSrt;

function downloadOutputVideo() {
    if (!currentInspectorTask || !currentInspectorTask.output_video) {
        alert('Chưa có video thành phẩm để tải về!');
        return;
    }
    const path = currentInspectorTask.output_video;
    downloadSingleVideo(encodeURIComponent(path));
}
window.downloadOutputVideo = downloadOutputVideo;

function downloadSingleVideo(encodedPath) {
    if (!encodedPath) return;
    const downloadUrl = '/download_file?path=' + encodedPath;
    const a = document.createElement('a');
    a.href = downloadUrl;
    a.download = '';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
}
window.downloadSingleVideo = downloadSingleVideo;

async function downloadBatchZip(onlySelected) {
    let targetIds = [];
    if (onlySelected) {
        const checked = document.querySelectorAll('.task-chk:checked');
        if (checked.length === 0) {
            alert('⚠️ Vui lòng tích chọn vào ô vuông ở đầu các dòng video bạn muốn tải!');
            return;
        }
        targetIds = Array.from(checked).map(c => c.value);
    }

    const completedTasks = allTasks.filter(t => t.state === 'COMPLETED' && t.output_video);
    if (completedTasks.length === 0) {
        alert('⚠️ Hiện chưa có video nào hoàn thành để tải về!');
        return;
    }

    if (onlySelected) {
        const selectedCompleted = completedTasks.filter(t => targetIds.includes(t.id));
        if (selectedCompleted.length === 0) {
            alert('⚠️ Trong các video bạn đã chọn, chưa có video nào ở trạng thái Hoàn Thành (COMPLETED)!');
            return;
        }
    }

    const btn = document.getElementById(onlySelected ? 'btn-batch-dl-selected' : 'btn-batch-dl-all');
    let oldTxt = '';
    if (btn) {
        oldTxt = btn.innerHTML;
        btn.innerHTML = '⏳ Đang nén ZIP...';
        btn.disabled = true;
    }

    try {
        let url = '/api/tasks/download_batch?scope=' + (onlySelected ? 'selected' : 'all');
        if (onlySelected && targetIds.length > 0) {
            url += '&ids=' + encodeURIComponent(targetIds.join(','));
        }

        const a = document.createElement('a');
        a.href = url;
        a.download = '';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    } catch (err) {
        alert('Lỗi khi kích hoạt tải file ZIP: ' + err.message);
    } finally {
        setTimeout(() => {
            if (btn) {
                btn.innerHTML = oldTxt;
                btn.disabled = false;
            }
        }, 3000);
    }
}
window.downloadBatchZip = downloadBatchZip;

async function saveInspectorSrtOnly() {
    if (!inspectorTaskId) return;

    if (inspSourceSrt.length > 0) {
        await fetch('/api/tasks/update_srt', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ id: inspectorTaskId, is_translated: false, subtitles: inspSourceSrt })
        });
    }

    if (inspTransSrt.length > 0) {
        await fetch('/api/tasks/update_srt', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ id: inspectorTaskId, is_translated: true, subtitles: inspTransSrt })
        });
    }

    alert('💾 Đã lưu các thay đổi phụ đề thành công!');
    fetchTasks();
}
window.saveInspectorSrtOnly = saveInspectorSrtOnly;

async function saveInspectorSrtAndResume() {
    if (!inspectorTaskId) return;

    if (inspSourceSrt.length > 0) {
        await fetch('/api/tasks/update_srt', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ id: inspectorTaskId, is_translated: false, subtitles: inspSourceSrt })
        });
    }

    if (inspTransSrt.length > 0) {
        await fetch('/api/tasks/update_srt', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ id: inspectorTaskId, is_translated: true, subtitles: inspTransSrt })
        });
    }

    await resumeTask(inspectorTaskId);
    closeModal('modal-inspector');
}
window.saveInspectorSrtAndResume = saveInspectorSrtAndResume;

function closeModal(id) {
    const el = document.getElementById(id);
    if (el) el.classList.add('hidden');
    const rV = document.getElementById('insp-raw-video');
    const oV = document.getElementById('insp-out-video');
    if (rV) rV.pause();
    if (oV) oV.pause();
}
window.closeModal = closeModal;

async function openOutputFolder() {
    await fetch('/api/open_output', { method: 'POST' });
}
window.openOutputFolder = openOutputFolder;

// 6. LIVE TERMINAL LOGS
async function fetchLogs() {
    try {
        const res = await fetch('/api/logs');
        const logs = await res.json();
        const box = document.getElementById('term-box');
        if (box) {
            box.innerHTML = logs.map(l => '<div>' + l + '</div>').join('');
            box.scrollTop = box.scrollHeight;
        }
    } catch (e) {}
}
window.fetchLogs = fetchLogs;

// 7. DOUYIN ACCOUNT STATUS & LOGIN
async function checkDouyinStatus() {
    try {
        const res = await fetch('/api/douyin/status');
        const data = await res.json();
        const badge = document.getElementById('douyin-login-badge');
        if (badge) {
            if (data.logged_in) {
                badge.style.background = 'rgba(16, 185, 129, 0.2)';
                badge.style.color = '#10b981';
                badge.style.border = '1px solid #10b981';
                badge.innerText = '✅ Đã kết nối tài khoản';
            } else {
                badge.style.background = 'rgba(148, 163, 184, 0.1)';
                badge.style.color = '#94a3b8';
                badge.style.border = '1px solid #475569';
                badge.innerText = 'Chưa đăng nhập (Khách)';
            }
        }
    } catch (e) {}
}
window.checkDouyinStatus = checkDouyinStatus;

async function loginDouyin() {
    if (!confirm("Hệ thống sẽ mở cửa sổ trình duyệt Chrome trên màn hình Windows.\nBạn chỉ cần mở app Douyin trên điện thoại -> Quét mã QR đăng nhập.\nSau khi đăng nhập xong, hệ thống sẽ tự động lưu cookies và đóng cửa sổ.\n\nBấm OK để mở cửa sổ đăng nhập!")) return;
    try {
        const res = await fetch('/api/douyin/login', { method: 'POST' });
        const data = await res.json();
        alert(data.message || "Đã mở cửa sổ đăng nhập! Vui lòng quét mã QR trên màn hình.");
        
        let attempts = 0;
        const interval = setInterval(async () => {
            attempts++;
            const sRes = await fetch('/api/douyin/status');
            const sData = await sRes.json();
            if (sData.logged_in) {
                clearInterval(interval);
                checkDouyinStatus();
                alert("🎉 ĐĂNG NHẬP THÀNH CÔNG!\nCookies tài khoản của bạn đã được lưu vĩnh viễn. Mọi video Douyin sẽ được tải ở độ phân giải cao nhất.");
            }
            if (attempts > 80) clearInterval(interval);
        }, 3000);
    } catch (e) {
        alert("Lỗi kết nối máy chủ: " + e.message);
    }
}
window.loginDouyin = loginDouyin;
