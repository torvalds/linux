#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m

check "verify help page" \
	"osnoise --help" 0 "osnoise version"
check "verify the --priority/-P param" \
	"osnoise top -P F:1 -c 0 -r 900000 -d 10s -q -S 1 --on-threshold shell,command=\"tests/scripts/check-priority.sh osnoise/ SCHED_FIFO 1\"" \
	2 "Priorities are set correctly"
check "verify the --stop/-s param" \
	"osnoise top -s 30 -T 1" 2 "osnoise hit stop tracing"
check "verify the  --trace param" \
	"osnoise hist -s 30 -T 1 -t" 2 "Saving trace to osnoise_trace.txt"
check "verify the --entries/-E param" \
	"osnoise hist -P F:1 -c 0 -r 900000 -d 10s -b 10 -E 25"

# Test setting default period by putting an absurdly high period
# and stopping on threshold.
# If default period is not set, this will time out.
check_with_osnoise_options "apply default period" \
	"osnoise hist -s 1" 2 period_us=600000000

# Actions tests
check "trace output through -t with custom filename" \
	"osnoise hist -S 2 -t custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check "trace output through --on-threshold trace" \
	"osnoise hist -S 2 --on-threshold trace" 2 "^  Saving trace to osnoise_trace.txt$"
check "trace output through --on-threshold trace with custom filename" \
	"osnoise hist -S 2 --on-threshold trace,file=custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check "exec command" \
	"osnoise hist -S 2 --on-threshold shell,command='echo TestOutput'" 2 "^TestOutput$"
check "multiple actions" \
	"osnoise hist -S 2 --on-threshold shell,command='echo -n 1' --on-threshold shell,command='echo 2'" 2 "^12$"
check "hist stop at failed action" \
	"osnoise hist -S 2 --on-threshold shell,command='echo -n 1; false' --on-threshold shell,command='echo -n 2'" 2 "^1# RTLA osnoise histogram$"
check "top stop at failed action" \
	"timerlat top -T 2 --on-threshold shell,command='echo -n abc; false' --on-threshold shell,command='echo -n defgh'" 2 "^abc" "defgh"
check "hist with continue" \
	"osnoise hist -S 2 -d 1s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check "top with continue" \
	"osnoise top -q -S 2 -d 1s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check "hist with trace output at end" \
	"osnoise hist -d 1s --on-end trace" 0 "^  Saving trace to osnoise_trace.txt$"
check "top with trace output at end" \
	"osnoise top -d 1s --on-end trace" 0 "^  Saving trace to osnoise_trace.txt$"

test_end
