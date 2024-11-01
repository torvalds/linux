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

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# amd-pstate-ut only run on x86/x86_64 AMD systems.
ARCH=$(uname -m 2>/dev/null | sed -e 's/i.86/x86/' -e 's/x86_64/x86/')
VENDOR=$(cat /proc/cpuinfo | grep -m 1 'vendor_id' | awk '{print $NF}')

if ! echo "$ARCH" | grep -q x86; then
	echo "$0 # Skipped: Test can only run on x86 architectures."
	exit $ksft_skip
fi

if ! echo "$VENDOR" | grep -iq amd; then
	echo "$0 # Skipped: Test can only run on AMD CPU."
	echo "$0 # Current cpu vendor is $VENDOR."
	exit $ksft_skip
fi

scaling_driver=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_driver)
if [ "$scaling_driver" != "amd-pstate" ]; then
	echo "$0 # Skipped: Test can only run on amd-pstate driver."
	echo "$0 # Please set X86_AMD_PSTATE enabled."
	echo "$0 # Current cpufreq scaling drvier is $scaling_driver."
	exit $ksft_skip
fi

msg="Skip all tests:"
if [ ! -w /dev ]; then
    echo $msg please run this as root >&2
    exit $ksft_skip
fi

if ! /sbin/modprobe -q -n amd-pstate-ut; then
	echo "amd-pstate-ut: module amd-pstate-ut is not found [SKIP]"
	exit $ksft_skip
fi
if /sbin/modprobe -q amd-pstate-ut; then
	/sbin/modprobe -q -r amd-pstate-ut
	echo "amd-pstate-ut: ok"
else
	echo "amd-pstate-ut: [FAIL]"
	exit 1
fi
