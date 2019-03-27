#! /bin/sh
#
# Script for creating a mesh in star topology.
# Node 3 will be the center.
#
# $FreeBSD$
#
PATH=../:.:$PATH
. config
. config.mesh

. common
ifconfig $WLAN mac:allow
MAC=`ifconfig $WLAN | grep ether | awk '{ print $2 }'`
case $MAC in
	$NODE1_MAC)
		ifconfig $WLAN inet $NODE1_IP
		ifconfig $WLAN mac:add $NODE3_MAC
		;;
	$NODE2_MAC)
		ifconfig $WLAN inet $NODE2_IP
		ifconfig $WLAN mac:add $NODE3_MAC
		;;
	$NODE3_MAC)
		ifconfig $WLAN inet $NODE3_IP
		ifconfig $WLAN mac:add $NODE1_MAC mac:add $NODE2_MAC mac:add \
		    $NODE4_MAC mac:add $NODE5_MAC
		;;
	$NODE4_MAC)
		ifconfig $WLAN inet $NODE4_IP
		ifconfig $WLAN mac:add $NODE3_MAC
		;;
	$NODE5_MAC)
		ifconfig $WLAN inet $NODE5_IP
		ifconfig $WLAN mac:add $NODE3_MAC
		;;
esac
ifconfig $WLAN up
