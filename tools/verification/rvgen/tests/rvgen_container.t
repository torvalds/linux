#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source ../tests/engine.sh
test_begin

set_timeout 30s

# Help tests
check "verify container subcommand help" \
	"$RVGEN container -h" 0 "model_name" "class"

check_and_compare_folder "container with description" \
	"$RVGEN container -n test_container -D 'Test container for grouping monitors'" \
	"test_container" "Writing the monitor into the directory test_container"

# Error handling tests
check "missing required model_name" \
	"$RVGEN container" 2 "the following arguments are required: -n/--model_name"

test_end
