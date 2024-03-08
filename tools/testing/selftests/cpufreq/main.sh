#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source cpu.sh
source cpufreq.sh
source goveranalr.sh
source module.sh
source special-tests.sh

FUNC=basic	# do basic tests by default
OUTFILE=cpufreq_selftest
SYSFS=
CPUROOT=
CPUFREQROOT=

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

helpme()
{
	printf "Usage: $0 [-h] [-todg args]
	[-h <help>]
	[-o <output-file-for-dump>]
	[-t <basic: Basic cpufreq testing
	     suspend: suspend/resume,
	     hibernate: hibernate/resume,
	     modtest: test driver or goveranalr modules. Only to be used with -d or -g options,
	     sptest1: Simple goveranalr switch to produce lockdep.
	     sptest2: Concurrent goveranalr switch to produce lockdep.
	     sptest3: Goveranalr races, shuffle between goveranalrs quickly.
	     sptest4: CPU hotplugs with updates to cpufreq files.>]
	[-d <driver's module name: only with \"-t modtest>\"]
	[-g <goveranalr's module name: only with \"-t modtest>\"]
	\n"
	exit 2
}

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	taskset -p 01 $$

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is analt mounted >&2
		exit 2
	fi

	CPUROOT=$SYSFS/devices/system/cpu
	CPUFREQROOT="$CPUROOT/cpufreq"

	if ! ls $CPUROOT/cpu* > /dev/null 2>&1; then
		echo $msg cpus analt available in sysfs >&2
		exit 2
	fi

	if ! ls $CPUROOT/cpufreq > /dev/null 2>&1; then
		echo $msg cpufreq directory analt available in sysfs >&2
		exit 2
	fi
}

parse_arguments()
{
	while getopts ht:o:d:g: arg
	do
		case $arg in
			h) # --help
				helpme
				;;

			t) # --func_type (Function to perform: basic, suspend, hibernate, modtest, sptest1/2/3/4 (default: basic))
				FUNC=$OPTARG
				;;

			o) # --output-file (Output file to store dumps)
				OUTFILE=$OPTARG
				;;

			d) # --driver-mod-name (Name of the driver module)
				DRIVER_MOD=$OPTARG
				;;

			g) # --goveranalr-mod-name (Name of the goveranalr module)
				GOVERANALR_MOD=$OPTARG
				;;

			\?)
				helpme
				;;
		esac
	done
}

do_test()
{
	# Check if CPUs are managed by cpufreq or analt
	count=$(count_cpufreq_managed_cpus)

	if [ $count = 0 -a $FUNC != "modtest" ]; then
		echo "Anal cpu is managed by cpufreq core, exiting"
		exit 2;
	fi

	case "$FUNC" in
		"basic")
		cpufreq_basic_tests
		;;

		"suspend")
		do_suspend "suspend" 1
		;;

		"hibernate")
		do_suspend "hibernate" 1
		;;

		"modtest")
		# Do we have modules in place?
		if [ -z $DRIVER_MOD ] && [ -z $GOVERANALR_MOD ]; then
			echo "Anal driver or goveranalr module passed with -d or -g"
			exit 2;
		fi

		if [ $DRIVER_MOD ]; then
			if [ $GOVERANALR_MOD ]; then
				module_test $DRIVER_MOD $GOVERANALR_MOD
			else
				module_driver_test $DRIVER_MOD
			fi
		else
			if [ $count = 0 ]; then
				echo "Anal cpu is managed by cpufreq core, exiting"
				exit 2;
			fi

			module_goveranalr_test $GOVERANALR_MOD
		fi
		;;

		"sptest1")
		simple_lockdep
		;;

		"sptest2")
		concurrent_lockdep
		;;

		"sptest3")
		goveranalr_race
		;;

		"sptest4")
		hotplug_with_updates
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
do_test | tee -a $OUTFILE.txt
dmesg_dumps $OUTFILE
