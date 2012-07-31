#!/bin/bash
#
# NAME
#	failcmd.sh - run a command with injecting slab/page allocation failures
#
# SYNOPSIS
#	failcmd.sh --help
#	failcmd.sh [<options>] command [arguments]
#
# DESCRIPTION
#	Run command with injecting slab/page allocation failures by fault
#	injection.
#
#	NOTE: you need to run this script as root.
#

usage()
{
	cat >&2 <<EOF
Usage: $0 [options] command [arguments]

OPTIONS
	-p percent
	--probability=percent
		likelihood of failure injection, in percent.
		Default value is 1

	-t value
	--times=value
		specifies how many times failures may happen at most.
		Default value is 1

	--oom-kill-allocating-task=value
		set /proc/sys/vm/oom_kill_allocating_task to specified value
		before running the command.
		Default value is 1

	-h, --help
		Display a usage message and exit

	--interval=value, --space=value, --verbose=value, --task-filter=value,
	--stacktrace-depth=value, --require-start=value, --require-end=value,
	--reject-start=value, --reject-end=value, --ignore-gfp-wait=value
		See Documentation/fault-injection/fault-injection.txt for more
		information

	failslab options:
	--cache-filter=value

	fail_page_alloc options:
	--ignore-gfp-highmem=value, --min-order=value

ENVIRONMENT
	FAILCMD_TYPE
		The following values for FAILCMD_TYPE are recognized:

		failslab
			inject slab allocation failures
		fail_page_alloc
			inject page allocation failures

		If FAILCMD_TYPE is not defined, then failslab is used.
EOF
}

if [ $UID != 0 ]; then
	echo must be run as root >&2
	exit 1
fi

DEBUGFS=`mount -t debugfs | head -1 | awk '{ print $3}'`

if [ ! -d "$DEBUGFS" ]; then
	echo debugfs is not mounted >&2
	exit 1
fi

FAILCMD_TYPE=${FAILCMD_TYPE:-failslab}
FAULTATTR=$DEBUGFS/$FAILCMD_TYPE

if [ ! -d $FAULTATTR ]; then
	echo $FAILCMD_TYPE is not available >&2
	exit 1
fi

LONGOPTS=probability:,interval:,times:,space:,verbose:,task-filter:
LONGOPTS=$LONGOPTS,stacktrace-depth:,require-start:,require-end:
LONGOPTS=$LONGOPTS,reject-start:,reject-end:,oom-kill-allocating-task:,help

if [ $FAILCMD_TYPE = failslab ]; then
	LONGOPTS=$LONGOPTS,ignore-gfp-wait:,cache-filter:
elif [ $FAILCMD_TYPE = fail_page_alloc ]; then
	LONGOPTS=$LONGOPTS,ignore-gfp-wait:,ignore-gfp-highmem:,min-order:
fi

TEMP=`getopt -o p:i:t:s:v:h --long $LONGOPTS -n 'failcmd.sh' -- "$@"`

if [ $? != 0 ]; then
	usage
	exit 1
fi

eval set -- "$TEMP"

fault_attr_default()
{
	echo N > $FAULTATTR/task-filter
	echo 0 > $FAULTATTR/probability
	echo 1 > $FAULTATTR/times
}

fault_attr_default

oom_kill_allocating_task_saved=`cat /proc/sys/vm/oom_kill_allocating_task`

restore_values()
{
	fault_attr_default
	echo $oom_kill_allocating_task_saved \
		> /proc/sys/vm/oom_kill_allocating_task
}

#
# Default options
#
declare -i oom_kill_allocating_task=1
declare task_filter=Y
declare -i probability=1
declare -i times=1

while true; do
	case "$1" in
	-p|--probability)
		probability=$2
		shift 2
		;;
	-i|--interval)
		echo $2 > $FAULTATTR/interval
		shift 2
		;;
	-t|--times)
		times=$2
		shift 2
		;;
	-s|--space)
		echo $2 > $FAULTATTR/space
		shift 2
		;;
	-v|--verbose)
		echo $2 > $FAULTATTR/verbose
		shift 2
		;;
	--task-filter)
		task_filter=$2
		shift 2
		;;
	--stacktrace-depth)
		echo $2 > $FAULTATTR/stacktrace-depth
		shift 2
		;;
	--require-start)
		echo $2 > $FAULTATTR/require-start
		shift 2
		;;
	--require-end)
		echo $2 > $FAULTATTR/require-end
		shift 2
		;;
	--reject-start)
		echo $2 > $FAULTATTR/reject-start
		shift 2
		;;
	--reject-end)
		echo $2 > $FAULTATTR/reject-end
		shift 2
		;;
	--oom-kill-allocating-task)
		oom_kill_allocating_task=$2
		shift 2
		;;
	--ignore-gfp-wait)
		echo $2 > $FAULTATTR/ignore-gfp-wait
		shift 2
		;;
	--cache-filter)
		echo $2 > $FAULTATTR/cache_filter
		shift 2
		;;
	--ignore-gfp-highmem)
		echo $2 > $FAULTATTR/ignore-gfp-highmem
		shift 2
		;;
	--min-order)
		echo $2 > $FAULTATTR/min-order
		shift 2
		;;
	-h|--help)
		usage
		exit 0
		shift
		;;
	--)
		shift
		break
		;;
	esac
done

[ -z "$1" ] && exit 0

echo $oom_kill_allocating_task > /proc/sys/vm/oom_kill_allocating_task
echo $task_filter > $FAULTATTR/task-filter
echo $probability > $FAULTATTR/probability
echo $times > $FAULTATTR/times

trap "restore_values" SIGINT SIGTERM EXIT

cmd="echo 1 > /proc/self/make-it-fail && exec $@"
bash -c "$cmd"
