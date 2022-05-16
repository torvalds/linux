#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

usage() {
	echo "Dump boot-time tracing bootconfig from ftrace"
	echo "Usage: $0 [--debug] [ > BOOTCONFIG-FILE]"
	exit 1
}

DEBUG=
while [ x"$1" != x ]; do
	case "$1" in
	"--debug")
		DEBUG=$1;;
	-*)
		usage
		;;
	esac
	shift 1
done

if [ x"$DEBUG" != x ]; then
	set -x
fi

TRACEFS=`grep -m 1 -w tracefs /proc/mounts | cut -f 2 -d " "`
if [ -z "$TRACEFS" ]; then
	if ! grep -wq debugfs /proc/mounts; then
		echo "Error: No tracefs/debugfs was mounted."
		exit 1
	fi
	TRACEFS=`grep -m 1 -w debugfs /proc/mounts | cut -f 2 -d " "`/tracing
	if [ ! -d $TRACEFS ]; then
		echo "Error: ftrace is not enabled on this kernel." 1>&2
		exit 1
	fi
fi

######## main #########

set -e

emit_kv() { # key =|+= value
	echo "$@"
}

global_options() {
	val=`cat $TRACEFS/max_graph_depth`
	[ $val != 0 ] && emit_kv kernel.fgraph_max_depth = $val
	if grep -qv "^#" $TRACEFS/set_graph_function $TRACEFS/set_graph_notrace ; then
		cat 1>&2 << EOF
# WARN: kernel.fgraph_filters and kernel.fgraph_notrace are not supported, since the wild card expression was expanded and lost from memory.
EOF
	fi
}

kprobe_event_options() {
	cat $TRACEFS/kprobe_events | while read p args; do
		case $p in
		r*)
		cat 1>&2 << EOF
# WARN: A return probe found but it is not supported by bootconfig. Skip it.
EOF
		continue;;
		esac
		p=${p#*:}
		event=${p#*/}
		group=${p%/*}
		if [ $group != "kprobes" ]; then
			cat 1>&2 << EOF
# WARN: kprobes group name $group is changed to "kprobes" for bootconfig.
EOF
		fi
		emit_kv $PREFIX.event.kprobes.$event.probes += $args
	done
}

synth_event_options() {
	cat $TRACEFS/synthetic_events | while read event fields; do
		emit_kv $PREFIX.event.synthetic.$event.fields = `echo $fields | sed "s/;/,/g"`
	done
}

# Variables resolver
DEFINED_VARS=
UNRESOLVED_EVENTS=

defined_vars() { # event-dir
	grep "^hist" $1/trigger | grep -o ':[a-zA-Z0-9]*='
}
referred_vars() {
	grep "^hist" $1/trigger | grep -o '$[a-zA-Z0-9]*'
}

per_event_options() { # event-dir
	evdir=$1
	# Check the special event which has no filter and no trigger
	[ ! -f $evdir/filter ] && return

	if grep -q "^hist:" $evdir/trigger; then
		# hist action can refer the undefined variables
		__vars=`defined_vars $evdir`
		for v in `referred_vars $evdir`; do
			if echo $DEFINED_VARS $__vars | grep -vqw ${v#$}; then
				# $v is not defined yet, defer it
				UNRESOLVED_EVENTS="$UNRESOLVED_EVENTS $evdir"
				return;
			fi
		done
		DEFINED_VARS="$DEFINED_VARS "`defined_vars $evdir`
	fi
	grep -v "^#" $evdir/trigger | while read action active; do
		emit_kv $PREFIX.event.$group.$event.actions += \'$action\'
	done

	# enable is not checked; this is done by set_event in the instance.
	val=`cat $evdir/filter`
	if [ "$val" != "none" ]; then
		emit_kv $PREFIX.event.$group.$event.filter = "$val"
	fi
}

retry_unresolved() {
	unresolved=$UNRESOLVED_EVENTS
	UNRESOLVED_EVENTS=
	for evdir in $unresolved; do
		event=${evdir##*/}
		group=${evdir%/*}; group=${group##*/}
		per_event_options $evdir
	done
}

event_options() {
	# PREFIX and INSTANCE must be set
	if [ $PREFIX = "ftrace" ]; then
		# define the dynamic events
		kprobe_event_options
		synth_event_options
	fi
	for group in `ls $INSTANCE/events/` ; do
		[ ! -d $INSTANCE/events/$group ] && continue
		for event in `ls $INSTANCE/events/$group/` ;do
			[ ! -d $INSTANCE/events/$group/$event ] && continue
			per_event_options $INSTANCE/events/$group/$event
		done
	done
	retry=0
	while [ $retry -lt 3 ]; do
		retry_unresolved
		retry=$((retry + 1))
	done
	if [ "$UNRESOLVED_EVENTS" ]; then
		cat 1>&2 << EOF
! ERROR: hist triggers in $UNRESOLVED_EVENTS use some undefined variables.
EOF
	fi
}

is_default_trace_option() { # option
grep -qw $1 << EOF
print-parent
nosym-offset
nosym-addr
noverbose
noraw
nohex
nobin
noblock
trace_printk
annotate
nouserstacktrace
nosym-userobj
noprintk-msg-only
context-info
nolatency-format
record-cmd
norecord-tgid
overwrite
nodisable_on_free
irq-info
markers
noevent-fork
nopause-on-trace
function-trace
nofunction-fork
nodisplay-graph
nostacktrace
notest_nop_accept
notest_nop_refuse
EOF
}

instance_options() { # [instance-name]
	if [ $# -eq 0 ]; then
		PREFIX="ftrace"
		INSTANCE=$TRACEFS
	else
		PREFIX="ftrace.instance.$1"
		INSTANCE=$TRACEFS/instances/$1
	fi
	val=
	for i in `cat $INSTANCE/trace_options`; do
		is_default_trace_option $i && continue
		val="$val, $i"
	done
	[ "$val" ] && emit_kv $PREFIX.options = "${val#,}"
	val="local"
	for i in `cat $INSTANCE/trace_clock` ; do
		[ "${i#*]}" ] && continue
		i=${i%]}; val=${i#[}
	done
	[ $val != "local" ] && emit_kv $PREFIX.trace_clock = $val
	val=`cat $INSTANCE/buffer_size_kb`
	if echo $val | grep -vq "expanded" ; then
		emit_kv $PREFIX.buffer_size = $val"KB"
	fi
	if grep -q "is allocated" $INSTANCE/snapshot ; then
		emit_kv $PREFIX.alloc_snapshot
	fi
	val=`cat $INSTANCE/tracing_cpumask`
	if [ `echo $val | sed -e s/f//g`x != x ]; then
		emit_kv $PREFIX.cpumask = $val
	fi

	val=
	for i in `cat $INSTANCE/set_event`; do
		val="$val, $i"
	done
	[ "$val" ] && emit_kv $PREFIX.events = "${val#,}"
	val=`cat $INSTANCE/current_tracer`
	[ $val != nop ] && emit_kv $PREFIX.tracer = $val
	if grep -qv "^#" $INSTANCE/set_ftrace_filter $INSTANCE/set_ftrace_notrace; then
		cat 1>&2 << EOF
# WARN: kernel.ftrace.filters and kernel.ftrace.notrace are not supported, since the wild card expression was expanded and lost from memory.
EOF
	fi
	event_options
}

global_options
instance_options
for i in `ls $TRACEFS/instances` ; do
	instance_options $i
done
