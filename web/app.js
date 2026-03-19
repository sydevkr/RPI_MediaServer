/**
 * RPI MediaServer - Web UI Controller
 * Mode switches keep a loading state and reconnect in-place when the new stream is ready.
 */

const CONFIG = {
    serverIP: '10.1.119.31',
    hlsPort: 8888,
    webrtcPort: 8889,
    streamPath: '/live',
    appPort: 8081,
    probeIntervalMs: 500,
    probeTimeoutMs: 20000
};

let currentProtocol = 'hls';
let displayedMode = '2x2';
let hls = null;
let hlsRetryTimer = null;
let hlsRetryCount = 0;
let webRTCRetryTimer = null;
let webRTCRetryCount = 0;
let isModeSwitching = false;
let streamReadyProbeTimer = null;
let streamReadyProbeStartedAt = 0;
let protocolConnectToken = 0;
let pendingMode = null;
let pendingSwitchSeq = 0;
let statusPollTimer = null;
let framebufferRefreshTimer = null;
let framebufferSnapshotIntervalMs = 5000;
let placeholderHideTimer = null;
let selectedAIFeatures = new Set();
let savedAIFeaturesFor2x2 = null;  // 2x2 진입 전 선택값 백업

const AI_RECT_CONFIG = {
    person: { color: '#7fff7f', label: '사람' },
    crisis: { color: '#ff8c00', label: '위기' },
    fire:   { color: '#ff3333', label: '화재' },
    ocr:    { color: '#87ceeb', label: 'OCR'  }
};
const AI_RECT_ORDER  = ['person', 'crisis', 'fire', 'ocr'];
const AI_RECT_WIDTH  = 160;
const AI_RECT_HEIGHT = 100;
const AI_RECT_GAP    = 12;
const AI_RECT_RIGHT  = 16;
const AI_RECT_MARGIN_TOP = 8;  // video-overlay 하단과의 간격

const HLS_MAX_RETRIES = 5;
const HLS_RETRY_DELAY_MS = 1500;
const WEBRTC_MAX_RETRIES = 5;
const WEBRTC_RETRY_DELAY_MS = 1200;
const STATUS_POLL_INTERVAL_MS = 3000;

document.addEventListener('DOMContentLoaded', async () => {
    const redirected = await ensureCanonicalUrl();
    if (redirected) {
        return;
    }

    ensureAIMenuSection();
    stabilizeMainLayout();
    window.addEventListener('resize', stabilizeMainLayout);

    const urlParams = new URLSearchParams(window.location.search);
    const ipParam = urlParams.get('ip');
    const protocolParam = urlParams.get('protocol');
    const modeParam = urlParams.get('mode');
    const aiParam = urlParams.get('ai');

    if (ipParam) {
        CONFIG.serverIP = ipParam;
        document.getElementById('server-ip').textContent = ipParam;
        updateInfoBox();
    }

    if (protocolParam === 'hls' || protocolParam === 'webrtc') {
        currentProtocol = protocolParam;
    }

    try {
        const saved = JSON.parse(window.localStorage.getItem('selectedAIFeatures')) || [];
        saved.filter(isSupportedAIFeature).forEach(f => selectedAIFeatures.add(f));
    } catch {}
    if (aiParam) {
        aiParam.split(',').filter(isSupportedAIFeature).forEach(f => selectedAIFeatures.add(f));
    }

    updateProtocolButtons();
    updateAIFeatureUI();

    const serverStatus = await fetchServerStatus();
    if (serverStatus && isSupportedMode(serverStatus.mode)) {
        syncModeStateFromStatus(serverStatus);
    } else if (isSupportedMode(modeParam)) {
        displayedMode = normalizeMode(modeParam);
    }

    updateModeUI();
    syncBrowserUrl();

    // 이미 파이프라인이 준비된 경우 즉시 연결, 아직 준비 중이면 대기 후 연결
    if (serverStatus?.pipeline_state === 'running' && serverStatus?.stream_ready) {
        startStream();
    } else {
        startStreamWhenReady();
    }

    startStatusPolling();

    const video = document.getElementById('video-player');
    video.addEventListener('loadedmetadata', onVideoLoaded);
    video.addEventListener('error', onVideoError);
});

function getHLSUrl() {
    return `http://${CONFIG.serverIP}:${CONFIG.hlsPort}${CONFIG.streamPath}/index.m3u8`;
}

