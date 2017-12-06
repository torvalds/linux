#!/bin/bash
# Copyright (c) 2016 Microsemi. All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# Author: Logan Gunthorpe <logang@deltatee.com>

REMOTE_HOST=
LIST_DEVS=FALSE

DEBUGFS=${DEBUGFS-/sys/kernel/debug}

DB_BITMASK=0x7FFF
PERF_RUN_ORDER=32
MAX_MW_SIZE=0
RUN_DMA_TESTS=
DONT_CLEANUP=
MW_SIZE=65536

function show_help()
{
	echo "Usage: $0 [OPTIONS] LOCAL_DEV REMOTE_DEV"
	echo "Run tests on a pair of NTB endpoints."
	echo
	echo "If the NTB device loops back to the same host then,"
	echo "just specifying the two PCI ids on the command line is"
	echo "sufficient. Otherwise, if the NTB link spans two hosts"
	echo "use the -r option to specify the hostname for the remote"
	echo "device. SSH will then be used to test the remote side."
	echo "An SSH key between the root users of the host would then"
	echo "be highly recommended."
	echo
	echo "Options:"
	echo "  -b BITMASK      doorbell clear bitmask for ntb_tool"
	echo "  -C              don't cleanup ntb modules on exit"
	echo "  -d              run dma tests"
	echo "  -h              show this help message"
	echo "  -l              list available local and remote PCI ids"
	echo "  -r REMOTE_HOST  specify the remote's hostname to connect"
        echo "                  to for the test (using ssh)"
	echo "  -p NUM          ntb_perf run order (default: $PERF_RUN_ORDER)"
	echo "  -w max_mw_size  maxmium memory window size"
	echo
}

function parse_args()
{
	OPTIND=0
	while getopts "b:Cdhlm:r:p:w:" opt; do
		case "$opt" in
		b)  DB_BITMASK=${OPTARG} ;;
		C)  DONT_CLEANUP=1 ;;
		d)  RUN_DMA_TESTS=1 ;;
		h)  show_help; exit 0 ;;
		l)  LIST_DEVS=TRUE ;;
		m)  MW_SIZE=${OPTARG} ;;
		r)  REMOTE_HOST=${OPTARG} ;;
		p)  PERF_RUN_ORDER=${OPTARG} ;;
		w)  MAX_MW_SIZE=${OPTARG} ;;
		\?)
		    echo "Invalid option: -$OPTARG" >&2
		    exit 1
		    ;;
		esac
	done
}

parse_args "$@"
shift $((OPTIND-1))
LOCAL_DEV=$1
shift
parse_args "$@"
shift $((OPTIND-1))
REMOTE_DEV=$1
shift
parse_args "$@"

set -e

function _modprobe()
{
	modprobe "$@"

	if [[ "$REMOTE_HOST" != "" ]]; then
		ssh "$REMOTE_HOST" modprobe "$@"
	fi
}

