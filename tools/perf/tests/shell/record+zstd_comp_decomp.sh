#!/bin/sh
# Zstd perf.data compression/decompression

trace_file=$(mktemp /tmp/perf.data.XXX)
perf_tool=perf
output=/dev/null

skip_if_no_z_record() {
	$perf_tool record -h 2>&1 | grep '\-z, \-\-compression\-level'
}

collect_z_record() {
	echo "Collecting compressed record file:"
	$perf_tool record -o $trace_file -g -z -F 5000 -- \
		dd count=500 if=/dev/random of=/dev/null > $output 2>&1
}

check_compressed_stats() {
	echo "Checking compressed events stats:"
	$perf_tool report -i $trace_file --header --stats | \
		grep -E "(# compressed : Zstd,)|(COMPRESSED events:)" > $output 2>&1
}

check_compressed_output() {
	$perf_tool inject -i $trace_file -o $trace_file.decomp &&
	$perf_tool report -i $trace_file --stdio | head -n -3 > $trace_file.comp.output &&
	$perf_tool report -i $trace_file.decomp --stdio | head -n -3 > $trace_file.decomp.output &&
	diff $trace_file.comp.output $trace_file.decomp.output > $output 2>&1
}

skip_if_no_z_record || exit 2
collect_z_record && check_compressed_stats && check_compressed_output
err=$?
rm -f $trace_file*
exit $err