function getWebRTCUrl() {
    return `http://${CONFIG.serverIP}:${CONFIG.webrtcPort}${CONFIG.streamPath}`;
}

function getFramebufferImageUrl() {
    return `http://${CONFIG.serverIP}:${CONFIG.appPort}/framebuffer/latest.jpg`;
}

function addCacheBuster(url) {
    const separator = url.includes('?') ? '&' : '?';
    return `${url}${separator}ts=${Date.now()}`;
}

async function ensureCanonicalUrl() {
    const currentUrl = new URL(window.location.href);
    const params = currentUrl.searchParams;
    let changed = false;

    if (!params.get('ip')) {
        params.set('ip', window.location.hostname);
        changed = true;
    }

    if (!params.get('protocol')) {
        params.set('protocol', 'hls');
        changed = true;
    }

    if (!params.get('mode')) {
        let resolvedMode = '2x2';
        try {
            const response = await fetch('/api/status', { cache: 'no-store' });
            if (response.ok) {
                const status = await response.json();
                if (isSupportedMode(status?.mode)) {
                    resolvedMode = normalizeMode(status.mode);
                }
            }
        } catch (error) {
            console.warn('Failed to resolve current mode for URL normalization:', error);
        }
        params.set('mode', resolvedMode);
        changed = true;
    }

    if (!params.has('ai')) {
        try {
            const saved = JSON.parse(window.localStorage.getItem('selectedAIFeatures')) || [];
            params.set('ai', saved.filter(isSupportedAIFeature).join(','));
        } catch {
            params.set('ai', '');
        }
        changed = true;
    }

    if (!changed) {
        return false;
    }

    window.location.replace(currentUrl.toString());
    return true;
}

function syncBrowserUrl() {
    const url = new URL(window.location.href);
    url.searchParams.set('ip', CONFIG.serverIP);
    url.searchParams.set('protocol', currentProtocol);
    url.searchParams.set('mode', displayedMode);
    url.searchParams.set('ai', [...selectedAIFeatures].join(','));
    window.history.replaceState({}, '', url);
}

function isSupportedAIFeature(feature) {
    return feature === 'person'
        || feature === 'fire'
        || feature === 'crisis'
        || feature === 'ocr';
}

function getAIFeaturesLabel(features) {
    const labels = { person: '사람', fire: '화재', crisis: '위기', ocr: 'OCR' };
    if (!features || features.size === 0) return '';
    return [...features].map(f => labels[f] || f).join(', ');
}

function createAIMenuSection() {
    const toolbar = document.createElement('div');
    toolbar.className = 'ai-toolbar';
    toolbar.innerHTML = `
        <span class="ai-toolbar-label">AI 분석</span>
        <button id="btn-ai-person" class="btn ai-btn" onclick="selectAIFeature('person')">🧍 사람</button>
        <button id="btn-ai-crisis" class="btn ai-btn" onclick="selectAIFeature('crisis')">🚨 위기</button>
        <button id="btn-ai-fire" class="btn ai-btn" onclick="selectAIFeature('fire')">🔥 화재</button>
        <button id="btn-ai-ocr" class="btn ai-btn" onclick="selectAIFeature('ocr')">🔤 OCR</button>
        <span id="ai-feature-label" class="ai-toolbar-status"></span>
    `;
    return toolbar;
}

function ensureAIMenuSection() {
    const videoSection = document.querySelector('.video-section');
    if (!videoSection || document.getElementById('btn-ai-person')) {
        return;
    }

    const toolbar = createAIMenuSection();
    const videoContainer = videoSection.querySelector('.video-container');
    videoSection.insertBefore(toolbar, videoContainer);
}

function stabilizeMainLayout() {
    const mainContent = document.querySelector('.main-content');
    const controlPanel = document.querySelector('.control-panel');
    const videoSection = document.querySelector('.video-section');
    if (!mainContent || !controlPanel || !videoSection) {
        return;
    }

    if (mainContent.firstElementChild !== controlPanel) {
        mainContent.insertBefore(controlPanel, videoSection);
    }

    // CSS 클래스가 레이아웃을 담당하므로 인라인 스타일을 모두 제거해 CSS에 위임
    mainContent.style.removeProperty('display');
    mainContent.style.removeProperty('flex-direction');
    mainContent.style.removeProperty('grid-template-columns');
    mainContent.style.removeProperty('grid-template-areas');
    mainContent.style.removeProperty('direction');
    mainContent.style.removeProperty('align-items');
    controlPanel.style.removeProperty('grid-area');
    controlPanel.style.removeProperty('grid-column');
    controlPanel.style.removeProperty('order');
    videoSection.style.removeProperty('grid-area');
    videoSection.style.removeProperty('grid-column');
    videoSection.style.removeProperty('order');
}

