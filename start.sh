#!/bin/bash

# RPI_MediaServer Startup Script

# Always run from this script's directory so relative paths (./web, ./build, ./config.ini) are stable.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MEDIAMTX_BIN=${MEDIAMTX_BIN:-/usr/local/bin/mediamtx}
MEDIAMTX_CONF=${MEDIAMTX_CONF:-./mediamtx.yml}
SERVER_BIN=${SERVER_BIN:-./build/RPI_MediaServer}
CONFIG_FILE=${1:-./config.ini}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== RPI_MediaServer Remote Guard ===${NC}"

# Get IP address (127.x 제외, 10.1.x 우선, 그 외 사설망 IP)
get_ip_address() {
    local ips=$(hostname -I)
    local preferred_ip=""
    
    # 10.1.x.x 대역 우선 검색
    for ip in $ips; do
        if [[ "$ip" =~ ^10\.1\. ]]; then
            preferred_ip="$ip"
            break
        fi
    done
    
    # 10.1.x.x 없으면 다른 사설망 IP 검색 (127.x 제외)
    if [ -z "$preferred_ip" ]; then
        for ip in $ips; do
            # 127.0.0.1 제외, 10.x.x.x, 172.16-31.x.x, 192.168.x.x 포함
            if [[ ! "$ip" =~ ^127\. ]] && [[ "$ip" =~ ^(10\.|172\.(1[6-9]|2[0-9]|3[01])\.|192\.168\.) ]]; then
                preferred_ip="$ip"
                break
            fi
        done
    fi
    
    # 사설망 IP 없으면 127.0.0.1 사용
    if [ -z "$preferred_ip" ]; then
        preferred_ip="127.0.0.1"
    fi
    
    echo "$preferred_ip"
}

IP_ADDRESS=$(get_ip_address)

# Extract RTSP URL from config
RTSP_URL=$(grep "^rtsp_url=" "$CONFIG_FILE" 2>/dev/null | cut -d'=' -f2)
if [ -z "$RTSP_URL" ]; then
    RTSP_URL="rtsp://localhost:8554/live"
fi
VIDEO_MODE=$(grep "^video_mode=" "$CONFIG_FILE" 2>/dev/null | cut -d'=' -f2)
WF_RECORDER_BIN=$(grep "^wf_recorder_path=" "$CONFIG_FILE" 2>/dev/null | cut -d'=' -f2)
if [ -z "$WF_RECORDER_BIN" ]; then
    WF_RECORDER_BIN="/usr/bin/wf-recorder"
fi
# Replace localhost with actual IP for external access
RTSP_URL_EXTERNAL=${RTSP_URL/localhost/$IP_ADDRESS}
WEBRTC_URL="http://$IP_ADDRESS:8889/live"

# Check for root (needed for V4L2)
if [ "$EUID" -ne 0 ]; then 
    echo -e "${YELLOW}⚠ Warning: Running without root may cause V4L2 permission issues${NC}"
fi

# Check MediaMTX
if [ ! -f "$MEDIAMTX_BIN" ]; then
    echo -e "${RED}✗ MediaMTX not found at $MEDIAMTX_BIN${NC}"
    echo "Please install: wget https://github.com/bluenviron/mediamtx/releases/download/v1.6.0/mediamtx_v1.6.0_linux_arm64v8.tar.gz"
    exit 1
fi

# Check wf-recorder when running Wayland screen capture mode
if [ "$VIDEO_MODE" = "5" ] && [ ! -x "$WF_RECORDER_BIN" ]; then
    echo -e "${RED}✗ wf-recorder not found or not executable: $WF_RECORDER_BIN${NC}"
    echo "Please install: sudo apt install wf-recorder"
    exit 1
fi

# Check server binary
if [ ! -f "$SERVER_BIN" ]; then
    echo -e "${YELLOW}⚠ Server binary not found. Building...${NC}"
    ./build.sh
fi

# Check config
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}✗ Config file not found: $CONFIG_FILE${NC}"
    exit 1
fi

# Stop any previously running server process (match exact process name only)
OLD_PIDS=$(pgrep -x 'RPI_MediaServer' || true)
if [ -n "$OLD_PIDS" ]; then
    echo "Stopping existing RPI_MediaServer: $OLD_PIDS"
    kill $OLD_PIDS || true
    sleep 1
    # Force kill if still alive
    REMAIN_PIDS=$(pgrep -x 'RPI_MediaServer' || true)
    if [ -n "$REMAIN_PIDS" ]; then
        echo "Force stopping remaining RPI_MediaServer: $REMAIN_PIDS"
        kill -9 $REMAIN_PIDS || true
        sleep 1
    fi
else
    echo "No existing RPI_MediaServer process found"
fi

# Check/start MediaMTX service state
if ! pgrep -x mediamtx >/dev/null 2>&1; then
    echo -e "${YELLOW}⚠ MediaMTX is not running. Trying to start...${NC}"

    if [ "$EUID" -eq 0 ]; then
        systemctl start mediamtx || true
    else
        echo -e "${BLUE}▶ Starting MediaMTX via sudo (password may be required)...${NC}"
        sudo systemctl start mediamtx || {
            echo -e "${RED}✗ Failed to start MediaMTX automatically${NC}"
            echo "Please run manually: sudo systemctl start mediamtx"
            exit 1
        }
    fi

    echo -e "${BLUE}▶ Waiting for MediaMTX to become ready...${NC}"
    MTX_READY=0
    for i in {1..10}; do
        if pgrep -x mediamtx >/dev/null 2>&1; then
            MTX_READY=1
            break
        fi
        sleep 1
    done

    if [ "$MTX_READY" -ne 1 ]; then
        echo -e "${RED}✗ MediaMTX did not start within timeout${NC}"
        echo "Check status: sudo systemctl status mediamtx"
        exit 1
    fi
fi
echo -e "${GREEN}✓ MediaMTX service is running${NC}"

# Function to print connection info
print_connection_info() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${NC}           ${GREEN}RPI_MediaServer 시작 완료${NC}                         ${CYAN}║${NC}"
    echo -e "${CYAN}╠══════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${CYAN}║${NC}  📹 ${YELLOW}RTSP 스트림:${NC}                                             ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}     ${GREEN}$RTSP_URL_EXTERNAL${NC}                                      ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  ⚡ ${YELLOW}WebRTC 재생:${NC}                                             ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}     ${GREEN}$WEBRTC_URL${NC}                                     ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  🌐 ${YELLOW}HTTP API (MediaMTX):${NC}                                     ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}     ${GREEN}http://$IP_ADDRESS:9997${NC}                                 ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  💻 ${YELLOW}VLC로 재생:${NC}                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}     vlc ${RTSP_URL_EXTERNAL}${NC}                                 ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  💻 ${YELLOW}FFplay로 재생:${NC}                                           ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}     ffplay -fflags nobuffer ${RTSP_URL_EXTERNAL}${NC}             ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}                                                              ${CYAN}║${NC}"
    echo -e "${CYAN}║${NC}  🔧 ${YELLOW}설정 파일:${NC} $CONFIG_FILE                                  ${CYAN}║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# Function to cleanup on exit
cleanup() {
    echo ""
    echo -e "${BLUE}▶ Shutting down...${NC}"
    echo -e "${GREEN}✓ Shutdown complete${NC}"
    exit 0
}

trap cleanup SIGINT SIGTERM

# Print connection info
print_connection_info

# Start server
echo -e "${BLUE}▶ Starting RPI_MediaServer...${NC}"
"$SERVER_BIN" "$CONFIG_FILE"

# MediaMTX is managed by systemd, so only the app exits here
cleanup
