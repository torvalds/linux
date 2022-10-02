#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only */
#
# Wrapper script which performs setup and cleanup for nx_huge_pages_test.
# Makes use of root privileges to set up huge pages and KVM module parameters.
#
# tools/testing/selftests/kvm/nx_huge_page_test.sh
# Copyright (C) 2022, Google LLC.

set -e

NX_HUGE_PAGES=$(cat /sys/module/kvm/parameters/nx_huge_pages)
NX_HUGE_PAGES_RECOVERY_RATIO=$(cat /sys/module/kvm/parameters/nx_huge_pages_recovery_ratio)
NX_HUGE_PAGES_RECOVERY_PERIOD=$(cat /sys/module/kvm/parameters/nx_huge_pages_recovery_period_ms)
HUGE_PAGES=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages)

set +e

function sudo_echo () {
	echo "$1" | sudo tee -a "$2" > /dev/null
}

NXECUTABLE="$(dirname $0)/nx_huge_pages_test"

sudo_echo test /dev/null || exit 4 # KSFT_SKIP=4

(
	set -e

	sudo_echo 1 /sys/module/kvm/parameters/nx_huge_pages
	sudo_echo 1 /sys/module/kvm/parameters/nx_huge_pages_recovery_ratio
	sudo_echo 100 /sys/module/kvm/parameters/nx_huge_pages_recovery_period_ms
	sudo_echo "$(( $HUGE_PAGES + 3 ))" /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

	# Test with reboot permissions
	if [ $(whoami) == "root" ] || sudo setcap cap_sys_boot+ep $NXECUTABLE 2> /dev/null; then
		echo Running test with CAP_SYS_BOOT enabled
		$NXECUTABLE -t 887563923 -p 100 -r
		test $(whoami) == "root" || sudo setcap cap_sys_boot-ep $NXECUTABLE
	else
		echo setcap failed, skipping nx_huge_pages_test with CAP_SYS_BOOT enabled
	fi

	# Test without reboot permissions
	if [ $(whoami) != "root" ] ; then
		echo Running test with CAP_SYS_BOOT disabled
		$NXECUTABLE -t 887563923 -p 100
	else
		echo Running as root, skipping nx_huge_pages_test with CAP_SYS_BOOT disabled
	fi
)
RET=$?

sudo_echo "$NX_HUGE_PAGES" /sys/module/kvm/parameters/nx_huge_pages
sudo_echo "$NX_HUGE_PAGES_RECOVERY_RATIO" /sys/module/kvm/parameters/nx_huge_pages_recovery_ratio
sudo_echo "$NX_HUGE_PAGES_RECOVERY_PERIOD" /sys/module/kvm/parameters/nx_huge_pages_recovery_period_ms
sudo_echo "$HUGE_PAGES" /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

exit $RET