function updateAIFeatureUI() {
    document.querySelectorAll('.ai-btn').forEach((btn) => {
        btn.classList.remove('active');
    });

    selectedAIFeatures.forEach(feature => {
        const btn = document.getElementById(`btn-ai-${feature}`);
        if (btn) btn.classList.add('active');
    });

    const featureLabel = document.getElementById('ai-feature-label');
    if (featureLabel) {
        featureLabel.textContent = getAIFeaturesLabel(selectedAIFeatures);
    }
    updateAIRects();
}

function updateAIToolbarForMode(mode) {
    const disabled = (mode === '2x2');

    if (disabled) {
        // 2x2 진입: 현재 선택값 백업 후 전부 해제
        if (savedAIFeaturesFor2x2 === null) {
            savedAIFeaturesFor2x2 = new Set(selectedAIFeatures);
        }
        selectedAIFeatures.clear();
    } else {
        // 싱글 복귀: 백업값 복원
        if (savedAIFeaturesFor2x2 !== null) {
            selectedAIFeatures = new Set(savedAIFeaturesFor2x2);
            savedAIFeaturesFor2x2 = null;
        }
    }

    document.querySelectorAll('.ai-btn').forEach(btn => {
        btn.disabled = disabled;
        btn.classList.toggle('btn-disabled', disabled);
    });

    const featureLabel = document.getElementById('ai-feature-label');
    if (featureLabel && disabled) {
        featureLabel.textContent = '2×2 모드 (AI 미지원)';
    } else if (featureLabel) {
        featureLabel.textContent = getAIFeaturesLabel(selectedAIFeatures);
    }

    updateAIFeatureUI();
}

function updateAIRects() {
    const container = document.getElementById('ai-rects-container');
    if (!container) return;
    container.innerHTML = '';

    if (displayedMode === '2x2') return;

    const selected = AI_RECT_ORDER.filter(f => selectedAIFeatures.has(f));
    selected.forEach((feature, idx) => {
        const cfg = AI_RECT_CONFIG[feature];
        const rect = document.createElement('div');
        rect.className = 'ai-rect';
        rect.style.borderColor = cfg.color;
        const overlay = document.getElementById('video-overlay');
        const overlayH = (overlay && currentProtocol !== 'webrtc') ? overlay.offsetHeight : 48;
        rect.style.top   = `${overlayH + AI_RECT_MARGIN_TOP}px`;
        rect.style.right = `${AI_RECT_RIGHT + idx * (AI_RECT_WIDTH + AI_RECT_GAP)}px`;

        const label = document.createElement('span');
        label.className = 'ai-rect-label';
        label.style.color = cfg.color;
        label.textContent = cfg.label;
        rect.appendChild(label);
        container.appendChild(rect);
    });
}

function selectAIFeature(feature) {
    if (!isSupportedAIFeature(feature)) {
        return;
    }
    if (selectedAIFeatures.has(feature)) {
        selectedAIFeatures.delete(feature);
    } else {
        selectedAIFeatures.add(feature);
    }
    window.localStorage.setItem('selectedAIFeatures', JSON.stringify([...selectedAIFeatures]));
    updateAIFeatureUI();
    syncBrowserUrl();
}

function detachHLSPlayer() {
    const video = document.getElementById('video-player');
    clearHLSRetry();

    if (hls) {
        hls.destroy();
        hls = null;
    }

    video.pause();
    video.removeAttribute('src');
    video.load();
}

function clearFramebufferRefresh() {
    if (framebufferRefreshTimer) {
        clearTimeout(framebufferRefreshTimer);
        framebufferRefreshTimer = null;
    }
}

function showFramebufferImage() {
    document.getElementById('framebuffer-image').classList.remove('hidden');
    document.getElementById('video-player').classList.add('hidden');
}

function hideFramebufferImage() {
    document.getElementById('framebuffer-image').classList.add('hidden');
    document.getElementById('video-player').classList.remove('hidden');
}

function refreshFramebufferImage() {
    const image = document.getElementById('framebuffer-image');
    image.src = addCacheBuster(getFramebufferImageUrl());
}

function clearPlaceholderHideTimer() {
    if (placeholderHideTimer) {
        clearTimeout(placeholderHideTimer);
        placeholderHideTimer = null;
    }
}

