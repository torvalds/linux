#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test governors

# protect against multiple inclusion
if [ $FILE_GOVERNOR ]; then
	return 0
else
	FILE_GOVERNOR=DONE
fi

source cpu.sh
source cpufreq.sh

CUR_GOV=
CUR_FREQ=

# Find governor's directory path
# $1: policy, $2: governor
find_gov_directory()
{
	if [ -d $CPUFREQROOT/$2 ]; then
		printf "$CPUFREQROOT/$2\n"
	elif [ -d $CPUFREQROOT/$1/$2 ]; then
		printf "$CPUFREQROOT/$1/$2\n"
	else
		printf "INVALID\n"
	fi
}

# $1: policy
find_current_governor()
{
	cat $CPUFREQROOT/$1/scaling_governor
}

# $1: policy
backup_governor()
{
	CUR_GOV=$(find_current_governor $1)

	printf "Governor backup done for $1: $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		CUR_FREQ=$(find_current_freq $1)
		printf "Governor frequency backup done for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# $1: policy
restore_governor()
{
	__switch_governor $1 $CUR_GOV

	printf "Governor restored for $1 to $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		set_cpu_frequency $1 $CUR_FREQ
		printf "Governor frequency restored for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# param:
# $1: policy, $2: governor
__switch_governor()
{
	echo $2 > $CPUFREQROOT/$1/scaling_governor
}

# param:
# $1: cpu, $2: governor
__switch_governor_for_cpu()
{
	echo $2 > $CPUROOT/$1/cpufreq/scaling_governor
}

# SWITCH GOVERNORS

# $1: cpu, $2: governor
switch_governor()
{
	local filepath=$CPUFREQROOT/$1/scaling_available_governors

	# check if governor is available
	local found=$(cat $filepath | grep $2 | wc -l)
	if [ $found = 0 ]; then
		echo 1;
		return
	fi

	__switch_governor $1 $2
	echo 0;
}

# $1: policy, $2: governor
switch_show_governor()
{
	cur_gov=find_current_governor
	if [ $cur_gov == "userspace" ]; then
		cur_freq=find_current_freq
	fi

	# switch governor
	__switch_governor $1 $2

	printf "\nSwitched governor for $1 to $2\n\n"

	if [ $2 == "userspace" -o $2 == "powersave" -o $2 == "performance" ]; then
		printf "No files to read for $2 governor\n\n"
		return
	fi

	# show governor files
	local govpath=$(find_gov_directory $1 $2)
	read_cpufreq_files_in_dir $govpath
}

# $1: function to be called, $2: policy
call_for_each_governor()
{
	local filepath=$CPUFREQROOT/$2/scaling_available_governors

	# Exit if cpu isn't managed by cpufreq core
	if [ ! -f $filepath ]; then
		return;
	fi

	backup_governor $2

	local governors=$(cat $filepath)
	printf "Available governors for $2: $governors\n"

	for governor in $governors; do
		$1 $2 $governor
	done

	restore_governor $2
}

# $1: loop count
shuffle_governors_for_all_cpus()
{
	printf "** Test: Running ${FUNCNAME[0]} for $1 loops **\n\n"

	for i in `seq 1 $1`; do
		for_each_policy call_for_each_governor switch_show_governor
	done
	printf "%s\n\n" "------------------------------------------------"
}
