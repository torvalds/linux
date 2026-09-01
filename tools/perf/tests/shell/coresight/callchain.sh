#!/bin/bash
# CoreSight synthesized callchain (exclusive)
# SPDX-License-Identifier: GPL-2.0

glb_err=1

if ! tmpdir=$(mktemp -d /tmp/perf-cs-callchain-test.XXXXXX); then
	echo "mktemp failed"
	exit 1
fi

cleanup_files()
{
	rm -rf "$tmpdir"
}

trap cleanup_files EXIT
trap 'cleanup_files; exit $glb_err' TERM INT

skip_if_system_is_not_ready()
{
	perf list | grep -Pzq 'cs_etm//' || {
		echo "[Skip] cs_etm event is not available" >&2
		return 2
	}

	# Requires root for trace in kernel
	[ "$(id -u)" = 0 ] || {
		echo "[Skip] No root permission" >&2
		return 2
	}

	return 0
}

record_trace()
{
	local data=$1
	local script=$2

	local cf="$tmpdir/ctl"
	local af="$tmpdir/ack"

	mkfifo "$cf" "$af"

	perf record -o "$data" -e cs_etm// --per-thread -D -1 --control fifo:"$cf","$af" -- \
		perf test --record-ctl fifo:"$cf","$af" -w callchain >/dev/null 2>&1 &&

	# It is safe to use 'i3i' with a three-instruction interval, since the
	# workload is compiled with -O0.
	perf script --itrace=g16i3il64 -i "$data" > "$script"
}

callchain_regex_1()
{
	printf '%s' \
'perf[[:space:]]+[0-9]+[[:space:]]+\[[0-9]+\][[:space:]]+([0-9.]+:[[:space:]]+)?[0-9]+ instructions:[[:space:]]*\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_foo\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'([[:space:]]+[[:xdigit:]]+ .*\n)*'
}

callchain_regex_2()
{
	printf '%s' \
'perf[[:space:]]+[0-9]+[[:space:]]+\[[0-9]+\][[:space:]]+([0-9.]+:[[:space:]]+)?[0-9]+ instructions:[[:space:]]*\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_do_syscall\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_foo\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'([[:space:]]+[[:xdigit:]]+ .*\n)*'
}

callchain_regex_3()
{
	printf '%s' \
'perf[[:space:]]+[0-9]+[[:space:]]+\[[0-9]+\][[:space:]]+([0-9.]+:[[:space:]]+)?[0-9]+ instructions:[[:space:]]*\n'\
'[[:space:]]+[[:xdigit:]]+ syscall(@plt)?\+0x[[:xdigit:]]+ \(.*\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_do_syscall\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_foo\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'([[:space:]]+[[:xdigit:]]+ .*\n)*'
}

callchain_regex_4()
{
	printf '%s' \
'perf[[:space:]]+[0-9]+[[:space:]]+\[[0-9]+\][[:space:]]+([0-9.]+:[[:space:]]+)?[0-9]+ instructions:[[:space:]]*\n'\
'[[:space:]]+[[:xdigit:]]+ .*\+0x[[:xdigit:]]+ \(\[kernel\.kallsyms\]\)\n'\
'[[:space:]]+[[:xdigit:]]+ syscall(@plt)?\+0x[[:xdigit:]]+ \(.*\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_do_syscall\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain_foo\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'[[:space:]]+[[:xdigit:]]+ callchain\+0x[[:xdigit:]]+ \(.*/perf\)\n'\
'([[:space:]]+[[:xdigit:]]+ .*\n)*'
}

find_after_line()
{
	local regex="$1"
	local file="$2"
	local start="$3"
	local offset
	local line

	# Search in byte offset
	offset=$(
		tail -n +"$start" "$file" |
		grep -Pzob -m1 "$regex" |
		tr '\0' '\n' |
		sed -n 's/^\([0-9][0-9]*\):.*/\1/p;q'
	)

	if [ -z "$offset" ]; then
		echo "Failed to match regex after line $start" >&2
		echo "Regex:" >&2
		printf '%s\n' "$regex" >&2
		echo "Context from line $start:" >&2
		sed -n "${start},$((start + 100))p" "$file" >&2
		return 1
	fi

	# Convert from offset to line
	line=$(
		tail -n +"$start" "$file" |
		head -c "$offset" |
		wc -l
	)

	echo "$((start + line))"
}

check_callchain_flow()
{
	local file="$1"
	local l1 l2 l3 l4 l5 l6 l7

	# Callchain push
	l1=$(find_after_line "$(callchain_regex_1)" "$file" 1) || return 1
	l2=$(find_after_line "$(callchain_regex_2)" "$file" "$((l1 + 1))") || return 1
	l3=$(find_after_line "$(callchain_regex_3)" "$file" "$((l2 + 1))") || return 1
	l4=$(find_after_line "$(callchain_regex_4)" "$file" "$((l3 + 1))") || return 1

	# Callchain pop
	l5=$(find_after_line "$(callchain_regex_3)" "$file" "$((l4 + 1))") || return 1
	l6=$(find_after_line "$(callchain_regex_2)" "$file" "$((l5 + 1))") || return 1
	l7=$(find_after_line "$(callchain_regex_1)" "$file" "$((l6 + 1))") || return 1

	echo "Callchain flow matched:"
	echo "  l1=$l1 l2=$l2 l3=$l3 l4=$l4 l5=$l5 l6=$l6 l7=$l7"

	return 0
}

run_test()
{
	local data=$tmpdir/perf.data
	local script=$tmpdir/perf.script

	if ! record_trace "$data" "$script"; then
		echo "perf record/script failed"
		return
	fi

	check_callchain_flow "$script" || return

	glb_err=0
}

skip_if_system_is_not_ready || exit 2

run_test

exit $glb_err
