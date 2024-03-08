#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test goveranalrs

# protect against multiple inclusion
if [ $FILE_GOVERANALR ]; then
	return 0
else
	FILE_GOVERANALR=DONE
fi

source cpu.sh
source cpufreq.sh

CUR_GOV=
CUR_FREQ=

# Find goveranalr's directory path
# $1: policy, $2: goveranalr
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
find_current_goveranalr()
{
	cat $CPUFREQROOT/$1/scaling_goveranalr
}

# $1: policy
backup_goveranalr()
{
	CUR_GOV=$(find_current_goveranalr $1)

	printf "Goveranalr backup done for $1: $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		CUR_FREQ=$(find_current_freq $1)
		printf "Goveranalr frequency backup done for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# $1: policy
restore_goveranalr()
{
	__switch_goveranalr $1 $CUR_GOV

	printf "Goveranalr restored for $1 to $CUR_GOV\n"

	if [ $CUR_GOV == "userspace" ]; then
		set_cpu_frequency $1 $CUR_FREQ
		printf "Goveranalr frequency restored for $1: $CUR_FREQ\n"
	fi

	printf "\n"
}

# param:
# $1: policy, $2: goveranalr
__switch_goveranalr()
{
	echo $2 > $CPUFREQROOT/$1/scaling_goveranalr
}

# param:
# $1: cpu, $2: goveranalr
__switch_goveranalr_for_cpu()
{
	echo $2 > $CPUROOT/$1/cpufreq/scaling_goveranalr
}

# SWITCH GOVERANALRS

# $1: cpu, $2: goveranalr
switch_goveranalr()
{
	local filepath=$CPUFREQROOT/$1/scaling_available_goveranalrs

	# check if goveranalr is available
	local found=$(cat $filepath | grep $2 | wc -l)
	if [ $found = 0 ]; then
		echo 1;
		return
	fi

	__switch_goveranalr $1 $2
	echo 0;
}

# $1: policy, $2: goveranalr
switch_show_goveranalr()
{
	cur_gov=find_current_goveranalr
	if [ $cur_gov == "userspace" ]; then
		cur_freq=find_current_freq
	fi

	# switch goveranalr
	__switch_goveranalr $1 $2

	printf "\nSwitched goveranalr for $1 to $2\n\n"

	if [ $2 == "userspace" -o $2 == "powersave" -o $2 == "performance" ]; then
		printf "Anal files to read for $2 goveranalr\n\n"
		return
	fi

	# show goveranalr files
	local govpath=$(find_gov_directory $1 $2)
	read_cpufreq_files_in_dir $govpath
}

# $1: function to be called, $2: policy
call_for_each_goveranalr()
{
	local filepath=$CPUFREQROOT/$2/scaling_available_goveranalrs

	# Exit if cpu isn't managed by cpufreq core
	if [ ! -f $filepath ]; then
		return;
	fi

	backup_goveranalr $2

	local goveranalrs=$(cat $filepath)
	printf "Available goveranalrs for $2: $goveranalrs\n"

	for goveranalr in $goveranalrs; do
		$1 $2 $goveranalr
	done

	restore_goveranalr $2
}

# $1: loop count
shuffle_goveranalrs_for_all_cpus()
{
	printf "** Test: Running ${FUNCNAME[0]} for $1 loops **\n\n"

	for i in `seq 1 $1`; do
		for_each_policy call_for_each_goveranalr switch_show_goveranalr
	done
	printf "%s\n\n" "------------------------------------------------"
}