function split_remote()
{
	VPATH=$1
	REMOTE=

	if [[ "$VPATH" == *":/"* ]]; then
		REMOTE=${VPATH%%:*}
		VPATH=${VPATH#*:}
	fi
}

function read_file()
{
	split_remote $1
	if [[ "$REMOTE" != "" ]]; then
		ssh "$REMOTE" cat "$VPATH"
	else
		cat "$VPATH"
	fi
}

function write_file()
{
	split_remote $2
	VALUE=$1

	if [[ "$REMOTE" != "" ]]; then
		ssh "$REMOTE" "echo \"$VALUE\" > \"$VPATH\""
	else
		echo "$VALUE" > "$VPATH"
	fi
}

function check_file()
{
	split_remote $1

	if [[ "$REMOTE" != "" ]]; then
		ssh "$REMOTE" "[[ -e ${VPATH} ]]"
	else
		[[ -e ${VPATH} ]]
	fi
}

function find_pidx()
{
	PORT=$1
	PPATH=$2

	for ((i = 0; i < 64; i++)); do
		PEER_DIR="$PPATH/peer$i"

		check_file ${PEER_DIR} || break

		PEER_PORT=$(read_file "${PEER_DIR}/port")
		if [[ ${PORT} -eq $PEER_PORT ]]; then
			echo $i
			return 0
		fi
	done

	return 1
}

function port_test()
{
	LOC=$1
	REM=$2

	echo "Running port tests on: $(basename $LOC) / $(basename $REM)"

	LOCAL_PORT=$(read_file "$LOC/port")
	REMOTE_PORT=$(read_file "$REM/port")

	LOCAL_PIDX=$(find_pidx ${REMOTE_PORT} "$LOC")
	REMOTE_PIDX=$(find_pidx ${LOCAL_PORT} "$REM")

	echo "Local port ${LOCAL_PORT} with index ${REMOTE_PIDX} on remote host"
	echo "Peer port ${REMOTE_PORT} with index ${LOCAL_PIDX} on local host"

	echo "  Passed"
}

function link_test()
{
	LOC=$1
	REM=$2
	EXP=0

	echo "Running link tests on: $(basename $LOC) / $(basename $REM)"

	if ! write_file "N" "$LOC/link" 2> /dev/null; then
		echo "  Unsupported"
		return
	fi

	write_file "N" "$LOC/link_event"

	if [[ $(read_file "$REM/link") != "N" ]]; then
		echo "Expected remote link to be down in $REM/link" >&2
		exit -1
	fi

	write_file "Y" "$LOC/link"
	write_file "Y" "$LOC/link_event"

	echo "  Passed"
}

function doorbell_test()
{
	LOC=$1
	REM=$2
	EXP=0

	echo "Running db tests on: $(basename $LOC) / $(basename $REM)"

	write_file "c $DB_BITMASK" "$REM/db"

	for ((i=1; i <= 8; i++)); do
		let DB=$(read_file "$REM/db") || true
		if [[ "$DB" != "$EXP" ]]; then
			echo "Doorbell doesn't match expected value $EXP " \
			     "in $REM/db" >&2
			exit -1
		fi

		let "MASK=1 << ($i-1)" || true
		let "EXP=$EXP | $MASK" || true
		write_file "s $MASK" "$LOC/peer_db"
	done

	echo "  Passed"
}

function read_spad()
{
       VPATH=$1
       IDX=$2

       ROW=($(read_file "$VPATH" | grep -e "^$IDX"))
       let VAL=${ROW[1]} || true
       echo $VAL
}

function scratchpad_test()
{
	LOC=$1
	REM=$2
	CNT=$(read_file "$LOC/spad" | wc -l)

	echo "Running spad tests on: $(basename $LOC) / $(basename $REM)"

	for ((i = 0; i < $CNT; i++)); do
		VAL=$RANDOM
		write_file "$i $VAL" "$LOC/peer_spad"
		RVAL=$(read_spad "$REM/spad" $i)

		if [[ "$VAL" != "$RVAL" ]]; then
			echo "Scratchpad doesn't match expected value $VAL " \
			     "in $REM/spad, got $RVAL" >&2
			exit -1
		fi

	done

	echo "  Passed"
}

function write_mw()
{
	split_remote $2

	if [[ "$REMOTE" != "" ]]; then
		ssh "$REMOTE" \
			dd if=/dev/urandom "of=$VPATH" 2> /dev/null || true
	else
		dd if=/dev/urandom "of=$VPATH" 2> /dev/null || true
	fi
}

function mw_test()
{
	IDX=$1
	LOC=$2
	REM=$3

	echo "Running $IDX tests on: $(basename $LOC) / $(basename $REM)"

	write_mw "$LOC/$IDX"

	split_remote "$LOC/$IDX"
	if [[ "$REMOTE" == "" ]]; then
		A=$VPATH
	else
		A=/tmp/ntb_test.$$.A
		ssh "$REMOTE" cat "$VPATH" > "$A"
	fi

	split_remote "$REM/peer_$IDX"
	if [[ "$REMOTE" == "" ]]; then
		B=$VPATH
	else
		B=/tmp/ntb_test.$$.B
		ssh "$REMOTE" cat "$VPATH" > "$B"
	fi

	cmp -n $MW_SIZE "$A" "$B"
	if [[ $? != 0 ]]; then
		echo "Memory window $MW did not match!" >&2
	fi

	if [[ "$A" == "/tmp/*" ]]; then
		rm "$A"
	fi

	if [[ "$B" == "/tmp/*" ]]; then
		rm "$B"
	fi

	echo "  Passed"
}

function pingpong_test()
{
	LOC=$1
	REM=$2

	echo "Running ping pong tests on: $(basename $LOC) / $(basename $REM)"

	LOC_START=$(read_file "$LOC/count")
	REM_START=$(read_file "$REM/count")

	sleep 7

	LOC_END=$(read_file "$LOC/count")
	REM_END=$(read_file "$REM/count")

	if [[ $LOC_START == $LOC_END ]] || [[ $REM_START == $REM_END ]]; then
		echo "Ping pong counter not incrementing!" >&2
		exit 1
	fi

	echo "  Passed"
}

function perf_test()
{
	USE_DMA=$1

	if [[ $USE_DMA == "1" ]]; then
		WITH="with"
	else
		WITH="without"
	fi

	_modprobe ntb_perf run_order=$PERF_RUN_ORDER \
		max_mw_size=$MAX_MW_SIZE use_dma=$USE_DMA

	echo "Running local perf test $WITH DMA"
	write_file "" "$LOCAL_PERF/run"
	echo -n "  "
	read_file "$LOCAL_PERF/run"
	echo "  Passed"

	echo "Running remote perf test $WITH DMA"
	write_file "" "$REMOTE_PERF/run"
	echo -n "  "
	read_file "$REMOTE_PERF/run"
	echo "  Passed"

	_modprobe -r ntb_perf
}

function ntb_tool_tests()
{
	LOCAL_TOOL="$DEBUGFS/ntb_tool/$LOCAL_DEV"
	REMOTE_TOOL="$REMOTE_HOST:$DEBUGFS/ntb_tool/$REMOTE_DEV"

	echo "Starting ntb_tool tests..."

	_modprobe ntb_tool

	port_test "$LOCAL_TOOL" "$REMOTE_TOOL"

	write_file "Y" "$LOCAL_TOOL/link_event"
	write_file "Y" "$REMOTE_TOOL/link_event"

	link_test "$LOCAL_TOOL" "$REMOTE_TOOL"
	link_test "$REMOTE_TOOL" "$LOCAL_TOOL"

	#Ensure the link is up on both sides before continuing
	write_file "Y" "$LOCAL_TOOL/link_event"
	write_file "Y" "$REMOTE_TOOL/link_event"

	for PEER_TRANS in $(ls "$LOCAL_TOOL"/peer_trans*); do
		PT=$(basename $PEER_TRANS)
		write_file $MW_SIZE "$LOCAL_TOOL/$PT"
		write_file $MW_SIZE "$REMOTE_TOOL/$PT"
	done

	doorbell_test "$LOCAL_TOOL" "$REMOTE_TOOL"
	doorbell_test "$REMOTE_TOOL" "$LOCAL_TOOL"
	scratchpad_test "$LOCAL_TOOL" "$REMOTE_TOOL"
	scratchpad_test "$REMOTE_TOOL" "$LOCAL_TOOL"

	for MW in $(ls "$LOCAL_TOOL"/mw*); do
		MW=$(basename $MW)

		mw_test $MW "$LOCAL_TOOL" "$REMOTE_TOOL"
		mw_test $MW "$REMOTE_TOOL" "$LOCAL_TOOL"
	done

	_modprobe -r ntb_tool
}

function ntb_pingpong_tests()
{
	LOCAL_PP="$DEBUGFS/ntb_pingpong/$LOCAL_DEV"
	REMOTE_PP="$REMOTE_HOST:$DEBUGFS/ntb_pingpong/$REMOTE_DEV"

	echo "Starting ntb_pingpong tests..."

	_modprobe ntb_pingpong

	pingpong_test $LOCAL_PP $REMOTE_PP

	_modprobe -r ntb_pingpong
}

function ntb_perf_tests()
{
	LOCAL_PERF="$DEBUGFS/ntb_perf/$LOCAL_DEV"
	REMOTE_PERF="$REMOTE_HOST:$DEBUGFS/ntb_perf/$REMOTE_DEV"

	echo "Starting ntb_perf tests..."

	perf_test 0

	if [[ $RUN_DMA_TESTS ]]; then
		perf_test 1
	fi
}

function cleanup()
{
	set +e
	_modprobe -r ntb_tool 2> /dev/null
	_modprobe -r ntb_perf 2> /dev/null
	_modprobe -r ntb_pingpong 2> /dev/null
	_modprobe -r ntb_transport 2> /dev/null
	set -e
}

cleanup

if ! [[ $$DONT_CLEANUP ]]; then
	trap cleanup EXIT
fi

if [ "$(id -u)" != "0" ]; then
	echo "This script must be run as root" 1>&2
	exit 1
fi

if [[ "$LIST_DEVS" == TRUE ]]; then
	echo "Local Devices:"
	ls -1 /sys/bus/ntb/devices
	echo

	if [[ "$REMOTE_HOST" != "" ]]; then
		echo "Remote Devices:"
		ssh $REMOTE_HOST ls -1 /sys/bus/ntb/devices
	fi

	exit 0
fi

if [[ "$LOCAL_DEV" == $"" ]] || [[ "$REMOTE_DEV" == $"" ]]; then
	show_help
	exit 1
fi

ntb_tool_tests
echo
ntb_pingpong_tests
echo
ntb_perf_tests
echo
