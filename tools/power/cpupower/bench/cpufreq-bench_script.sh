#!/bin/bash

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

# Author/Copyright(c): 2009, Thomas Renninger <trenn@suse.de>, Novell Inc.

# Ondemand up_threshold and sampling rate test script for cpufreq-bench
# mircobenchmark.
# Modify the general variables at the top or extend or copy out parts
# if you want to test other things
#

# Default with latest kernels is 95, before micro account patches
# it was 80, cmp. with git commit 808009131046b62ac434dbc796
UP_THRESHOLD="60 80 95"
# Depending on the kernel and the HW sampling rate could be restricted
# and cannot be set that low...
# E.g. before git commit cef9615a853ebc4972084f7 one could only set
# min sampling rate of 80000 if CONFIG_HZ=250
SAMPLING_RATE="20000 80000"

function measure()
{
    local -i up_threshold_set
    local -i sampling_rate_set

    for up_threshold in $UP_THRESHOLD;do
	for sampling_rate in $SAMPLING_RATE;do
	    # Set values in sysfs
	    echo $up_threshold >/sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold
	    echo $sampling_rate >/sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate
	    up_threshold_set=$(cat /sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold)
	    sampling_rate_set=$(cat /sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate)

	    # Verify set values in sysfs
	    if [ ${up_threshold_set} -eq ${up_threshold} ];then
		echo "up_threshold: $up_threshold, set in sysfs: ${up_threshold_set}"
	    else
		echo "WARNING: Tried to set up_threshold: $up_threshold, set in sysfs: ${up_threshold_set}"
	    fi
	    if [ ${sampling_rate_set} -eq ${sampling_rate} ];then
		echo "sampling_rate: $sampling_rate, set in sysfs: ${sampling_rate_set}"
	    else
		echo "WARNING: Tried to set sampling_rate: $sampling_rate, set in sysfs: ${sampling_rate_set}"
	    fi

	    # Benchmark
	    cpufreq-bench -o /var/log/cpufreq-bench/up_threshold_${up_threshold}_sampling_rate_${sampling_rate}
	done
    done
}

function create_plots()
{
    local command

    for up_threshold in $UP_THRESHOLD;do
	command="cpufreq-bench_plot.sh -o \"sampling_rate_${SAMPLING_RATE}_up_threshold_${up_threshold}\" -t \"Ondemand sampling_rate: ${SAMPLING_RATE} comparison - Up_threshold: $up_threshold %\""
	for sampling_rate in $SAMPLING_RATE;do
	    command="${command} /var/log/cpufreq-bench/up_threshold_${up_threshold}_sampling_rate_${sampling_rate}/* \"sampling_rate = $sampling_rate\""
	done
	echo $command
	eval "$command"
	echo
    done

    for sampling_rate in $SAMPLING_RATE;do
	command="cpufreq-bench_plot.sh -o \"up_threshold_${UP_THRESHOLD}_sampling_rate_${sampling_rate}\" -t \"Ondemand up_threshold: ${UP_THRESHOLD} % comparison - sampling_rate: $sampling_rate\""
	for up_threshold in $UP_THRESHOLD;do
	    command="${command} /var/log/cpufreq-bench/up_threshold_${up_threshold}_sampling_rate_${sampling_rate}/* \"up_threshold = $up_threshold\""
	done
	echo $command
	eval "$command"
	echo
    done

    command="cpufreq-bench_plot.sh -o \"up_threshold_${UP_THRESHOLD}_sampling_rate_${SAMPLING_RATE}\" -t \"Ondemand up_threshold: ${UP_THRESHOLD} and sampling_rate ${SAMPLING_RATE} comparison\""
    for sampling_rate in $SAMPLING_RATE;do
	for up_threshold in $UP_THRESHOLD;do
	    command="${command} /var/log/cpufreq-bench/up_threshold_${up_threshold}_sampling_rate_${sampling_rate}/* \"up_threshold = $up_threshold - sampling_rate = $sampling_rate\""
	done
    done
    echo "$command"
    eval "$command"
}

measure
create_plots