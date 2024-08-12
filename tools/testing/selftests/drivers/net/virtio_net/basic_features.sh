#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# See virtio_net_common.sh comments for more details about assumed setup

ALL_TESTS="
	initial_ping_test
	f_mac_test
"

source virtio_net_common.sh

lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

h1=${NETIFS[p1]}
h2=${NETIFS[p2]}

h1_create()
{
	simple_if_init $h1 $H1_IPV4/24 $H1_IPV6/64
}

h1_destroy()
{
	simple_if_fini $h1 $H1_IPV4/24 $H1_IPV6/64
}

h2_create()
{
	simple_if_init $h2 $H2_IPV4/24 $H2_IPV6/64
}

h2_destroy()
{
	simple_if_fini $h2 $H2_IPV4/24 $H2_IPV6/64
}

initial_ping_test()
{
	setup_cleanup
	setup_prepare
	ping_test $h1 $H2_IPV4 " simple"
}

f_mac_test()
{
	RET=0
	local test_name="mac feature filtered"

	virtio_feature_present $h1 $VIRTIO_NET_F_MAC
	if [ $? -ne 0 ]; then
		log_test_skip "$test_name" "Device $h1 is missing feature $VIRTIO_NET_F_MAC."
		return 0
	fi
	virtio_feature_present $h1 $VIRTIO_NET_F_MAC
	if [ $? -ne 0 ]; then
		log_test_skip "$test_name" "Device $h2 is missing feature $VIRTIO_NET_F_MAC."
		return 0
	fi

	setup_cleanup
	setup_prepare

	grep -q 0 /sys/class/net/$h1/addr_assign_type
	check_err $? "Permanent address assign type for $h1 is not set"
	grep -q 0 /sys/class/net/$h2/addr_assign_type
	check_err $? "Permanent address assign type for $h2 is not set"

	setup_cleanup
	virtio_filter_feature_add $h1 $VIRTIO_NET_F_MAC
	virtio_filter_feature_add $h2 $VIRTIO_NET_F_MAC
	setup_prepare

	grep -q 0 /sys/class/net/$h1/addr_assign_type
	check_fail $? "Permanent address assign type for $h1 is set when F_MAC feature is filtered"
	grep -q 0 /sys/class/net/$h2/addr_assign_type
	check_fail $? "Permanent address assign type for $h2 is set when F_MAC feature is filtered"

	ping_do $h1 $H2_IPV4
	check_err $? "Ping failed"

	log_test "$test_name"
}

setup_prepare()
{
	virtio_device_rebind $h1
	virtio_device_rebind $h2
	wait_for_dev $h1
	wait_for_dev $h2

	vrf_prepare

	h1_create
	h2_create
}

setup_cleanup()
{
	h2_destroy
	h1_destroy

	vrf_cleanup

	virtio_filter_features_clear $h1
	virtio_filter_features_clear $h2
	virtio_device_rebind $h1
	virtio_device_rebind $h2
	wait_for_dev $h1
	wait_for_dev $h2
}

cleanup()
{
	pre_cleanup
	setup_cleanup
}

check_driver $h1 "virtio_net"
check_driver $h2 "virtio_net"
check_virtio_debugfs $h1
check_virtio_debugfs $h2

trap cleanup EXIT

setup_prepare

tests_run

exit "$EXIT_STATUS"
