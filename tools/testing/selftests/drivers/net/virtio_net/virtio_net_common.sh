#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This assumes running on a host with two virtio interfaces connected
# back to back. Example script to do such wire-up of tap devices would
# look like this:
#
# =======================================================================================================
# #!/bin/bash
#
# DEV1="$1"
# DEV2="$2"
#
# sudo tc qdisc add dev $DEV1 clsact
# sudo tc qdisc add dev $DEV2 clsact
# sudo tc filter add dev $DEV1 ingress protocol all pref 1 matchall action mirred egress redirect dev $DEV2
# sudo tc filter add dev $DEV2 ingress protocol all pref 1 matchall action mirred egress redirect dev $DEV1
# sudo ip link set $DEV1 up
# sudo ip link set $DEV2 up
# =======================================================================================================

REQUIRE_MZ="no"
NETIF_CREATE="no"
NETIF_FIND_DRIVER="virtio_net"
NUM_NETIFS=2

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

VIRTIO_NET_F_MAC=5

virtio_device_get()
{
	local dev=$1; shift
	local device_path="/sys/class/net/$dev/device/"

	basename `realpath $device_path`
}

virtio_device_rebind()
{
	local dev=$1; shift
	local device=`virtio_device_get $dev`

	echo "$device" > /sys/bus/virtio/drivers/virtio_net/unbind
	echo "$device" > /sys/bus/virtio/drivers/virtio_net/bind
}

virtio_debugfs_get()
{
	local dev=$1; shift
	local device=`virtio_device_get $dev`

	echo /sys/kernel/debug/virtio/$device/
}

check_virtio_debugfs()
{
	local dev=$1; shift
	local debugfs=`virtio_debugfs_get $dev`

	if [ ! -f "$debugfs/device_features" ] ||
	   [ ! -f "$debugfs/filter_feature_add"  ] ||
	   [ ! -f "$debugfs/filter_feature_del"  ] ||
	   [ ! -f "$debugfs/filter_features"  ] ||
	   [ ! -f "$debugfs/filter_features_clear"  ]; then
		echo "SKIP: not possible to access debugfs for $dev"
		exit $ksft_skip
	fi
}

virtio_feature_present()
{
	local dev=$1; shift
	local feature=$1; shift
	local debugfs=`virtio_debugfs_get $dev`

	cat $debugfs/device_features |grep "^$feature$" &> /dev/null
	return $?
}

virtio_filter_features_clear()
{
	local dev=$1; shift
	local debugfs=`virtio_debugfs_get $dev`

	echo "1" > $debugfs/filter_features_clear
}

virtio_filter_feature_add()
{
	local dev=$1; shift
	local feature=$1; shift
	local debugfs=`virtio_debugfs_get $dev`

	echo "$feature" > $debugfs/filter_feature_add
}
