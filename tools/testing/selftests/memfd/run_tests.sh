#!/bin/bash
# please run as root

#
# Normal tests requiring no special resources
#
./run_fuse_test.sh
./memfd_test

#
# To test memfd_create with hugetlbfs, there needs to be hpages_test
# huge pages free.  Attempt to allocate enough pages to test.
#
hpages_test=8

#
# Get count of free huge pages from /proc/meminfo
#
while read name size unit; do
        if [ "$name" = "HugePages_Free:" ]; then
                freepgs=$size
        fi
done < /proc/meminfo

#
# If not enough free huge pages for test, attempt to increase
#
if [ -n "$freepgs" ] && [ $freepgs -lt $hpages_test ]; then
	nr_hugepgs=`cat /proc/sys/vm/nr_hugepages`
	hpages_needed=`expr $hpages_test - $freepgs`

	echo 3 > /proc/sys/vm/drop_caches
	echo $(( $hpages_needed + $nr_hugepgs )) > /proc/sys/vm/nr_hugepages
	if [ $? -ne 0 ]; then
		echo "Please run this test as root"
		exit 1
	fi
	while read name size unit; do
		if [ "$name" = "HugePages_Free:" ]; then
			freepgs=$size
		fi
	done < /proc/meminfo
fi

#
# If still not enough huge pages available, exit.  But, give back any huge
# pages potentially allocated above.
#
if [ $freepgs -lt $hpages_test ]; then
	# nr_hugepgs non-zero only if we attempted to increase
	if [ -n "$nr_hugepgs" ]; then
		echo $nr_hugepgs > /proc/sys/vm/nr_hugepages
	fi
	printf "Not enough huge pages available (%d < %d)\n" \
		$freepgs $needpgs
	exit 1
fi

#
# Run the hugetlbfs test
#
./memfd_test hugetlbfs
./run_fuse_test.sh hugetlbfs

#
# Give back any huge pages allocated for the test
#
if [ -n "$nr_hugepgs" ]; then
	echo $nr_hugepgs > /proc/sys/vm/nr_hugepages
fi
