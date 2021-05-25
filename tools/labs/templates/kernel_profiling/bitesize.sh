#!/bin/bash
#
# bitesize - show disk I/O size as a histogram.
#            Written using Linux perf_events (aka "perf").
#
# This can be used to characterize the distribution of block device I/O
# sizes. To study I/O in more detail, see iosnoop(8).
#
# USAGE: bitesize [-h] [-b buckets] [seconds]
#    eg,
#        ./bitesize 10
#
# Run "bitesize -h" for full usage.
#
# REQUIREMENTS: perf_events and block:block_rq_issue tracepoint, which you may
# already have on recent kernels.
#
# This uses multiple counting tracepoints with different filters, one for each
# histogram bucket. While this is summarized in-kernel, the use of multiple
# tracepoints does add addiitonal overhead, which is more evident if you add
# more buckets. In the future this functionality will be available in an
# efficient way in the kernel, and this tool can be rewritten.
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
# 22-Jul-2014	Brendan Gregg	Created this.

duration=0
buckets=(1 8 64 128)
secsz=512
trap ':' INT QUIT TERM PIPE HUP

function usage {
	cat <<-END >&2
	USAGE: bitesize [-h] [-b buckets] [seconds]
	                 -b buckets      # specify histogram buckets (Kbytes)
	                 -h              # this usage message
	   eg,
	       bitesize                  # trace I/O size until Ctrl-C
	       bitesize 10               # trace I/O size for 10 seconds
	       bitesize -b "8 16 32"     # specify custom bucket points
END
	exit
}

function die {
	echo >&2 "$@"
	exit 1
}

### process options
while getopts b:h opt
do
	case $opt in
	b)	buckets=($OPTARG) ;;
	h|?)	usage ;;
	esac
done
shift $(( $OPTIND - 1 ))
tpoint=block:block_rq_issue
var=nr_sector
duration=$1

### convert buckets (Kbytes) to disk sectors
i=0
sectors=(${buckets[*]})
((max_i = ${#buckets[*]} - 1))
while (( i <= max_i )); do
	(( sectors[$i] = ${sectors[$i]} * 1024 / $secsz ))
	# avoid negative array index errors for old version bash
	if (( i > 0 ));then
		if (( ${sectors[$i]} <= ${sectors[$i - 1]} )); then
			die "ERROR: bucket list must increase in size."
		fi
	fi
	(( i++ ))
done

### build list of tracepoints and filters for each histogram bucket
max_b=${buckets[$max_i]}
max_s=${sectors[$max_i]}
tpoints="-e $tpoint --filter \"$var < ${sectors[0]}\""
awkarray=
i=0
while (( i < max_i )); do
	tpoints="$tpoints -e $tpoint --filter \"$var >= ${sectors[$i]} && "
	tpoints="$tpoints $var < ${sectors[$i + 1]}\""
	awkarray="$awkarray buckets[$i]=${buckets[$i]};"
	(( i++ ))
done
awkarray="$awkarray buckets[$max_i]=${buckets[$max_i]};"
tpoints="$tpoints -e $tpoint --filter \"$var >= ${sectors[$max_i]}\""

### prepare to run
if (( duration )); then
	etext="for $duration seconds"
	cmd="sleep $duration"
else
	etext="until Ctrl-C"
	cmd="sleep 999999"
fi
echo "Tracing block I/O size (bytes), $etext..."

### run perf
out="-o /dev/stdout"	# a workaround needed in linux 3.2; not by 3.4.15
stat=$(eval ./perf stat $tpoints -a $out $cmd 2>&1)

### find max value for ASCII histogram
most=$(echo "$stat" | awk -v tpoint=$tpoint '
	$2 == tpoint { gsub(/,/, ""); if ($1 > m) { m = $1 } }
	END { print m }'
)

### process output
echo
echo "$stat" | awk -v tpoint=$tpoint -v max_i=$max_i -v most=$most '
	function star(sval, smax, swidth) {
		stars = ""
		# using int could avoid error on gawk
		if (int(smax) == 0) return ""
		for (si = 0; si < (swidth * sval / smax); si++) {
			stars = stars "#"
		}
		return stars
	}
	BEGIN {
		'"$awkarray"'
		printf("            %-15s: %-8s %s\n", "Kbytes", "I/O",
		    "Distribution")
	}
	/Performance counter stats/ { i = -1 }
	# reverse order of rule set is important
	{ ok = 0 }
	$2 == tpoint { num = $1; gsub(/,/, "", num); ok = 1 }
	ok && i >= max_i {
		printf("   %10.1f -> %-10s: %-8s |%-38s|\n",
		    buckets[i], "", num, star(num, most, 38))
		next
	}
	ok && i >= 0 && i < max_i {
		printf("   %10.1f -> %-10.1f: %-8s |%-38s|\n",
		    buckets[i], buckets[i+1] - 0.1, num,
		    star(num, most, 38))
		i++
		next
	}
	ok && i == -1 {
		printf("   %10s -> %-10.1f: %-8s |%-38s|\n", "",
		    buckets[0] - 0.1, num, star(num, most, 38))
		i++
	}
'
