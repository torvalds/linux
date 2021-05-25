#!/bin/bash
#
# kprobe - trace a given kprobe definition. Kernel dynamic tracing.
#          Written using Linux ftrace.
#
# This will create, trace, then destroy a given kprobe definition. See
# Documentation/trace/kprobetrace.txt in the Linux kernel source for the
# syntax of a kprobe definition, and "kprobe -h" for examples. With this tool,
# the probe alias is optional (it will become to kprobe:<funcname> if not
# specified).
#
# USAGE: ./kprobe [-FhHsv] [-d secs] [-p pid] [-L tid] kprobe_definition [filter]
#
# Run "kprobe -h" for full usage.
#
# I wrote this because I kept testing different custom kprobes at the command
# line, and wanted a way to automate the steps.
#
# WARNING: This uses dynamic tracing of kernel functions, and could cause
# kernel panics or freezes, depending on the function traced. Test in a lab
# environment, and know what you are doing, before use.
#
# REQUIREMENTS: FTRACE and KPROBE CONFIG, which you may already have on recent
# kernel versions.
#
# From perf-tools: https://github.com/brendangregg/perf-tools
#
# See the kprobe(8) man page (in perf-tools) for more info.
#
# COPYRIGHT: Copyright (c) 2014 Brendan Gregg.
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
#  (http://www.gnu.org/copyleft/gpl.html)
#
# 22-Jul-2014	Brendan Gregg	Created this.

### default variables
tracing=/sys/kernel/debug/tracing
flock=/var/tmp/.ftrace-lock; wroteflock=0
opt_duration=0; duration=; opt_pid=0; pid=; opt_tid=0; tid=
opt_filter=0; filter=; opt_view=0; opt_headers=0; opt_stack=0; dmesg=2
debug=0; opt_force=0
trap ':' INT QUIT TERM PIPE HUP	# sends execution to end tracing section

function usage {
	cat <<-END >&2
	USAGE: kprobe [-FhHsv] [-d secs] [-p PID] [-L TID] kprobe_definition [filter]
	                 -F              # force. trace despite warnings.
	                 -d seconds      # trace duration, and use buffers
	                 -p PID          # PID to match on events
	                 -L TID          # thread id to match on events
	                 -v              # view format file (don't trace)
	                 -H              # include column headers
	                 -s              # show kernel stack traces
	                 -h              # this usage message
	
	Note that these examples may need modification to match your kernel
	version's function names and platform's register usage.
	   eg,
	       kprobe p:do_sys_open
	                                 # trace open() entry
	       kprobe r:do_sys_open
	                                 # trace open() return
	       kprobe 'r:do_sys_open \$retval'
	                                 # trace open() return value
	       kprobe 'r:myopen do_sys_open \$retval'
	                                 # use a custom probe name
	       kprobe 'p:myopen do_sys_open mode=%cx:u16'
	                                 # trace open() file mode
	       kprobe 'p:myopen do_sys_open filename=+0(%si):string'
	                                 # trace open() with filename
	       kprobe -s 'p:myprobe tcp_retransmit_skb'
	                                 # show kernel stacks
	       kprobe 'p:do_sys_open file=+0(%si):string' 'file ~ "*stat"'
	                                 # opened files ending in "stat"

	See the man page and example file for more info.
END
	exit
}

function warn {
	if ! eval "$@"; then
		echo >&2 "WARNING: command failed \"$@\""
	fi
}

function end {
	# disable tracing
	echo 2>/dev/null
	echo "Ending tracing..." 2>/dev/null
	cd $tracing
	warn "echo 0 > events/kprobes/$kname/enable"
	if (( opt_filter )); then
		warn "echo 0 > events/kprobes/$kname/filter"
	fi
	warn "echo -:$kname >> kprobe_events"
	(( opt_stack )) && warn "echo 0 > options/stacktrace"
	warn "echo > trace"
	(( wroteflock )) && warn "rm $flock"
}

function die {
	echo >&2 "$@"
	exit 1
}

function edie {
	# die with a quiet end()
	echo >&2 "$@"
	exec >/dev/null 2>&1
	end
	exit 1
}

### process options
while getopts Fd:hHp:L:sv opt
do
	case $opt in
	F)	opt_force=1 ;;
	d)	opt_duration=1; duration=$OPTARG ;;
	p)	opt_pid=1; pid=$OPTARG ;;
	L)	opt_tid=1; tid=$OPTARG ;;
	H)	opt_headers=1 ;;
	s)	opt_stack=1 ;;
	v)	opt_view=1 ;;
	h|?)	usage ;;
	esac
