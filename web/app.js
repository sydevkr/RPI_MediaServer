/**
 * RPI MediaServer - Web UI Controller
 * HLS 및 WebRTC 스트리밍 지원
 */

// 설정
const CONFIG = {
    serverIP: '10.1.119.31',
    hlsPort: 8888,
    webrtcPort: 8889,
    streamPath: '/live'
};

// 상태
let currentProtocol = 'hls';  // 'hls' 또는 'webrtc'
let currentMode = '2x2';      // 기본값은 2x2, 실제 초기 상태는 서버 응답으로 동기화
let hls = null;
let hlsRetryTimer = null;
let hlsRetryCount = 0;
let webRTCRetryTimer = null;
let webRTCRetryCount = 0;

const HLS_MAX_RETRIES = 5;
const HLS_RETRY_DELAY_MS = 1500;
const WEBRTC_MAX_RETRIES = 5;
const WEBRTC_RETRY_DELAY_MS = 1200;

/**
 * 초기화
 */
document.addEventListener('DOMContentLoaded', async () => {
    // URL 파라미터 파싱
    const urlParams = new URLSearchParams(window.location.search);
    
    // 서버 IP 자동 감지
    const ipParam = urlParams.get('ip');
    if (ipParam) {
        CONFIG.serverIP = ipParam;
        document.getElementById('server-ip').textContent = ipParam;
        updateInfoBox();
    }
    
    // 모드 파라미터는 서버 상태 조회 실패 시에만 fallback으로 사용
    const modeParam = urlParams.get('mode');
    const serverStatus = await fetchServerStatus();

    if (serverStatus && isSupportedMode(serverStatus.mode)) {
        currentMode = serverStatus.mode;
    } else if (isSupportedMode(modeParam)) {
        currentMode = modeParam;
    }

    // 초기 UI 상태 설정은 실제 서버 상태 기준으로 반영
    updateModeUI();
    
    // 초기 스트림 시작
    startStream();
    
    // 비디오 이벤트 리스너
    const video = document.getElementById('video-player');
    video.addEventListener('loadedmetadata', onVideoLoaded);
    video.addEventListener('error', onVideoError);
});

/**
 * HLS 스트리밍 시작
 */
function startHLS() {
    const video = document.getElementById('video-player');
    const hlsUrl = `http://${CONFIG.serverIP}:${CONFIG.hlsPort}${CONFIG.streamPath}/index.m3u8`;
    clearHLSRetry();
    clearWebRTCRetry();
    resetWebRTCFrame();

    updateStatus('HLS 스트림 로딩 중...', 'loading');
    
    // HLS.js 지원 확인
    if (Hls.isSupported()) {
        if (hls) {
            hls.destroy();
            hls = null;
        }

        video.pause();
        video.removeAttribute('src');
        video.load();

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
            hlsRetryCount = 0;
            video.play();
            updateStatus('HLS 연결됨', 'connected');
            updateProtocolBadge('RTSP/HLS');
            updateStats('HLS', '-', '-');
        });
        
        hls.on(Hls.Events.ERROR, (event, data) => {
            console.error('HLS Error:', data);
            if (data.fatal) {
                scheduleHLSRetry(data.type);
            }
        });
    } else if (video.canPlayType('application/vnd.apple.mpegurl')) {
        // 네이티브 HLS 지원 (Safari)
        video.src = hlsUrl;
        video.addEventListener('loadedmetadata', () => {
            hlsRetryCount = 0;
            video.play();
            updateStatus('HLS 연결됨 (Native)', 'connected');
            updateProtocolBadge('RTSP/HLS');
        }, { once: true });
    } else {
        updateStatus('HLS를 지원하지 않는 브라우저', 'error');
    }
    
    // UI 업데이트
    showVideoPlayer();
    hideWebRTC();
}

/**
 * WebRTC 스트리밍 시작
 */
function startWebRTC() {
    const webrtcUrl = `http://${CONFIG.serverIP}:${CONFIG.webrtcPort}${CONFIG.streamPath}`;
    clearHLSRetry();
    clearWebRTCRetry();

    updateStatus('WebRTC 재연결 중...', 'loading');

    // iframe을 먼저 비운 뒤 재로드하여 세션 재협상을 유도
    const frame = document.getElementById('webrtc-frame');
    frame.onload = null;
    frame.onerror = null;
    frame.src = 'about:blank';

    setTimeout(() => {
        frame.onload = () => {
            webRTCRetryCount = 0;
            updateStatus('WebRTC 연결됨', 'connected');
            updateProtocolBadge('WebRTC');
            updateStats('WebRTC', '-', '-');
        };
        frame.onerror = () => {
            scheduleWebRTCRetry();
        };
        frame.src = webrtcUrl;
    }, 250);
    
    // UI 업데이트
    hideVideoPlayer();
    showWebRTC();
}

