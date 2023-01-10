#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# amd-pstate-ut is a test module for testing the amd-pstate driver.
# It can only run on x86 architectures and current cpufreq driver
# must be amd-pstate.
# (1) It can help all users to verify their processor support
# (SBIOS/Firmware or Hardware).
# (2) Kernel can have a basic function test to avoid the kernel
# regression during the update.
# (3) We can introduce more functional or performance tests to align
# the result together, it will benefit power and performance scale optimization.

# protect against multiple inclusion
if [ $FILE_BASIC ]; then
	return 0
else
	FILE_BASIC=DONE
fi

amd_pstate_basic()
{
	printf "\n---------------------------------------------\n"
	printf "*** Running AMD P-state ut                ***"
	printf "\n---------------------------------------------\n"

	if ! /sbin/modprobe -q -n amd-pstate-ut; then
		echo "amd-pstate-ut: module amd-pstate-ut is not found [SKIP]"
		exit $ksft_skip
	fi
	if /sbin/modprobe -q amd-pstate-ut; then
		/sbin/modprobe -q -r amd-pstate-ut
		echo "amd-pstate-basic: ok"
	else
		echo "amd-pstate-basic: [FAIL]"
		exit 1
	fi
}
