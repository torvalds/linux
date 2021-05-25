#!/bin/bash
#
# iolatency - summarize block device I/O latency as a histogram.
#             Written using Linux ftrace.
#
# This shows the distribution of latency, allowing modes and latency outliers
# to be identified and studied.
#
# USAGE: ./iolatency [-hQT] [-d device] [-i iotype] [interval [count]]
#
# REQUIREMENTS: FTRACE CONFIG and block:block_rq_* tracepoints, which you may
# already have on recent kernels.
#
# OVERHEAD: block device I/O issue and completion events are traced and buffered
# in-kernel, then processed and summarized in user space. There may be
# measurable overhead with this approach, relative to the block device IOPS.
#
# This was written as a proof of concept for ftrace.
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
# 20-Jul-2014	Brendan Gregg	Created this.

### default variables
tracing=/sys/kernel/debug/tracing
flock=/var/tmp/.ftrace-lock
bufsize_kb=4096
opt_device=0; device=; opt_iotype=0; iotype=; opt_timestamp=0
opt_interval=0; interval=1; opt_count=0; count=0; opt_queue=0
trap ':' INT QUIT TERM PIPE HUP	# sends execution to end tracing section

function usage {
	cat <<-END >&2
	USAGE: iolatency [-hQT] [-d device] [-i iotype] [interval [count]]
	                 -d device       # device string (eg, "202,1)
	                 -i iotype       # match type (eg, '*R*' for all reads)
	                 -Q              # use queue insert as start time
	                 -T              # timestamp on output
	                 -h              # this usage message
	                 interval        # summary interval, seconds (default 1)
	                 count           # number of summaries
	  eg,
	       iolatency                 # summarize latency every second
	       iolatency -Q              # include block I/O queue time
	       iolatency 5 2             # 2 x 5 second summaries
	       iolatency -i '*R*'        # trace reads
	       iolatency -d 202,1        # trace device 202,1 only

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
	warn "echo 0 > events/block/$b_start/enable"
	warn "echo 0 > events/block/block_rq_complete/enable"
	if (( opt_device || opt_iotype )); then
		warn "echo 0 > events/block/$b_start/filter"
		warn "echo 0 > events/block/block_rq_complete/filter"
	fi
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
while getopts d:hi:QT opt
do
	case $opt in
	d)	opt_device=1; device=$OPTARG ;;
	i)	opt_iotype=1; iotype=$OPTARG ;;
	Q)	opt_queue=1 ;;
	T)	opt_timestamp=1 ;;
	h|?)	usage ;;
	esac
