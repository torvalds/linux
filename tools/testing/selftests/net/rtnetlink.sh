#!/bin/bash
#
# This test is for checking rtnetlink callpaths, and get as much coverage as possible.
#
# set -e

ALL_TESTS="
	kci_test_polrouting
	kci_test_route_get
	kci_test_addrlft
	kci_test_promote_secondaries
	kci_test_tc
	kci_test_gre
	kci_test_gretap
	kci_test_ip6gretap
	kci_test_erspan
	kci_test_ip6erspan
	kci_test_bridge
	kci_test_addrlabel
	kci_test_ifalias
	kci_test_vrf
	kci_test_encap
	kci_test_macsec
	kci_test_macsec_offload
	kci_test_ipsec
	kci_test_ipsec_offload
	kci_test_fdb_get
	kci_test_neigh_get
	kci_test_bridge_parent_id
	kci_test_address_proto
"

devdummy="test-dummy0"
VERBOSE=0
PAUSE=no
PAUSE_ON_FAIL=no

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# set global exit status, but never reset nonzero one.
check_err()
{
	if [ $ret -eq 0 ]; then
		ret=$1
	fi
}

# same but inverted -- used when command must fail for test to pass
check_fail()
{
	if [ $1 -eq 0 ]; then
		ret=1
	fi
}

