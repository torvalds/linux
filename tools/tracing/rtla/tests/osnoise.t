#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m

check "verify help page" \
	"osnoise --help" 129 "osnoise version"
check_top_hist "verify help page" \
	"osnoise TOOL --help" 129 "rtla osnoise"
check_top_q_hist "verify the --stop/-s param" \
	"osnoise TOOL -s 30 -T 1" 2 "osnoise hit stop tracing"
check_top_q_hist "verify the --trace param" \
	"osnoise TOOL -s 30 -T 1 -t" 2 "Saving trace to osnoise_trace.txt"

# Thread tests
check_top_q_hist "verify the --priority/-P param" \
	"osnoise TOOL -P F:1 -c 0 -r 900000 -d 10s -S 1 --on-threshold shell,command=\"$testdir/scripts/check-priority.sh SCHED_FIFO 1\"" \
	2 "Priorities are set correctly"
check_top_q_hist "verify the -C/--cgroup param" \
	"osnoise TOOL -C -c 0 -r 900000 -d 10s -S 1 --on-threshold shell,command=\"$testdir/scripts/check-cgroup-match.sh\"" \
	2 "cgroup matches for all workload PIDs"
check_top_q_hist "verify the -c/--cpus param" \
	"osnoise TOOL -P F:1 -c 0 -r 900000 -d 10s -S 1 --on-threshold shell,command=$testdir/scripts/check-cpus.sh" 2 "^Affinity of threads: 0$"
check_top_q_hist "verify the -H/--house-keeping param" \
	"osnoise TOOL -P F:1 -H 0 -r 900000 -d 10s -S 1 --on-threshold shell,command=$testdir/scripts/check-housekeeping-cpus.sh" 2 "^Affinity of threads: 0$"

# Histogram tests
check "hist with -b/--bucket-size" \
	"osnoise hist -b 1 -d 1s"
check "hist with -E/--entries" \
	"osnoise hist -E 10 -d 1s"
check "hist with -E/--entries out of range" \
	"osnoise hist -E 1 -d 1s" 1 "^Entries must be > 10 and < 10000000$"
check "hist with --no-header" \
	"osnoise hist --no-header -d 1s" 0 "" "RTLA osnoise histogram"
check "hist with --with-zeros" \
	"osnoise hist --with-zeros -b 100000 -E 21 -d 1s" 0 '^2000000\s+0\s+'
check "hist with --no-index" \
	"osnoise hist --no-index --with-zeros -d 1s" 0 "" "^count:"
check "hist with --no-summary" \
	"osnoise hist --no-summary -d 1s" 0 "" "^count:"

# Test setting default period by putting an absurdly high period
# and stopping on threshold.
# If default period is not set, this will time out.
check_with_osnoise_options "apply default period" \
	"osnoise hist -s 1" 2 period_us=600000000

# Actions tests
check_top_q_hist "trace output through -t with custom filename" \
	"osnoise TOOL -S 2 -t custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check_top_q_hist "trace output through --on-threshold trace" \
	"osnoise TOOL -S 2 --on-threshold trace" 2 "^  Saving trace to osnoise_trace.txt$"
check_top_q_hist "trace output through --on-threshold trace with custom filename" \
	"osnoise TOOL -S 2 --on-threshold trace,file=custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check_top_q_hist "exec command" \
	"osnoise TOOL -S 2 --on-threshold shell,command='echo TestOutput'" 2 "^TestOutput$"
check_top_q_hist "multiple actions" \
	"osnoise TOOL -S 2 --on-threshold shell,command='echo -n 1' --on-threshold shell,command='echo 2'" 2 "^12$"
check "hist stop at failed action" \
	"osnoise hist -S 2 --on-threshold shell,command='echo -n 1; false' --on-threshold shell,command='echo -n 2'" 2 "^1# RTLA osnoise histogram$"
check "top stop at failed action" \
	"osnoise top -S 2 --on-threshold shell,command='echo -n abc; false' --on-threshold shell,command='echo -n defgh'" 2 "^abc" "defgh"
check_top_q_hist "with continue" \
	"osnoise TOOL -S 2 -d 5s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check_top_q_hist "with conditional continue" \
	"osnoise TOOL -S 2 --on-threshold shell,command='if [ -f a ]; then echo 2; exit 1; else echo -n 1; touch a; fi' --on-threshold continue" 2 "^12$" "^2$"
check_top_hist "with trace output at end" \
	"osnoise TOOL -d 1s --on-end trace" 0 "^  Saving trace to osnoise_trace.txt$"

test_end
