#!/bin/bash
# SVA Formal Verification Example - XMR Elimination
#
# This example demonstrates XMR elimination for formal verification testbenches
# where SVA assertions use hierarchical references to access DUT internal signals.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Output directory
OUTPUT_DIR="$SCRIPT_DIR/output"
GOLDEN_FILE="$SCRIPT_DIR/design.golden.sv"

echo "=== SVA Formal Verification XMR Elimination Example ==="
echo "Project root: $PROJECT_ROOT"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Clean previous output
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Build xmr-eliminate if needed
if [ ! -f "$PROJECT_ROOT/bin/xmr-eliminate" ]; then
    echo "Building xmr-eliminate..."
    cd "$PROJECT_ROOT"
    xmake build xmr-eliminate
fi

echo "Running XMR elimination on formal testbench..."
"$PROJECT_ROOT/bin/xmr-eliminate" \
    "$SCRIPT_DIR/design.sv" \
    -o "$OUTPUT_DIR" \
    -m tb_formal \
    --verbose

echo ""
echo "✓ XMR elimination completed successfully!"
echo ""
echo "Generated files:"
ls -la "$OUTPUT_DIR"
echo ""
echo "Note: All XMR references in SVA assertions have been converted to"
echo "explicit port connections, making the design compatible with formal"
echo "verification tools."

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
