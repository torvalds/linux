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

(
	set -e

	sudo_echo 1 /sys/module/kvm/parameters/nx_huge_pages
	sudo_echo 1 /sys/module/kvm/parameters/nx_huge_pages_recovery_ratio
	sudo_echo 100 /sys/module/kvm/parameters/nx_huge_pages_recovery_period_ms
	sudo_echo "$(( $HUGE_PAGES + 3 ))" /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

	"$(dirname $0)"/nx_huge_pages_test -t 887563923 -p 100
)
RET=$?

sudo_echo "$NX_HUGE_PAGES" /sys/module/kvm/parameters/nx_huge_pages
sudo_echo "$NX_HUGE_PAGES_RECOVERY_RATIO" /sys/module/kvm/parameters/nx_huge_pages_recovery_ratio
sudo_echo "$NX_HUGE_PAGES_RECOVERY_PERIOD" /sys/module/kvm/parameters/nx_huge_pages_recovery_period_ms
sudo_echo "$HUGE_PAGES" /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

exit $RET
