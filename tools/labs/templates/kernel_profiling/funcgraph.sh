#!/bin/bash
#
# funcgraph - trace kernel function graph, showing child function calls.
#             Uses Linux ftrace.
#
# This is an exploratory tool that shows the graph of child function calls
# for a given kernel function. This can cost moderate overhead to execute, and
# should only be used to understand kernel behavior for a given function before
# using other, lower overhead tools. This is a proof of concept using Linux
# ftrace capabilities on older kernels.
#
# USAGE: funcgraph [-aCDhHPtT] [-m maxdepth] [-p PID] [-L TID] [-d secs] funcstring
#
# Run "funcgraph -h" for full usage.
#
# The output format is the same as the ftrace function graph trace format,
# described in the kernel source under Documentation/trace/ftrace.txt.
# Note that the output may be shuffled when different CPU buffers are read;
# check the CPU column for changes, or include timestamps (-t) and post sort.
#
# The "-d duration" mode leaves the trace data in the kernel buffer, and
# only reads it at the end. If the trace data is large, beware of exhausting
# buffer space (/sys/kernel/debug/tracing/buffer_size_kb) and losing data.
#
# Also beware of feedback loops: tracing tcp* functions over an ssh session,
# or writing ext4* functions to an ext4 file system. For the former, tcp
# trace data could be redirected to a file (as in the usage message). For
# the latter, trace to the screen or a different file system.
#
# WARNING: This uses dynamic tracing of kernel functions, and could cause
# kernel panics or freezes. Test, and know what you are doing, before use.
#
# OVERHEADS: This tool causes moderate to high overheads. Use with caution for
# exploratory purposes, then switch to lower overhead techniques based on
# findings. It's expected that the kernel will run at least 50% slower while
# this tool is running -- even while no output is being generated. This is
# because ALL kernel functions are traced, and filtered based on the function
# of interest. When output is generated, it can generate many lines quickly
# depending on the traced event. Such data will cause performance overheads.
# This also works without buffering by default, printing function events
# as they happen (uses trace_pipe), context switching and consuming CPU to do
# so. If needed, you can try the "-d secs" option, which buffers events
# instead, reducing overhead. If you think the buffer option is losing events,
# try increasing the buffer size (buffer_size_kb).
#
# From perf-tools: https://github.com/brendangregg/perf-tools
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
# 12-Jul-2014	Brendan Gregg	Created this.

### default variables
tracing=/sys/kernel/debug/tracing
flock=/var/tmp/.ftrace-lock
opt_duration=0; duration=; opt_pid=0; pid=; opt_tid=0; tid=; pidtext=
opt_headers=0; opt_proc=0; opt_time=0; opt_tail=0; opt_nodur=0; opt_cpu=0
opt_max=0; max=0
trap ':' INT QUIT TERM PIPE HUP	# sends execution to end tracing section

function usage {
	cat <<-END >&2
	USAGE: funcgraph [-aCDhHPtT] [-m maxdepth] [-p PID] [-L TID] [-d secs] funcstring
	                 -a              # all info (same as -HPt)
	                 -C              # measure on-CPU time only
	                 -d seconds      # trace duration, and use buffers
	                 -D              # do not show function duration
	                 -h              # this usage message
	                 -H              # include column headers
	                 -m maxdepth     # max stack depth to show
	                 -p PID          # trace when this pid is on-CPU
	                 -L TID          # trace when this thread is on-CPU
	                 -P              # show process names & PIDs
	                 -t              # show timestamps
	                 -T              # comment function tails
	  eg,
	       funcgraph do_nanosleep    # trace do_nanosleep() and children
	       funcgraph -m 3 do_sys_open # trace do_sys_open() to 3 levels only
	       funcgraph -a do_sys_open    # include timestamps and process name
	       funcgraph -p 198 do_sys_open # trace vfs_read() for PID 198 only
	       funcgraph -d 1 do_sys_open >out # trace 1 sec, then write to file

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

	(( opt_time )) && warn "echo nofuncgraph-abstime > trace_options"
	(( opt_proc )) && warn "echo nofuncgraph-proc > trace_options"
	(( opt_tail )) && warn "echo nofuncgraph-tail > trace_options"
	(( opt_nodur )) && warn "echo funcgraph-duration > trace_options"
	(( opt_cpu )) && warn "echo sleep-time > trace_options"

	warn "echo nop > current_tracer"
	(( opt_pid || opt_tid )) && warn "echo > set_ftrace_pid"
	(( opt_max )) && warn "echo 0 > max_graph_depth"
	warn "echo > set_graph_function"
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
while getopts aCd:DhHm:p:L:PtT opt
do
	case $opt in
	a)	opt_headers=1; opt_proc=1; opt_time=1 ;;
	C)	opt_cpu=1; ;;
	d)	opt_duration=1; duration=$OPTARG ;;
	D)	opt_nodur=1; ;;
	m)	opt_max=1; max=$OPTARG ;;
	p)	opt_pid=1; pid=$OPTARG ;;
	L)	opt_tid=1; tid=$OPTARG ;;
	H)	opt_headers=1; ;;
	P)	opt_proc=1; ;;
	t)	opt_time=1; ;;
	T)	opt_tail=1; ;;
	h|?)	usage ;;
	esac
