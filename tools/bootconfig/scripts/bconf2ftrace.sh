#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

usage() {
	echo "Ftrace boottime trace test tool"
	echo "Usage: $0 [--apply|--init] [--debug] BOOTCONFIG-FILE"
	echo "    --apply: Test actual apply to tracefs (need sudo)"
	echo "    --init:  Initialize ftrace before applying (imply --apply)"
	exit 1
}

[ $# -eq 0 ] && usage

BCONF=
DEBUG=
APPLY=
INIT=
while [ x"$1" != x ]; do
	case "$1" in
	"--debug")
		DEBUG=$1;;
	"--apply")
		APPLY=$1;;
	"--init")
		APPLY=$1
		INIT=$1;;
	*)
		[ ! -f $1 ] && usage
		BCONF=$1;;
	esac
	shift 1
done

if [ x"$APPLY" != x ]; then
	if [ `id -u` -ne 0 ]; then
		echo "This must be run by root user. Try sudo." 1>&2
		exec sudo $0 $DEBUG $APPLY $BCONF
	fi
fi

run_cmd() { # command
	echo "$*"
	if [ x"$APPLY" != x ]; then # apply command
		eval $*
	fi
}

if [ x"$DEBUG" != x ]; then
	set -x
fi

TRACEFS=`grep -m 1 -w tracefs /proc/mounts | cut -f 2 -d " "`
if [ -z "$TRACEFS" ]; then
	if ! grep -wq debugfs /proc/mounts; then
		echo "Error: No tracefs/debugfs was mounted." 1>&2
		exit 1
	fi
	TRACEFS=`grep -m 1 -w debugfs /proc/mounts | cut -f 2 -d " "`/tracing
	if [ ! -d $TRACEFS ]; then
		echo "Error: ftrace is not enabled on this kernel." 1>&2
		exit 1
	fi
fi

if [ x"$INIT" != x ]; then
	. `dirname $0`/ftrace.sh
	(cd $TRACEFS; initialize_ftrace)
fi

. `dirname $0`/xbc.sh

######## main #########
set -e

xbc_init $BCONF

set_value_of() { # key file
	if xbc_has_key $1; then
		val=`xbc_get_val $1 1`
		run_cmd "echo '$val' >> $2"
	fi
}

set_array_of() { # key file
	if xbc_has_key $1; then
		xbc_get_val $1 | while read line; do
			run_cmd "echo '$line' >> $2"
		done
	fi
}

compose_synth() { # event_name branch
	echo -n "$1 "
	xbc_get_val $2 | while read field; do echo -n "$field; "; done
}

print_hist_array() { # prefix key
	__sep="="
	if xbc_has_key ${1}.${2}; then
		echo -n ":$2"
		xbc_get_val ${1}.${2} | while read field; do
			echo -n "$__sep$field"; __sep=","
		done
	fi
}

print_hist_action_array() { # prefix key
	__sep="("
	echo -n ".$2"
	xbc_get_val ${1}.${2} | while read field; do
		echo -n "$__sep$field"; __sep=","
	done
	echo -n ")"
}

print_hist_one_action() { # prefix handler param
	echo -n ":${2}("`xbc_get_val ${1}.${3}`")"
	if xbc_has_key "${1}.trace"; then
		print_hist_action_array ${1} "trace"
	elif xbc_has_key "${1}.save"; then
		print_hist_action_array ${1} "save"
	elif xbc_has_key "${1}.snapshot"; then
		echo -n ".snapshot()"
	fi
}

print_hist_actions() { # prefix handler param
	for __hdr in `xbc_subkeys ${1}.${2} 1 ".[0-9]"`; do
		print_hist_one_action ${1}.${2}.$__hdr ${2} ${3}
	done
	if xbc_has_key ${1}.${2}.${3} ; then
		print_hist_one_action ${1}.${2} ${2} ${3}
	fi
}

print_hist_var() { # prefix varname
	echo -n ":${2}="`xbc_get_val ${1}.var.${2} | tr -d [:space:]`
}

print_one_histogram() { # prefix
	echo -n "hist"
	print_hist_array $1 "keys"
	print_hist_array $1 "values"
	print_hist_array $1 "sort"
	if xbc_has_key "${1}.size"; then
		echo -n ":size="`xbc_get_val ${1}.size`
	fi
	if xbc_has_key "${1}.name"; then
		echo -n ":name="`xbc_get_val ${1}.name`
	fi
	for __var in `xbc_subkeys "${1}.var" 1`; do
		print_hist_var ${1} ${__var}
	done
	if xbc_has_key "${1}.pause"; then
		echo -n ":pause"
	elif xbc_has_key "${1}.continue"; then
		echo -n ":continue"
	elif xbc_has_key "${1}.clear"; then
		echo -n ":clear"
	fi
	print_hist_actions ${1} "onmax" "var"
	print_hist_actions ${1} "onchange" "var"
	print_hist_actions ${1} "onmatch" "event"

	if xbc_has_key "${1}.filter"; then
		echo -n " if "`xbc_get_val ${1}.filter`
	fi
}

