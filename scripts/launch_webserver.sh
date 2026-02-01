#!/bin/bash
# Launch script for the PokéRL Map Visualizer
# Starts both the WebSocket server and a simple HTTP server for the frontend

set -e

# =============================================================================
# Configuration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WEBSERVER_DIR="$PROJECT_ROOT/webserver"
WS_SERVER_DIR="$WEBSERVER_DIR/ws-server"

# Ports
WS_PORT=3344
HTTP_PORT=8080

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Process IDs
WS_PID=""
HTTP_PID=""

# =============================================================================
# Utility Functions
# =============================================================================

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_data() {
    echo -e "${CYAN}[DATA]${NC} $1"
}

# =============================================================================
# Dependency Checks
# =============================================================================

check_node() {
    if ! command -v node &> /dev/null; then
        log_error "Node.js is not installed. Please install Node.js first."
        exit 1
    fi
    log_info "Node.js found: $(node --version)"
}

check_python() {
    if command -v python3 &> /dev/null; then
        PYTHON_CMD="python3"
        log_info "Python found: $(python3 --version)"
        return 0
    elif command -v python &> /dev/null; then
        PYTHON_CMD="python"
        log_info "Python found: $(python --version)"
        return 0
    else
        log_error "Python is not installed. Cannot start HTTP server."
        return 1
    fi
}

install_dependencies() {
    if [ ! -d "$WS_SERVER_DIR/node_modules" ]; then
        log_warn "Installing WebSocket server dependencies..."
        cd "$WS_SERVER_DIR"
        npm install
    fi
}

# =============================================================================
# Server Functions
# =============================================================================

start_websocket_server() {
    log_info "Starting WebSocket server on ws://localhost:${WS_PORT}"
    cd "$WS_SERVER_DIR"
    LOG_USERDATA=true node --expose-gc index.js &
    WS_PID=$!
    sleep 1
    
    if ! kill -0 $WS_PID 2>/dev/null; then
        log_error "WebSocket server failed to start"
        exit 1
    fi
}

start_http_server() {
    log_info "Starting HTTP server on http://localhost:${HTTP_PORT}"
    cd "$WEBSERVER_DIR"
    $PYTHON_CMD -m http.server $HTTP_PORT &
    HTTP_PID=$!
    sleep 1
    
    if ! kill -0 $HTTP_PID 2>/dev/null; then
        log_error "HTTP server failed to start"
        cleanup
        exit 1
    fi
}

show_banner() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  PokéRL Map Visualizer is running!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "  Frontend:   ${YELLOW}http://localhost:${HTTP_PORT}${NC}"
    echo -e "  WebSocket:  ${YELLOW}ws://localhost:${WS_PORT}${NC}"
    echo ""
    echo -e "  ${CYAN}Userdata logging enabled (coords excluded)${NC}"
    echo ""
    echo -e "  Press ${RED}Ctrl+C${NC} to stop all servers"
    echo ""
}

# =============================================================================
# Cleanup
# =============================================================================

cleanup() {
    echo -e "\n${YELLOW}Shutting down servers...${NC}"
    if [ -n "$WS_PID" ] && kill -0 $WS_PID 2>/dev/null; then
        kill $WS_PID 2>/dev/null || true
        log_info "WebSocket server stopped (PID: $WS_PID)"
    fi
    if [ -n "$HTTP_PID" ] && kill -0 $HTTP_PID 2>/dev/null; then
        kill $HTTP_PID 2>/dev/null || true
        log_info "HTTP server stopped (PID: $HTTP_PID)"
    fi
    echo -e "${GREEN}All servers stopped.${NC}"
    exit 0
}

# =============================================================================
# Main
# =============================================================================

main() {
    trap cleanup SIGINT SIGTERM
    
    # Check dependencies
    check_node
    check_python || exit 1
    install_dependencies
    
    # Start servers
    start_websocket_server
    start_http_server
    
    # Show status
    show_banner
    
    # Wait for processes
    wait
}

# Run main function
main
