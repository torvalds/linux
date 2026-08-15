#!/bin/bash -e
# CoreSight raw dump stress (exclusive)

# SPDX-License-Identifier: GPL-2.0

if [ "$(id -u)" != 0 ]; then
	# Requires root for larger buffer size
	echo "[Skip] No root permission"
	exit 2
fi

# If CoreSight is not available, skip the test
perf list pmu | grep -q cs_etm || exit 2

tmpdir=$(mktemp -d /tmp/__perf_test.coresight_raw_dump_stress.XXXXX)

cleanup() {
	rm -r "${tmpdir}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

# Use exit snapshot to record 2M of trace to make about 80MB of raw dump data.
echo "Recording..."
perf record -e cs_etm/timestamp=0/u -m,2M -Se -o "$tmpdir/data" -- \
	perf test -w brstack 20000 > /dev/null 2>&1

# Test raw dump runs to completion but don't decode because that's too slow for
# a test
echo "Dumping raw trace..."
perf report --dump-raw-trace -i "$tmpdir/data" 2>/dev/null > "$tmpdir/rawdump"

# Get the size and offset of the first AUXTRACE buffer and the index of the last
# packet in the raw dump.
read -r size offset last_idx <<< "$(awk '
	found && /PERF_RECORD_/ { exit }
	/PERF_RECORD_AUXTRACE / { found = 1; size = $7; offset = $9; next }
	found && /Idx:/ { last_idx = $1; gsub(/Idx:|;/, "", last_idx) }
	END { if (last_idx) print size, offset, last_idx }
' "$tmpdir/rawdump")"

# The last Idx minus start offset should equal the size of the buffer if
# everything was dumped. Allow 48 bytes difference to cover 3 frames: current
# frame length, a partial frame and a final empty one, all of which aren't
# dumped.
#
# TODO: for a single snapshot, offset should always be zero. However, we
# currently output AUX records in snapshot mode when we shouldn't, which
# increments the offset. Allow for that until it's fixed so we can test raw
# dumping.
decode_size=$((1 + last_idx - offset))
if [ "$decode_size" -gt "$((size - 48))" ] && [ "$decode_size" -le "$((size))" ]; then
	echo "PASS: AUXTRACE buffer length matches dumped packet index"
	cleanup
	exit 0
fi

echo "FAIL: AUXTRACE buffer length mismatch: size=$size offset=$offset last_idx=$last_idx decode_size=$decode_size"
cleanup
exit 1
