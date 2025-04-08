#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m

check "verify help page" \
	"osnoise --help"
check "verify the --priority/-P param" \
	"osnoise top -P F:1 -c 0 -r 900000 -d 1M -q"
check "verify the --stop/-s param" \
	"osnoise top -s 30 -T 1 -t"
check "verify the  --trace param" \
	"osnoise hist -s 30 -T 1 -t"
check "verify the --entries/-E param" \
	"osnoise hist -P F:1 -c 0 -r 900000 -d 1M -b 10 -E 25"

# Test setting default period by putting an absurdly high period
# and stopping on threshold.
# If default period is not set, this will time out.
check_with_osnoise_options "apply default period" \
	"osnoise hist -s 1" period_us=600000000

test_end
