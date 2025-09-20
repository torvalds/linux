# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

# This is sourced from a driver script so no need for #!/bin... etc. at the
# top - the assumption below is that it runs as part of sourcing after the
# test sets up some basic env vars to say what it is.

# This currently works with ETMv4 / ETF not any other packet types at thi
# point. This will need changes if that changes.

# perf record options for the perf tests to use
PERFRECMEM="-m ,16M"
PERFRECOPT="$PERFRECMEM -e cs_etm//u"

TOOLS=$(dirname $0)
DIR="$TOOLS/$TEST"
BIN="$DIR/$TEST"
# If the test tool/binary does not exist and is executable then skip the test
if ! test -x "$BIN"; then exit 2; fi
# If CoreSight is not available, skip the test
perf list pmu | grep -q cs_etm || exit 2
DATD="."
# If the data dir env is set then make the data dir use that instead of ./
if test -n "$PERF_TEST_CORESIGHT_DATADIR"; then
	DATD="$PERF_TEST_CORESIGHT_DATADIR";
fi
# If the stat dir env is set then make the data dir use that instead of ./
STATD="."
if test -n "$PERF_TEST_CORESIGHT_STATDIR"; then
	STATD="$PERF_TEST_CORESIGHT_STATDIR";
fi

# Called if the test fails - error code 1
err() {
	echo "$1"
	exit 1
}

# Check that some statistics from our perf
check_val_min() {
	STATF="$4"
	if test "$2" -lt "$3"; then
		echo ", FAILED" >> "$STATF"
		err "Sanity check number of $1 is too low ($2 < $3)"
	fi
}

perf_dump_aux_verify() {
	# Some basic checking that the AUX chunk contains some sensible data
	# to see that we are recording something and at least a minimum
	# amount of it. We should almost always see Fn packets in just about
	# anything but certainly we will see some trace info and async
	# packets
	DUMP="$DATD/perf-tmp-aux-dump.txt"
	perf report --stdio --dump -i "$1" | \
		grep -o -e I_ATOM_F -e I_ASYNC -e I_TRACE_INFO > "$DUMP"
	# Simply count how many of these packets we find to see that we are
	# producing a reasonable amount of data - exact checks are not sane
	# as this is a lossy process where we may lose some blocks and the
	# compiler may produce different code depending on the compiler and
	# optimization options, so this is rough just to see if we're
	# either missing almost all the data or all of it
	ATOM_FX_NUM=$(grep -c I_ATOM_F "$DUMP")
	ASYNC_NUM=$(grep -c I_ASYNC "$DUMP")
	TRACE_INFO_NUM=$(grep -c I_TRACE_INFO "$DUMP")
	rm -f "$DUMP"

	# Arguments provide minimums for a pass
	CHECK_FX_MIN="$2"
	CHECK_ASYNC_MIN="$3"
	CHECK_TRACE_INFO_MIN="$4"

	# Write out statistics, so over time you can track results to see if
	# there is a pattern - for example we have less "noisy" results that
	# produce more consistent amounts of data each run, to see if over
	# time any techinques to  minimize data loss are having an effect or
	# not
	STATF="$STATD/stats-$TEST-$DATV.csv"
	if ! test -f "$STATF"; then
		echo "ATOM Fx Count, Minimum, ASYNC Count, Minimum, TRACE INFO Count, Minimum" > "$STATF"
	fi
	echo -n "$ATOM_FX_NUM, $CHECK_FX_MIN, $ASYNC_NUM, $CHECK_ASYNC_MIN, $TRACE_INFO_NUM, $CHECK_TRACE_INFO_MIN" >> "$STATF"

	# Actually check to see if we passed or failed.
	check_val_min "ATOM_FX" "$ATOM_FX_NUM" "$CHECK_FX_MIN" "$STATF"
	check_val_min "ASYNC" "$ASYNC_NUM" "$CHECK_ASYNC_MIN" "$STATF"
	check_val_min "TRACE_INFO" "$TRACE_INFO_NUM" "$CHECK_TRACE_INFO_MIN" "$STATF"
	echo ", Ok" >> "$STATF"
}

perf_dump_aux_tid_verify() {
	# Specifically crafted test will produce a list of Tread ID's to
	# stdout that need to be checked to  see that they have had trace
	# info collected in AUX blocks in the perf data. This will go
	# through all the TID's that are listed as CID=0xabcdef and see
	# that all the Thread IDs the test tool reports are  in the perf
	# data AUX chunks

	# The TID test tools will print a TID per stdout line that are being
	# tested
	TIDS=$(cat "$2")
	# Scan the perf report to find the TIDs that are actually CID in hex
	# and build a list of the ones found
	FOUND_TIDS=$(perf report --stdio --dump -i "$1" | \
			grep -o "CID=0x[0-9a-z]\+" | sed 's/CID=//g' | \
			uniq | sort | uniq)
	# No CID=xxx found - maybe your kernel is reporting these as
	# VMID=xxx so look there
	if test -z "$FOUND_TIDS"; then
		FOUND_TIDS=$(perf report --stdio --dump -i "$1" | \
				grep -o "VMID=0x[0-9a-z]\+" | sed 's/VMID=//g' | \
				uniq | sort | uniq)
	fi

	# Iterate over the list of TIDs that the test says it has and find
	# them in the TIDs found in the perf report
	MISSING=""
	for TID2 in $TIDS; do
		FOUND=""
		for TIDHEX in $FOUND_TIDS; do
			TID=$(printf "%i" $TIDHEX)
			if test "$TID" -eq "$TID2"; then
				FOUND="y"
				break
			fi
		done
		if test -z "$FOUND"; then
			MISSING="$MISSING $TID"
		fi
	done
	if test -n "$MISSING"; then
		err "Thread IDs $MISSING not found in perf AUX data"
	fi
}