function placeholderDelayMs(protocolLabel) {
    if (protocolLabel === 'RTSP/HLS') {
        return 2000;
    }
    if (protocolLabel === 'WebRTC') {
        return 1000;
    }
    return 0;
}

function schedulePlaceholderHide(connectToken, protocolLabel) {
    clearPlaceholderHideTimer();
    const delayMs = placeholderDelayMs(protocolLabel);

    placeholderHideTimer = setTimeout(() => {
        placeholderHideTimer = null;
        if (connectToken !== protocolConnectToken) {
            return;
        }
        hidePlaceholder();
    }, delayMs);
}

function finalizeMediaReady(connectToken, onConnected, statusMessage, protocolLabel, resolution = '-', bitrate = '-') {
    if (connectToken !== protocolConnectToken) {
        return;
    }

    if (isModeSwitching) {
        completeModeSwitchUI();
    }

    updateStatus(statusMessage, 'connected');
    updateProtocolBadge(protocolLabel);
    updateStats(protocolLabel, resolution, bitrate);
    syncBrowserUrl();
    schedulePlaceholderHide(connectToken, protocolLabel);

    if (typeof onConnected === 'function') {
        onConnected();
    }
}

function waitForImageReady(image, connectToken, onConnected) {
    const handleLoad = () => {
        image.onload = null;
        image.onerror = null;
        finalizeMediaReady(connectToken, onConnected, 'FrameBuffer 이미지 갱신 중...', 'IMAGE', 'snapshot', '5s');
    };

    const handleError = () => {
        image.onload = null;
        image.onerror = null;
        if (connectToken !== protocolConnectToken) {
            return;
        }
        if (isModeSwitching) {
            failModeSwitchUI('FrameBuffer 이미지를 불러오지 못했습니다.');
            return;
        }
        showPlaceholder('FrameBuffer 로드 실패', '최신 이미지를 불러오지 못했습니다.');
        updateStatus('FrameBuffer 이미지 로드 실패', 'error');
    };

    image.onload = handleLoad;
    image.onerror = handleError;
}

function waitForVideoReady(video, connectToken, onConnected, statusMessage, protocolLabel) {
    const finalize = () => {
        video.removeEventListener('loadeddata', finalize);
        video.removeEventListener('playing', finalize);

        const resolution = video.videoWidth && video.videoHeight
            ? `${video.videoWidth}x${video.videoHeight}`
            : '-';
        finalizeMediaReady(connectToken, onConnected, statusMessage, protocolLabel, resolution, '-');
    };

    video.addEventListener('loadeddata', finalize, { once: true });
    video.addEventListener('playing', finalize, { once: true });
}

function startFramebufferImageMode(options = {}) {
    const { connectToken = protocolConnectToken } = options;
    clearHLSRetry();
    clearWebRTCRetry();
    clearFramebufferRefresh();
    detachHLSPlayer();
    resetWebRTCFrame();
    showVideoPlayer();
    hideWebRTC();
    showFramebufferImage();
    waitForImageReady(document.getElementById('framebuffer-image'), connectToken, null);
    refreshFramebufferImage();
    updateStatus('FrameBuffer 이미지 로딩 중...', 'loading');

    const tick = () => {
        refreshFramebufferImage();
        framebufferRefreshTimer = setTimeout(tick, framebufferSnapshotIntervalMs);
    };
    framebufferRefreshTimer = setTimeout(tick, framebufferSnapshotIntervalMs);
}

function resetWebRTCFrame() {
    const frame = document.getElementById('webrtc-frame');
    frame.onload = null;
    frame.onerror = null;
    frame.src = 'about:blank';
}

function showPlaceholder(title, subtitle) {
    const placeholder = document.getElementById('stream-placeholder');
    document.getElementById('placeholder-title').textContent = title;
    document.getElementById('placeholder-subtitle').textContent = subtitle;
    placeholder.classList.remove('hidden');
}

function hidePlaceholder() {
    document.getElementById('stream-placeholder').classList.add('hidden');
}

function enterModeSwitchUI() {
    isModeSwitching = true;
    protocolConnectToken += 1;
    disableModeButtons(true);
    clearPlaceholderHideTimer();
    clearStreamProbe();
    clearHLSRetry();
    clearWebRTCRetry();
    clearFramebufferRefresh();
    detachHLSPlayer();
    resetWebRTCFrame();
    hideFramebufferImage();
    showVideoPlayer();
    hideWebRTC();
    showPlaceholder('비디오 모드 전환 중...', '새 스트림 연결 확인 중입니다.');
    updateStatus('비디오 모드 전환 중...', 'loading');
}

