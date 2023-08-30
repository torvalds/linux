#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# protect against multiple inclusion
if [ $FILE_MAIN ]; then
	return 0
else
	FILE_MAIN=DONE
fi

source basic.sh
source tbench.sh
source gitsource.sh

# amd-pstate-ut only run on x86/x86_64 AMD systems.
ARCH=$(uname -m 2>/dev/null | sed -e 's/i.86/x86/' -e 's/x86_64/x86/')
VENDOR=$(cat /proc/cpuinfo | grep -m 1 'vendor_id' | awk '{print $NF}')

msg="Skip all tests:"
FUNC=all
OUTFILE=selftest
OUTFILE_TBENCH="$OUTFILE.tbench"
OUTFILE_GIT="$OUTFILE.gitsource"

SYSFS=
CPUROOT=
CPUFREQROOT=
MAKE_CPUS=

TIME_LIMIT=100
PROCESS_NUM=128
LOOP_TIMES=3
TRACER_INTERVAL=10
CURRENT_TEST=amd-pstate
COMPARATIVE_TEST=

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
all_scaling_names=("acpi-cpufreq" "amd-pstate")

# Get current cpufreq scaling driver name
scaling_name()
{
	if [ "$COMPARATIVE_TEST" = "" ]; then
		echo "$CURRENT_TEST"
	else
		echo "$COMPARATIVE_TEST"
	fi
}

# Counts CPUs with cpufreq directories
count_cpus()
{
	count=0;

	for cpu in `ls $CPUROOT | grep "cpu[0-9].*"`; do
		if [ -d $CPUROOT/$cpu/cpufreq ]; then
			let count=count+1;
		fi
	done

	echo $count;
}

# $1: policy
find_current_governor()
{
	cat $CPUFREQROOT/$1/scaling_governor
}

backup_governor()
{
	policies=$(ls $CPUFREQROOT| grep "policy[0-9].*")
	for policy in $policies; do
		cur_gov=$(find_current_governor $policy)
		echo "$policy $cur_gov" >> $OUTFILE.backup_governor.log
	done

	printf "Governor $cur_gov backup done.\n"
}

restore_governor()
{
	i=0;

	policies=$(awk '{print $1}' $OUTFILE.backup_governor.log)
	for policy in $policies; do
		let i++;
		governor=$(sed -n ''$i'p' $OUTFILE.backup_governor.log | awk '{print $2}')

		# switch governor
		echo $governor > $CPUFREQROOT/$policy/scaling_governor
	done

	printf "Governor restored to $governor.\n"
}

# $1: governor
switch_governor()
{
	policies=$(ls $CPUFREQROOT| grep "policy[0-9].*")
	for policy in $policies; do
		filepath=$CPUFREQROOT/$policy/scaling_available_governors

		# Exit if cpu isn't managed by cpufreq core
		if [ ! -f $filepath ]; then
			return;
		fi

		echo $1 > $CPUFREQROOT/$policy/scaling_governor
	done

	printf "Switched governor to $1.\n"
}

# All amd-pstate tests
amd_pstate_all()
{
	printf "\n=============================================\n"
	printf "***** Running AMD P-state Sanity Tests  *****\n"
	printf "=============================================\n\n"

	count=$(count_cpus)
	if [ $count = 0 ]; then
		printf "No cpu is managed by cpufreq core, exiting\n"
		exit;
	else
		printf "AMD P-state manages: $count CPUs\n"
	fi

	# unit test for amd-pstate kernel driver
	amd_pstate_basic

	# tbench
	amd_pstate_tbench

	# gitsource
	amd_pstate_gitsource
}

help()
{
	printf "Usage: $0 [OPTION...]
	[-h <help>]
	[-o <output-file-for-dump>]
	[-c <all: All testing,
	     basic: Basic testing,
	     tbench: Tbench testing,
	     gitsource: Gitsource testing.>]
	[-t <tbench time limit>]
	[-p <tbench process number>]
	[-l <loop times for tbench>]
	[-i <amd tracer interval>]
	[-m <comparative test: acpi-cpufreq>]
	\n"
	exit 2
}