done
shift $(( $OPTIND - 1 ))
if (( $# )); then
	opt_interval=1
	interval=$1
	shift
fi
if (( $# )); then
	opt_count=1
	count=$1
fi
if (( opt_device )); then
	major=${device%,*}
	minor=${device#*,}
	dev=$(( (major << 20) + minor ))
fi
if (( opt_queue )); then
	b_start=block_rq_insert
else
	b_start=block_rq_issue
fi

### select awk
[[ -x /usr/bin/mawk ]] && awk='mawk -W interactive' || awk=awk

### check permissions
cd $tracing || die "ERROR: accessing tracing. Root user? Kernel has FTRACE?
    debugfs mounted? (mount -t debugfs debugfs /sys/kernel/debug)"

### ftrace lock
[[ -e $flock ]] && die "ERROR: ftrace may be in use by PID $(cat $flock) $flock"
echo $$ > $flock || die "ERROR: unable to write $flock."
wroteflock=1

### setup and begin tracing
warn "echo nop > current_tracer"
warn "echo $bufsize_kb > buffer_size_kb"
filter=
if (( opt_iotype )); then
	filter="rwbs ~ \"$iotype\""
fi
if (( opt_device )); then
	[[ "$filter" != "" ]] && filter="$filter && "
	filter="${filter}dev == $dev"
fi
if (( opt_iotype || opt_device )); then
	if ! echo "$filter" > events/block/$b_start/filter || \
	    ! echo "$filter" > events/block/block_rq_complete/filter
	then
		edie "ERROR: setting -d or -t filter. Exiting."
	fi
fi
if ! echo 1 > events/block/$b_start/enable || \
    ! echo 1 > events/block/block_rq_complete/enable; then
	edie "ERROR: enabling block I/O tracepoints. Exiting."
fi
etext=
(( !opt_count )) && etext=" Ctrl-C to end."
echo "Tracing block I/O. Output every $interval seconds.$etext"

#
# Determine output format. It may be one of the following (newest first):
#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION
#           TASK-PID    CPU#    TIMESTAMP  FUNCTION
# To differentiate between them, the number of header fields is counted,
# and an offset set, to skip the extra column when needed.
#
offset=$($awk 'BEGIN { o = 0; }
	$1 == "#" && $2 ~ /TASK/ && NF == 6 { o = 1; }
	$2 ~ /TASK/ { print o; exit }' trace)

### print trace buffer
warn "echo > trace"
i=0
while (( !opt_count || (i < count) )); do
	(( i++ ))
	sleep $interval

	# snapshots were added in 3.10
	if [[ -x snapshot ]]; then
		echo 1 > snapshot
		echo > trace
		cat snapshot
	else
		cat trace
		echo > trace
	fi

	(( opt_timestamp )) && printf "time %(%H:%M:%S)T:\n" -1
	echo "tick"
done | \
$awk -v o=$offset -v opt_timestamp=$opt_timestamp -v b_start=$b_start '
	function star(sval, smax, swidth) {
		stars = ""
		if (smax == 0) return ""
		for (si = 0; si < (swidth * sval / smax); si++) {
			stars = stars "#"
		}
		return stars
	}

	BEGIN { max_i = 0 }

	# common fields
	$1 != "#" {
		time = $(3+o); sub(":", "", time)
		dev = $(5+o)
	}

	# block I/O request
	$1 != "#" && $0 ~ b_start {
		#
		# example: (fields1..4+o) 202,1 W 0 () 12862264 + 8 [tar]
		# The cmd field "()" might contain multiple words (hex),
		# hence stepping from the right (NF-3).
		#
		loc = $(NF-3)
		starts[dev, loc] = time
		next
	}

	# block I/O completion
	$1 != "#" && $0 ~ /rq_complete/ {
		#
		# example: (fields1..4+o) 202,1 W () 12862256 + 8 [0]
		#
		dir = $(6+o)
		loc = $(NF-3)

		if (starts[dev, loc] > 0) {
			latency_ms = 1000 * (time - starts[dev, loc])
			i = 0
			for (ms = 1; latency_ms > ms; ms *= 2) { i++ }
			hist[i]++
			if (i > max_i)
				max_i = i
			delete starts[dev, loc]
		}
		next
	}

	# timestamp
	$1 == "time" {
		lasttime = $2
	}

	# print summary
	$1 == "tick" {
		print ""
		if (opt_timestamp)
			print lasttime

		# find max value
		max_v = 0
		for (i = 0; i <= max_i; i++) {
			if (hist[i] > max_v)
				max_v = hist[i]
		}

		# print histogram
		printf "%8s .. %-8s: %-8s |%-38s|\n", ">=(ms)", "<(ms)",
		    "I/O", "Distribution"
		ms = 1
		from = 0
		for (i = 0; i <= max_i; i++) {
			printf "%8d -> %-8d: %-8d |%-38s|\n", from, ms,
			    hist[i], star(hist[i], max_v, 38)
			from = ms
			ms *= 2
		}
		fflush()
		delete hist
		delete starts	# invalid if events missed between snapshots
		max_i = 0
	}

	$0 ~ /LOST.*EVENTS/ { print "WARNING: " $0 > "/dev/stderr" }
'

### end tracing
end
