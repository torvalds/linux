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


compare_file "tracing_on" "0"
compare_file "current_tracer" "function_graph"

compare_file_partial "events/kprobes/start_event/enable" "1"
compare_file_partial "events/kprobes/start_event/trigger" "traceon"
file_contains "kprobe_events" 'start_event.*pci_proc_init'

compare_file_partial "events/kprobes/end_event/enable" "1"
compare_file_partial "events/kprobes/end_event/trigger" "traceoff"
file_contains "kprobe_events" '^r.*end_event.*pci_proc_init'

exit 0
