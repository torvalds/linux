#!/bin/bash
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
source governor.sh

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
# $2: If we want to play with CPUs (1) or not (0)
module_driver_test_single()
{
	printf "** Test: Running ${FUNCNAME[0]} for driver $1 and cpus_hotplug=$2 **\n\n"

	if [ $2 -eq 1 ]; then
		# offline all non-boot CPUs
		for_each_non_boot_cpu offline_cpu
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
		# online all non-boot CPUs
		for_each_non_boot_cpu online_cpu
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

	# There shouldn't be any cpufreq directories now.
	for_each_cpu cpu_should_not_have_cpufreq_directory
	printf "\n"
}

# $1: cpufreq-driver module to insert
module_driver_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if module is present or not
	ls $1 > /dev/null
	if [ $? != 0 ]; then
		printf "$1: not present in `pwd` folder\n"
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

# find governor name based on governor module name
# $1: governor module name
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

# $1: governor string, $2: governor module, $3: policy
# example: module_governor_test_single "ondemand" "cpufreq_ondemand.ko" 2
module_governor_test_single()
{
	printf "** Test: Running ${FUNCNAME[0]} for $3 **\n\n"

	backup_governor $3

	# switch to new governor
	printf "Switch from $CUR_GOV to $1\n"
	switch_show_governor $3 $1

	# try removing module, it should fail as governor is used
	printf "Removing $2 module\n\n"
	rmmod $2
	if [ $? = 0 ]; then
		printf "WARN: rmmod $2 succeeded even if governor is used\n"
		insmod $2
	else
		printf "Pass: unable to remove $2 while it is being used\n\n"
	fi

	# switch back to old governor
	printf "Switchback to $CUR_GOV from $1\n"
	restore_governor $3
	printf "\n"
}

# Insert cpufreq governor module and perform basic tests
# $1: cpufreq-governor module to insert
module_governor_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if module is present or not
	ls $1 > /dev/null
	if [ $? != 0 ]; then
		printf "$1: not present in `pwd` folder\n"
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

	# switch to new governor for each cpu
	for_each_policy module_governor_test_single $(find_gov_name $1) $1

	# remove module
	printf "Removing $1 module\n\n"
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		return;
	fi
	printf "\n"
}

# test modules: driver and governor
# $1: driver module, $2: governor module
module_test()
{
	printf "** Test: Running ${FUNCNAME[0]} **\n\n"

	# check if modules are present or not
	ls $1 $2 > /dev/null
	if [ $? != 0 ]; then
		printf "$1 or $2: is not present in `pwd` folder\n"
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

	# run governor tests
	module_governor_test $2

	# remove driver module
	printf "Removing $1 module\n\n"
	rmmod $1
	if [ $? != 0 ]; then
		printf "rmmod $1 failed\n"
		return;
	fi

	# TEST2: Insert driver after governor
	# insert governor module
	printf "Inserting $2 module\n\n"
	insmod $2
	if [ $? != 0 ]; then
		printf "Insmod $2 failed\n"
		return;
	fi

	# run governor tests
	module_driver_test $1

	# remove driver module
	printf "Removing $2 module\n\n"
	rmmod $2
	if [ $? != 0 ]; then
		printf "rmmod $2 failed\n"
		return;
	fi
}
