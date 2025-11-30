#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo -e "${YELLOW}=== Slang Common Test Suite ===${NC}"
echo "Project root: $PROJECT_ROOT"
echo ""

# Build tests using xmake
echo -e "${YELLOW}Building tests...${NC}"
cd "$PROJECT_ROOT"

if ! xmake build tests; then
    echo -e "${RED}Failed to build tests${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"
echo ""

# Run tests
echo -e "${YELLOW}Running tests...${NC}"
TEST_BINARY="$PROJECT_ROOT/build/linux/x86_64/release/tests"

if [ ! -f "$TEST_BINARY" ]; then
    # Try debug build
    TEST_BINARY="$PROJECT_ROOT/build/linux/x86_64/debug/tests"
fi

if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}Test binary not found${NC}"
    echo "Expected locations:"
    echo "  - $PROJECT_ROOT/build/linux/x86_64/release/tests"
    echo "  - $PROJECT_ROOT/build/linux/x86_64/debug/tests"
    exit 1
fi

# Run the tests with Catch2 options
if "$TEST_BINARY" "$@"; then
    echo ""
    echo -e "${GREEN}✓ All tests passed!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}✗ Tests failed!${NC}"
    exit 1
fi
