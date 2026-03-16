#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT="$SCRIPT_DIR"
CONFIG_FILE="${CONFIG_FILE:-$PROJECT_ROOT/config.ini}"
MODE="${1:-${WFREC_MODE:-single}}"

log() {
    echo "[start-wfrec] $*" >&2
}

fail() {
    log "ERROR: $*"
    exit 1
}

read_config_value() {
    local key="$1"
    local file="$2"

    awk -F= -v key="$key" '
        $0 ~ "^[[:space:]]*#" { next }
        $1 ~ "^[[:space:]]*" key "[[:space:]]*$" {
            sub(/^[[:space:]]+/, "", $2)
            sub(/[[:space:]]+$/, "", $2)
            print $2
            exit
        }
    ' "$file"
}

derive_mix_rtsp_url() {
    local base_url="$1"

    if [[ "$base_url" == */* ]]; then
        echo "${base_url%/*}/fb"
    else
        echo "${base_url}/fb"
    fi
}

detect_runtime_dir() {
    local configured_runtime_dir="$1"

    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
        echo "$XDG_RUNTIME_DIR"
        return
    fi

    if [[ -n "$configured_runtime_dir" ]]; then
        echo "$configured_runtime_dir"
        return
    fi

    echo "/run/user/$(id -u)"
}

detect_wayland_display() {
    local runtime_dir="$1"
    local configured_display="$2"

    if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
        echo "$WAYLAND_DISPLAY"
        return
    fi

    if [[ -n "$configured_display" ]]; then
        echo "$configured_display"
        return
    fi

    local candidate
    candidate=$(find "$runtime_dir" -maxdepth 1 -type s -name 'wayland-*' | sort | head -n 1 || true)
    if [[ -n "$candidate" ]]; then
        basename "$candidate"
        return
    fi

    echo ""
}

if [[ ! -f "$CONFIG_FILE" ]]; then
    fail "config file not found: $CONFIG_FILE"
fi

case "$MODE" in
    single|mix)
        ;;
    *)
        fail "invalid mode '$MODE' (use: single or mix)"
        ;;
esac

WF_RECORDER_BIN=$(read_config_value "wf_recorder_path" "$CONFIG_FILE")
RTSP_URL=$(read_config_value "rtsp_url" "$CONFIG_FILE")
FPS=$(read_config_value "fps" "$CONFIG_FILE")
SINGLE_WIDTH=$(read_config_value "single_width" "$CONFIG_FILE")
SINGLE_HEIGHT=$(read_config_value "single_height" "$CONFIG_FILE")
MIX_WIDTH=$(read_config_value "mix_width" "$CONFIG_FILE")
MIX_HEIGHT=$(read_config_value "mix_height" "$CONFIG_FILE")
CONFIG_WAYLAND_DISPLAY=$(read_config_value "wayland_display" "$CONFIG_FILE")
CONFIG_RUNTIME_DIR=$(read_config_value "xdg_runtime_dir" "$CONFIG_FILE")

WF_RECORDER_BIN="${WF_RECORDER_BIN:-/usr/bin/wf-recorder}"
RTSP_URL="${RTSP_URL:-rtsp://localhost:8554/live}"
FPS="${FPS:-30}"
SINGLE_WIDTH="${SINGLE_WIDTH:-1280}"
SINGLE_HEIGHT="${SINGLE_HEIGHT:-720}"
MIX_WIDTH="${MIX_WIDTH:-320}"
MIX_HEIGHT="${MIX_HEIGHT:-240}"

if [[ "$MODE" == "single" ]]; then
    OUTPUT_WIDTH="$SINGLE_WIDTH"
    OUTPUT_HEIGHT="$SINGLE_HEIGHT"
    PUBLISH_URL="${WFREC_RTSP_URL:-$RTSP_URL}"
else
    OUTPUT_WIDTH="$MIX_WIDTH"
    OUTPUT_HEIGHT="$MIX_HEIGHT"
    PUBLISH_URL="${WFREC_RTSP_URL:-$(derive_mix_rtsp_url "$RTSP_URL")}"
fi

RUNTIME_DIR=$(detect_runtime_dir "$CONFIG_RUNTIME_DIR")
[[ -d "$RUNTIME_DIR" ]] || fail "XDG_RUNTIME_DIR does not exist: $RUNTIME_DIR"

WAYLAND_DISPLAY_VALUE=$(detect_wayland_display "$RUNTIME_DIR" "$CONFIG_WAYLAND_DISPLAY")
[[ -n "$WAYLAND_DISPLAY_VALUE" ]] || fail "WAYLAND_DISPLAY not found under $RUNTIME_DIR"
[[ -S "$RUNTIME_DIR/$WAYLAND_DISPLAY_VALUE" ]] || fail "Wayland socket does not exist: $RUNTIME_DIR/$WAYLAND_DISPLAY_VALUE"

if [[ -n "${XDG_SESSION_TYPE:-}" && "${XDG_SESSION_TYPE}" != "wayland" ]]; then
    log "XDG_SESSION_TYPE=${XDG_SESSION_TYPE}; continuing because a valid Wayland socket was found"
fi

[[ -x "$WF_RECORDER_BIN" ]] || fail "wf-recorder not executable: $WF_RECORDER_BIN"
[[ -n "$PUBLISH_URL" ]] || fail "RTSP publish URL is empty"

export XDG_RUNTIME_DIR="$RUNTIME_DIR"
export WAYLAND_DISPLAY="$WAYLAND_DISPLAY_VALUE"
export XDG_SESSION_TYPE="wayland"

FILTER="scale=${OUTPUT_WIDTH}:${OUTPUT_HEIGHT}"

log "mode=$MODE"
log "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
log "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
log "publish_url=$PUBLISH_URL"
log "resolution=${OUTPUT_WIDTH}x${OUTPUT_HEIGHT}"
log "fps=$FPS"

exec "$WF_RECORDER_BIN" \
    --muxer=rtsp \
    --codec=libx264 \
    --file="$PUBLISH_URL" \
    --framerate="$FPS" \
    -F "$FILTER"
