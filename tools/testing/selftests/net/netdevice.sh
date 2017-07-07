#!/bin/sh
#
# This test is for checking network interface
# For the moment it tests only ethernet interface (but wifi could be easily added)
#
# We assume that all network driver are loaded
# if not they probably have failed earlier in the boot process and their logged error will be catched by another test
#

# this function will try to up the interface
# if already up, nothing done
# arg1: network interface name
kci_net_start()
{
	netdev=$1

	ip link show "$netdev" |grep -q UP
	if [ $? -eq 0 ];then
		echo "SKIP: $netdev: interface already up"
		return 0
	fi

	ip link set "$netdev" up
	if [ $? -ne 0 ];then
		echo "FAIL: $netdev: Fail to up interface"
		return 1
	else
		echo "PASS: $netdev: set interface up"
		NETDEV_STARTED=1
	fi
	return 0
}

# this function will try to setup an IP and MAC address on a network interface
# Doing nothing if the interface was already up
# arg1: network interface name
kci_net_setup()
{
	netdev=$1

	# do nothing if the interface was already up
	if [ $NETDEV_STARTED -eq 0 ];then
		return 0
	fi

	MACADDR='02:03:04:05:06:07'
	ip link set dev $netdev address "$MACADDR"
	if [ $? -ne 0 ];then
		echo "FAIL: $netdev: Cannot set MAC address"
	else
		ip link show $netdev |grep -q "$MACADDR"
		if [ $? -eq 0 ];then
			echo "PASS: $netdev: set MAC address"
		else
			echo "FAIL: $netdev: Cannot set MAC address"
		fi
	fi

	#check that the interface did not already have an IP
	ip address show "$netdev" |grep '^[[:space:]]*inet'
	if [ $? -eq 0 ];then
		echo "SKIP: $netdev: already have an IP"
		return 0
	fi

	# TODO what ipaddr to set ? DHCP ?
	echo "SKIP: $netdev: set IP address"
	return 0
}

# test an ethtool command
# arg1: return code for not supported (see ethtool code source)
# arg2: summary of the command
# arg3: command to execute
kci_netdev_ethtool_test()
{
	if [ $# -le 2 ];then
		echo "SKIP: $netdev: ethtool: invalid number of arguments"
		return 1
	fi
	$3 >/dev/null
	ret=$?
	if [ $ret -ne 0 ];then
		if [ $ret -eq "$1" ];then
			echo "SKIP: $netdev: ethtool $2 not supported"
		else
			echo "FAIL: $netdev: ethtool $2"
			return 1
		fi
	else
		echo "PASS: $netdev: ethtool $2"
	fi
	return 0
}

# test ethtool commands
# arg1: network interface name
kci_netdev_ethtool()
{
	netdev=$1

	#check presence of ethtool
	ethtool --version 2>/dev/null >/dev/null
	if [ $? -ne 0 ];then
		echo "SKIP: ethtool not present"
		return 1
	fi

	TMP_ETHTOOL_FEATURES="$(mktemp)"
	if [ ! -e "$TMP_ETHTOOL_FEATURES" ];then
		echo "SKIP: Cannot create a tmp file"
		return 1
	fi

	ethtool -k "$netdev" > "$TMP_ETHTOOL_FEATURES"
	if [ $? -ne 0 ];then
		echo "FAIL: $netdev: ethtool list features"
		rm "$TMP_ETHTOOL_FEATURES"
		return 1
	fi
	echo "PASS: $netdev: ethtool list features"
	#TODO for each non fixed features, try to turn them on/off
	rm "$TMP_ETHTOOL_FEATURES"

	kci_netdev_ethtool_test 74 'dump' "ethtool -d $netdev"
	kci_netdev_ethtool_test 94 'stats' "ethtool -S $netdev"
	return 0
}

# stop a netdev
# arg1: network interface name
kci_netdev_stop()
{
	netdev=$1

	if [ $NETDEV_STARTED -eq 0 ];then
		echo "SKIP: $netdev: interface kept up"
		return 0
	fi

	ip link set "$netdev" down
	if [ $? -ne 0 ];then
		echo "FAIL: $netdev: stop interface"
		return 1
	fi
	echo "PASS: $netdev: stop interface"
	return 0
}

# run all test on a netdev
# arg1: network interface name
kci_test_netdev()
{
	NETDEV_STARTED=0
	IFACE_TO_UPDOWN="$1"
	IFACE_TO_TEST="$1"
	#check for VLAN interface
	MASTER_IFACE="$(echo $1 | cut -d@ -f2)"
	if [ ! -z "$MASTER_IFACE" ];then
		IFACE_TO_UPDOWN="$MASTER_IFACE"
		IFACE_TO_TEST="$(echo $1 | cut -d@ -f1)"
	fi

	NETDEV_STARTED=0
	kci_net_start "$IFACE_TO_UPDOWN"

	kci_net_setup "$IFACE_TO_TEST"

	kci_netdev_ethtool "$IFACE_TO_TEST"

	kci_netdev_stop "$IFACE_TO_UPDOWN"
	return 0
}

#check for needed privileges
if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit 0
fi

ip link show 2>/dev/null >/dev/null
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without the ip tool"
	exit 0
fi

TMP_LIST_NETDEV="$(mktemp)"
if [ ! -e "$TMP_LIST_NETDEV" ];then
	echo "FAIL: Cannot create a tmp file"
	exit 1
fi

ip link show |grep '^[0-9]' | grep -oE '[[:space:]].*eth[0-9]*:|[[:space:]].*enp[0-9]s[0-9]:' | cut -d\  -f2 | cut -d: -f1> "$TMP_LIST_NETDEV"
while read netdev
do
	kci_test_netdev "$netdev"
done < "$TMP_LIST_NETDEV"

rm "$TMP_LIST_NETDEV"
exit 0
