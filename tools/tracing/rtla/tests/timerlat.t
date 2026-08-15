#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m
timerlat_sample_event='/sys/kernel/tracing/events/osnoise/timerlat_sample'

if ldd $RTLA | grep libbpf >/dev/null && [ -d "$timerlat_sample_event" ]
then
	# rtla build with BPF and system supports BPF mode
	no_bpf_options='0 1'
else
	no_bpf_options='1'
fi

# Do every test with and without BPF
for option in $no_bpf_options
do
export RTLA_NO_BPF=$option

# Basic tests
check "verify help page" \
	"timerlat --help" 129 "timerlat version"
check_top_hist "verify help page" \
	"timerlat TOOL --help" 129 "rtla timerlat"
check_top_hist "verify -s/--stack" \
	"timerlat TOOL -s 3 -T 10 -t" 2 "Blocking thread stack trace"
check_top_hist "test in nanoseconds" \
	"timerlat TOOL -i 2 -c 0 -n -d 10s" 2 "ns"
check_top_hist "set the automatic trace mode" \
	"timerlat TOOL -a 5" 2 "analyzing it"
check_top_hist "dump tasks" \
	"timerlat TOOL -a 5 --dump-tasks" 2 "Printing CPU tasks"
check "verify --aa-only stop on threshold" \
	"timerlat top --aa-only 5" 2 "analyzing it" "Timer Latency"
check "verify --aa-only max latency" \
	"timerlat top --aa-only 2000000 -d 1s" 0 "^  Max latency was" "Timer Latency"
check_top_hist "disable auto-analysis" \
	"timerlat TOOL -s 3 -T 10 -t --no-aa" 2 "" "analyzing it"

# Thread tests
check_top_hist "verify -P/--priority" \
	"timerlat TOOL -P F:1 -c 0 -d 10s -T 1 --on-threshold shell,command=\"$testdir/scripts/check-priority.sh SCHED_FIFO 1\"" \
	2 "Priorities are set correctly"
check_top_hist "verify -C/--cgroup" \
	"timerlat TOOL -k -C -c 0 -d 10s -T 1 --on-threshold shell,command=\"$testdir/scripts/check-cgroup-match.sh\"" \
	2 "cgroup matches for all workload PIDs"
check_top_q_hist "verify -c/--cpus" \
	"timerlat TOOL -c 0 -d 10s -T 1 --on-threshold shell,command=$testdir/scripts/check-cpus.sh" 2 "^Affinity of threads: 0$"
check_top_q_hist "verify -H/--house-keeping" \
	"timerlat TOOL -H 0 -d 10s -T 1 --on-threshold shell,command=$testdir/scripts/check-housekeeping-cpus.sh" 2 "^Affinity of threads: 0$"
check_top_q_hist "verify -k/--kernel-threads" \
	"timerlat TOOL -k -c 0 -d 10s -T 1 --on-threshold shell,command=$testdir/scripts/check-user-kernel-threads.sh" 2 "1 kernel threads, 0 user threads"
check_top_q_hist "verify -u/--user-threads" \
	"timerlat TOOL -u -c 0 -d 10s -T 1 --on-threshold shell,command=$testdir/scripts/check-user-kernel-threads.sh" 2 "0 kernel threads, 1 user threads"

# Histogram tests
check "hist with -b/--bucket-size" \
	"timerlat hist -b 1 -d 1s"
check "hist with -E/--entries" \
	"timerlat hist -E 10 -d 1s"
check "hist with -E/--entries out of range" \
	"timerlat hist -E 1 -d 1s" 1 "^Entries must be > 10 and < 10000000$"
check "hist with --no-header" \
	"timerlat hist --no-header -d 1s" 0 "" "RTLA timerlat histogram"
check "hist with --with-zeros" \
	"timerlat hist --with-zeros -b 100000 -E 21 -d 1s" 0 '^2000000\s+0\s+'
check "hist with --no-index" \
	"timerlat hist --no-index --with-zeros -d 1s" 0 "" "^count:"
check "hist with --no-summary" \
	"timerlat hist --no-summary -d 1s" 0 "" "^ALL:"
check "hist with --no-irq" \
	"timerlat hist --no-irq -d 1s" 0 "" "IRQ-"
check "hist with --no-thread" \
	"timerlat hist --no-thread -d 1s" 0 "" "Thr-"

# Actions tests
check_top_q_hist "trace output through -t" \
	"timerlat TOOL -T 2 -t" 2 "^  Saving trace to timerlat_trace.txt$"
check_top_q_hist "trace output through -t with custom filename" \
	"timerlat TOOL -T 2 -t custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check_top_q_hist "trace output through --on-threshold trace" \
	"timerlat TOOL -T 2 --on-threshold trace" 2 "^  Saving trace to timerlat_trace.txt$"
check_top_q_hist "trace output through --on-threshold trace with custom filename" \
	"timerlat TOOL -T 2 --on-threshold trace,file=custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check_top_q_hist "exec command" \
	"timerlat TOOL -T 2 --on-threshold shell,command='echo TestOutput'" 2 "^TestOutput$"
check_top_q_hist "multiple actions" \
	"timerlat TOOL -T 2 --on-threshold shell,command='echo -n 1' --on-threshold shell,command='echo 2'" 2 "^12$"
check "hist stop at failed action" \
	"timerlat hist -T 2 --on-threshold shell,command='echo -n 1; false' --on-threshold shell,command='echo -n 2'" 2 "^1# RTLA timerlat histogram$"
check "top stop at failed action" \
	"timerlat top -T 2 --on-threshold shell,command='echo -n abc; false' --on-threshold shell,command='echo -n defgh'" 2 "^abc" "defgh"
check_top_q_hist "with continue" \
	"timerlat TOOL -T 2 -d 5s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check_top_q_hist "with conditional continue" \
	"timerlat TOOL -T 2 --on-threshold shell,command='if [ -f a ]; then echo 2; exit 1; else echo -n 1; touch a; fi' --on-threshold continue" 2 "^12$" "^2$"
check_top_hist "with trace output at end" \
	"timerlat TOOL -d 1s --on-end trace" 0 "^  Saving trace to timerlat_trace.txt$"

# BPF action program tests
if [ "$option" -eq 0 ]
then
	# Test BPF action program properly in BPF mode
	[ -z "$BPFTOOL" ] && BPFTOOL=bpftool
	check_top_q_hist "with BPF action program (BPF mode)" \
		"timerlat TOOL -T 2 --bpf-action $testdir/bpf/bpf_action_map.o --on-threshold shell,command='$BPFTOOL map dump name rtla_test_map'" \
		2 '"value": 42'
else
	# Test BPF action program failure in non-BPF mode
	check_top_q_hist "with BPF action program (non-BPF mode)" \
		"timerlat TOOL -T 2 --bpf-action $testdir/bpf/bpf_action_map.o" \
		1 "BPF actions are not supported in tracefs-only mode"
fi
done

test_end
