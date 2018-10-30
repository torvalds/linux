#!/bin/bash

# Sergey Senozhatsky, 2015
# sergey.senozhatsky.work@gmail.com
#
# This software is licensed under the terms of the GNU General Public
# License version 2, as published by the Free Software Foundation, and
# may be copied, distributed, and modified under those terms.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.


# This program is intended to plot a `slabinfo -X' stats, collected,
# for example, using the following command:
#   while [ 1 ]; do slabinfo -X >> stats; sleep 1; done
#
# Use `slabinfo-gnuplot.sh stats' to pre-process collected records
# and generate graphs (totals, slabs sorted by size, slabs sorted
# by size).
#
# Graphs can be [individually] regenerate with different ranges and
# size (-r %d,%d and -s %d,%d options).
#
# To visually compare N `totals' graphs, do
# slabinfo-gnuplot.sh -t FILE1-totals FILE2-totals ... FILEN-totals
#

min_slab_name_size=11
xmin=0
xmax=0
width=1500
height=700
mode=preprocess

usage()
{
	echo "Usage: [-s W,H] [-r MIN,MAX] [-t|-l] FILE1 [FILE2 ..]"
	echo "FILEs must contain 'slabinfo -X' samples"
	echo "-t 			- plot totals for FILE(s)"
	echo "-l 			- plot slabs stats for FILE(s)"
	echo "-s %d,%d		- set image width and height"
	echo "-r %d,%d		- use data samples from a given range"
}

check_file_exist()
{
	if [ ! -f "$1" ]; then
		echo "File '$1' does not exist"
		exit 1
	fi
}

do_slabs_plotting()
{
	local file=$1
	local out_file
	local range="every ::$xmin"
	local xtic=""
	local xtic_rotate="norotate"
	local lines=2000000
	local wc_lines

	check_file_exist "$file"

	out_file=`basename "$file"`
	if [ $xmax -ne 0 ]; then
		range="$range::$xmax"
		lines=$((xmax-xmin))
	fi

	wc_lines=`cat "$file" | wc -l`
	if [ $? -ne 0 ] || [ "$wc_lines" -eq 0 ] ; then
		wc_lines=$lines
	fi

	if [ "$wc_lines" -lt "$lines" ]; then
		lines=$wc_lines
	fi

	if [ $((width / lines)) -gt $min_slab_name_size ]; then
		xtic=":xtic(1)"
		xtic_rotate=90
	fi

gnuplot -p << EOF
#!/usr/bin/env gnuplot

set terminal png enhanced size $width,$height large
set output '$out_file.png'
set autoscale xy
set xlabel 'samples'
set ylabel 'bytes'
set style histogram columnstacked title textcolor lt -1
set style fill solid 0.15
set xtics rotate $xtic_rotate
set key left above Left title reverse

plot "$file" $range u 2$xtic title 'SIZE' with boxes,\
	'' $range u 3 title 'LOSS' with boxes
EOF

	if [ $? -eq 0 ]; then
		echo "$out_file.png"
	fi
}

do_totals_plotting()
{
	local gnuplot_cmd=""
	local range="every ::$xmin"
	local file=""

	if [ $xmax -ne 0 ]; then
		range="$range::$xmax"
	fi

	for i in "${t_files[@]}"; do
		check_file_exist "$i"

		file="$file"`basename "$i"`
		gnuplot_cmd="$gnuplot_cmd '$i' $range using 1 title\
			'$i Memory usage' with lines,"
		gnuplot_cmd="$gnuplot_cmd '' $range using 2 title \
			'$i Loss' with lines,"
	done

gnuplot -p << EOF
#!/usr/bin/env gnuplot

set terminal png enhanced size $width,$height large
set autoscale xy
set output '$file.png'
set xlabel 'samples'
set ylabel 'bytes'
set key left above Left title reverse

plot $gnuplot_cmd
EOF

	if [ $? -eq 0 ]; then
		echo "$file.png"
	fi
}

do_preprocess()
{
	local out
	local lines
	local in=$1

	check_file_exist "$in"

	# use only 'TOP' slab (biggest memory usage or loss)
	let lines=3
	out=`basename "$in"`"-slabs-by-loss"
	`cat "$in" | grep -A "$lines" 'Slabs sorted by loss' |\
		egrep -iv '\-\-|Name|Slabs'\
		| awk '{print $1" "$4+$2*$3" "$4}' > "$out"`
	if [ $? -eq 0 ]; then
		do_slabs_plotting "$out"
	fi

	let lines=3
	out=`basename "$in"`"-slabs-by-size"
	`cat "$in" | grep -A "$lines" 'Slabs sorted by size' |\
		egrep -iv '\-\-|Name|Slabs'\
		| awk '{print $1" "$4" "$4-$2*$3}' > "$out"`
	if [ $? -eq 0 ]; then
		do_slabs_plotting "$out"
	fi

	out=`basename "$in"`"-totals"
	`cat "$in" | grep "Memory used" |\
		awk '{print $3" "$7}' > "$out"`
	if [ $? -eq 0 ]; then
		t_files[0]=$out
		do_totals_plotting
	fi
}

parse_opts()
{
	local opt

	while getopts "tlr::s::h" opt; do
		case $opt in
			t)
				mode=totals
				;;
			l)
				mode=slabs
				;;
			s)
				array=(${OPTARG//,/ })
				width=${array[0]}
				height=${array[1]}
				;;
			r)
				array=(${OPTARG//,/ })
				xmin=${array[0]}
				xmax=${array[1]}
				;;
			h)
				usage
				exit 0
				;;
			\?)
				echo "Invalid option: -$OPTARG" >&2
				exit 1
				;;
			:)
				echo "-$OPTARG requires an argument." >&2
				exit 1
				;;
		esac
	done

	return $OPTIND
}

parse_args()
{
	local idx=0
	local p

	for p in "$@"; do
		case $mode in
			preprocess)
				files[$idx]=$p
				idx=$idx+1
				;;
			totals)
				t_files[$idx]=$p
				idx=$idx+1
				;;
			slabs)
				files[$idx]=$p
				idx=$idx+1
				;;
		esac
	done
}

parse_opts "$@"
argstart=$?
parse_args "${@:$argstart}"

if [ ${#files[@]} -eq 0 ] && [ ${#t_files[@]} -eq 0 ]; then
	usage
	exit 1
fi

case $mode in
	preprocess)
		for i in "${files[@]}"; do
			do_preprocess "$i"
		done
		;;
	totals)
		do_totals_plotting
		;;
	slabs)
		for i in "${files[@]}"; do
			do_slabs_plotting "$i"
		done
		;;
	*)
		echo "Unknown mode $mode" >&2
		usage
		exit 1
	;;
esac
