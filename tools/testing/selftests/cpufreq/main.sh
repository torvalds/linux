#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source cpu.sh
source cpufreq.sh
source governor.sh
source module.sh
source special-tests.sh

DIR="$(dirname $(readlink -f "$0"))"
source "${DIR}"/../kselftest/ktap_helpers.sh

FUNC=basic	# do basic tests by default
OUTFILE=cpufreq_selftest
SYSFS=
CPUROOT=
CPUFREQROOT=

helpme()
{
	printf "Usage: $0 [-h] [-todg args]
	[-h <help>]
	[-o <output-file-for-dump>]
	[-t <basic: Basic cpufreq testing
	     suspend: suspend/resume,
	     hibernate: hibernate/resume,
	     suspend_rtc: suspend/resume back using the RTC wakeup alarm,
	     hibernate_rtc: hibernate/resume back using the RTC wakeup alarm,
	     modtest: test driver or governor modules. Only to be used with -d or -g options,
	     sptest1: Simple governor switch to produce lockdep.
	     sptest2: Concurrent governor switch to produce lockdep.
	     sptest3: Governor races, shuffle between governors quickly.
	     sptest4: CPU hotplugs with updates to cpufreq files.>]
	[-d <driver's module name: only with \"-t modtest>\"]
	[-g <governor's module name: only with \"-t modtest>\"]
	\n"
	exit "${KSFT_FAIL}"
}

prerequisite()
{
	msg="skip all tests:"

	if [ $UID != 0 ]; then
		ktap_skip_all "$msg must be run as root"
		exit "${KSFT_SKIP}"
	fi

	taskset -p 01 $$

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		ktap_skip_all "$msg sysfs is not mounted"
		exit "${KSFT_SKIP}"
	fi

	CPUROOT=$SYSFS/devices/system/cpu
	CPUFREQROOT="$CPUROOT/cpufreq"

	if ! ls $CPUROOT/cpu* > /dev/null 2>&1; then
		ktap_skip_all "$msg cpus not available in sysfs"
		exit "${KSFT_SKIP}"
	fi

	if ! ls $CPUROOT/cpufreq > /dev/null 2>&1; then
		ktap_skip_all "$msg cpufreq directory not available in sysfs"
		exit "${KSFT_SKIP}"
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

			t) # --func_type (Function to perform: basic, suspend, hibernate,
			   # suspend_rtc, hibernate_rtc, modtest, sptest1/2/3/4 (default: basic))
				FUNC=$OPTARG
				;;

			o) # --output-file (Output file to store dumps)
				OUTFILE=$OPTARG
				;;

			d) # --driver-mod-name (Name of the driver module)
				DRIVER_MOD=$OPTARG
				;;

			g) # --governor-mod-name (Name of the governor module)
				GOVERNOR_MOD=$OPTARG
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

	if [ $count = 0 -a $FUNC != "modtest" ]; then
		ktap_exit_fail_msg "No cpu is managed by cpufreq core, exiting"
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

		"suspend_rtc")
                do_suspend "suspend" 1 rtc
                ;;

                "hibernate_rtc")
                do_suspend "hibernate" 1 rtc
                ;;

		"modtest")
		# Do we have modules in place?
		if [ -z $DRIVER_MOD ] && [ -z $GOVERNOR_MOD ]; then
			ktap_exit_fail_msg "No driver or governor module passed with -d or -g"
		fi

		if [ $DRIVER_MOD ]; then
			if [ $GOVERNOR_MOD ]; then
				module_test $DRIVER_MOD $GOVERNOR_MOD
			else
				module_driver_test $DRIVER_MOD
			fi
		else
			if [ $count = 0 ]; then
				ktap_exit_fail_msg "No cpu is managed by cpufreq core, exiting"
			fi

			module_governor_test $GOVERNOR_MOD
		fi
		;;

		"sptest1")
		simple_lockdep
		;;

		"sptest2")
		concurrent_lockdep
		;;

		"sptest3")
		governor_race
		;;

		"sptest4")
		hotplug_with_updates
		;;

		*)
		ktap_print_msg "Invalid [-f] function type"
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

ktap_print_header

# Parse arguments
parse_arguments $@

ktap_set_plan 1

# Make sure all requirements are met
prerequisite

# Run requested functions
clear_dumps $OUTFILE
do_test | tee -a $OUTFILE.txt
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    exit ${PIPESTATUS[0]};
fi
dmesg_dumps $OUTFILE

ktap_test_pass "Completed successfully"

ktap_print_totals
exit "${KSFT_PASS}"
