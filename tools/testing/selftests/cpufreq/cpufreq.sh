#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# protect against multiple inclusion
if [ $FILE_CPUFREQ ]; then
	return 0
else
	FILE_CPUFREQ=DONE
fi

source cpu.sh


# $1: cpu
cpu_should_have_cpufreq_directory()
{
	if [ ! -d $CPUROOT/$1/cpufreq ]; then
		printf "Warning: No cpufreq directory present for $1\n"
	fi
}

cpu_should_not_have_cpufreq_directory()
{
	if [ -d $CPUROOT/$1/cpufreq ]; then
		printf "Warning: cpufreq directory present for $1\n"
	fi
}

for_each_policy()
{
	policies=$(ls $CPUFREQROOT| grep "policy[0-9].*")
	for policy in $policies; do
		$@ $policy
	done
}

for_each_policy_concurrent()
{
	policies=$(ls $CPUFREQROOT| grep "policy[0-9].*")
	for policy in $policies; do
		$@ $policy &
	done
}

# $1: Path
read_cpufreq_files_in_dir()
{
	local files=`ls $1`

	printf "Printing directory: $1\n\n"

	for file in $files; do
		if [ -f $1/$file ]; then
			printf "$file:"
			cat $1/$file
		else
			printf "\n"
			read_cpufreq_files_in_dir "$1/$file"
		fi
	done
	printf "\n"
}


read_all_cpufreq_files()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	read_cpufreq_files_in_dir $CPUFREQROOT

	printf "%s\n\n" "------------------------------------------------"
}


# UPDATE CPUFREQ FILES

# $1: directory path
update_cpufreq_files_in_dir()
{
	local files=`ls $1`

	printf "Updating directory: $1\n\n"

	for file in $files; do
		if [ -f $1/$file ]; then
			# file is writable ?
			local wfile=$(ls -l $1/$file | awk '$1 ~ /^.*w.*/ { print $NF; }')

			if [ ! -z $wfile ]; then
				# scaling_setspeed is a special file and we
				# should skip updating it
				if [ $file != "scaling_setspeed" ]; then
					local val=$(cat $1/$file)
					printf "Writing $val to: $file\n"
					echo $val > $1/$file
				fi
			fi
		else
			printf "\n"
			update_cpufreq_files_in_dir "$1/$file"
		fi
	done

	printf "\n"
}

# Update all writable files with their existing values
update_all_cpufreq_files()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	update_cpufreq_files_in_dir $CPUFREQROOT

	printf "%s\n\n" "------------------------------------------------"
}


# CHANGE CPU FREQUENCIES

# $1: policy
find_current_freq()
{
	cat $CPUFREQROOT/$1/scaling_cur_freq
}

# $1: policy
# $2: frequency
set_cpu_frequency()
{
	printf "Change frequency for $1 to $2\n"
	echo $2 > $CPUFREQROOT/$1/scaling_setspeed
}

# $1: policy
test_all_frequencies()
{
	local filepath="$CPUFREQROOT/$1"

	backup_governor $1

	local found=$(switch_governor $1 "userspace")
	if [ $found = 1 ]; then
		printf "${FUNCNAME[0]}: userspace governor not available for: $1\n"
		return;
	fi

	printf "Switched governor for $1 to userspace\n\n"

	local freqs=$(cat $filepath/scaling_available_frequencies)
	printf "Available frequencies for $1: $freqs\n\n"

	# Set all frequencies one-by-one
	for freq in $freqs; do
		set_cpu_frequency $1 $freq
	done

	printf "\n"

	restore_governor $1
}

# $1: loop count
shuffle_frequency_for_all_cpus()
{
	printf "** Test: Running ${FUNCNAME[0]} for $1 loops **\n\n"

	for i in `seq 1 $1`; do
		for_each_policy test_all_frequencies
	done
	printf "\n%s\n\n" "------------------------------------------------"
}

# Basic cpufreq tests
cpufreq_basic_tests()
{
	printf "*** RUNNING CPUFREQ SANITY TESTS ***\n"
	printf "====================================\n\n"

	count=$(count_cpufreq_managed_cpus)
	if [ $count = 0 ]; then
		ktap_exit_fail_msg "No cpu is managed by cpufreq core, exiting\n"
	else
		printf "CPUFreq manages: $count CPUs\n\n"
	fi

	# Detect & print which CPUs are not managed by cpufreq
	print_unmanaged_cpus

	# read/update all cpufreq files
	read_all_cpufreq_files
	update_all_cpufreq_files

	# hotplug cpus
	reboot_cpus 5

	# Test all frequencies
	shuffle_frequency_for_all_cpus 2

	# Test all governors
	shuffle_governors_for_all_cpus 1
}

# Suspend/resume
# $1: "suspend" or "hibernate", $2: loop count
do_suspend()
{
	printf "** Test: Running ${FUNCNAME[0]}: Trying $1 for $2 loops **\n\n"

	# Is the directory available
	if [ ! -d $SYSFS/power/ -o ! -f $SYSFS/power/state ]; then
		printf "$SYSFS/power/state not available\n"
		return 1
	fi

	if [ $1 = "suspend" ]; then
		filename="mem"
	elif [ $1 = "hibernate" ]; then
		filename="disk"
	else
		printf "$1 is not a valid option\n"
		return 1
	fi

	if [ -n $filename ]; then
		present=$(cat $SYSFS/power/state | grep $filename)

		if [ -z "$present" ]; then
			printf "Tried to $1 but $filename isn't present in $SYSFS/power/state\n"
			return 1;
		fi

		for i in `seq 1 $2`; do
			printf "Starting $1\n"

			if [ "$3" = "rtc" ]; then
				if ! command -v rtcwake &> /dev/null; then
					printf "rtcwake could not be found, please install it.\n"
					return 1
				fi

				rtcwake -m $filename -s 15

				if [ $? -ne 0 ]; then
					printf "Failed to suspend using RTC wake alarm\n"
					return 1
				fi
			fi

			echo $filename > $SYSFS/power/state
			printf "Came out of $1\n"

			printf "Do basic tests after finishing $1 to verify cpufreq state\n\n"
			cpufreq_basic_tests
		done
	fi
}
