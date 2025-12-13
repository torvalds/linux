#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# IPsec over bonding offload test:
#
#  +----------------+
#  |     bond0      |
#  |       |        |
#  |  eth0    eth1  |
#  +---+-------+----+
#
# We use netdevsim instead of physical interfaces
#-------------------------------------------------------------------
# Example commands
#   ip x s add proto esp src 192.0.2.1 dst 192.0.2.2 \
#            spi 0x07 mode transport reqid 0x07 replay-window 32 \
#            aead 'rfc4106(gcm(aes))' 1234567890123456dcba 128 \
#            sel src 192.0.2.1/24 dst 192.0.2.2/24
#            offload dev bond0 dir out
#   ip x p add dir out src 192.0.2.1/24 dst 192.0.2.2/24 \
#            tmpl proto esp src 192.0.2.1 dst 192.0.2.2 \
#            spi 0x07 mode transport reqid 0x07
#
#-------------------------------------------------------------------

lib_dir=$(dirname "$0")
# shellcheck disable=SC1091
source "$lib_dir"/../../../net/lib.sh
srcip=192.0.2.1
dstip=192.0.2.2
ipsec0=/sys/kernel/debug/netdevsim/netdevsim0/ports/0/ipsec
ipsec1=/sys/kernel/debug/netdevsim/netdevsim0/ports/1/ipsec
active_slave=""

# shellcheck disable=SC2317
active_slave_changed()
{
	local old_active_slave=$1
	local new_active_slave

	# shellcheck disable=SC2154
	new_active_slave=$(ip -n "${ns}" -d -j link show bond0 | \
		jq -r ".[].linkinfo.info_data.active_slave")
	[ "$new_active_slave" != "$old_active_slave" ] && [ "$new_active_slave" != "null" ]
}

test_offload()
{
	# use ping to exercise the Tx path
	ip netns exec "$ns" ping -I bond0 -c 3 -W 1 -i 0 "$dstip" >/dev/null

	active_slave=$(ip -n "${ns}" -d -j link show bond0 | \
		       jq -r ".[].linkinfo.info_data.active_slave")

	if [ "$active_slave" = "$nic0" ]; then
		sysfs=$ipsec0
	elif [ "$active_slave" = "$nic1" ]; then
		sysfs=$ipsec1
	else
		check_err 1 "bond_ipsec_offload invalid active_slave $active_slave"
	fi

	# The tx/rx order in sysfs may changed after failover
	grep -q "SA count=2 tx=3" "$sysfs" && grep -q "tx ipaddr=$dstip" "$sysfs"
	check_err $? "incorrect tx count with link ${active_slave}"

	log_test bond_ipsec_offload "active_slave ${active_slave}"
}

setup_env()
{
	if ! mount | grep -q debugfs; then
		mount -t debugfs none /sys/kernel/debug/ &> /dev/null
		defer umount /sys/kernel/debug/

	fi

	# setup netdevsim since dummy/veth dev doesn't have offload support
	if [ ! -w /sys/bus/netdevsim/new_device ] ; then
		if ! modprobe -q netdevsim; then
			echo "SKIP: can't load netdevsim for ipsec offload"
			# shellcheck disable=SC2154
			exit "$ksft_skip"
		fi
		defer modprobe -r netdevsim
	fi

	setup_ns ns
	defer cleanup_ns "$ns"
}

setup_bond()
{
	ip -n "$ns" link add bond0 type bond mode active-backup miimon 100
	ip -n "$ns" addr add "$srcip/24" dev bond0
	ip -n "$ns" link set bond0 up

	echo "0 2" | ip netns exec "$ns" tee /sys/bus/netdevsim/new_device >/dev/null
	nic0=$(ip netns exec "$ns" ls /sys/bus/netdevsim/devices/netdevsim0/net | head -n 1)
	nic1=$(ip netns exec "$ns" ls /sys/bus/netdevsim/devices/netdevsim0/net | tail -n 1)
	ip -n "$ns" link set "$nic0" master bond0
	ip -n "$ns" link set "$nic1" master bond0

	# we didn't create a peer, make sure we can Tx by adding a permanent
	# neighbour this need to be added after enslave
	ip -n "$ns" neigh add "$dstip" dev bond0 lladdr 00:11:22:33:44:55

	# create offloaded SAs, both in and out
	ip -n "$ns" x p add dir out src "$srcip/24" dst "$dstip/24" \
	    tmpl proto esp src "$srcip" dst "$dstip" spi 9 \
	    mode transport reqid 42

	ip -n "$ns" x p add dir in src "$dstip/24" dst "$srcip/24" \
	    tmpl proto esp src "$dstip" dst "$srcip" spi 9 \
	    mode transport reqid 42

	ip -n "$ns" x s add proto esp src "$srcip" dst "$dstip" spi 9 \
	    mode transport reqid 42 aead "rfc4106(gcm(aes))" \
	    0x3132333435363738393031323334353664636261 128 \
	    sel src "$srcip/24" dst "$dstip/24" \
	    offload dev bond0 dir out

	ip -n "$ns" x s add proto esp src "$dstip" dst "$srcip" spi 9 \
	    mode transport reqid 42 aead "rfc4106(gcm(aes))" \
	    0x3132333435363738393031323334353664636261 128 \
	    sel src "$dstip/24" dst "$srcip/24" \
	    offload dev bond0 dir in

	# does offload show up in ip output
	lines=$(ip -n "$ns" x s list | grep -c "crypto offload parameters: dev bond0 dir")
	if [ "$lines" -ne 2 ] ; then
		check_err 1 "bond_ipsec_offload SA offload missing from list output"
	fi
}

trap defer_scopes_cleanup EXIT
setup_env
setup_bond

# start Offload testing
test_offload

# do failover and re-test
ip -n "$ns" link set "$active_slave" down
slowwait 5 active_slave_changed "$active_slave"
test_offload

# make sure offload get removed from driver
ip -n "$ns" x s flush
ip -n "$ns" x p flush
line0=$(grep -c "SA count=0" "$ipsec0")
line1=$(grep -c "SA count=0" "$ipsec1")
[ "$line0" -ne 1 ] || [ "$line1" -ne 1 ]
check_fail $? "bond_ipsec_offload SA not removed from driver"

exit "$EXIT_STATUS"
