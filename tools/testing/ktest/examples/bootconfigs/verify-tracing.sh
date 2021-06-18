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

compare_file "current_tracer" "function_graph"
compare_file "options/event-fork" "1"
compare_file "options/sym-addr" "1"
compare_file "options/stacktrace" "1"
compare_file "buffer_size_kb" "1024"
file_contains "snapshot" "Snapshot is allocated"
file_contains "trace_clock" '\[global\]'

compare_file "events/initcall/enable" "1"
compare_file "events/task/task_newtask/enable" "1"
compare_file "events/sched/sched_process_exec/filter" "pid < 128"
compare_file "events/kprobes/enable" "1"

compare_file "instances/bar/events/kprobes/myevent/enable" "1"
compare_file "instances/bar/events/kprobes/myevent2/enable" "1"
compare_file "instances/bar/events/kprobes/myevent3/enable" "1"

compare_file "instances/foo/current_tracer" "function"
compare_file "instances/foo/tracing_on" "0"

compare_file "/proc/sys/kernel/ftrace_dump_on_oops" "2"
compare_file "/proc/sys/kernel/traceoff_on_warning" "1"

exit 0