function completeModeSwitchUI() {
    isModeSwitching = false;
    pendingMode = null;
    pendingSwitchSeq = 0;
    disableModeButtons(false);
    updateModeUI();
}

function failModeSwitchUI(message) {
    isModeSwitching = false;
    pendingMode = null;
    pendingSwitchSeq = 0;
    clearPlaceholderHideTimer();
    disableModeButtons(false);
    showPlaceholder('모드 전환 실패', message);
    updateStatus(message, 'error');
    updateModeUI();
}

function disableModeButtons(disabled) {
    document.querySelectorAll('.mode-btn').forEach((btn) => {
        btn.disabled = disabled;
    });
}

async function waitForStreamReadyAndReconnect() {
    const connectToken = ++protocolConnectToken;
    clearStreamProbe();
    streamReadyProbeStartedAt = Date.now();
    showPlaceholder('새 스트림 연결 확인 중...', '현재 페이지에서 새 스트림으로 다시 연결합니다.');
    updateStatus('새 스트림 연결 확인 중...', 'loading');

    const poll = async () => {
        if (!isModeSwitching || connectToken !== protocolConnectToken) {
            return;
        }

        const status = await fetchServerStatus();
        const pipelineState = status?.pipeline_state || 'unknown';
        const statusMode = status?.mode;
        const targetMode = status?.target_mode;
        const streamReady = Boolean(status?.stream_ready);
        const switchSeq = Number(status?.switch_seq || 0);
        const lastCompletedSwitchSeq = Number(status?.last_completed_switch_seq || 0);
        const timedOut = Date.now() - streamReadyProbeStartedAt >= CONFIG.probeTimeoutMs;

        if (status) {
            syncModeStateFromStatus(status);
        }

        console.debug('mode switch poll', {
            pipelineState,
            statusMode,
            targetMode,
            pendingMode,
            streamReady,
            switchSeq,
            pendingSwitchSeq,
            lastCompletedSwitchSeq
        });

        const modeReady = !pendingMode || statusMode === pendingMode;
        const switchCompleted = pendingSwitchSeq > 0
            ? lastCompletedSwitchSeq >= pendingSwitchSeq
            : switchSeq > 0 && lastCompletedSwitchSeq >= switchSeq;

        if (pipelineState === 'running' && modeReady && streamReady && switchCompleted) {
            clearStreamProbe();
            displayedMode = isSupportedMode(statusMode) ? statusMode : displayedMode;
            pendingMode = null;
            pendingSwitchSeq = lastCompletedSwitchSeq;
            updateModeUI();
            reconnectForCurrentMode(connectToken);
            return;
        }

        if (pipelineState === 'error' || timedOut) {
            clearStreamProbe();
            failModeSwitchUI('새 스트림 준비에 실패했습니다.');
            return;
        }

        streamReadyProbeTimer = setTimeout(poll, CONFIG.probeIntervalMs);
    };

    streamReadyProbeTimer = setTimeout(poll, CONFIG.probeIntervalMs);
}

function clearStreamProbe() {
    if (streamReadyProbeTimer) {
        clearTimeout(streamReadyProbeTimer);
        streamReadyProbeTimer = null;
    }
}

function reconnectForCurrentMode(connectToken = protocolConnectToken) {
    clearPlaceholderHideTimer();

    if (displayedMode === 'screen') {
        startFramebufferImageMode({ connectToken });
        return;
    }

    if (currentProtocol === 'hls') {
        startHLS({
            connectToken,
            onError: () => failModeSwitchUI('HLS 재연결에 실패했습니다.')
        });
        return;
    }

    startWebRTC({
        connectToken,
        onError: () => failModeSwitchUI('WebRTC 재연결에 실패했습니다.')
    });
}

