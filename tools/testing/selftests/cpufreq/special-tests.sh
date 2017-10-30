#!/bin/bash
#
# Special test cases reported by people

# Testcase 1: Reported here: http://marc.info/?l=linux-pm&m=140618592709858&w=2

# protect against multiple inclusion
if [ $FILE_SPECIAL ]; then
	return 0
else
	FILE_SPECIAL=DONE
fi

source cpu.sh
source cpufreq.sh
source governor.sh

# Test 1
# $1: policy
__simple_lockdep()
{
	# switch to ondemand
	__switch_governor $1 "ondemand"

	# cat ondemand files
	local ondir=$(find_gov_directory $1 "ondemand")
	if [ -z $ondir ]; then
		printf "${FUNCNAME[0]}Ondemand directory not created, quit"
		return
	fi

	cat $ondir/*

	# switch to conservative
	__switch_governor $1 "conservative"
}

simple_lockdep()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n"

	for_each_policy __simple_lockdep
}

# Test 2
# $1: policy
__concurrent_lockdep()
{
	for i in `seq 0 100`; do
		__simple_lockdep $1
	done
}

concurrent_lockdep()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n"

	for_each_policy_concurrent __concurrent_lockdep
}

# Test 3
quick_shuffle()
{
	# this is called concurrently from governor_race
	for I in `seq 1000`
	do
		echo ondemand | sudo tee $CPUFREQROOT/policy*/scaling_governor &
		echo userspace | sudo tee $CPUFREQROOT/policy*/scaling_governor &
	done
}

governor_race()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n"

	# run 8 concurrent instances
	for I in `seq 8`
	do
		quick_shuffle &
	done
}

# Test 4
# $1: cpu
hotplug_with_updates_cpu()
{
	local filepath="$CPUROOT/$1/cpufreq"

	# switch to ondemand
	__switch_governor_for_cpu $1 "ondemand"

	for i in `seq 1 5000`
	do
		reboot_cpu $1
	done &

	local freqs=$(cat $filepath/scaling_available_frequencies)
	local oldfreq=$(cat $filepath/scaling_min_freq)

	for j in `seq 1 5000`
	do
		# Set all frequencies one-by-one
		for freq in $freqs; do
			echo $freq > $filepath/scaling_min_freq
		done
	done

	# restore old freq
	echo $oldfreq > $filepath/scaling_min_freq
}

hotplug_with_updates()
{
	for_each_non_boot_cpu hotplug_with_updates_cpu
}
