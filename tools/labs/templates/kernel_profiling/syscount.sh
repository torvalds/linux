#!/bin/bash
#
# syscount - count system calls.
#            Written using Linux perf_events (aka "perf").
#
# This is a proof-of-concept using perf_events capabilities for older kernel
# versions, that lack custom in-kernel aggregations. Once they exist, this
# script can be substantially rewritten and improved (lower overhead).
#
# USAGE: syscount [-chv] [-t top] {-p PID|-d seconds|command}
#
# Run "syscount -h" for full usage.
#
# REQUIREMENTS: Linux perf_events: add linux-tools-common, run "perf", then
# add any additional packages it requests. Also needs awk.
#
# OVERHEADS: Modes that report syscall names only (-c, -cp PID, -cd secs) have
# lower overhead, since they use in-kernel counts. Other modes which report
# process IDs (-cv) or process names (default) create a perf.data file for
# post processing, and you will see messages about it doing this. Beware of
# the file size (test for short durations, or use -c to see counts based on
# in-kernel counters), and gauge overheads based on the perf.data size.
#
# Note that this script delibrately does not pipe perf record into
# perf script, which would avoid perf.data, because it can create a feedback
# loop where the perf script syscalls are recorded. Hopefully there will be a
# fix for this in a later perf version, so perf.data can be skipped, or other
# kernel features to aggregate by process name in-kernel directly (eg, via
# eBPF, ktap, or SystemTap).
#
# From perf-tools: https://github.com/brendangregg/perf-tools
#
# See the syscount(8) man page (in perf-tools) for more info.
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
# 07-Jul-2014	Brendan Gregg	Created this.

# default variables
opt_count=0; opt_pid=0; opt_verbose=0; opt_cmd=0; opt_duration=0; opt_tail=0
tnum=; pid=; duration=; cmd=; cpus=-a; opts=; tcmd=cat; ttext=
trap '' INT QUIT TERM PIPE HUP

stdout_workaround=1	# needed for older perf versions
write_workaround=1	# needed for perf versions that trace their own writes

### parse options
while getopts cd:hp:t:v opt
do
	case $opt in
	c)	opt_count=1 ;;
	d)	opt_duration=1; duration=$OPTARG ;;
	p)	opt_pid=1; pid=$OPTARG ;;
	t)	opt_tail=1; tnum=$OPTARG ;;
	v)	opt_verbose=1 ;;
	h|?)	cat <<-END >&2
		USAGE: syscount [-chv] [-t top] {-p PID|-d seconds|command}
		       syscount                  # count by process name
		                -c               # show counts by syscall name
		                -h               # this usage message
		                -v               # verbose: shows PID
		                -p PID           # trace this PID only
		                -d seconds       # duration of trace
		                -t num           # show top number only
		                command          # run and trace this command
		  eg,
		        syscount                 # syscalls by process name
		        syscount -c              # syscalls by syscall name
		        syscount -d 5            # trace for 5 seconds
		        syscount -cp 923         # syscall names for PID 923
		        syscount -c ls           # syscall names for "ls"

		See the man page and example file for more info.
		END
		exit 1
	esac
done
shift $(( $OPTIND - 1 ))

### option logic
if (( $# > 0 )); then
	opt_cmd=1
	cmd="$@"
	cpus=
fi
if (( opt_pid + opt_duration + opt_cmd > 1 )); then
	echo >&2 "ERROR: Pick one of {-p PID|-n name|-d seconds|command}"
	exit 1
fi
if (( opt_tail )); then
	tcmd="tail -$tnum"
	ttext=" Top $tnum only."
fi
if (( opt_duration )); then
	cmd="sleep $duration"
	echo "Tracing for $duration seconds.$ttext.."
fi
if (( opt_pid )); then
	cpus=
	cmd="-p $pid"
	echo "Tracing PID $pid.$ttext.. Ctrl-C to end."
fi
(( opt_cmd )) && echo "Tracing while running: \"$cmd\".$ttext.."
(( opt_pid + opt_duration + opt_cmd == 0 )) && \
    echo "Tracing.$ttext.. Ctrl-C to end."
(( stdout_workaround )) && opts="-o /dev/stdout"

ulimit -n 32768		# often needed

### execute syscall name mode
if (( opt_count && ! opt_verbose )); then
	: ${cmd:=sleep 999999}
	out=$(./perf stat $opts -e 'syscalls:sys_enter_*' $cpus $cmd)
	printf "%-17s %8s\n" "SYSCALL" "COUNT"
	echo "$out" | awk '
	$1 && $2 ~ /syscalls:/ {
		sub("syscalls:sys_enter_", ""); sub(":", "")
		gsub(",", "")
		printf "%-17s %8s\n", $2, $1
	}' | sort -n -k2 | $tcmd
	exit
fi

### execute syscall name with pid mode
if (( opt_count && opt_verbose )); then
	if (( write_workaround )); then
		# this list must end in write to associate the filter
		tp=$(./perf list syscalls:sys_enter_* | awk '
		    $1 != "syscalls:sys_enter_write" &&  $1 ~ /syscalls:/ { printf "-e %s ", $1 }')
		tp="$tp -e syscalls:sys_enter_write"
		sh -c "./perf record $tp --filter 'common_pid != '\$\$ $cpus $cmd"
	else
		./perf record 'syscalls:sys_enter_*' $cpus $cmd
		# could also pipe direct to perf script
	fi

	printf "%-6s %-16s %-17s %8s\n" "PID" "COMM" "SYSCALL" "COUNT"
	./perf script --fields pid,comm,event | awk '$1 != "#" {
		sub("syscalls:sys_enter_", ""); sub(":", "")
		a[$1 ";" $2 ";" $3]++
	}
	END {
		for (k in a) {
			split(k, b, ";");
			printf "%-6s %-16s %-17s %8d\n", b[2], b[1], b[3], a[k]
		}
	}' | sort -n -k4 | $tcmd
	exit
fi

### execute process name mode
tp="-e raw_syscalls:sys_enter"
if (( write_workaround )); then
	sh -c "./perf record $tp --filter 'common_pid != '\$\$ $cpus $cmd"
else
	./perf record $tp $cpus $cmd
fi

if (( opt_verbose )); then
	printf "%-6s %-16s %8s\n" "PID" "COMM" "COUNT"
	./perf script --fields pid,comm | awk '$1 != "#" { a[$1 ";" $2]++ }
	END {
		for (k in a) {
			split(k, b, ";");
			printf "%-6s %-16s %8d\n", b[2], b[1],  a[k]
		}
	}' | sort -n -k3 | $tcmd
else
	printf "%-16s %8s\n" "COMM" "COUNT"
	./perf script --fields comm | awk '$1 != "#" { a[$1]++ }
	END {
		for (k in a) {
			printf "%-16s %8d\n", k,  a[k]
		}
	}' | sort -n -k2 | $tcmd
fi