function startHLS(options = {}) {
    const {
        onConnected = null,
        onError = null,
        connectToken = protocolConnectToken
    } = options;
    const video = document.getElementById('video-player');
    const hlsUrl = addCacheBuster(getHLSUrl());
    clearHLSRetry();
    clearWebRTCRetry();
    clearFramebufferRefresh();
    resetWebRTCFrame();
    hideFramebufferImage();

    updateStatus('HLS 스트림 로딩 중...', 'loading');

    if (Hls.isSupported()) {
        detachHLSPlayer();

        hls = new Hls({
            enableWorker: true,
            lowLatencyMode: true,
            backBufferLength: 1,
            maxBufferLength: 2,
            liveSyncDurationCount: 1,
            liveMaxLatencyDurationCount: 2
        });

        hls.loadSource(hlsUrl);
        hls.attachMedia(video);

        hls.on(Hls.Events.MANIFEST_PARSED, () => {
            if (connectToken !== protocolConnectToken) {
                return;
            }
            hlsRetryCount = 0;
            waitForVideoReady(video, connectToken, onConnected, 'HLS 연결됨', 'RTSP/HLS');
            video.play();
            showVideoPlayer();
            hideWebRTC();
        });

        hls.on(Hls.Events.ERROR, (event, data) => {
            console.error('HLS Error:', data);
            if (connectToken !== protocolConnectToken) {
                return;
            }
            if (isModeSwitching) {
                if (typeof onError === 'function' && data?.fatal) {
                    onError(data.type || 'fatal');
                }
                return;
            }
            if (data.fatal) {
                scheduleHLSRetry(data.type);
            }
        });
    } else if (video.canPlayType('application/vnd.apple.mpegurl')) {
        video.src = hlsUrl;
        video.addEventListener('loadedmetadata', () => {
            if (connectToken !== protocolConnectToken) {
                return;
            }
            hlsRetryCount = 0;
            waitForVideoReady(video, connectToken, onConnected, 'HLS 연결됨 (Native)', 'RTSP/HLS');
            video.play();
            showVideoPlayer();
            hideWebRTC();
        }, { once: true });
    } else {
        updateStatus('HLS를 지원하지 않는 브라우저', 'error');
        if (typeof onError === 'function') {
            onError('unsupported');
        }
    }
}

function startWebRTC(options = {}) {
    const {
        onConnected = null,
        onError = null,
        connectToken = protocolConnectToken
    } = options;
    const frame = document.getElementById('webrtc-frame');
    const webrtcUrl = addCacheBuster(getWebRTCUrl());
    clearHLSRetry();
    clearWebRTCRetry();
    clearFramebufferRefresh();
    detachHLSPlayer();
    resetWebRTCFrame();
    hideFramebufferImage();

    updateStatus('WebRTC 재연결 중...', 'loading');

    setTimeout(() => {
        if (connectToken !== protocolConnectToken) {
            return;
        }
        frame.onload = () => {
            webRTCRetryCount = 0;
            if (connectToken !== protocolConnectToken) {
                return;
            }
            hideVideoPlayer();
            showWebRTC();
            finalizeMediaReady(connectToken, onConnected, 'WebRTC 연결됨', 'WebRTC', '-', '-');
        };
        frame.onerror = () => {
            if (connectToken !== protocolConnectToken) {
                return;
            }
            if (isModeSwitching) {
                if (typeof onError === 'function') {
                    onError('iframe');
                }
                return;
            }
            scheduleWebRTCRetry();
        };
        frame.src = webrtcUrl;
    }, 200);
}

function startStream() {
    reconnectForCurrentMode();
}

function startStreamWhenReady() {
    const startedAt = Date.now();
    showPlaceholder('서버 연결 중...', '파이프라인이 준비될 때까지 대기합니다.');
    updateStatus('서버 연결 중...', 'loading');

    const poll = async () => {
        const status = await fetchServerStatus();
        const pipelineState = status?.pipeline_state || 'unknown';
        const streamReady = Boolean(status?.stream_ready);
        const timedOut = Date.now() - startedAt >= CONFIG.probeTimeoutMs;

        if (status && isSupportedMode(status.mode)) {
            syncModeStateFromStatus(status);
        }

        if (pipelineState === 'running' && streamReady) {
            clearStreamProbe();
            startStream();
            return;
        }

        if (pipelineState === 'error' || timedOut) {
            clearStreamProbe();
            showPlaceholder('서버 연결 실패', '페이지를 새로고침하거나 서버 상태를 확인하세요.');
            updateStatus('서버 연결 실패', 'error');
            return;
        }

        streamReadyProbeTimer = setTimeout(poll, CONFIG.probeIntervalMs);
    };

    streamReadyProbeTimer = setTimeout(poll, CONFIG.probeIntervalMs);
}

function updateProtocolButtons() {
    document.getElementById('btn-hls').classList.toggle('active', currentProtocol === 'hls');
    document.getElementById('btn-webrtc').classList.toggle('active', currentProtocol === 'webrtc');
}

