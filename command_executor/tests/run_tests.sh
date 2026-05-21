#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
# run_tests.sh — Launch CommandExecutor + Run Test Suite + Teardown
#
# Usage:
#   cd /home/radxa/oro_base/oro_base_edge_layer/command_executor
#   bash tests/run_tests.sh
# ═══════════════════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_DIR/build/command_executor_node"
PID_FILE="/tmp/command_executor_test.pid"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

cleanup() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo -e "${YELLOW}[teardown]${NC} Stopping command_executor_node (PID=$PID)..."
            kill "$PID" 2>/dev/null || true
            wait "$PID" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
    fi
    # Clean up IPC sockets
    rm -f /tmp/oro_cmd_exec.ipc /tmp/oro_cmd_result.ipc
}

trap cleanup EXIT

# ── 1. Build ──────────────────────────────────────────────────────────────────
echo -e "${YELLOW}[build]${NC} Building command_executor_node..."
cd "$PROJECT_DIR"
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1 && make -j$(nproc) > /dev/null 2>&1
cd "$PROJECT_DIR"

if [ ! -f "$BINARY" ]; then
    echo -e "${RED}[error]${NC} Binary not found at $BINARY"
    exit 1
fi
echo -e "${GREEN}[build]${NC} Build successful."

# ── 2. Start the service ─────────────────────────────────────────────────────
echo -e "${YELLOW}[launch]${NC} Starting command_executor_node in background..."
cleanup  # kill any leftover instance
"$BINARY" &
echo $! > "$PID_FILE"
sleep 1  # let ZMQ sockets bind

PID=$(cat "$PID_FILE")
if ! kill -0 "$PID" 2>/dev/null; then
    echo -e "${RED}[error]${NC} command_executor_node failed to start."
    exit 1
fi
echo -e "${GREEN}[launch]${NC} Service running (PID=$PID)."

# ── 3. Check pyzmq ───────────────────────────────────────────────────────────
if ! python3 -c "import zmq" 2>/dev/null; then
    echo -e "${YELLOW}[deps]${NC} Installing pyzmq..."
    pip3 install pyzmq --quiet
fi

# ── 4. Run tests ─────────────────────────────────────────────────────────────
echo ""
echo -e "${YELLOW}[test]${NC} Running integration tests..."
echo "────────────────────────────────────────────────────────────────────"
python3 "$SCRIPT_DIR/test_command_executor.py" -v
TEST_EXIT=$?
echo "────────────────────────────────────────────────────────────────────"

# ── 5. Report ────────────────────────────────────────────────────────────────
if [ $TEST_EXIT -eq 0 ]; then
    echo -e "${GREEN}[result]${NC} All tests passed ✓"
else
    echo -e "${RED}[result]${NC} Some tests failed ✗ (exit code=$TEST_EXIT)"
fi

exit $TEST_EXIT
