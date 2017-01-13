#!/bin/bash
#
# CPU helpers

# protect against multiple inclusion
if [ $FILE_CPU ]; then
	return 0
else
	FILE_CPU=DONE
fi

source cpufreq.sh

for_each_cpu()
{
	cpus=$(ls $CPUROOT | grep "cpu[0-9].*")
	for cpu in $cpus; do
		$@ $cpu
	done
}

for_each_non_boot_cpu()
{
	cpus=$(ls $CPUROOT | grep "cpu[1-9].*")
	for cpu in $cpus; do
		$@ $cpu
	done
}

#$1: cpu
offline_cpu()
{
	printf "Offline $1\n"
	echo 0 > $CPUROOT/$1/online
}

#$1: cpu
online_cpu()
{
	printf "Online $1\n"
	echo 1 > $CPUROOT/$1/online
}

#$1: cpu
reboot_cpu()
{
	offline_cpu $1
	online_cpu $1
}

# Reboot CPUs
# param: number of times we want to run the loop
reboot_cpus()
{
	printf "** Test: Running ${FUNCNAME[0]} for $1 loops **\n\n"

	for i in `seq 1 $1`; do
		for_each_non_boot_cpu offline_cpu
		for_each_non_boot_cpu online_cpu
		printf "\n"
	done

	printf "\n%s\n\n" "------------------------------------------------"
}

# Prints warning for all CPUs with missing cpufreq directory
print_unmanaged_cpus()
{
	for_each_cpu cpu_should_have_cpufreq_directory
}

# Counts CPUs with cpufreq directories
count_cpufreq_managed_cpus()
{
	count=0;

	for cpu in `ls $CPUROOT | grep "cpu[0-9].*"`; do
		if [ -d $CPUROOT/$cpu/cpufreq ]; then
			let count=count+1;
		fi
	done

	echo $count;
}