function switchToHLS() {
    if (currentProtocol === 'hls') return;
    currentProtocol = 'hls';
    updateProtocolButtons();
    syncBrowserUrl();

    if (isModeSwitching) {
        return;
    }

    if (displayedMode === 'screen') {
        startFramebufferImageMode();
        return;
    }

    startHLS();
    updateAIRects();
}

function switchToWebRTC() {
    if (currentProtocol === 'webrtc') return;
    currentProtocol = 'webrtc';
    updateProtocolButtons();
    syncBrowserUrl();

    if (isModeSwitching) {
        return;
    }

    if (displayedMode === 'screen') {
        startFramebufferImageMode();
        return;
    }

    startWebRTC();
    updateAIRects();
}

function updateModeUI() {
    document.querySelectorAll('.mode-btn').forEach((btn) => {
        btn.classList.remove('active');
        btn.classList.remove('pending');
    });

    const activeBtn = document.getElementById(`btn-${displayedMode}`);
    if (activeBtn) {
        activeBtn.classList.add('active');
    }

    if (pendingMode) {
        const pendingBtn = document.getElementById(`btn-${pendingMode}`);
        if (pendingBtn && pendingBtn !== activeBtn) {
            pendingBtn.classList.add('pending');
        }
    }

    const modeNames = {
        'usb': 'USB Camera',
        'screen': 'Screen Capture',
        'csi': 'CSI Camera',
        'hdmi': 'HDMI Capture',
        '2x2': '2×2 Mix'
    };

    const modeDisplay = document.getElementById('current-mode');
    if (modeDisplay) {
        if (pendingMode && pendingMode !== displayedMode) {
            modeDisplay.textContent = `전환 중: ${(modeNames[displayedMode] || displayedMode)} -> ${(modeNames[pendingMode] || pendingMode)}`;
        } else {
            modeDisplay.textContent = `현재 모드: ${modeNames[displayedMode] || displayedMode}`;
        }
    }

    updateAIToolbarForMode(displayedMode);
}

async function switchMode(mode) {
    const canonicalMode = normalizeMode(mode);
    if (displayedMode === canonicalMode || isModeSwitching) return;

    try {
        enterModeSwitchUI();

        const response = await fetch(`http://${CONFIG.serverIP}:${CONFIG.appPort}/api/mode`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ mode: canonicalMode })
        });

        const result = await response.json();

        if (!result.success) {
            failModeSwitchUI(result.error || '모드 변경 실패');
            return;
        }

        pendingMode = canonicalMode;
        pendingSwitchSeq = Number(result.switch_seq || 0);
        console.debug('mode switch requested', {
            requestedMode: canonicalMode,
            pendingSwitchSeq,
            result
        });
        updateModeUI();
        waitForStreamReadyAndReconnect();
    } catch (error) {
        console.error('Mode switch error:', error);
        failModeSwitchUI('모드 변경 중 오류가 발생했습니다.');
    }
}

async function fetchServerStatus() {
    try {
        const response = await fetch(`http://${CONFIG.serverIP}:${CONFIG.appPort}/api/status`, {
            cache: 'no-store'
        });
        return await response.json();
    } catch (error) {
        console.error('Failed to fetch server status:', error);
        return null;
    }
}

function isSupportedMode(mode) {
    const normalized = normalizeMode(mode);
    return normalized === 'usb' || normalized === 'screen' || normalized === 'csi' || normalized === 'hdmi' || normalized === '2x2';
}

function normalizeMode(mode) {
    if (mode === 'fb') {
        return 'screen';
    }
    return mode;
}

function syncModeStateFromStatus(status) {
    const statusMode = normalizeMode(status?.mode);
    const targetMode = normalizeMode(status?.target_mode);
    const pipelineState = status?.pipeline_state || 'unknown';
    const switchSeq = Number(status?.switch_seq || 0);
    const lastCompletedSwitchSeq = Number(status?.last_completed_switch_seq || 0);

    if (isSupportedMode(statusMode) && (!isModeSwitching || !pendingMode || statusMode === pendingMode)) {
        displayedMode = statusMode;
    }

    if (status?.framebuffer_snapshot_interval_sec) {
        framebufferSnapshotIntervalMs = Number(status.framebuffer_snapshot_interval_sec) * 1000;
    }

    if (pipelineState === 'restarting' && isSupportedMode(targetMode) && targetMode !== displayedMode) {
        pendingMode = targetMode;
        if (switchSeq > 0) {
            pendingSwitchSeq = switchSeq;
        }
    } else if (pipelineState === 'running' && (lastCompletedSwitchSeq >= pendingSwitchSeq || targetMode === displayedMode)) {
        pendingMode = null;
        pendingSwitchSeq = lastCompletedSwitchSeq;
    }

    updateModeUI();
}

