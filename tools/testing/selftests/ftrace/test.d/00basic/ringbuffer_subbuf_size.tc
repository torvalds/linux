#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Change the ringbuffer sub-buffer size
# requires: buffer_subbuf_size_kb
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
	printf -- 'X%.0s' $(seq $cnt)
}

write_buffer() {
	size=$1

	str=`make_str $size`

	# clear the buffer
	echo > trace

	# write the string into the marker
	echo $str > trace_marker

	echo $str
}

test_buffer() {
	size_kb=$1
	page_size=$((size_kb*1024))

	size=`get_buffer_data_size`

	# the size must be greater than or equal to page_size - data_offset
	page_size=$((page_size-data_offset))
	if [ $size -lt $page_size ]; then
		exit fail
	fi

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

ORIG=`cat buffer_subbuf_size_kb`

# Could test bigger sizes than 32K, but then creating the string
# to write into the ring buffer takes too long
for a in 4 8 16 32 ; do
	echo $a > buffer_subbuf_size_kb
	test_buffer $a
done

echo $ORIG > buffer_subbuf_size_kb

