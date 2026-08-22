#!/bin/bash
# Zstd perf.data compression/decompression of multi-record data
# SPDX-License-Identifier: GPL-2.0

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
recout=$(mktemp /tmp/__perf_test.zstd.rec.XXXXX)
injout=$(mktemp /tmp/__perf_test.zstd.inj.XXXXX)
perf_tool=perf

cleanup() {
	rm -f "${perfdata}" "${perfdata}".old "${perfdata}".decomp "${recout}" "${injout}"
}
trap cleanup EXIT TERM INT

skip_if_no_z_record() {
	$perf_tool record -h 2>&1 | grep -q -- '-z, --compression-level'
}

collect_z_record() {
	echo "Collecting compressed record file:"
	[ "$(uname -m)" != s390x ] && gflag='-g'
	$perf_tool record -o "${perfdata}" $gflag -z -F max -m 32M --per-thread -- \
		$perf_tool test -w thloop 5 1 \
		>/dev/null 2>"${recout}"
}

check_record() {
	echo "Checking record did not fail to write data:"
	if grep -q "failed to write perf data" "${recout}"; then
		cat "${recout}"
		return 1
	fi
}

check_decompress() {
	echo "Checking compressed file decompresses cleanly:"
	if ! $perf_tool inject -i "${perfdata}" -o "${perfdata}".decomp 2>"${injout}"; then
		cat "${injout}"
		return 1
	fi
	if grep -Eqi "decompress|corrupt|failed to process type" "${injout}"; then
		cat "${injout}"
		return 1
	fi
}

skip_if_no_z_record || exit 2
collect_z_record
check_record || exit 1

# Need >1 record, else the multi-record path wasn't exercised.
# Skip rather than pass/fail spuriously.
nr=$($perf_tool report -i "${perfdata}" --stats 2>/dev/null |
	awk '/COMPRESSED2 events:/ { print $3 }')
if [ -z "${nr}" ] || [ "${nr}" -lt 2 ]; then
	echo "less than two compressed records (${nr:-0}), skipping"
	exit 2
fi
echo "Produced ${nr} compressed records"

check_decompress
err=$?
exit $err