run_cmd_common()
{
	local cmd="$*"
	local out
	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: ${cmd}"
	fi
	out=$($cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi
	return $rc
}

run_cmd() {
	run_cmd_common "$@"
	rc=$?
	check_err $rc
	return $rc
}
run_cmd_fail()
{
	run_cmd_common "$@"
	rc=$?
	check_fail $rc
	return $rc
}

run_cmd_grep_common()
{
	local find="$1"; shift
	local cmd="$*"
	local out
	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: ${cmd} 2>&1 | grep -q '${find}'"
	fi
	out=$($cmd 2>&1 | grep -q "${find}" 2>&1)
	return $?
}

run_cmd_grep() {
	run_cmd_grep_common "$@"
	rc=$?
	check_err $rc
	return $rc
}

run_cmd_grep_fail()
{
	run_cmd_grep_common "$@"
	rc=$?
	check_fail $rc
	return $rc
}

end_test()
{
	echo "$*"
	[ "${VERBOSE}" = "1" ] && echo

	if [[ $ret -ne 0 ]] && [[ "${PAUSE_ON_FAIL}" = "yes" ]]; then
		echo "Hit enter to continue"
		read a
	fi;

	if [ "${PAUSE}" = "yes" ]; then
		echo "Hit enter to continue"
		read a
	fi

}


kci_add_dummy()
{
	run_cmd ip link add name "$devdummy" type dummy
	run_cmd ip link set "$devdummy" up
}

kci_del_dummy()
{
	run_cmd ip link del dev "$devdummy"
}

kci_test_netconf()
{
	dev="$1"
	r=$ret
	run_cmd ip netconf show dev "$dev"
	for f in 4 6; do
		run_cmd ip -$f netconf show dev "$dev"
	done

	if [ $ret -ne 0 ] ;then
		end_test "FAIL: ip netconf show $dev"
		test $r -eq 0 && ret=0
		return 1
	fi
}

# add a bridge with vlans on top
kci_test_bridge()
{
	devbr="test-br0"
	vlandev="testbr-vlan1"

	local ret=0
	run_cmd ip link add name "$devbr" type bridge
	run_cmd ip link set dev "$devdummy" master "$devbr"
	run_cmd ip link set "$devbr" up
	run_cmd ip link add link "$devbr" name "$vlandev" type vlan id 1
	run_cmd ip addr add dev "$vlandev" 10.200.7.23/30
	run_cmd ip -6 addr add dev "$vlandev" dead:42::1234/64
	run_cmd ip -d link
	run_cmd ip r s t all

	for name in "$devbr" "$vlandev" "$devdummy" ; do
		kci_test_netconf "$name"
	done
	run_cmd ip -6 addr del dev "$vlandev" dead:42::1234/64
	run_cmd ip link del dev "$vlandev"
	run_cmd ip link del dev "$devbr"

	if [ $ret -ne 0 ];then
		end_test "FAIL: bridge setup"
		return 1
	fi
	end_test "PASS: bridge setup"

}

kci_test_gre()
{
	gredev=neta
	rem=10.42.42.1
	loc=10.0.0.1

	local ret=0
	run_cmd ip tunnel add $gredev mode gre remote $rem local $loc ttl 1
	run_cmd ip link set $gredev up
	run_cmd ip addr add 10.23.7.10 dev $gredev
	run_cmd ip route add 10.23.8.0/30 dev $gredev
	run_cmd ip addr add dev "$devdummy" 10.23.7.11/24
	run_cmd ip link
	run_cmd ip addr

	kci_test_netconf "$gredev"
	run_cmd ip addr del dev "$devdummy" 10.23.7.11/24
	run_cmd ip link del $gredev

	if [ $ret -ne 0 ];then
		end_test "FAIL: gre tunnel endpoint"
		return 1
	fi
	end_test "PASS: gre tunnel endpoint"
}

# tc uses rtnetlink too, for full tc testing
# please see tools/testing/selftests/tc-testing.
kci_test_tc()
{
	dev=lo
	local ret=0

	run_cmd tc qdisc add dev "$dev" root handle 1: htb
	run_cmd tc class add dev "$dev" parent 1: classid 1:10 htb rate 1mbit
	run_cmd tc filter add dev "$dev" parent 1:0 prio 5 handle ffe: protocol ip u32 divisor 256
	run_cmd tc filter add dev "$dev" parent 1:0 prio 5 handle ffd: protocol ip u32 divisor 256
	run_cmd tc filter add dev "$dev" parent 1:0 prio 5 handle ffc: protocol ip u32 divisor 256
	run_cmd tc filter add dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:3 u32 ht ffe:2: match ip src 10.0.0.3 flowid 1:10
	run_cmd tc filter add dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:2 u32 ht ffe:2: match ip src 10.0.0.2 flowid 1:10
	run_cmd tc filter show dev "$dev" parent  1:0
	run_cmd tc filter del dev "$dev" protocol ip parent 1: prio 5 handle ffe:2:3 u32
	run_cmd tc filter show dev "$dev" parent  1:0
	run_cmd tc qdisc del dev "$dev" root handle 1: htb

	if [ $ret -ne 0 ];then
		end_test "FAIL: tc htb hierarchy"
		return 1
	fi
	end_test "PASS: tc htb hierarchy"

}

kci_test_polrouting()
{
	local ret=0
	run_cmd ip rule add fwmark 1 lookup 100
	run_cmd ip route add local 0.0.0.0/0 dev lo table 100
	run_cmd ip r s t all
	run_cmd ip rule del fwmark 1 lookup 100
	run_cmd ip route del local 0.0.0.0/0 dev lo table 100

	if [ $ret -ne 0 ];then
		end_test "FAIL: policy route test"
		return 1
	fi
	end_test "PASS: policy routing"
}

kci_test_route_get()
{
	local hash_policy=$(sysctl -n net.ipv4.fib_multipath_hash_policy)

	local ret=0
	run_cmd ip route get 127.0.0.1
	run_cmd ip route get 127.0.0.1 dev "$devdummy"
	run_cmd ip route get ::1
	run_cmd ip route get fe80::1 dev "$devdummy"
	run_cmd ip route get 127.0.0.1 from 127.0.0.1 oif lo tos 0x10 mark 0x1
	run_cmd ip route get ::1 from ::1 iif lo oif lo tos 0x10 mark 0x1
	run_cmd ip addr add dev "$devdummy" 10.23.7.11/24
	run_cmd ip route get 10.23.7.11 from 10.23.7.12 iif "$devdummy"
	run_cmd ip route add 10.23.8.0/24 \
		nexthop via 10.23.7.13 dev "$devdummy" \
		nexthop via 10.23.7.14 dev "$devdummy"

	sysctl -wq net.ipv4.fib_multipath_hash_policy=0
	run_cmd ip route get 10.23.8.11
	sysctl -wq net.ipv4.fib_multipath_hash_policy=1
	run_cmd ip route get 10.23.8.11
	sysctl -wq net.ipv4.fib_multipath_hash_policy="$hash_policy"
	run_cmd ip route del 10.23.8.0/24
	run_cmd ip addr del dev "$devdummy" 10.23.7.11/24


	if [ $ret -ne 0 ];then
		end_test "FAIL: route get"
		return 1
	fi

	end_test "PASS: route get"
}

kci_test_addrlft()
{
	for i in $(seq 10 100) ;do
		lft=$(((RANDOM%3) + 1))
		run_cmd ip addr add 10.23.11.$i/32 dev "$devdummy" preferred_lft $lft valid_lft $((lft+1))
	done

	sleep 5
	run_cmd_grep "10.23.11." ip addr show dev "$devdummy"
	if [ $? -eq 0 ]; then
		check_err 1
		end_test "FAIL: preferred_lft addresses remaining"
		return
	fi

	end_test "PASS: preferred_lft addresses have expired"
}

kci_test_promote_secondaries()
{
	promote=$(sysctl -n net.ipv4.conf.$devdummy.promote_secondaries)

	sysctl -q net.ipv4.conf.$devdummy.promote_secondaries=1

	for i in $(seq 2 254);do
		IP="10.23.11.$i"
		ip -f inet addr add $IP/16 brd + dev "$devdummy"
		ifconfig "$devdummy" $IP netmask 255.255.0.0
	done

	ip addr flush dev "$devdummy"

	[ $promote -eq 0 ] && sysctl -q net.ipv4.conf.$devdummy.promote_secondaries=0

	end_test "PASS: promote_secondaries complete"
}

kci_test_addrlabel()
{
	local ret=0
	run_cmd ip addrlabel add prefix dead::/64 dev lo label 1
	run_cmd_grep "prefix dead::/64 dev lo label 1" ip addrlabel list
	run_cmd ip addrlabel del prefix dead::/64 dev lo label 1
	run_cmd ip addrlabel add prefix dead::/64 label 1
	run_cmd ip addrlabel del prefix dead::/64 label 1

	# concurrent add/delete
	for i in $(seq 1 1000); do
		ip addrlabel add prefix 1c3::/64 label 12345 2>/dev/null
	done &

	for i in $(seq 1 1000); do
		ip addrlabel del prefix 1c3::/64 label 12345 2>/dev/null
	done

	wait

	ip addrlabel del prefix 1c3::/64 label 12345 2>/dev/null

	if [ $ret -ne 0 ];then
		end_test "FAIL: ipv6 addrlabel"
		return 1
	fi

	end_test "PASS: ipv6 addrlabel"
}

kci_test_ifalias()
{
	local ret=0
	namewant=$(uuidgen)
	syspathname="/sys/class/net/$devdummy/ifalias"
	run_cmd ip link set dev "$devdummy" alias "$namewant"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: cannot set interface alias of $devdummy to $namewant"
		return 1
	fi
	run_cmd_grep "alias $namewant" ip link show "$devdummy"

	if [ -r "$syspathname" ] ; then
		read namehave < "$syspathname"
		if [ "$namewant" != "$namehave" ]; then
			end_test "FAIL: did set ifalias $namewant but got $namehave"
			return 1
		fi

		namewant=$(uuidgen)
		echo "$namewant" > "$syspathname"
	        run_cmd_grep "alias $namewant" ip link show "$devdummy"

		# sysfs interface allows to delete alias again
		echo "" > "$syspathname"
	        run_cmd_grep_fail "alias $namewant" ip link show "$devdummy"

		for i in $(seq 1 100); do
			uuidgen > "$syspathname" &
		done

		wait

		# re-add the alias -- kernel should free mem when dummy dev is removed
		run_cmd ip link set dev "$devdummy" alias "$namewant"

	fi

	if [ $ret -ne 0 ]; then
		end_test "FAIL: set interface alias $devdummy to $namewant"
		return 1
	fi

	end_test "PASS: set ifalias $namewant for $devdummy"
}

kci_test_vrf()
{
	vrfname="test-vrf"
	local ret=0
	run_cmd ip link show type vrf
	if [ $? -ne 0 ]; then
		end_test "SKIP: vrf: iproute2 too old"
		return $ksft_skip
	fi
	run_cmd ip link add "$vrfname" type vrf table 10
	if [ $ret -ne 0 ];then
		end_test "FAIL: can't add vrf interface, skipping test"
		return 0
	fi
	run_cmd_grep "$vrfname" ip -br link show type vrf
	if [ $ret -ne 0 ];then
		end_test "FAIL: created vrf device not found"
		return 1
	fi

	run_cmd ip link set dev "$vrfname" up
	run_cmd ip link set dev "$devdummy" master "$vrfname"
	run_cmd ip link del dev "$vrfname"

	if [ $ret -ne 0 ];then
		end_test "FAIL: vrf"
		return 1
	fi

	end_test "PASS: vrf"
}

kci_test_encap_vxlan()
{
	local ret=0
	vxlan="test-vxlan0"
	vlan="test-vlan0"
	testns="$1"
	run_cmd ip -netns "$testns" link add "$vxlan" type vxlan id 42 group 239.1.1.1 \
		dev "$devdummy" dstport 4789
	if [ $? -ne 0 ]; then
		end_test "FAIL: can't add vxlan interface, skipping test"
		return 0
	fi

	run_cmd ip -netns "$testns" addr add 10.2.11.49/24 dev "$vxlan"
	run_cmd ip -netns "$testns" link set up dev "$vxlan"
	run_cmd ip -netns "$testns" link add link "$vxlan" name "$vlan" type vlan id 1

	# changelink testcases
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan vni 43
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan group ffe5::5 dev "$devdummy"
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan ttl inherit

	run_cmd ip -netns "$testns" link set dev "$vxlan" type vxlan ttl 64
	run_cmd ip -netns "$testns" link set dev "$vxlan" type vxlan nolearning

	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan proxy
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan norsc
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan l2miss
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan l3miss
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan external
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan udpcsum
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan udp6zerocsumtx
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan udp6zerocsumrx
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan remcsumtx
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan remcsumrx
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan gbp
	run_cmd_fail ip -netns "$testns" link set dev "$vxlan" type vxlan gpe
	run_cmd ip -netns "$testns" link del "$vxlan"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: vxlan"
		return 1
	fi
	end_test "PASS: vxlan"
}

kci_test_encap_fou()
{
	local ret=0
	name="test-fou"
	testns="$1"
	run_cmd_grep 'Usage: ip fou' ip fou help
	if [ $? -ne 0 ];then
		end_test "SKIP: fou: iproute2 too old"
		return $ksft_skip
	fi

	if ! /sbin/modprobe -q -n fou; then
		end_test "SKIP: module fou is not found"
		return $ksft_skip
	fi
	/sbin/modprobe -q fou

	run_cmd ip -netns "$testns" fou add port 7777 ipproto 47
	if [ $? -ne 0 ];then
		end_test "FAIL: can't add fou port 7777, skipping test"
		return 1
	fi
	run_cmd ip -netns "$testns" fou add port 8888 ipproto 4
	run_cmd_fail ip -netns "$testns" fou del port 9999
	run_cmd ip -netns "$testns" fou del port 7777
	if [ $ret -ne 0 ]; then
		end_test "FAIL: fou"s
		return 1
	fi

	end_test "PASS: fou"
}

# test various encap methods, use netns to avoid unwanted interference
kci_test_encap()
{
	testns="testns"
	local ret=0
	run_cmd ip netns add "$testns"
	if [ $? -ne 0 ]; then
		end_test "SKIP encap tests: cannot add net namespace $testns"
		return $ksft_skip
	fi
	run_cmd ip -netns "$testns" link set lo up
	run_cmd ip -netns "$testns" link add name "$devdummy" type dummy
	run_cmd ip -netns "$testns" link set "$devdummy" up
	run_cmd kci_test_encap_vxlan "$testns"
	run_cmd kci_test_encap_fou "$testns"

	ip netns del "$testns"
	return $ret
}

kci_test_macsec()
{
	msname="test_macsec0"
	local ret=0
	run_cmd_grep "^Usage: ip macsec" ip macsec help
	if [ $? -ne 0 ]; then
		end_test "SKIP: macsec: iproute2 too old"
		return $ksft_skip
	fi
	run_cmd ip link add link "$devdummy" "$msname" type macsec port 42 encrypt on
	if [ $ret -ne 0 ];then
		end_test "FAIL: can't add macsec interface, skipping test"
		return 1
	fi
	run_cmd ip macsec add "$msname" tx sa 0 pn 1024 on key 01 12345678901234567890123456789012
	run_cmd ip macsec add "$msname" rx port 1234 address "1c:ed:de:ad:be:ef"
	run_cmd ip macsec add "$msname" rx port 1234 address "1c:ed:de:ad:be:ef" sa 0 pn 1 on key 00 0123456789abcdef0123456789abcdef
	run_cmd ip macsec show
	run_cmd ip link del dev "$msname"

	if [ $ret -ne 0 ];then
		end_test "FAIL: macsec"
		return 1
	fi

	end_test "PASS: macsec"
}

kci_test_macsec_offload()
{
	sysfsd=/sys/kernel/debug/netdevsim/netdevsim0/ports/0/
	sysfsnet=/sys/bus/netdevsim/devices/netdevsim0/net/
	probed=false
	local ret=0
	run_cmd_grep "^Usage: ip macsec" ip macsec help
	if [ $? -ne 0 ]; then
		end_test "SKIP: macsec: iproute2 too old"
		return $ksft_skip
	fi

	# setup netdevsim since dummydev doesn't have offload support
	if [ ! -w /sys/bus/netdevsim/new_device ] ; then
		run_cmd modprobe -q netdevsim

		if [ $ret -ne 0 ]; then
			end_test "SKIP: macsec_offload can't load netdevsim"
			return $ksft_skip
		fi
		probed=true
	fi

	echo "0" > /sys/bus/netdevsim/new_device
	while [ ! -d $sysfsnet ] ; do :; done
	udevadm settle
	dev=`ls $sysfsnet`

	ip link set $dev up
	if [ ! -d $sysfsd ] ; then
		end_test "FAIL: macsec_offload can't create device $dev"
		return 1
	fi
	run_cmd_grep 'macsec-hw-offload: on' ethtool -k $dev
	if [ $? -eq 1 ] ; then
		end_test "FAIL: macsec_offload netdevsim doesn't support MACsec offload"
		return 1
	fi
	run_cmd ip link add link $dev kci_macsec1 type macsec port 4 offload mac
	run_cmd ip link add link $dev kci_macsec2 type macsec address "aa:bb:cc:dd:ee:ff" port 5 offload mac
	run_cmd ip link add link $dev kci_macsec3 type macsec sci abbacdde01020304 offload mac
	run_cmd_fail ip link add link $dev kci_macsec4 type macsec port 8 offload mac

	msname=kci_macsec1
	run_cmd ip macsec add "$msname" tx sa 0 pn 1024 on key 01 12345678901234567890123456789012
	run_cmd ip macsec add "$msname" rx port 1234 address "1c:ed:de:ad:be:ef"
	run_cmd ip macsec add "$msname" rx port 1234 address "1c:ed:de:ad:be:ef" sa 0 pn 1 on \
		key 00 0123456789abcdef0123456789abcdef
	run_cmd_fail ip macsec add "$msname" rx port 1235 address "1c:ed:de:ad:be:ef"
	# clean up any leftovers
	for msdev in kci_macsec{1,2,3,4} ; do
	    ip link del $msdev 2> /dev/null
	done
	echo 0 > /sys/bus/netdevsim/del_device
	$probed && rmmod netdevsim

	if [ $ret -ne 0 ]; then
		end_test "FAIL: macsec_offload"
		return 1
	fi
	end_test "PASS: macsec_offload"
}

#-------------------------------------------------------------------
# Example commands
#   ip x s add proto esp src 14.0.0.52 dst 14.0.0.70 \
#            spi 0x07 mode transport reqid 0x07 replay-window 32 \
#            aead 'rfc4106(gcm(aes))' 1234567890123456dcba 128 \
#            sel src 14.0.0.52/24 dst 14.0.0.70/24
#   ip x p add dir out src 14.0.0.52/24 dst 14.0.0.70/24 \
#            tmpl proto esp src 14.0.0.52 dst 14.0.0.70 \
#            spi 0x07 mode transport reqid 0x07
#
# Subcommands not tested
#    ip x s update
#    ip x s allocspi
#    ip x s deleteall
#    ip x p update
#    ip x p deleteall
#    ip x p set
#-------------------------------------------------------------------
kci_test_ipsec()
{
	local ret=0
	algo="aead rfc4106(gcm(aes)) 0x3132333435363738393031323334353664636261 128"
	srcip=192.168.123.1
	dstip=192.168.123.2
	spi=7

	ip addr add $srcip dev $devdummy

	# flush to be sure there's nothing configured
	run_cmd ip x s flush ; ip x p flush

	# start the monitor in the background
	tmpfile=`mktemp /var/run/ipsectestXXX`
	mpid=`(ip x m > $tmpfile & echo $!) 2>/dev/null`
	sleep 0.2

	ipsecid="proto esp src $srcip dst $dstip spi 0x07"
	run_cmd ip x s add $ipsecid \
            mode transport reqid 0x07 replay-window 32 \
            $algo sel src $srcip/24 dst $dstip/24


	lines=`ip x s list | grep $srcip | grep $dstip | wc -l`
	run_cmd test $lines -eq 2
	run_cmd_grep "SAD count 1" ip x s count

	lines=`ip x s get $ipsecid | grep $srcip | grep $dstip | wc -l`
	run_cmd test $lines -eq 2
	run_cmd ip x s delete $ipsecid

	lines=`ip x s list | wc -l`
	run_cmd test $lines -eq 0

	ipsecsel="dir out src $srcip/24 dst $dstip/24"
	run_cmd ip x p add $ipsecsel \
		    tmpl proto esp src $srcip dst $dstip \
		    spi 0x07 mode transport reqid 0x07


	lines=`ip x p list | grep $srcip | grep $dstip | wc -l`
	run_cmd test $lines -eq 2

	run_cmd_grep "SPD IN  0 OUT 1 FWD 0" ip x p count

	lines=`ip x p get $ipsecsel | grep $srcip | grep $dstip | wc -l`
	run_cmd test $lines -eq 2

	run_cmd ip x p delete $ipsecsel

	lines=`ip x p list | wc -l`
	run_cmd test $lines -eq 0

	# check the monitor results
	kill $mpid
	lines=`wc -l $tmpfile | cut "-d " -f1`
	run_cmd test $lines -eq 20
	rm -rf $tmpfile

	# clean up any leftovers
	run_cmd ip x s flush
	run_cmd ip x p flush
	ip addr del $srcip/32 dev $devdummy

	if [ $ret -ne 0 ]; then
		end_test "FAIL: ipsec"
		return 1
	fi
	end_test "PASS: ipsec"
}

#-------------------------------------------------------------------
# Example commands
#   ip x s add proto esp src 14.0.0.52 dst 14.0.0.70 \
#            spi 0x07 mode transport reqid 0x07 replay-window 32 \
#            aead 'rfc4106(gcm(aes))' 1234567890123456dcba 128 \
#            sel src 14.0.0.52/24 dst 14.0.0.70/24
#            offload dev sim1 dir out
#   ip x p add dir out src 14.0.0.52/24 dst 14.0.0.70/24 \
#            tmpl proto esp src 14.0.0.52 dst 14.0.0.70 \
#            spi 0x07 mode transport reqid 0x07
#
#-------------------------------------------------------------------
kci_test_ipsec_offload()
{
	local ret=0
	algo="aead rfc4106(gcm(aes)) 0x3132333435363738393031323334353664636261 128"
	srcip=192.168.123.3
	dstip=192.168.123.4
	sysfsd=/sys/kernel/debug/netdevsim/netdevsim0/ports/0/
	sysfsf=$sysfsd/ipsec
	sysfsnet=/sys/bus/netdevsim/devices/netdevsim0/net/
	probed=false

	# setup netdevsim since dummydev doesn't have offload support
	if [ ! -w /sys/bus/netdevsim/new_device ] ; then
		run_cmd modprobe -q netdevsim
		if [ $ret -ne 0 ]; then
			end_test "SKIP: ipsec_offload can't load netdevsim"
			return $ksft_skip
		fi
		probed=true
	fi

	echo "0" > /sys/bus/netdevsim/new_device
	while [ ! -d $sysfsnet ] ; do :; done
	udevadm settle
	dev=`ls $sysfsnet`

	ip addr add $srcip dev $dev
	ip link set $dev up
	if [ ! -d $sysfsd ] ; then
		end_test "FAIL: ipsec_offload can't create device $dev"
		return 1
	fi
	if [ ! -f $sysfsf ] ; then
		end_test "FAIL: ipsec_offload netdevsim doesn't support IPsec offload"
		return 1
	fi

	# flush to be sure there's nothing configured
	ip x s flush ; ip x p flush

	# create offloaded SAs, both in and out
	run_cmd ip x p add dir out src $srcip/24 dst $dstip/24 \
	    tmpl proto esp src $srcip dst $dstip spi 9 \
	    mode transport reqid 42

	run_cmd ip x p add dir in src $dstip/24 dst $srcip/24 \
	    tmpl proto esp src $dstip dst $srcip spi 9 \
	    mode transport reqid 42

	run_cmd ip x s add proto esp src $srcip dst $dstip spi 9 \
	    mode transport reqid 42 $algo sel src $srcip/24 dst $dstip/24 \
	    offload dev $dev dir out

	run_cmd ip x s add proto esp src $dstip dst $srcip spi 9 \
	    mode transport reqid 42 $algo sel src $dstip/24 dst $srcip/24 \
	    offload dev $dev dir in

	if [ $ret -ne 0 ]; then
		end_test "FAIL: ipsec_offload can't create SA"
		return 1
	fi

	# does offload show up in ip output
	lines=`ip x s list | grep -c "crypto offload parameters: dev $dev dir"`
	if [ $lines -ne 2 ] ; then
		check_err 1
		end_test "FAIL: ipsec_offload SA offload missing from list output"
	fi

	# use ping to exercise the Tx path
	ping -I $dev -c 3 -W 1 -i 0 $dstip >/dev/null

	# does driver have correct offload info
	run_cmd diff $sysfsf - << EOF
SA count=2 tx=3
sa[0] tx ipaddr=0x00000000 00000000 00000000 00000000
sa[0]    spi=0x00000009 proto=0x32 salt=0x61626364 crypt=1
sa[0]    key=0x34333231 38373635 32313039 36353433
sa[1] rx ipaddr=0x00000000 00000000 00000000 037ba8c0
sa[1]    spi=0x00000009 proto=0x32 salt=0x61626364 crypt=1
sa[1]    key=0x34333231 38373635 32313039 36353433
EOF
	if [ $? -ne 0 ] ; then
		end_test "FAIL: ipsec_offload incorrect driver data"
		check_err 1
	fi

	# does offload get removed from driver
	ip x s flush
	ip x p flush
	lines=`grep -c "SA count=0" $sysfsf`
	if [ $lines -ne 1 ] ; then
		check_err 1
		end_test "FAIL: ipsec_offload SA not removed from driver"
	fi

	# clean up any leftovers
	echo 0 > /sys/bus/netdevsim/del_device
	$probed && rmmod netdevsim

	if [ $ret -ne 0 ]; then
		end_test "FAIL: ipsec_offload"
		return 1
	fi
	end_test "PASS: ipsec_offload"
}

kci_test_gretap()
{
	testns="testns"
	DEV_NS=gretap00
	local ret=0

	run_cmd ip netns add "$testns"
	if [ $? -ne 0 ]; then
		end_test "SKIP gretap tests: cannot add net namespace $testns"
		return $ksft_skip
	fi

	run_cmd_grep "^Usage:" ip link help gretap
	if [ $? -ne 0 ];then
		end_test "SKIP: gretap: iproute2 too old"
		ip netns del "$testns"
		return $ksft_skip
	fi

	# test native tunnel
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type gretap seq \
		key 102 local 172.16.1.100 remote 172.16.1.200


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" 10.1.1.100/24
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test external mode
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type gretap external
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: gretap"
		ip netns del "$testns"
		return 1
	fi
	end_test "PASS: gretap"

	ip netns del "$testns"
}

kci_test_ip6gretap()
{
	testns="testns"
	DEV_NS=ip6gretap00
	local ret=0

	run_cmd ip netns add "$testns"
	if [ $? -ne 0 ]; then
		end_test "SKIP ip6gretap tests: cannot add net namespace $testns"
		return $ksft_skip
	fi

	run_cmd_grep "^Usage:" ip link help ip6gretap
	if [ $? -ne 0 ];then
		end_test "SKIP: ip6gretap: iproute2 too old"
		ip netns del "$testns"
		return $ksft_skip
	fi

	# test native tunnel
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type ip6gretap seq \
		key 102 local fc00:100::1 remote fc00:100::2


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" fc00:200::1/96
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test external mode
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type ip6gretap external
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: ip6gretap"
		ip netns del "$testns"
		return 1
	fi
	end_test "PASS: ip6gretap"

	ip netns del "$testns"
}

kci_test_erspan()
{
	testns="testns"
	DEV_NS=erspan00
	local ret=0
	run_cmd_grep "^Usage:" ip link help erspan
	if [ $? -ne 0 ];then
		end_test "SKIP: erspan: iproute2 too old"
		return $ksft_skip
	fi
	run_cmd ip netns add "$testns"
	if [ $? -ne 0 ]; then
		end_test "SKIP erspan tests: cannot add net namespace $testns"
		return $ksft_skip
	fi

	# test native tunnel erspan v1
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type erspan seq \
		key 102 local 172.16.1.100 remote 172.16.1.200 \
		erspan_ver 1 erspan 488


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" 10.1.1.100/24
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test native tunnel erspan v2
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type erspan seq \
		key 102 local 172.16.1.100 remote 172.16.1.200 \
		erspan_ver 2 erspan_dir ingress erspan_hwid 7


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" 10.1.1.100/24
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test external mode
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type erspan external
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: erspan"
		ip netns del "$testns"
		return 1
	fi
	end_test "PASS: erspan"

	ip netns del "$testns"
}

kci_test_ip6erspan()
{
	testns="testns"
	DEV_NS=ip6erspan00
	local ret=0
	run_cmd_grep "^Usage:" ip link help ip6erspan
	if [ $? -ne 0 ];then
		end_test "SKIP: ip6erspan: iproute2 too old"
		return $ksft_skip
	fi
	run_cmd ip netns add "$testns"
	if [ $? -ne 0 ]; then
		end_test "SKIP ip6erspan tests: cannot add net namespace $testns"
		return $ksft_skip
	fi

	# test native tunnel ip6erspan v1
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type ip6erspan seq \
		key 102 local fc00:100::1 remote fc00:100::2 \
		erspan_ver 1 erspan 488


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" 10.1.1.100/24
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test native tunnel ip6erspan v2
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" type ip6erspan seq \
		key 102 local fc00:100::1 remote fc00:100::2 \
		erspan_ver 2 erspan_dir ingress erspan_hwid 7


	run_cmd ip -netns "$testns" addr add dev "$DEV_NS" 10.1.1.100/24
	run_cmd ip -netns "$testns" link set dev $DEV_NS up
	run_cmd ip -netns "$testns" link del "$DEV_NS"

	# test external mode
	run_cmd ip -netns "$testns" link add dev "$DEV_NS" \
		type ip6erspan external

	run_cmd ip -netns "$testns" link del "$DEV_NS"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: ip6erspan"
		ip netns del "$testns"
		return 1
	fi
	end_test "PASS: ip6erspan"

	ip netns del "$testns"
}

kci_test_fdb_get()
{
	IP="ip -netns testns"
	BRIDGE="bridge -netns testns"
	brdev="test-br0"
	vxlandev="vxlan10"
	test_mac=de:ad:be:ef:13:37
	localip="10.0.2.2"
	dstip="10.0.2.3"
	local ret=0

	run_cmd_grep 'bridge fdb get' bridge fdb help
	if [ $? -ne 0 ];then
		end_test "SKIP: fdb get tests: iproute2 too old"
		return $ksft_skip
	fi

	run_cmd ip netns add testns
	if [ $? -ne 0 ]; then
		end_test "SKIP fdb get tests: cannot add net namespace $testns"
		return $ksft_skip
	fi
	run_cmd $IP link add "$vxlandev" type vxlan id 10 local $localip \
                dstport 4789
	run_cmd $IP link add name "$brdev" type bridge
	run_cmd $IP link set dev "$vxlandev" master "$brdev"
	run_cmd $BRIDGE fdb add $test_mac dev "$vxlandev" master
	run_cmd $BRIDGE fdb add $test_mac dev "$vxlandev" dst $dstip self
	run_cmd_grep "dev $vxlandev master $brdev" $BRIDGE fdb get $test_mac brport "$vxlandev"
	run_cmd_grep "dev $vxlandev master $brdev" $BRIDGE fdb get $test_mac br "$brdev"
	run_cmd_grep "dev $vxlandev dst $dstip" $BRIDGE fdb get $test_mac dev "$vxlandev" self

	ip netns del testns &>/dev/null

	if [ $ret -ne 0 ]; then
		end_test "FAIL: bridge fdb get"
		return 1
	fi

	end_test "PASS: bridge fdb get"
}

kci_test_neigh_get()
{
	dstmac=de:ad:be:ef:13:37
	dstip=10.0.2.4
	dstip6=dead::2
	local ret=0

	run_cmd_grep 'ip neigh get' ip neigh help
	if [ $? -ne 0 ];then
		end_test "SKIP: fdb get tests: iproute2 too old"
		return $ksft_skip
	fi

	# ipv4
	run_cmd ip neigh add $dstip lladdr $dstmac dev "$devdummy"
	run_cmd_grep "$dstmac" ip neigh get $dstip dev "$devdummy"
	run_cmd ip neigh del $dstip lladdr $dstmac dev "$devdummy"

	# ipv4 proxy
	run_cmd ip neigh add proxy $dstip dev "$devdummy"
	run_cmd_grep "$dstip" ip neigh get proxy $dstip dev "$devdummy"
	run_cmd ip neigh del proxy $dstip dev "$devdummy"

	# ipv6
	run_cmd ip neigh add $dstip6 lladdr $dstmac dev "$devdummy"
	run_cmd_grep "$dstmac" ip neigh get $dstip6 dev "$devdummy"
	run_cmd ip neigh del $dstip6 lladdr $dstmac dev "$devdummy"

	# ipv6 proxy
	run_cmd ip neigh add proxy $dstip6 dev "$devdummy"
	run_cmd_grep "$dstip6" ip neigh get proxy $dstip6 dev "$devdummy"
	run_cmd ip neigh del proxy $dstip6 dev "$devdummy"

	if [ $ret -ne 0 ];then
		end_test "FAIL: neigh get"
		return 1
	fi

	end_test "PASS: neigh get"
}

kci_test_bridge_parent_id()
{
	local ret=0
	sysfsnet=/sys/bus/netdevsim/devices/netdevsim
	probed=false

	if [ ! -w /sys/bus/netdevsim/new_device ] ; then
		run_cmd modprobe -q netdevsim
		if [ $ret -ne 0 ]; then
			end_test "SKIP: bridge_parent_id can't load netdevsim"
			return $ksft_skip
		fi
		probed=true
	fi

	echo "10 1" > /sys/bus/netdevsim/new_device
	while [ ! -d ${sysfsnet}10 ] ; do :; done
	echo "20 1" > /sys/bus/netdevsim/new_device
	while [ ! -d ${sysfsnet}20 ] ; do :; done
	udevadm settle
	dev10=`ls ${sysfsnet}10/net/`
	dev20=`ls ${sysfsnet}20/net/`
	run_cmd ip link add name test-bond0 type bond mode 802.3ad
	run_cmd ip link set dev $dev10 master test-bond0
	run_cmd ip link set dev $dev20 master test-bond0
	run_cmd ip link add name test-br0 type bridge
	run_cmd ip link set dev test-bond0 master test-br0

	# clean up any leftovers
	ip link del dev test-br0
	ip link del dev test-bond0
	echo 20 > /sys/bus/netdevsim/del_device
	echo 10 > /sys/bus/netdevsim/del_device
	$probed && rmmod netdevsim

	if [ $ret -ne 0 ]; then
		end_test "FAIL: bridge_parent_id"
		return 1
	fi
	end_test "PASS: bridge_parent_id"
}

address_get_proto()
{
	local addr=$1; shift

	ip -N -j address show dev "$devdummy" |
	    jq -e -r --arg addr "${addr%/*}" \
	       '.[].addr_info[] | select(.local == $addr) | .protocol'
}

address_count()
{
	ip -N -j address show dev "$devdummy" "$@" |
	    jq -e -r '[.[].addr_info[] | .local | select(. != null)] | length'
}

do_test_address_proto()
{
	local what=$1; shift
	local addr=$1; shift
	local addr2=${addr%/*}2/${addr#*/}
	local addr3=${addr%/*}3/${addr#*/}
	local proto
	local count
	local ret=0
	local err

	ip address add dev "$devdummy" "$addr3"
	check_err $?
	proto=$(address_get_proto "$addr3")
	[[ "$proto" == null ]]
	check_err $?

	ip address add dev "$devdummy" "$addr2" proto 0x99
	check_err $?
	proto=$(address_get_proto "$addr2")
	[[ "$proto" == 0x99 ]]
	check_err $?

	ip address add dev "$devdummy" "$addr" proto 0xab
	check_err $?
	proto=$(address_get_proto "$addr")
	[[ "$proto" == 0xab ]]
	check_err $?

	ip address replace dev "$devdummy" "$addr" proto 0x11
	proto=$(address_get_proto "$addr")
	check_err $?
	[[ "$proto" == 0x11 ]]
	check_err $?

	count=$(address_count)
	check_err $?
	(( count >= 3 )) # $addr, $addr2 and $addr3 plus any kernel addresses
	check_err $?

	count=$(address_count proto 0)
	check_err $?
	(( count == 1 )) # just $addr3
	check_err $?

	count=$(address_count proto 0x11)
	check_err $?
	(( count == 2 )) # $addr and $addr3
	check_err $?

	count=$(address_count proto 0xab)
	check_err $?
	(( count == 1 )) # just $addr3
	check_err $?

	ip address del dev "$devdummy" "$addr"
	ip address del dev "$devdummy" "$addr2"
	ip address del dev "$devdummy" "$addr3"

	if [ $ret -ne 0 ]; then
		end_test "FAIL: address proto $what"
		return 1
	fi
	end_test "PASS: address proto $what"
}

kci_test_address_proto()
{
	local ret=0

	do_test_address_proto IPv4 192.0.2.1/28
	check_err $?

	do_test_address_proto IPv6 2001:db8:1::1/64
	check_err $?

	return $ret
}

kci_test_rtnl()
{
	local current_test
	local ret=0

	kci_add_dummy
	if [ $ret -ne 0 ];then
		end_test "FAIL: cannot add dummy interface"
		return 1
	fi

	for current_test in ${TESTS:-$ALL_TESTS}; do
		$current_test
		check_err $?
	done

	kci_del_dummy
	return $ret
}

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $(echo $ALL_TESTS))
        -v          Verbose mode (show commands and output)
        -P          Pause after every test
        -p          Pause after every failing test before cleanup (for debugging)
EOF
}

#check for needed privileges
if [ "$(id -u)" -ne 0 ];then
	end_test "SKIP: Need root privileges"
	exit $ksft_skip
fi

for x in ip tc;do
	$x -Version 2>/dev/null >/dev/null
	if [ $? -ne 0 ];then
		end_test "SKIP: Could not run test without the $x tool"
		exit $ksft_skip
	fi
done

while getopts t:hvpP o; do
	case $o in
		t) TESTS=$OPTARG;;
		v) VERBOSE=1;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

[ $PAUSE = "yes" ] && PAUSE_ON_FAIL="no"

kci_test_rtnl

exit $?
