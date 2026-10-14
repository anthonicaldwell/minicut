#!/usr/bin/env bash
# ==============================================================================
# minicut Reinstaller Script
# Builds the latest source and reinstalls in place without disrupting
# desktop favorites or system configuration.
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "Rebuilding minicut..."
echo "=========================================="

cmake -B build -S .
cmake --build build -j"$(nproc)"

echo "=========================================="
echo "Reinstalling minicut in place..."
echo "=========================================="

./install.sh "$@"

echo "=========================================="
echo "minicut successfully reinstalled!"
echo "=========================================="
