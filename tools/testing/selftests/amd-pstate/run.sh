#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# protect against multiple inclusion
if [ $FILE_MAIN ]; then
	return 0
else
	FILE_MAIN=DONE
fi

source basic.sh

# amd-pstate-ut only run on x86/x86_64 AMD systems.
ARCH=$(uname -m 2>/dev/null | sed -e 's/i.86/x86/' -e 's/x86_64/x86/')
VENDOR=$(cat /proc/cpuinfo | grep -m 1 'vendor_id' | awk '{print $NF}')

FUNC=all
OUTFILE=selftest

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# All amd-pstate tests
amd_pstate_all()
{
	printf "\n=============================================\n"
	printf "***** Running AMD P-state Sanity Tests  *****\n"
	printf "=============================================\n\n"

	# unit test for amd-pstate kernel driver
	amd_pstate_basic
}

help()
{
	printf "Usage: $0 [OPTION...]
	[-h <help>]
	[-o <output-file-for-dump>]
	[-c <all: All testing,
	     basic: Basic testing.>]
	\n"
	exit 2
}

parse_arguments()
{
	while getopts ho:c: arg
	do
		case $arg in
			h) # --help
				help
				;;

			c) # --func_type (Function to perform: basic (default: all))
				FUNC=$OPTARG
				;;

			o) # --output-file (Output file to store dumps)
				OUTFILE=$OPTARG
				;;

			*)
				help
				;;
		esac
	done
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
	if [ "$scaling_driver" != "amd-pstate" ]; then
		echo "$0 # Skipped: Test can only run on amd-pstate driver."
		echo "$0 # Please set X86_AMD_PSTATE enabled."
		echo "$0 # Current cpufreq scaling drvier is $scaling_driver."
		exit $ksft_skip
	fi

	msg="Skip all tests:"
	if [ ! -w /dev ]; then
		echo $msg please run this as root >&2
		exit $ksft_skip
	fi
}

do_test()
{
	case "$FUNC" in
		"all")
			amd_pstate_all
			;;

		"basic")
			amd_pstate_basic
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
			rm -rf $OUTFILE*
			;;

		*)
			;;
	esac
}

post_clear_dumps()
{
	rm -rf $OUTFILE.log
}

# Parse arguments
parse_arguments $@

# Make sure all requirements are met
prerequisite

# Run requested functions
pre_clear_dumps
do_test | tee -a $OUTFILE.log
post_clear_dumps
