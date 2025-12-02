#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Output directory
OUTPUT_DIR="$SCRIPT_DIR/output"

# Create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Build the xmr-eliminate tool if needed
if [ ! -f "$PROJECT_ROOT/bin/xmr-eliminate" ]; then
    echo "Building xmr-eliminate tool..."
    cd "$PROJECT_ROOT"
    xmake build xmr-eliminate
    if [ $? -ne 0 ]; then
        echo "Failed to build xmr-eliminate"
        exit 1
    fi
fi

"$PROJECT_ROOT/bin/xmr-eliminate" \
    -t tb_top \
    "$SCRIPT_DIR/dut.sv" \
    "$SCRIPT_DIR/others.sv" \
    "$SCRIPT_DIR/tb_top.sv" \
    --output "$OUTPUT_DIR"

if [ $? -ne 0 ]; then
    echo "✗ XMR elimination failed!"
    exit 1
fi

echo ""
echo "✓ XMR elimination completed successfully!"

