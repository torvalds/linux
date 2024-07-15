#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Basic tests on writing to trace_marker
# requires: trace_marker
# flags: instance

get_buffer_data_size() {
	sed -ne 's/^.*data.*size:\([0-9][0-9]*\).*/\1/p' events/header_page
}

get_buffer_data_offset() {
	sed -ne 's/^.*data.*offset:\([0-9][0-9]*\).*/\1/p' events/header_page
}

get_event_header_size() {
	type_len=`sed -ne 's/^.*type_len.*:[^0-9]*\([0-9][0-9]*\).*/\1/p' events/header_event`
	time_len=`sed -ne 's/^.*time_delta.*:[^0-9]*\([0-9][0-9]*\).*/\1/p' events/header_event`
	array_len=`sed -ne 's/^.*array.*:[^0-9]*\([0-9][0-9]*\).*/\1/p' events/header_event`
	total_bits=$((type_len+time_len+array_len))
	total_bits=$((total_bits+7))
	echo $((total_bits/8))
}

get_print_event_buf_offset() {
	sed -ne 's/^.*buf.*offset:\([0-9][0-9]*\).*/\1/p' events/ftrace/print/format
}

event_header_size=`get_event_header_size`
print_header_size=`get_print_event_buf_offset`

data_offset=`get_buffer_data_offset`

marker_meta=$((event_header_size+print_header_size))

make_str() {
        cnt=$1
	# subtract two for \n\0 as marker adds these
	cnt=$((cnt-2))
	printf -- 'X%.0s' $(seq $cnt)
}

write_buffer() {
	size=$1

	str=`make_str $size`

	# clear the buffer
	echo > trace

	# write the string into the marker
	echo -n $str > trace_marker

	echo $str
}

test_buffer() {

	size=`get_buffer_data_size`
	oneline_size=$((size-marker_meta))
	echo size = $size
	echo meta size = $marker_meta

	# Now add a little more the meta data overhead will overflow

	str=`write_buffer $size`

	# Make sure the line was broken
	new_str=`awk ' /tracing_mark_write:/ { sub(/^.*tracing_mark_write: /,"");printf "%s", $0; exit}' trace`

	if [ "$new_str" = "$str" ]; then
		exit fail;
	fi

	# Make sure the entire line can be found
	new_str=`awk ' /tracing_mark_write:/ { sub(/^.*tracing_mark_write: /,"");printf "%s", $0; }' trace`

	if [ "$new_str" != "$str" ]; then
		exit fail;
	fi
}

test_buffer