setup_one_histogram() { # prefix trigger-file
	run_cmd "echo '`print_one_histogram ${1}`' >> ${2}"
}

setup_histograms() { # prefix trigger-file
	for __hist in `xbc_subkeys ${1} 1 ".[0-9]"`; do
		setup_one_histogram ${1}.$__hist ${2}
	done
	if xbc_has_key ${1}.keys; then
		setup_one_histogram ${1} ${2}
	fi
}

setup_event() { # prefix group event [instance]
	branch=$1.$2.$3
	if [ "$4" ]; then
		eventdir="$TRACEFS/instances/$4/events/$2/$3"
	else
		eventdir="$TRACEFS/events/$2/$3"
	fi
	# group enable
	if [ "$3" = "enable" ]; then
		run_cmd "echo 1 > ${eventdir}"
		return
	fi

	case $2 in
	kprobes)
		xbc_get_val ${branch}.probes | while read line; do
			run_cmd "echo 'p:kprobes/$3 $line' >> $TRACEFS/kprobe_events"
		done
		;;
	synthetic)
		run_cmd "echo '`compose_synth $3 ${branch}.fields`' >> $TRACEFS/synthetic_events"
		;;
	esac

	set_value_of ${branch}.filter ${eventdir}/filter
	set_array_of ${branch}.actions ${eventdir}/trigger

	setup_histograms ${branch}.hist ${eventdir}/trigger

	if xbc_has_key ${branch}.enable; then
		run_cmd "echo 1 > ${eventdir}/enable"
	fi
}

setup_events() { # prefix("ftrace" or "ftrace.instance.INSTANCE") [instance]
	prefix="${1}.event"
	if xbc_has_branch ${1}.event; then
		for grpev in `xbc_subkeys ${1}.event 2`; do
			setup_event $prefix ${grpev%.*} ${grpev#*.} $2
		done
	fi
	if xbc_has_branch ${1}.event.enable; then
		if [ "$2" ]; then
			run_cmd "echo 1 > $TRACEFS/instances/$2/events/enable"
		else
			run_cmd "echo 1 > $TRACEFS/events/enable"
		fi
	fi
}

size2kb() { # size[KB|MB]
	case $1 in
	*KB)
		echo ${1%KB};;
	*MB)
		expr ${1%MB} \* 1024;;
	*)
		expr $1 / 1024 ;;
	esac
}

setup_instance() { # [instance]
	if [ "$1" ]; then
		instance="ftrace.instance.${1}"
		instancedir=$TRACEFS/instances/$1
	else
		instance="ftrace"
		instancedir=$TRACEFS
	fi

	set_array_of ${instance}.options ${instancedir}/trace_options
	set_value_of ${instance}.trace_clock ${instancedir}/trace_clock
	set_value_of ${instance}.cpumask ${instancedir}/tracing_cpumask
	set_value_of ${instance}.tracing_on ${instancedir}/tracing_on
	set_value_of ${instance}.tracer ${instancedir}/current_tracer
	set_array_of ${instance}.ftrace.filters \
		${instancedir}/set_ftrace_filter
	set_array_of ${instance}.ftrace.notrace \
		${instancedir}/set_ftrace_notrace

	if xbc_has_key ${instance}.alloc_snapshot; then
		run_cmd "echo 1 > ${instancedir}/snapshot"
	fi

	if xbc_has_key ${instance}.buffer_size; then
		size=`xbc_get_val ${instance}.buffer_size 1`
		size=`eval size2kb $size`
		run_cmd "echo $size >> ${instancedir}/buffer_size_kb"
	fi

	setup_events ${instance} $1
	set_array_of ${instance}.events ${instancedir}/set_event
}

# ftrace global configs (kernel.*)
if xbc_has_key "kernel.dump_on_oops"; then
	dump_mode=`xbc_get_val "kernel.dump_on_oops" 1`
	[ "$dump_mode" ] && dump_mode=`eval echo $dump_mode` || dump_mode=1
	run_cmd "echo \"$dump_mode\" > /proc/sys/kernel/ftrace_dump_on_oops"
fi

set_value_of kernel.fgraph_max_depth $TRACEFS/max_graph_depth
set_array_of kernel.fgraph_filters $TRACEFS/set_graph_function
set_array_of kernel.fgraph_notraces $TRACEFS/set_graph_notrace

# Per-instance/per-event configs
if ! xbc_has_branch "ftrace" ; then
	exit 0
fi

setup_instance # root instance

if xbc_has_branch "ftrace.instance"; then
	for i in `xbc_subkeys "ftrace.instance" 1`; do
		run_cmd "mkdir -p $TRACEFS/instances/$i"
		setup_instance $i
	done
fi

