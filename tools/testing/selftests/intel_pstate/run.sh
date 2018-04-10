#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test runs on Intel x86 based hardware which support the intel_pstate
# driver.  The test checks the frequency settings from the maximum turbo
# state to the minimum supported frequency, in decrements of 100MHz.  The
# test runs the aperf.c program to put load on each processor.
#
# The results are displayed in a table which indicate the "Target" state,
# or the requested frequency in MHz, the Actual frequency, as read from
# /proc/cpuinfo, the difference between the Target and Actual frequencies,
# and the value of MSR 0x199 (MSR_IA32_PERF_CTL) which indicates what
# pstate the cpu is in, and the value of
# /sys/devices/system/cpu/intel_pstate/max_perf_pct X maximum turbo state
#
# Notes: In some cases several frequency values may be placed in the
# /tmp/result.X files.  This is done on purpose in order to catch cases
# where the pstate driver may not be working at all.  There is the case
# where, for example, several "similar" frequencies are in the file:
#
#
#/tmp/result.3100:1:cpu MHz              : 2899.980
#/tmp/result.3100:2:cpu MHz              : 2900.000
#/tmp/result.3100:3:msr 0x199: 0x1e00
#/tmp/result.3100:4:max_perf_pct 94
#
# and the test will error out in those cases.  The result.X file can be checked
# for consistency and modified to remove the extra MHz values.  The result.X
# files can be re-evaluated by setting EVALUATE_ONLY to 1 below.

EVALUATE_ONLY=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if ! uname -m | sed -e s/i.86/x86/ -e s/x86_64/x86/ | grep -q x86; then
	echo "$0 # Skipped: Test can only run on x86 architectures."
	exit $ksft_skip
fi

max_cpus=$(($(nproc)-1))

function run_test () {

	file_ext=$1
	for cpu in `seq 0 $max_cpus`
	do
		echo "launching aperf load on $cpu"
		./aperf $cpu &
	done

	echo "sleeping for 5 seconds"
	sleep 5
	grep MHz /proc/cpuinfo | sort -u > /tmp/result.freqs
	num_freqs=$(wc -l /tmp/result.freqs | awk ' { print $1 } ')
	if [ $num_freqs -ge 2 ]; then
		tail -n 1 /tmp/result.freqs > /tmp/result.$1
	else
		cp /tmp/result.freqs /tmp/result.$1
	fi
	./msr 0 >> /tmp/result.$1

	max_perf_pct=$(cat /sys/devices/system/cpu/intel_pstate/max_perf_pct)
	echo "max_perf_pct $max_perf_pct" >> /tmp/result.$1

	for job in `jobs -p`
	do
		echo "waiting for job id $job"
		wait $job
	done
}

#
# MAIN (ALL UNITS IN MHZ)
#

# Get the marketing frequency
_mkt_freq=$(cat /proc/cpuinfo | grep -m 1 "model name" | awk '{print $NF}')
_mkt_freq=$(echo $_mkt_freq | tr -d [:alpha:][:punct:])
mkt_freq=${_mkt_freq}0

# Get the ranges from cpupower
_min_freq=$(cpupower frequency-info -l | tail -1 | awk ' { print $1 } ')
min_freq=$(($_min_freq / 1000))
_max_freq=$(cpupower frequency-info -l | tail -1 | awk ' { print $2 } ')
max_freq=$(($_max_freq / 1000))


[ $EVALUATE_ONLY -eq 0 ] && for freq in `seq $max_freq -100 $min_freq`
do
	echo "Setting maximum frequency to $freq"
	cpupower frequency-set -g powersave --max=${freq}MHz >& /dev/null
	run_test $freq
done

[ $EVALUATE_ONLY -eq 0 ] && cpupower frequency-set -g powersave --max=${max_freq}MHz >& /dev/null

echo "=============================================================================="
echo "The marketing frequency of the cpu is $mkt_freq MHz"
echo "The maximum frequency of the cpu is $max_freq MHz"
echo "The minimum frequency of the cpu is $min_freq MHz"

# make a pretty table
echo "Target      Actual      Difference     MSR(0x199)     max_perf_pct"
for freq in `seq $max_freq -100 $min_freq`
do
	result_freq=$(cat /tmp/result.${freq} | grep "cpu MHz" | awk ' { print $4 } ' | awk -F "." ' { print $1 } ')
	msr=$(cat /tmp/result.${freq} | grep "msr" | awk ' { print $3 } ')
	max_perf_pct=$(cat /tmp/result.${freq} | grep "max_perf_pct" | awk ' { print $2 } ' )
	echo " $freq        $result_freq          $(($result_freq-$freq))          $msr          $(($max_perf_pct*$max_freq))"
done
exit 0
