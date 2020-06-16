#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2016 Microsemi. All Rights Reserved.
#
# Author: Logan Gunthorpe <logang@deltatee.com>

REMOTE_HOST=
LIST_DEVS=FALSE

DEBUGFS=${DEBUGFS-/sys/kernel/debug}

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
	echo "  -C              don't cleanup ntb modules on exit"
	echo "  -h              show this help message"
	echo "  -l              list available local and remote PCI ids"
	echo "  -r REMOTE_HOST  specify the remote's hostname to connect"
	echo "                  to for the test (using ssh)"
	echo "  -m MW_SIZE      memory window size for ntb_tool"
	echo "                  (default: $MW_SIZE)"
	echo "  -d              run dma tests for ntb_perf"
	echo "  -p ORDER        total data order for ntb_perf"
	echo "                  (default: $PERF_RUN_ORDER)"
	echo "  -w MAX_MW_SIZE  maxmium memory window size for ntb_perf"
	echo
}

function parse_args()
{
	OPTIND=0
	while getopts "b:Cdhlm:r:p:w:" opt; do
		case "$opt" in
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
	modprobe "$@" || return 1

	if [[ "$REMOTE_HOST" != "" ]]; then
		ssh "$REMOTE_HOST" modprobe "$@" || return 1
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

function subdirname()
{
	echo $(basename $(dirname $1)) 2> /dev/null
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

	echo "Running link tests on: $(subdirname $LOC) / $(subdirname $REM)"

	if ! write_file "N" "$LOC/../link" 2> /dev/null; then
		echo "  Unsupported"
		return
	fi

	write_file "N" "$LOC/link_event"

	if [[ $(read_file "$REM/link") != "N" ]]; then
		echo "Expected link to be down in $REM/link" >&2
		exit -1
	fi

	write_file "Y" "$LOC/../link"

	echo "  Passed"
}

function doorbell_test()
{
	LOC=$1
	REM=$2
	EXP=0

	echo "Running db tests on: $(basename $LOC) / $(basename $REM)"

	DB_VALID_MASK=$(read_file "$LOC/db_valid_mask")

	write_file "c $DB_VALID_MASK" "$REM/db"

	for ((i = 0; i < 64; i++)); do
		DB=$(read_file "$REM/db")
		if [[ "$DB" -ne "$EXP" ]]; then
			echo "Doorbell doesn't match expected value $EXP " \
			     "in $REM/db" >&2
			exit -1
		fi

		let "MASK = (1 << $i) & $DB_VALID_MASK" || true
		let "EXP = $EXP | $MASK" || true

		write_file "s $MASK" "$LOC/peer_db"
	done

	write_file "c $DB_VALID_MASK" "$REM/db_mask"
	write_file $DB_VALID_MASK "$REM/db_event"
	write_file "s $DB_VALID_MASK" "$REM/db_mask"

	write_file "c $DB_VALID_MASK" "$REM/db"

	echo "  Passed"
}

function get_files_count()
{
	NAME=$1
	LOC=$2

	split_remote $LOC

	if [[ "$REMOTE" == "" ]]; then
		echo $(ls -1 "$VPATH"/${NAME}* 2>/dev/null | wc -l)
	else
		echo $(ssh "$REMOTE" "ls -1 \"$VPATH\"/${NAME}* | \
		       wc -l" 2> /dev/null)
	fi
}

function scratchpad_test()
{
	LOC=$1
	REM=$2

	echo "Running spad tests on: $(subdirname $LOC) / $(subdirname $REM)"

	CNT=$(get_files_count "spad" "$LOC")

	if [[ $CNT -eq 0 ]]; then
		echo "  Unsupported"
		return
	fi

	for ((i = 0; i < $CNT; i++)); do
		VAL=$RANDOM
		write_file "$VAL" "$LOC/spad$i"
		RVAL=$(read_file "$REM/../spad$i")

		if [[ "$VAL" -ne "$RVAL" ]]; then
			echo "Scratchpad $i value $RVAL doesn't match $VAL" >&2
			exit -1
		fi
	done

	echo "  Passed"
}

function message_test()
{
	LOC=$1
	REM=$2

	echo "Running msg tests on: $(subdirname $LOC) / $(subdirname $REM)"

	CNT=$(get_files_count "msg" "$LOC")

	if [[ $CNT -eq 0 ]]; then
		echo "  Unsupported"
		return
	fi

	MSG_OUTBITS_MASK=$(read_file "$LOC/../msg_inbits")
	MSG_INBITS_MASK=$(read_file "$REM/../msg_inbits")

	write_file "c $MSG_OUTBITS_MASK" "$LOC/../msg_sts"
	write_file "c $MSG_INBITS_MASK" "$REM/../msg_sts"

	for ((i = 0; i < $CNT; i++)); do
		VAL=$RANDOM
		write_file "$VAL" "$LOC/msg$i"
		RVAL=$(read_file "$REM/../msg$i")

		if [[ "$VAL" -ne "${RVAL%%<-*}" ]]; then
			echo "Message $i value $RVAL doesn't match $VAL" >&2
			exit -1
		fi
	done

	echo "  Passed"
}

function get_number()
{
	KEY=$1

	sed -n "s/^\(${KEY}\)[ \t]*\(0x[0-9a-fA-F]*\)\(\[p\]\)\?$/\2/p"
}

function mw_alloc()
{
	IDX=$1
	LOC=$2
	REM=$3

	write_file $MW_SIZE "$LOC/mw_trans$IDX"

	INB_MW=$(read_file "$LOC/mw_trans$IDX")
	MW_ALIGNED_SIZE=$(echo "$INB_MW" | get_number "Window Size")
	MW_DMA_ADDR=$(echo "$INB_MW" | get_number "DMA Address")

	write_file "$MW_DMA_ADDR:$(($MW_ALIGNED_SIZE))" "$REM/peer_mw_trans$IDX"

	if [[ $MW_SIZE -ne $MW_ALIGNED_SIZE ]]; then
		echo "MW $IDX size aligned to $MW_ALIGNED_SIZE"
	fi
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

function mw_check()
{
	IDX=$1
	LOC=$2
	REM=$3

	write_mw "$LOC/mw$IDX"

	split_remote "$LOC/mw$IDX"
	if [[ "$REMOTE" == "" ]]; then
		A=$VPATH
	else
		A=/tmp/ntb_test.$$.A
		ssh "$REMOTE" cat "$VPATH" > "$A"
	fi

	split_remote "$REM/peer_mw$IDX"
	if [[ "$REMOTE" == "" ]]; then
		B=$VPATH
	else
		B=/tmp/ntb_test.$$.B
		ssh "$REMOTE" cat "$VPATH" > "$B"
	fi

	cmp -n $MW_ALIGNED_SIZE "$A" "$B"
	if [[ $? != 0 ]]; then
		echo "Memory window $MW did not match!" >&2
	fi

	if [[ "$A" == "/tmp/*" ]]; then
		rm "$A"
	fi

	if [[ "$B" == "/tmp/*" ]]; then
		rm "$B"
	fi
}

function mw_free()
{
	IDX=$1
	LOC=$2
	REM=$3

	write_file "$MW_DMA_ADDR:0" "$REM/peer_mw_trans$IDX"

	write_file 0 "$LOC/mw_trans$IDX"
}

function mw_test()
{
	LOC=$1
	REM=$2

	CNT=$(get_files_count "mw_trans" "$LOC")

	for ((i = 0; i < $CNT; i++)); do
		echo "Running mw$i tests on: $(subdirname $LOC) / " \
		     "$(subdirname $REM)"

		mw_alloc $i $LOC $REM

		mw_check $i $LOC $REM

		mw_free $i $LOC  $REM

		echo "  Passed"
	done

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

function msi_test()
{
	LOC=$1
	REM=$2

	write_file 1 $LOC/ready

	echo "Running MSI interrupt tests on: $(subdirname $LOC) / $(subdirname $REM)"

	CNT=$(read_file "$LOC/count")
	for ((i = 0; i < $CNT; i++)); do
		START=$(read_file $REM/../irq${i}_occurrences)
		write_file $i $LOC/trigger
		END=$(read_file $REM/../irq${i}_occurrences)

		if [[ $(($END - $START)) != 1 ]]; then
			echo "MSI did not trigger the interrupt on the remote side!" >&2
			exit 1
		fi
	done

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

	_modprobe ntb_perf total_order=$PERF_RUN_ORDER \
		max_mw_size=$MAX_MW_SIZE use_dma=$USE_DMA

	echo "Running local perf test $WITH DMA"
	write_file "$LOCAL_PIDX" "$LOCAL_PERF/run"
	echo -n "  "
	read_file "$LOCAL_PERF/run"
	echo "  Passed"

	echo "Running remote perf test $WITH DMA"
	write_file "$REMOTE_PIDX" "$REMOTE_PERF/run"
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

	LOCAL_PEER_TOOL="$LOCAL_TOOL/peer$LOCAL_PIDX"
	REMOTE_PEER_TOOL="$REMOTE_TOOL/peer$REMOTE_PIDX"

	link_test "$LOCAL_PEER_TOOL" "$REMOTE_PEER_TOOL"
	link_test "$REMOTE_PEER_TOOL" "$LOCAL_PEER_TOOL"

	#Ensure the link is up on both sides before continuing
	write_file "Y" "$LOCAL_PEER_TOOL/link_event"
	write_file "Y" "$REMOTE_PEER_TOOL/link_event"

	doorbell_test "$LOCAL_TOOL" "$REMOTE_TOOL"
	doorbell_test "$REMOTE_TOOL" "$LOCAL_TOOL"

	scratchpad_test "$LOCAL_PEER_TOOL" "$REMOTE_PEER_TOOL"
	scratchpad_test "$REMOTE_PEER_TOOL" "$LOCAL_PEER_TOOL"

	message_test "$LOCAL_PEER_TOOL" "$REMOTE_PEER_TOOL"
	message_test "$REMOTE_PEER_TOOL" "$LOCAL_PEER_TOOL"

	mw_test "$LOCAL_PEER_TOOL" "$REMOTE_PEER_TOOL"
	mw_test "$REMOTE_PEER_TOOL" "$LOCAL_PEER_TOOL"

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

function ntb_msi_tests()
{
	LOCAL_MSI="$DEBUGFS/ntb_msi_test/$LOCAL_DEV"
	REMOTE_MSI="$REMOTE_HOST:$DEBUGFS/ntb_msi_test/$REMOTE_DEV"

	echo "Starting ntb_msi_test tests..."

	if ! _modprobe ntb_msi_test 2> /dev/null; then
		echo "  Not doing MSI tests seeing the module is not available."
		return
	fi

	port_test $LOCAL_MSI $REMOTE_MSI

	LOCAL_PEER="$LOCAL_MSI/peer$LOCAL_PIDX"
	REMOTE_PEER="$REMOTE_MSI/peer$REMOTE_PIDX"

	msi_test $LOCAL_PEER $REMOTE_PEER
	msi_test $REMOTE_PEER $LOCAL_PEER

	_modprobe -r ntb_msi_test
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
	_modprobe -r ntb_msi_test 2> /dev/null
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
ntb_msi_tests
echo
ntb_perf_tests
echo
