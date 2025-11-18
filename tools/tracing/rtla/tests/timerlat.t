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
	"timerlat --help" 0 "timerlat version"
check "verify -s/--stack" \
	"timerlat top -s 3 -T 10 -t" 2 "Blocking thread stack trace"
check "verify -P/--priority" \
	"timerlat top -P F:1 -c 0 -d 10s -q -T 1 --on-threshold shell,command=\"tests/scripts/check-priority.sh timerlatu/ SCHED_FIFO 1\"" \
	2 "Priorities are set correctly"
check "test in nanoseconds" \
	"timerlat top -i 2 -c 0 -n -d 10s" 2 "ns"
check "set the automatic trace mode" \
	"timerlat top -a 5" 2 "analyzing it"
check "dump tasks" \
	"timerlat top -a 5 --dump-tasks" 2 "Printing CPU tasks"
check "print the auto-analysis if hits the stop tracing condition" \
	"timerlat top --aa-only 5" 2
check "disable auto-analysis" \
	"timerlat top -s 3 -T 10 -t --no-aa" 2
check "verify -c/--cpus" \
	"timerlat hist -c 0 -d 10s"
check "hist test in nanoseconds" \
	"timerlat hist -i 2 -c 0 -n -d 10s" 2 "ns"

# Actions tests
check "trace output through -t" \
	"timerlat hist -T 2 -t" 2 "^  Saving trace to timerlat_trace.txt$"
check "trace output through -t with custom filename" \
	"timerlat hist -T 2 -t custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check "trace output through --on-threshold trace" \
	"timerlat hist -T 2 --on-threshold trace" 2 "^  Saving trace to timerlat_trace.txt$"
check "trace output through --on-threshold trace with custom filename" \
	"timerlat hist -T 2 --on-threshold trace,file=custom_filename.txt" 2 "^  Saving trace to custom_filename.txt$"
check "exec command" \
	"timerlat hist -T 2 --on-threshold shell,command='echo TestOutput'" 2 "^TestOutput$"
check "multiple actions" \
	"timerlat hist -T 2 --on-threshold shell,command='echo -n 1' --on-threshold shell,command='echo 2'" 2 "^12$"
check "hist stop at failed action" \
	"timerlat hist -T 2 --on-threshold shell,command='echo -n 1; false' --on-threshold shell,command='echo -n 2'" 2 "^1# RTLA timerlat histogram$"
check "top stop at failed action" \
	"timerlat top -T 2 --on-threshold shell,command='echo -n 1; false' --on-threshold shell,command='echo -n 2'" 2 "^1ALL"
check "hist with continue" \
	"timerlat hist -T 2 -d 1s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check "top with continue" \
	"timerlat top -q -T 2 -d 1s --on-threshold shell,command='echo TestOutput' --on-threshold continue" 0 "^TestOutput$"
check "hist with trace output at end" \
	"timerlat hist -d 1s --on-end trace" 0 "^  Saving trace to timerlat_trace.txt$"
check "top with trace output at end" \
	"timerlat top -d 1s --on-end trace" 0 "^  Saving trace to timerlat_trace.txt$"
done

test_end
