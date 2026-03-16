/**
 * RPI MediaServer - Web UI Controller
 * Mode switches keep a loading state and reload the page when the new stream is ready.
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

const HLS_MAX_RETRIES = 5;
const HLS_RETRY_DELAY_MS = 1500;
const WEBRTC_MAX_RETRIES = 5;
const WEBRTC_RETRY_DELAY_MS = 1200;
const STATUS_POLL_INTERVAL_MS = 3000;

document.addEventListener('DOMContentLoaded', async () => {
    const urlParams = new URLSearchParams(window.location.search);
    const ipParam = urlParams.get('ip');
    const protocolParam = urlParams.get('protocol');
    const modeParam = urlParams.get('mode');

    if (ipParam) {
        CONFIG.serverIP = ipParam;
        document.getElementById('server-ip').textContent = ipParam;
        updateInfoBox();
    }

    if (protocolParam === 'hls' || protocolParam === 'webrtc') {
        currentProtocol = protocolParam;
    }

    updateProtocolButtons();

    const serverStatus = await fetchServerStatus();
    if (serverStatus && isSupportedMode(serverStatus.mode)) {
        syncModeStateFromStatus(serverStatus);
    } else if (isSupportedMode(modeParam)) {
        displayedMode = normalizeMode(modeParam);
    }

    updateModeUI();
    startStream();
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

function buildReloadUrl() {
    const url = new URL(window.location.href);
    url.searchParams.set('ip', CONFIG.serverIP);
    url.searchParams.set('protocol', currentProtocol);
    url.searchParams.set('mode', displayedMode);
    return url.toString();
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

function startFramebufferImageMode() {
    clearHLSRetry();
    clearWebRTCRetry();
    clearFramebufferRefresh();
    detachHLSPlayer();
    resetWebRTCFrame();
    showVideoPlayer();
    hideWebRTC();
    showFramebufferImage();
    refreshFramebufferImage();
    updateStatus('FrameBuffer 이미지 갱신 중...', 'connected');
    updateProtocolBadge('IMAGE');
    updateStats('IMAGE', 'snapshot', '5s');
    if (isModeSwitching) {
        completeModeSwitchUI();
    } else {
        hidePlaceholder();
    }

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
    hidePlaceholder();
    updateModeUI();
}

function failModeSwitchUI(message) {
    isModeSwitching = false;
    pendingMode = null;
    pendingSwitchSeq = 0;
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
    showPlaceholder('새 스트림 연결 확인 중...', '준비가 끝나면 현재 페이지를 자동으로 다시 불러옵니다.');
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
            window.location.replace(buildReloadUrl());
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

function startHLS(options = {}) {
    const {
        keepPlaceholder = false,
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
            video.play();
            if (!keepPlaceholder) {
                completeModeSwitchUI();
            }
            updateStatus('HLS 연결됨', 'connected');
            updateProtocolBadge('RTSP/HLS');
            updateStats('HLS', '-', '-');
            showVideoPlayer();
            hideWebRTC();
            if (typeof onConnected === 'function') {
                onConnected();
            }
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
            video.play();
            if (!keepPlaceholder) {
                completeModeSwitchUI();
            }
            updateStatus('HLS 연결됨 (Native)', 'connected');
            updateProtocolBadge('RTSP/HLS');
            showVideoPlayer();
            hideWebRTC();
            if (typeof onConnected === 'function') {
                onConnected();
            }
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
        keepPlaceholder = false,
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
            if (!keepPlaceholder) {
                completeModeSwitchUI();
            }
            updateStatus('WebRTC 연결됨', 'connected');
            updateProtocolBadge('WebRTC');
            updateStats('WebRTC', '-', '-');
            hideVideoPlayer();
            showWebRTC();
            if (typeof onConnected === 'function') {
                onConnected();
            }
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
    if (displayedMode === 'screen') {
        startFramebufferImageMode();
        return;
    }
    if (currentProtocol === 'hls') {
        startHLS();
    } else {
        startWebRTC();
    }
}

function updateProtocolButtons() {
    document.getElementById('btn-hls').classList.toggle('active', currentProtocol === 'hls');
    document.getElementById('btn-webrtc').classList.toggle('active', currentProtocol === 'webrtc');
}

function switchToHLS() {
    if (currentProtocol === 'hls') return;
    currentProtocol = 'hls';
    updateProtocolButtons();

    if (isModeSwitching) {
        return;
    }

    if (displayedMode === 'screen') {
        startFramebufferImageMode();
        return;
    }

    startHLS();
}

function switchToWebRTC() {
    if (currentProtocol === 'webrtc') return;
    currentProtocol = 'webrtc';
    updateProtocolButtons();

    if (isModeSwitching) {
        return;
    }

    if (displayedMode === 'screen') {
        startFramebufferImageMode();
        return;
    }

    startWebRTC();
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

    if (isSupportedMode(statusMode)) {
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
