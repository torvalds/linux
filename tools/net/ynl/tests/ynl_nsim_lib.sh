#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Shared netdevsim setup/cleanup for YNL C test wrappers

NSIM_ID="1337"
NSIM_DEV=""
KSFT_SKIP=4

nsim_cleanup() {
	echo "$NSIM_ID" > /sys/bus/netdevsim/del_device 2>/dev/null || true
}

nsim_setup() {
	modprobe netdevsim 2>/dev/null
	if ! [ -f /sys/bus/netdevsim/new_device ]; then
		echo "netdevsim module not available, skipping" >&2
		exit "$KSFT_SKIP"
	fi

	trap nsim_cleanup EXIT

	echo "$NSIM_ID 1" > /sys/bus/netdevsim/new_device
	udevadm settle

	NSIM_DEV=$(ls /sys/bus/netdevsim/devices/netdevsim${NSIM_ID}/net 2>/dev/null | head -1)
	if [ -z "$NSIM_DEV" ]; then
		echo "failed to find netdevsim device" >&2
		exit 1
	fi

	ip link set dev "$NSIM_DEV" name nsim0
	ip link set dev nsim0 up
	ip addr add 192.168.1.1/24 dev nsim0
	ip addr add 2001:db8::1/64 dev nsim0 nodad
}
