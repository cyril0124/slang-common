#!/bin/bash
# Deep Hierarchy XMR Elimination Example
# This script runs XMR elimination and compares output with golden file

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Output directory
OUTPUT_DIR="$SCRIPT_DIR/output"
GOLDEN_FILE="$SCRIPT_DIR/design.golden.sv"

echo "=== Deep Hierarchy XMR Elimination Example ==="
echo "Project root: $PROJECT_ROOT"
echo "Output directory: $OUTPUT_DIR"
echo ""

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

# Run XMR elimination
echo "Running XMR elimination on design.sv..."
"$PROJECT_ROOT/bin/xmr-eliminate" \
    "$SCRIPT_DIR/design.sv" \
    --output "$OUTPUT_DIR" \
    --module top,mid_module

if [ $? -ne 0 ]; then
    echo "✗ XMR elimination failed!"
    exit 1
fi

echo ""
echo "✓ XMR elimination completed successfully!"

# Compare with golden file
if [ -f "$GOLDEN_FILE" ]; then
    echo ""
    echo "=== Comparing output with golden file ==="
    if diff -q "$GOLDEN_FILE" "$OUTPUT_DIR/design.sv" > /dev/null 2>&1; then
        echo "✓ Output matches golden file!"
    else
        echo "✗ Output differs from golden file:"
        echo ""
        diff "$GOLDEN_FILE" "$OUTPUT_DIR/design.sv" || true
        exit 1
    fi
else
    echo ""
    echo "Warning: Golden file not found at $GOLDEN_FILE"
    echo "Showing output file content:"
    cat "$OUTPUT_DIR/design.sv"
fi

