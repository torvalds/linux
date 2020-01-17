#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test goveryesrs

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

# Find goveryesr's directory path
# $1: policy, $2: goveryesr
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
find_current_goveryesr()
{
	cat $CPUFREQROOT/$1/scaling_goveryesr
}

# $1: policy
backup_goveryesr()
{
	CUR_GOV=$(find_current_goveryesr $1)

	printf "Goveryesr backup done for $1: $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		CUR_FREQ=$(find_current_freq $1)
		printf "Goveryesr frequency backup done for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# $1: policy
restore_goveryesr()
{
	__switch_goveryesr $1 $CUR_GOV

	printf "Goveryesr restored for $1 to $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		set_cpu_frequency $1 $CUR_FREQ
		printf "Goveryesr frequency restored for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# param:
# $1: policy, $2: goveryesr
__switch_goveryesr()
{
	echo $2 > $CPUFREQROOT/$1/scaling_goveryesr
}

# param:
# $1: cpu, $2: goveryesr
__switch_goveryesr_for_cpu()
{
	echo $2 > $CPUROOT/$1/cpufreq/scaling_goveryesr
}

# SWITCH GOVERNORS

# $1: cpu, $2: goveryesr
switch_goveryesr()
{
	local filepath=$CPUFREQROOT/$1/scaling_available_goveryesrs

	# check if goveryesr is available
	local found=$(cat $filepath | grep $2 | wc -l)
	if [ $found = 0 ]; then
		echo 1;
		return
	fi

	__switch_goveryesr $1 $2
	echo 0;
}

# $1: policy, $2: goveryesr
switch_show_goveryesr()
{
	cur_gov=find_current_goveryesr
	if [ $cur_gov == "userspace" ]; then
		cur_freq=find_current_freq
	fi

	# switch goveryesr
	__switch_goveryesr $1 $2

	printf "\nSwitched goveryesr for $1 to $2\n\n"

	if [ $2 == "userspace" -o $2 == "powersave" -o $2 == "performance" ]; then
		printf "No files to read for $2 goveryesr\n\n"
		return
	fi

	# show goveryesr files
	local govpath=$(find_gov_directory $1 $2)
	read_cpufreq_files_in_dir $govpath
}

# $1: function to be called, $2: policy
call_for_each_goveryesr()
{
	local filepath=$CPUFREQROOT/$2/scaling_available_goveryesrs

	# Exit if cpu isn't managed by cpufreq core
	if [ ! -f $filepath ]; then
		return;
	fi

	backup_goveryesr $2

	local goveryesrs=$(cat $filepath)
	printf "Available goveryesrs for $2: $goveryesrs\n"

	for goveryesr in $goveryesrs; do
		$1 $2 $goveryesr
	done

	restore_goveryesr $2
}

# $1: loop count
shuffle_goveryesrs_for_all_cpus()
{
	printf "** Test: Running ${FUNCNAME[0]} for $1 loops **\n\n"

	for i in `seq 1 $1`; do
		for_each_policy call_for_each_goveryesr switch_show_goveryesr
	done
	printf "%s\n\n" "------------------------------------------------"
}