parse_arguments()
{
	while getopts ho:c:t:p:l:i:m: arg
	do
		case $arg in
			h) # --help
				help
				;;

			c) # --func_type (Function to perform: basic, tbench, gitsource (default: all))
				FUNC=$OPTARG
				;;

			o) # --output-file (Output file to store dumps)
				OUTFILE=$OPTARG
				;;

			t) # --tbench-time-limit
				TIME_LIMIT=$OPTARG
				;;

			p) # --tbench-process-number
				PROCESS_NUM=$OPTARG
				;;

			l) # --tbench/gitsource-loop-times
				LOOP_TIMES=$OPTARG
				;;

			i) # --amd-tracer-interval
				TRACER_INTERVAL=$OPTARG
				;;

			m) # --comparative-test
				COMPARATIVE_TEST=$OPTARG
				;;

			*)
				help
				;;
		esac
	done
}

command_perf()
{
	if ! command -v perf > /dev/null; then
		echo $msg please install perf. >&2
		exit $ksft_skip
	fi
}

command_tbench()
{
	if ! command -v tbench > /dev/null; then
		if apt policy dbench > /dev/null 2>&1; then
			echo $msg apt install dbench >&2
			exit $ksft_skip
		elif yum list available | grep dbench > /dev/null 2>&1; then
			echo $msg yum install dbench >&2
			exit $ksft_skip
		fi
	fi

	if ! command -v tbench > /dev/null; then
		echo $msg please install tbench. >&2
		exit $ksft_skip
	fi
}

prerequisite()
{
	if ! echo "$ARCH" | grep -q x86; then
		echo "$0 # Skipped: Test can only run on x86 architectures."
		exit $ksft_skip
	fi

	if ! echo "$VENDOR" | grep -iq amd; then
		echo "$0 # Skipped: Test can only run on AMD CPU."
		echo "$0 # Current cpu vendor is $VENDOR."
		exit $ksft_skip
	fi

	scaling_driver=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_driver)
	if [ "$COMPARATIVE_TEST" = "" ]; then
		if [ "$scaling_driver" != "$CURRENT_TEST" ]; then
			echo "$0 # Skipped: Test can only run on $CURRENT_TEST driver or run comparative test."
			echo "$0 # Please set X86_AMD_PSTATE enabled or run comparative test."
			echo "$0 # Current cpufreq scaling driver is $scaling_driver."
			exit $ksft_skip
		fi
	else
		case "$FUNC" in
			"tbench" | "gitsource")
				if [ "$scaling_driver" != "$COMPARATIVE_TEST" ]; then
					echo "$0 # Skipped: Comparison test can only run on $COMPARISON_TEST driver."
					echo "$0 # Current cpufreq scaling driver is $scaling_driver."
					exit $ksft_skip
				fi
				;;

			*)
				echo "$0 # Skipped: Comparison test are only for tbench or gitsource."
				echo "$0 # Current comparative test is for $FUNC."
				exit $ksft_skip
				;;
		esac
	fi

	if [ ! -w /dev ]; then
		echo $msg please run this as root >&2
		exit $ksft_skip
	fi

	case "$FUNC" in
		"all")
			command_perf
			command_tbench
			;;

		"tbench")
			command_perf
			command_tbench
			;;

		"gitsource")
			command_perf
			;;
	esac

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

do_test()
{
	# Check if CPUs are managed by cpufreq or not
	count=$(count_cpus)
	MAKE_CPUS=$((count*2))

	if [ $count = 0 ]; then
		echo "No cpu is managed by cpufreq core, exiting"
		exit 2;
	fi

	case "$FUNC" in
		"all")
			amd_pstate_all
			;;

		"basic")
			amd_pstate_basic
			;;

		"tbench")
			amd_pstate_tbench
			;;

		"gitsource")
			amd_pstate_gitsource
			;;

		*)
			echo "Invalid [-f] function type"
			help
			;;
	esac
}

# clear dumps
pre_clear_dumps()
{
	case "$FUNC" in
		"all")
			rm -rf $OUTFILE.log
			rm -rf $OUTFILE.backup_governor.log
			rm -rf *.png
			;;

		"tbench")
			rm -rf $OUTFILE.log
			rm -rf $OUTFILE.backup_governor.log
			rm -rf tbench_*.png
			;;

		"gitsource")
			rm -rf $OUTFILE.log
			rm -rf $OUTFILE.backup_governor.log
			rm -rf gitsource_*.png
			;;

		*)
			;;
	esac
}

post_clear_dumps()
{
	rm -rf $OUTFILE.log
	rm -rf $OUTFILE.backup_governor.log
}

# Parse arguments
parse_arguments $@

# Make sure all requirements are met
prerequisite

# Run requested functions
pre_clear_dumps
do_test | tee -a $OUTFILE.log
post_clear_dumps
