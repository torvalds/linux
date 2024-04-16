#!/bin/sh

cd /sys/kernel/tracing

compare_file() {
	file="$1"
	val="$2"
	content=`cat $file`
	if [ "$content" != "$val" ]; then
		echo "FAILED: $file has '$content', expected '$val'"
		exit 1
	fi
}

compare_file_partial() {
	file="$1"
	val="$2"
	content=`cat $file | sed -ne "/^$val/p"`
	if [ -z "$content" ]; then
		echo "FAILED: $file does not contain '$val'"
		cat $file
		exit 1
	fi
}

file_contains() {
	file=$1
	val="$2"

	if ! grep -q "$val" $file ; then
		echo "FAILED: $file does not contain $val"
		cat $file
		exit 1
	fi
}

compare_mask() {
	file=$1
	val="$2"

	content=`cat $file | sed -ne "/^[0 ]*$val/p"`
	if [ -z "$content" ]; then
		echo "FAILED: $file does not have mask '$val'"
		cat $file
		exit 1
	fi
}

compare_file "events/task/task_newtask/filter" "pid < 128"
compare_file "events/task/task_newtask/enable" "1"

compare_file "events/kprobes/vfs_read/filter" "common_pid < 200"
compare_file "events/kprobes/vfs_read/enable" "1"

compare_file_partial "events/synthetic/initcall_latency/trigger" "hist:keys=func.sym,lat:vals=hitcount,lat:sort=lat"
compare_file_partial "events/synthetic/initcall_latency/enable" "0"

compare_file_partial "events/initcall/initcall_start/trigger" "hist:keys=func:vals=hitcount:ts0=common_timestamp.usecs"
compare_file_partial "events/initcall/initcall_start/enable" "1"

compare_file_partial "events/initcall/initcall_finish/trigger" 'hist:keys=func:vals=hitcount:lat=common_timestamp.usecs-\$ts0:sort=hitcount:size=2048:clock=global:onmatch(initcall.initcall_start).trace(initcall_latency,func,\$lat)'
compare_file_partial "events/initcall/initcall_finish/enable" "1"

compare_file "instances/foo/current_tracer" "function"
file_contains "instances/foo/set_ftrace_filter" "^user"
compare_file "instances/foo/buffer_size_kb" "512"
compare_mask "instances/foo/tracing_cpumask" "1"
compare_file "instances/foo/options/sym-addr" "0"
file_contains "instances/foo/trace_clock" '\[mono\]'
compare_file_partial "instances/foo/events/signal/signal_deliver/trigger" "snapshot"

compare_file "instances/bar/current_tracer" "function"
file_contains "instances/bar/set_ftrace_filter" "^kernel"
compare_mask "instances/bar/tracing_cpumask" "2"
file_contains "instances/bar/trace_clock" '\[x86-tsc\]'

file_contains "snapshot" "Snapshot is allocated"
compare_file "options/sym-addr" "1"
compare_file "events/initcall/enable" "1"
compare_file "buffer_size_kb" "1027"
compare_file "current_tracer" "function"
file_contains "set_ftrace_filter" '^vfs'

exit 0
