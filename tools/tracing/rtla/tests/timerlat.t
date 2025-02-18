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
check "verify help page" \
	"timerlat --help"
check "verify -s/--stack" \
	"timerlat top -s 3 -T 10 -t"
check "verify -P/--priority" \
	"timerlat top -P F:1 -c 0 -d 1M -q"
check "test in nanoseconds" \
	"timerlat top -i 2 -c 0 -n -d 30s"
check "set the automatic trace mode" \
	"timerlat top -a 5 --dump-tasks"
check "print the auto-analysis if hits the stop tracing condition" \
	"timerlat top --aa-only 5"
check "disable auto-analysis" \
	"timerlat top -s 3 -T 10 -t --no-aa"
check "verify -c/--cpus" \
	"timerlat hist -c 0 -d 30s"
check "hist test in nanoseconds" \
	"timerlat hist -i 2 -c 0 -n -d 30s"
done

test_end
