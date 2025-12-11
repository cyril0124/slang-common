#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
EXAMPLES_DIR="$PROJECT_ROOT/examples"

export CHECK_OUTPUT=1

echo -e "${YELLOW}=== Slang Common Test Suite ===${NC}"
echo "Project root: $PROJECT_ROOT"
echo ""

# Track overall results
UNIT_TESTS_PASSED=0
EXAMPLES_PASSED=0
EXAMPLES_TOTAL=0

# Build tests using xmake
echo -e "${YELLOW}Building tests...${NC}"
cd "$PROJECT_ROOT"

if ! xmake build tests; then
    echo -e "${RED}Failed to build tests${NC}"
    exit 1
fi

echo -e "${GREEN}Build successful!${NC}"
echo ""

# Run unit tests
echo -e "${YELLOW}Running unit tests...${NC}"
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

# Run the unit tests with Catch2 options
if "$TEST_BINARY" "$@"; then
    echo ""
    echo -e "${GREEN}✓ All unit tests passed!${NC}"
    UNIT_TESTS_PASSED=1
else
    echo ""
    echo -e "${RED}✗ Unit tests failed!${NC}"
fi

echo ""
echo -e "${YELLOW}=== Running Example Tests ===${NC}"
echo ""

# Run all examples
if [ -d "$EXAMPLES_DIR" ]; then
    for example_dir in "$EXAMPLES_DIR"/*/; do
        if [ -f "$example_dir/run.sh" ]; then
            example_name=$(basename "$example_dir")
            EXAMPLES_TOTAL=$((EXAMPLES_TOTAL + 1))
            
            echo -e "${YELLOW}--- Running example: $example_name ---${NC}"
            
            cd "$example_dir"
            if bash run.sh; then
                echo -e "${GREEN}✓ Example '$example_name' passed!${NC}"
                EXAMPLES_PASSED=$((EXAMPLES_PASSED + 1))
            else
                echo -e "${RED}✗ Example '$example_name' failed!${NC}"
            fi
            echo ""
        fi
    done
else
    echo -e "${YELLOW}No examples directory found${NC}"
fi

# Summary
echo -e "${YELLOW}=== Test Summary ===${NC}"
echo ""

if [ $UNIT_TESTS_PASSED -eq 1 ]; then
    echo -e "${GREEN}✓ Unit tests: PASSED${NC}"
else
    echo -e "${RED}✗ Unit tests: FAILED${NC}"
fi

echo -e "  Examples: $EXAMPLES_PASSED/$EXAMPLES_TOTAL passed"

if [ $UNIT_TESTS_PASSED -eq 1 ] && [ $EXAMPLES_PASSED -eq $EXAMPLES_TOTAL ]; then
    echo ""
    echo -e "${GREEN}✓ All tests and examples passed!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}✗ Some tests or examples failed!${NC}"
    exit 1
fi
