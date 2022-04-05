#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later


# Author/Copyright(c): 2009, Thomas Renninger <trenn@suse.de>, Novell Inc.

# Helper script to easily create nice plots of your cpufreq-bench results

dir=`mktemp -d`
output_file="cpufreq-bench.png"
global_title="cpufreq-bench plot"
picture_type="jpeg"
file[0]=""

function usage()
{
    echo "cpufreq-bench_plot.sh [OPTIONS] logfile [measure_title] [logfile [measure_title]] ...]"
    echo
    echo "Options"
    echo "   -o output_file"
    echo "   -t global_title"
    echo "   -p picture_type [jpeg|gif|png|postscript|...]"
    exit 1
}

if [ $# -eq 0 ];then
	echo "No benchmark results file provided"
	echo
	usage
fi

while getopts o:t:p: name ; do
    case $name in
	o)
	    output_file="$OPTARG".$picture_type
	    ;;
	t)
	    global_title="$OPTARG"
	    ;;
	p)
	    picture_type="$OPTARG"
	    ;;
        ?)
	    usage
	    ;;
    esac
done
shift $(($OPTIND -1))

plots=0
while [ "$1" ];do
    if [ ! -f "$1" ];then
	echo "File $1 does not exist"
	usage
    fi
    file[$plots]="$1"
    title[$plots]="$2"
    # echo "File: ${file[$plots]} - ${title[plots]}"
    shift;shift
    plots=$((plots + 1))
done

echo "set terminal $picture_type"	>> $dir/plot_script.gpl
echo "set output \"$output_file\""	>> $dir/plot_script.gpl
echo "set title \"$global_title\""	>> $dir/plot_script.gpl
echo "set xlabel \"sleep/load time\""	>> $dir/plot_script.gpl
echo "set ylabel \"Performance (%)\""	>> $dir/plot_script.gpl

for((plot=0;plot<$plots;plot++));do

    # Sanity check
    ###### I am to dump to get this redirected to stderr/stdout in one awk call... #####
    cat ${file[$plot]} |grep -v "^#" |awk '{if ($2 != $3) printf("Error in measure %d:Load time %s does not equal sleep time %s, plot will not be correct\n", $1, $2, $3); ERR=1}'
    ###### I am to dump to get this redirected in one awk call... #####

    # Parse out load time (which must be equal to sleep time for a plot), divide it by 1000
    # to get ms and parse out the performance in percentage and write it to a temp file for plotting
    cat ${file[$plot]} |grep -v "^#" |awk '{printf "%lu %.1f\n",$2/1000, $6}' >$dir/data_$plot

    if [ $plot -eq 0 ];then
	echo -n "plot " >> $dir/plot_script.gpl
    fi
    echo -n "\"$dir/data_$plot\" title \"${title[$plot]}\" with lines" >> $dir/plot_script.gpl
    if [ $(($plot + 1)) -ne $plots ];then
	echo -n ", " >> $dir/plot_script.gpl
    fi
done
echo >> $dir/plot_script.gpl

gnuplot $dir/plot_script.gpl
rm -r $dir