/**
 * 스트림 시작 (현재 설정에 따라)
 */
function startStream() {
    if (currentProtocol === 'hls') {
        startHLS();
    } else {
        startWebRTC();
    }
}

/**
 * HLS로 전환
 */
function switchToHLS() {
    if (currentProtocol === 'hls') return;
    
    currentProtocol = 'hls';
    
    // 버튼 상태 업데이트
    document.getElementById('btn-hls').classList.add('active');
    document.getElementById('btn-webrtc').classList.remove('active');
    
    startHLS();
}

/**
 * WebRTC로 전환
 */
function switchToWebRTC() {
    if (currentProtocol === 'webrtc') return;
    
    currentProtocol = 'webrtc';
    
    // 버튼 상태 업데이트
    document.getElementById('btn-webrtc').classList.add('active');
    document.getElementById('btn-hls').classList.remove('active');
    
    startWebRTC();
}

/**
 * 모드 UI 업데이트 (초기화 및 모드 변경 시 호출)
 */
function updateModeUI() {
    console.log('updateModeUI called, currentMode:', currentMode);
    
    // 버튼 상태 업데이트
    const buttons = document.querySelectorAll('.mode-btn');
    console.log('Found mode buttons:', buttons.length);
    
    buttons.forEach(btn => {
        btn.classList.remove('active');
    });
    
    const activeBtn = document.getElementById(`btn-${currentMode}`);
    if (activeBtn) {
        activeBtn.classList.add('active');
        console.log(`Activated button: btn-${currentMode}`);
    } else {
        console.error(`Button not found: btn-${currentMode}`);
    }
    
    // 모드 표시 업데이트
    const modeNames = {
        'usb': 'USB Camera',
        'fb': 'Screen Capture',
        'screen': 'Screen Capture',
        'csi': 'CSI Camera',
        'hdmi': 'HDMI Capture',
        '2x2': '2×2 Mix'
    };
    const modeDisplay = document.getElementById('current-mode');
    if (modeDisplay) {
        modeDisplay.textContent = modeNames[currentMode] || currentMode;
        console.log('Updated mode display to:', modeNames[currentMode] || currentMode);
    }
}

/**
 * 비디오 모드 전환
 * HTTP API 호출 후 페이지 새로고침
 */
async function switchMode(mode) {
    if (currentMode === mode) return;
    
    console.log(`Switching mode from ${currentMode} to ${mode}`);
    
    try {
        // HTTP API로 모드 변경 요청
        const response = await fetch(`http://${CONFIG.serverIP}:8081/api/mode`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ mode: mode })
        });
        
        const result = await response.json();
        console.log('API response:', result);
        
        if (result.success) {
            currentMode = isSupportedMode(result.mode) ? result.mode : mode;
            updateModeUI();

            // 파이프라인 재시작 후 실제 서버 상태를 다시 반영
            setTimeout(async () => {
                const status = await fetchServerStatus();
                if (status && isSupportedMode(status.mode)) {
                    currentMode = status.mode;
                    updateModeUI();
                }

                if (currentProtocol === 'hls') {
                    startHLS();
                } else {
                    startWebRTC();
                }
            }, 1000);
        } else {
            alert('모드 변경 실패: ' + (result.error || 'Unknown error'));
        }
    } catch (error) {
        console.error('Mode switch error:', error);
        alert('모드 변경 중 오류 발생: ' + error.message);
    }
}

/**
 * 현재 서버 상태 조회
 */
async function fetchServerStatus() {
    try {
        const response = await fetch(`http://${CONFIG.serverIP}:8081/api/status`);
        const status = await response.json();
        console.log('Server status:', status);
        return status;
    } catch (error) {
        console.error('Failed to fetch server status:', error);
        return null;
    }
}

function isSupportedMode(mode) {
    return mode === 'usb' || mode === 'fb' || mode === 'screen' || mode === 'csi' || mode === 'hdmi' || mode === '2x2';
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

function resetWebRTCFrame() {
    const frame = document.getElementById('webrtc-frame');
    frame.onload = null;
    frame.onerror = null;
    frame.src = 'about:blank';
}

/**
 * 비디오 로드 완료 처리
 */
function onVideoLoaded() {
    const video = document.getElementById('video-player');
    const width = video.videoWidth;
    const height = video.videoHeight;
    
    if (width && height) {
        updateStats(currentProtocol === 'hls' ? 'HLS' : 'WebRTC', 
                    `${width}x${height}`, '-');
    }
}

/**
 * 비디오 오류 처리
 */
function onVideoError(e) {
    console.error('Video error:', e);
    updateStatus('비디오 오류 발생', 'error');
}

/**
 * UI 헬퍼 함수들
 */
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

// 전역 함수로 등록 (HTML onclick에서 사용)
window.switchToHLS = switchToHLS;
window.switchToWebRTC = switchToWebRTC;
window.switchMode = switchMode;
