#!/bin/bash
# Test that perf report includes file offsets and event type names in diagnostic messages.
# SPDX-License-Identifier: GPL-2.0
#
# The file_offset diagnostic series adds "at offset 0x...: TYPE (N)"
# to all skip/stop/error messages.  This test corrupts an event's size
# field in a perf.data file, then verifies the resulting warning
# includes the file offset and event type.

err=0

cleanup() {
	[ -n "${perfdata}" ] && rm -f "${perfdata}" "${perfdata}.old"
	rm -f "${corrupted}" "${stderrfile}"
	trap - EXIT TERM INT
}
trap 'cleanup; exit 1' TERM INT
trap cleanup EXIT

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2
corrupted=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2
stderrfile=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2

if ! perf record -o "${perfdata}" -- perf test -w noploop 2>/dev/null; then
	echo "Skip: perf record failed"
	cleanup
	exit 2
fi

# Find the file offset of the first MMAP2 event via perf report -D.
# Format: "timestamp 0xOFFSET [0xSIZE]: PERF_RECORD_MMAP2 ..."
mmap2_line=$(perf report -D -i "${perfdata}" 2>/dev/null | grep "PERF_RECORD_MMAP2" | head -1)
if [ -z "${mmap2_line}" ]; then
	echo "Skip: no MMAP2 events found in perf.data"
	cleanup
	exit 2
fi

mmap2_offset=$(echo "${mmap2_line}" | awk '{print $2}')
mmap2_offset_dec=$((mmap2_offset))

# Copy the file and corrupt the MMAP2 event's size field.
# perf_event_header layout: type(u32) misc(u16) size(u16)
# Set size to 16 — below the MMAP2 minimum, which triggers
# the "event size too small" warning.
#
# perf.data uses native byte order, so write the u16 in the
# host's endianness to work on both LE and BE architectures.
cp "${perfdata}" "${corrupted}"
if printf '\x01\x02' | od -A n -t x2 | grep -q "0201"; then
	# Little-endian
	printf '\x10\x00'
else
	# Big-endian
	printf '\x00\x10'
fi | dd of="${corrupted}" bs=1 seek=$((mmap2_offset_dec + 6)) conv=notrunc 2>/dev/null

perf report -i "${corrupted}" --stdio > /dev/null 2> "${stderrfile}"

# Check that warnings include "at offset 0x..."
if grep -q "at offset 0x" "${stderrfile}"; then
	echo "File offset in diagnostics [Success]"
else
	echo "File offset in diagnostics [Failed: no 'at offset 0x...' found]"
	echo "  stderr was:"
	head -5 "${stderrfile}"
	err=1
fi

# Check that the event type name and numeric id appear: "MMAP2 (10)"
if grep -q "MMAP2 (10)" "${stderrfile}"; then
	echo "Event type name in diagnostics [Success]"
else
	echo "Event type name in diagnostics [Failed: no 'MMAP2 (10)' found]"
	echo "  stderr was:"
	head -5 "${stderrfile}"
	err=1
fi

# Check that the reported offset matches the actual corruption point
if grep -q "at offset ${mmap2_offset}:" "${stderrfile}"; then
	echo "Correct offset reported [Success]"
else
	echo "Correct offset reported [Failed: expected offset ${mmap2_offset}]"
	echo "  stderr was:"
	head -5 "${stderrfile}"
	err=1
fi

cleanup
exit ${err}