done
shift $(( $OPTIND - 1 ))

### option logic
(( $# == 0 )) && usage
(( opt_pid && opt_tid )) && edie "ERROR: You can use -p or -L but not both."
funcs="$1"
(( opt_pid )) && pidtext=" for PID $pid"
(( opt_tid )) && pidtext=" for TID $tid"
if (( opt_duration )); then
	echo "Tracing \"$funcs\"$pidtext for $duration seconds..."
else
	echo "Tracing \"$funcs\"$pidtext... Ctrl-C to end."
fi

### check permissions
cd $tracing || die "ERROR: accessing tracing. Root user? Kernel has FTRACE?
    debugfs mounted? (mount -t debugfs debugfs /sys/kernel/debug)"

### ftrace lock
[[ -e $flock ]] && die "ERROR: ftrace may be in use by PID $(cat $flock) $flock"
echo $$ > $flock || die "ERROR: unable to write $flock."
wroteflock=1

### setup and commence tracing
sysctl -q kernel.ftrace_enabled=1	# doesn't set exit status
read mode < current_tracer
[[ "$mode" != "nop" ]] && edie "ERROR: ftrace active (current_tracer=$mode)"
if (( opt_max )); then
	if ! echo $max > max_graph_depth; then
		edie "ERROR: setting -m $max. Older kernel version? Exiting."
	fi
fi
if (( opt_pid )); then
    echo > set_ftrace_pid
    # ftrace expects kernel pids, which are thread ids
    for tid in /proc/$pid/task/*; do
        if ! echo ${tid##*/} >> set_ftrace_pid; then
            edie "ERROR: setting -p $pid (PID exist?). Exiting."
        fi
    done
fi
if (( opt_tid )); then
    if ! echo $tid > set_ftrace_pid; then
        edie "ERROR: setting -L $tid (TID exist?). Exiting."
    fi
fi
if ! echo > set_ftrace_filter; then
	edie "ERROR: writing to set_ftrace_filter. Exiting."
fi
if ! echo "$funcs" > set_graph_function; then
	edie "ERROR: enabling \"$funcs\". Exiting."
fi
if ! echo function_graph > current_tracer; then
	edie "ERROR: setting current_tracer to \"function\". Exiting."
fi
if (( opt_cpu )); then
	if ! echo nosleep-time > trace_options; then
		edie "ERROR: setting -C (nosleep-time). Exiting."
	fi
fi
# the following must be done after setting current_tracer
if (( opt_time )); then
	if ! echo funcgraph-abstime > trace_options; then
		edie "ERROR: setting -t (funcgraph-abstime). Exiting."
	fi
fi
if (( opt_proc )); then
	if ! echo funcgraph-proc > trace_options; then
		edie "ERROR: setting -P (funcgraph-proc). Exiting."
	fi
fi
if (( opt_tail )); then
	if ! echo funcgraph-tail > trace_options; then
		edie "ERROR: setting -T (funcgraph-tail). Old kernel? Exiting."
	fi
fi
if (( opt_nodur )); then
	if ! echo nofuncgraph-duration > trace_options; then
		edie "ERROR: setting -D (nofuncgraph-duration). Exiting."
	fi
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