function startStatusPolling() {
    clearStatusPolling();

    const poll = async () => {
        const status = await fetchServerStatus();
        if (status) {
            syncModeStateFromStatus(status);
        }
        statusPollTimer = setTimeout(poll, STATUS_POLL_INTERVAL_MS);
    };

    statusPollTimer = setTimeout(poll, STATUS_POLL_INTERVAL_MS);
}

function clearStatusPolling() {
    if (statusPollTimer) {
        clearTimeout(statusPollTimer);
        statusPollTimer = null;
    }
}

function clearHLSRetry() {
    if (hlsRetryTimer) {
        clearTimeout(hlsRetryTimer);
        hlsRetryTimer = null;
    }
}

function clearWebRTCRetry() {
    if (webRTCRetryTimer) {
        clearTimeout(webRTCRetryTimer);
        webRTCRetryTimer = null;
    }
}

function scheduleHLSRetry(reason) {
    if (hlsRetryCount >= HLS_MAX_RETRIES) {
        updateStatus('HLS 오류: ' + reason, 'error');
        return;
    }

    hlsRetryCount += 1;
    updateStatus(`HLS 재연결 중... (${hlsRetryCount}/${HLS_MAX_RETRIES})`, 'loading');
    clearHLSRetry();
    hlsRetryTimer = setTimeout(() => {
        startHLS();
    }, HLS_RETRY_DELAY_MS);
}

function scheduleWebRTCRetry() {
    if (webRTCRetryCount >= WEBRTC_MAX_RETRIES) {
        updateStatus('WebRTC 오류', 'error');
        return;
    }

    webRTCRetryCount += 1;
    updateStatus(`WebRTC 재연결 중... (${webRTCRetryCount}/${WEBRTC_MAX_RETRIES})`, 'loading');
    clearWebRTCRetry();
    webRTCRetryTimer = setTimeout(() => {
        startWebRTC();
    }, WEBRTC_RETRY_DELAY_MS);
}

function onVideoLoaded() {
    const video = document.getElementById('video-player');
    const width = video.videoWidth;
    const height = video.videoHeight;

    if (width && height) {
        updateStats(currentProtocol === 'hls' ? 'HLS' : 'WebRTC', `${width}x${height}`, '-');
    }
}

function onVideoError(e) {
    console.error('Video error:', e);
    if (!isModeSwitching) {
        updateStatus('비디오 오류 발생', 'error');
    }
}

function showVideoPlayer() {
    document.querySelector('.video-container').style.display = 'block';
}

function hideVideoPlayer() {
    document.querySelector('.video-container').style.display = 'none';
}

function showWebRTC() {
    document.getElementById('webrtc-container').classList.remove('hidden');
}

function hideWebRTC() {
    document.getElementById('webrtc-container').classList.add('hidden');
}

function updateStatus(message, type) {
    const statusEl = document.getElementById('status');
    statusEl.textContent = message;
    statusEl.className = 'status ' + type;
}

function updateProtocolBadge(protocol) {
    const badge = document.getElementById('protocol-badge');
    badge.textContent = protocol;
    badge.className = 'badge ' + (protocol === 'WebRTC' ? 'webrtc' : 'rtsp');
}

function updateStats(protocol, resolution, bitrate) {
    document.getElementById('stat-protocol').textContent = protocol || '-';
    document.getElementById('stat-resolution').textContent = resolution || '-';
    document.getElementById('stat-bitrate').textContent = bitrate || '-';
}

function updateInfoBox() {
    const ip = CONFIG.serverIP;
    const infoRows = document.querySelectorAll('.info-row code');
    if (infoRows.length >= 3) {
        infoRows[0].textContent = `rtsp://${ip}:8554/live`;
        infoRows[1].textContent = `http://${ip}:8888/live`;
        infoRows[2].textContent = `http://${ip}:8889/live`;
    }
}

window.switchToHLS = switchToHLS;
window.switchToWebRTC = switchToWebRTC;
window.switchMode = switchMode;
window.selectAIFeature = selectAIFeature;
