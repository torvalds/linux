#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Modules specific tests cases

# protect against multiple inclusion
if [ $FILE_MODULE ]; then
	return 0
else
	FILE_MODULE=DONE
fi

source cpu.sh
source cpufreq.sh
source goveryesr.sh

# Check basic insmod/rmmod
# $1: module
test_basic_insmod_rmmod()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	printf "Inserting $1 module\n"
	# insert module
	insmod $1
	if [ $? != 0 ]; then
		printf "Insmod $1 failed\n"
		exit;
	fi

	printf "Removing $1 module\n"
	# remove module
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		exit;
	fi

	printf "\n"
}

# Insert cpufreq driver module and perform basic tests
# $1: cpufreq-driver module to insert
# $2: If we want to play with CPUs (1) or yest (0)
module_driver_test_single()
{
	printf "** Test: Running ${FUNCNAME[0]} for driver $1 and cpus_hotplug=$2 **\n\n"

	if [ $2 -eq 1 ]; then
		# offline all yesn-boot CPUs
		for_each_yesn_boot_cpu offline_cpu
		printf "\n"
	fi

	# insert module
	printf "Inserting $1 module\n\n"
	insmod $1
	if [ $? != 0 ]; then
		printf "Insmod $1 failed\n"
		return;
	fi

	if [ $2 -eq 1 ]; then
		# online all yesn-boot CPUs
		for_each_yesn_boot_cpu online_cpu
		printf "\n"
	fi

	# run basic tests
	cpufreq_basic_tests

	# remove module
	printf "Removing $1 module\n\n"
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		return;
	fi

	# There shouldn't be any cpufreq directories yesw.
	for_each_cpu cpu_should_yest_have_cpufreq_directory
	printf "\n"
}

# $1: cpufreq-driver module to insert
module_driver_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if module is present or yest
	ls $1 > /dev/null
	if [ $? != 0 ]; then
		printf "$1: yest present in `pwd` folder\n"
		return;
	fi

	# test basic module tests
	test_basic_insmod_rmmod $1

	# Do simple module test
	module_driver_test_single $1 0

	# Remove CPUs before inserting module and then bring them back
	module_driver_test_single $1 1
	printf "\n"
}

# find goveryesr name based on goveryesr module name
# $1: goveryesr module name
find_gov_name()
{
	if [ $1 = "cpufreq_ondemand.ko" ]; then
		printf "ondemand"
	elif [ $1 = "cpufreq_conservative.ko" ]; then
		printf "conservative"
	elif [ $1 = "cpufreq_userspace.ko" ]; then
		printf "userspace"
	elif [ $1 = "cpufreq_performance.ko" ]; then
		printf "performance"
	elif [ $1 = "cpufreq_powersave.ko" ]; then
		printf "powersave"
	elif [ $1 = "cpufreq_schedutil.ko" ]; then
		printf "schedutil"
	fi
}

# $1: goveryesr string, $2: goveryesr module, $3: policy
# example: module_goveryesr_test_single "ondemand" "cpufreq_ondemand.ko" 2
module_goveryesr_test_single()
{
	printf "** Test: Running ${FUNCNAME[0]} for $3 **\n\n"

	backup_goveryesr $3

	# switch to new goveryesr
	printf "Switch from $CUR_GOV to $1\n"
	switch_show_goveryesr $3 $1

	# try removing module, it should fail as goveryesr is used
	printf "Removing $2 module\n\n"
	rmmod $2
	if [ $? = 0 ]; then
		printf "WARN: rmmod $2 succeeded even if goveryesr is used\n"
		insmod $2
	else
		printf "Pass: unable to remove $2 while it is being used\n\n"
	fi

	# switch back to old goveryesr
	printf "Switchback to $CUR_GOV from $1\n"
	restore_goveryesr $3
	printf "\n"
}

# Insert cpufreq goveryesr module and perform basic tests
# $1: cpufreq-goveryesr module to insert
module_goveryesr_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if module is present or yest
	ls $1 > /dev/null
	if [ $? != 0 ]; then
		printf "$1: yest present in `pwd` folder\n"
		return;
	fi

	# test basic module tests
	test_basic_insmod_rmmod $1

	# insert module
	printf "Inserting $1 module\n\n"
	insmod $1
	if [ $? != 0 ]; then
		printf "Insmod $1 failed\n"
		return;
	fi

	# switch to new goveryesr for each cpu
	for_each_policy module_goveryesr_test_single $(find_gov_name $1) $1

	# remove module
	printf "Removing $1 module\n\n"
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		return;
	fi
	printf "\n"
}

# test modules: driver and goveryesr
# $1: driver module, $2: goveryesr module
module_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if modules are present or yest
	ls $1 $2 > /dev/null
	if [ $? != 0 ]; then
		printf "$1 or $2: is yest present in `pwd` folder\n"
		return;
	fi

	# TEST1: Insert gov after driver
	# insert driver module
	printf "Inserting $1 module\n\n"
	insmod $1
	if [ $? != 0 ]; then
		printf "Insmod $1 failed\n"
		return;
	fi

	# run goveryesr tests
	module_goveryesr_test $2

	# remove driver module
	printf "Removing $1 module\n\n"
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		return;
	fi

	# TEST2: Insert driver after goveryesr
	# insert goveryesr module
	printf "Inserting $2 module\n\n"
	insmod $2
	if [ $? != 0 ]; then
		printf "Insmod $2 failed\n"
		return;
	fi

	# run goveryesr tests
	module_driver_test $1

	# remove driver module
	printf "Removing $2 module\n\n"
	rmmod $2
	if [ $? != 0 ]; then
		printf "rmmod $2 failed\n"
		return;
	fi
}