done
shift $(( $OPTIND - 1 ))
(( $# )) || usage
kprobe=$1
shift
if (( $# )); then
	opt_filter=1
	filter=$1
fi

### option logic
(( opt_pid + opt_filter + opt_tid > 1 )) && \
	die "ERROR: use at most one of -p, -L, or filter."
(( opt_duration && opt_view )) && die "ERROR: use either -d or -v."
if (( opt_pid )); then
	# convert to filter
	opt_filter=1
	# ftrace common_pid is thread id from user's perspective
	for tid in /proc/$pid/task/*; do
		filter="$filter || common_pid == ${tid##*/}"
	done
	filter=${filter:3}  # trim leading ' || ' (four characters)
fi
if (( opt_tid )); then
	opt_filter=1
	filter="common_pid == $tid"
fi
if [[ "$kprobe" != p:* && "$kprobe" != r:* ]]; then
	echo >&2 "ERROR: invalid kprobe definition (should start with p: or r:)"
	usage
fi
#
# parse the following:
# r:do_sys_open
# r:my_sys_open do_sys_open
# r:do_sys_open %ax
# r:do_sys_open $retval %ax
# r:my_sys_open do_sys_open $retval %ax
# r:do_sys_open rval=$retval
# r:my_sys_open do_sys_open rval=$retval
# r:my_sys_open do_sys_open rval=$retval %ax
# ... and examples from USAGE message
#
krest=${kprobe#*:}
kname=${krest%% *}
set -- $krest
if [[ $2 == "" || $2 == *[=%\$]* ]]; then
	# if probe name unspecified, default to function name
	ktype=${kprobe%%:*}
	kprobe="$ktype:$kname $krest"
fi
if (( debug )); then
	echo "kname: $kname, kprobe: $kprobe"
fi

### check permissions
cd $tracing || die "ERROR: accessing tracing. Root user? Kernel has FTRACE?
    debugfs mounted? (mount -t debugfs debugfs /sys/kernel/debug)"

## check function
set -- $kprobe
fname=$2
if (( !opt_force )) && ! grep -w $fname available_filter_functions >/dev/null \
    2>&1
then
	echo >&2 "ERROR: func $fname not in $PWD/available_filter_functions."
	printf >&2 "Either it doesn't exist, or, it might be unsafe to kprobe. "
	echo >&2 "Exiting. Use -F to override."
	exit 1
fi

if (( !opt_view )); then
	if (( opt_duration )); then
		echo "Tracing kprobe $kname for $duration seconds (buffered)..."
	else
		echo "Tracing kprobe $kname. Ctrl-C to end."
	fi
fi

### ftrace lock
[[ -e $flock ]] && die "ERROR: ftrace may be in use by PID $(cat $flock) $flock"
echo $$ > $flock || die "ERROR: unable to write $flock."
wroteflock=1

### setup and begin tracing
echo nop > current_tracer
if ! echo "$kprobe" >> kprobe_events; then
	echo >&2 "ERROR: adding kprobe \"$kprobe\"."
	if (( dmesg )); then
		echo >&2 "Last $dmesg dmesg entries (might contain reason):"
		dmesg | tail -$dmesg | sed 's/^/    /'
	fi
	edie "Exiting."
fi
if (( opt_view )); then
	cat events/kprobes/$kname/format
	edie ""
fi
if (( opt_filter )); then
	if ! echo "$filter" > events/kprobes/$kname/filter; then
		edie "ERROR: setting filter or -p. Exiting."
	fi
fi
if (( opt_stack )); then
	if ! echo 1 > options/stacktrace; then
		edie "ERROR: enabling stack traces (-s). Exiting"
	fi
fi
if ! echo 1 > events/kprobes/$kname/enable; then
	edie "ERROR: enabling kprobe $kname. Exiting."
fi

### print trace buffer
warn "echo > trace"
if (( opt_duration )); then
	sleep $duration
	if (( opt_headers )); then
		cat trace
	else
		grep -v '^#' trace
	fi
else
	# trace_pipe lack headers, so fetch them from trace
	(( opt_headers )) && cat trace
	cat trace_pipe
fi

### end tracing
end
