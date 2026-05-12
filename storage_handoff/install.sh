#!/usr/bin/env bash
# =============================================================================
# install.sh — Build and install libstorage_handoff.so system-wide
#
# Usage:
#   ./install.sh             # installs to /usr/local (default prefix)
#   ./install.sh /opt/myapp  # installs to a custom prefix
#
# After running this script, any package can link the library with:
#   find_package(storage_handoff REQUIRED)
#   target_link_libraries(my_target PRIVATE storage_handoff::storage_handoff)
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_PREFIX="${1:-/usr/local}"
BUILD_DIR="${SCRIPT_DIR}/build_install"

echo "════════════════════════════════════════════"
echo "  storage_handoff — Build & Install"
echo "  Prefix: ${INSTALL_PREFIX}"
echo "════════════════════════════════════════════"

mkdir -p "${BUILD_DIR}"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo ""
echo "Installing to ${INSTALL_PREFIX} ..."
sudo cmake --install "${BUILD_DIR}"

# Refresh the dynamic linker cache so the .so is found at runtime
if command -v ldconfig &>/dev/null; then
    sudo ldconfig
    echo "✅ ldconfig refreshed."
fi

echo ""
echo "════════════════════════════════════════════"
echo "  Installation complete."
echo ""
echo "  Library:  ${INSTALL_PREFIX}/lib/libstorage_handoff.so"
echo "  Headers:  ${INSTALL_PREFIX}/include/storage_handoff/"
echo "  CMake:    ${INSTALL_PREFIX}/lib/cmake/storage_handoff/"
echo "════════════════════════════════════════════"
