#!/bin/bash

source cpu.sh
source cpufreq.sh
source governor.sh

FUNC=basic	# do basic tests by default
OUTFILE=cpufreq_selftest
SYSFS=
CPUROOT=
CPUFREQROOT=

helpme()
{
	printf "Usage: $0 [-h] [-to args]
	[-h <help>]
	[-o <output-file-for-dump>]
	[-t <basic: Basic cpufreq testing>]
	\n"
	exit 2
}

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit 2
	fi

	taskset -p 01 $$

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit 2
	fi

	CPUROOT=$SYSFS/devices/system/cpu
	CPUFREQROOT="$CPUROOT/cpufreq"

	if ! ls $CPUROOT/cpu* > /dev/null 2>&1; then
		echo $msg cpus not available in sysfs >&2
		exit 2
	fi

	if ! ls $CPUROOT/cpufreq > /dev/null 2>&1; then
		echo $msg cpufreq directory not available in sysfs >&2
		exit 2
	fi
}

parse_arguments()
{
	while getopts ht:o: arg
	do
		case $arg in
			h) # --help
				helpme
				;;

			t) # --func_type (Function to perform: basic (default: basic))
				FUNC=$OPTARG
				;;

			o) # --output-file (Output file to store dumps)
				OUTFILE=$OPTARG
				;;

			\?)
				helpme
				;;
		esac
	done
}

do_test()
{
	# Check if CPUs are managed by cpufreq or not
	count=$(count_cpufreq_managed_cpus)

	if [ $count = 0 ]; then
		echo "No cpu is managed by cpufreq core, exiting"
		exit 2;
	fi

	case "$FUNC" in
		"basic")
		cpufreq_basic_tests
		;;

		*)
		echo "Invalid [-f] function type"
		helpme
		;;
	esac
}

# clear dumps
# $1: file name
clear_dumps()
{
	echo "" > $1.txt
	echo "" > $1.dmesg_cpufreq.txt
	echo "" > $1.dmesg_full.txt
}

# $1: output file name
dmesg_dumps()
{
	dmesg | grep cpufreq >> $1.dmesg_cpufreq.txt

	# We may need the full logs as well
	dmesg >> $1.dmesg_full.txt
}

# Parse arguments
parse_arguments $@

# Make sure all requirements are met
prerequisite

# Run requested functions
clear_dumps $OUTFILE
do_test >> $OUTFILE.txt
dmesg_dumps $OUTFILE
