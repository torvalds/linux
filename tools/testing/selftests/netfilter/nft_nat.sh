#!/bin/bash
#
# This test is for basic NAT functionality: snat, dnat, redirect, masquerade.
#

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0
test_inet_nat=true

nft --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip netns add ns0
ip netns add ns1
ip netns add ns2

ip link add veth0 netns ns0 type veth peer name eth0 netns ns1
ip link add veth1 netns ns0 type veth peer name eth0 netns ns2

ip -net ns0 link set lo up
ip -net ns0 link set veth0 up
ip -net ns0 addr add 10.0.1.1/24 dev veth0
ip -net ns0 addr add dead:1::1/64 dev veth0

ip -net ns0 link set veth1 up
ip -net ns0 addr add 10.0.2.1/24 dev veth1
ip -net ns0 addr add dead:2::1/64 dev veth1

for i in 1 2; do
  ip -net ns$i link set lo up
  ip -net ns$i link set eth0 up
  ip -net ns$i addr add 10.0.$i.99/24 dev eth0
  ip -net ns$i route add default via 10.0.$i.1
  ip -net ns$i addr add dead:$i::99/64 dev eth0
  ip -net ns$i route add default via dead:$i::1
done

bad_counter()
{
	local ns=$1
	local counter=$2
	local expect=$3

	echo "ERROR: $counter counter in $ns has unexpected value (expected $expect)" 1>&2
	ip netns exec $ns nft list counter inet filter $counter 1>&2
}

check_counters()
{
	ns=$1
	local lret=0

	cnt=$(ip netns exec $ns nft list counter inet filter ns0in | grep -q "packets 1 bytes 84")
	if [ $? -ne 0 ]; then
		bad_counter $ns ns0in "packets 1 bytes 84"
		lret=1
	fi
	cnt=$(ip netns exec $ns nft list counter inet filter ns0out | grep -q "packets 1 bytes 84")
	if [ $? -ne 0 ]; then
		bad_counter $ns ns0out "packets 1 bytes 84"
		lret=1
	fi

	expect="packets 1 bytes 104"
	cnt=$(ip netns exec $ns nft list counter inet filter ns0in6 | grep -q "$expect")
	if [ $? -ne 0 ]; then
		bad_counter $ns ns0in6 "$expect"
		lret=1
	fi
	cnt=$(ip netns exec $ns nft list counter inet filter ns0out6 | grep -q "$expect")
	if [ $? -ne 0 ]; then
		bad_counter $ns ns0out6 "$expect"
		lret=1
	fi

	return $lret
}

check_ns0_counters()
{
	local ns=$1
	local lret=0

	cnt=$(ip netns exec ns0 nft list counter inet filter ns0in | grep -q "packets 0 bytes 0")
	if [ $? -ne 0 ]; then
		bad_counter ns0 ns0in "packets 0 bytes 0"
		lret=1
	fi

	cnt=$(ip netns exec ns0 nft list counter inet filter ns0in6 | grep -q "packets 0 bytes 0")
	if [ $? -ne 0 ]; then
		bad_counter ns0 ns0in6 "packets 0 bytes 0"
		lret=1
	fi

	cnt=$(ip netns exec ns0 nft list counter inet filter ns0out | grep -q "packets 0 bytes 0")
	if [ $? -ne 0 ]; then
		bad_counter ns0 ns0out "packets 0 bytes 0"
		lret=1
	fi
	cnt=$(ip netns exec ns0 nft list counter inet filter ns0out6 | grep -q "packets 0 bytes 0")
	if [ $? -ne 0 ]; then
		bad_counter ns0 ns0out6 "packets 0 bytes 0"
		lret=1
	fi

	for dir in "in" "out" ; do
		expect="packets 1 bytes 84"
		cnt=$(ip netns exec ns0 nft list counter inet filter ${ns}${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 $ns$dir "$expect"
			lret=1
		fi

		expect="packets 1 bytes 104"
		cnt=$(ip netns exec ns0 nft list counter inet filter ${ns}${dir}6 | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 $ns$dir6 "$expect"
			lret=1
		fi
	done

	return $lret
}

reset_counters()
{
	for i in 0 1 2;do
		ip netns exec ns$i nft reset counters inet > /dev/null
	done
}

test_local_dnat6()
{
	local family=$1
	local lret=0
	local IPF=""

	if [ $family = "inet" ];then
		IPF="ip6"
	fi

ip netns exec ns0 nft -f - <<EOF
table $family nat {
	chain output {
		type nat hook output priority 0; policy accept;
		ip6 daddr dead:1::99 dnat $IPF to dead:2::99
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add add $family dnat hook"
		return $ksft_skip
	fi

	# ping netns1, expect rewrite to netns2
	ip netns exec ns0 ping -q -c 1 dead:1::99 > /dev/null
	if [ $? -ne 0 ]; then
		lret=1
		echo "ERROR: ping6 failed"
		return $lret
	fi

	expect="packets 0 bytes 0"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns1$dir "$expect"
			lret=1
		fi
	done

	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns2$dir "$expect"
			lret=1
		fi
	done

	# expect 0 count in ns1
	expect="packets 0 bytes 0"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	# expect 1 packet in ns2
	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns2 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns0$dir "$expect"
			lret=1
		fi
	done

	test $lret -eq 0 && echo "PASS: ipv6 ping to ns1 was $family NATted to ns2"
	ip netns exec ns0 nft flush chain ip6 nat output

	return $lret
}

test_local_dnat()
{
	local family=$1
	local lret=0
	local IPF=""

	if [ $family = "inet" ];then
		IPF="ip"
	fi

ip netns exec ns0 nft -f - <<EOF 2>/dev/null
table $family nat {
	chain output {
		type nat hook output priority 0; policy accept;
		ip daddr 10.0.1.99 dnat $IPF to 10.0.2.99
	}
}
EOF
	if [ $? -ne 0 ]; then
		if [ $family = "inet" ];then
			echo "SKIP: inet nat tests"
			test_inet_nat=false
			return $ksft_skip
		fi
		echo "SKIP: Could not add add $family dnat hook"
		return $ksft_skip
	fi

	# ping netns1, expect rewrite to netns2
	ip netns exec ns0 ping -q -c 1 10.0.1.99 > /dev/null
	if [ $? -ne 0 ]; then
		lret=1
		echo "ERROR: ping failed"
		return $lret
	fi

	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns1$dir "$expect"
			lret=1
		fi
	done

	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns2$dir "$expect"
			lret=1
		fi
	done

	# expect 0 count in ns1
	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	# expect 1 packet in ns2
	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns2 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns0$dir "$expect"
			lret=1
		fi
	done

	test $lret -eq 0 && echo "PASS: ping to ns1 was $family NATted to ns2"

	ip netns exec ns0 nft flush chain $family nat output

	reset_counters
	ip netns exec ns0 ping -q -c 1 10.0.1.99 > /dev/null
	if [ $? -ne 0 ]; then
		lret=1
		echo "ERROR: ping failed"
		return $lret
	fi

	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns1$dir "$expect"
			lret=1
		fi
	done
	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns2$dir "$expect"
			lret=1
		fi
	done

	# expect 1 count in ns1
	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns0 ns0$dir "$expect"
			lret=1
		fi
	done

	# expect 0 packet in ns2
	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns2 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns2$dir "$expect"
			lret=1
		fi
	done

	test $lret -eq 0 && echo "PASS: ping to ns1 OK after $family nat output chain flush"

	return $lret
}


test_masquerade6()
{
	local family=$1
	local natflags=$1
	local lret=0

	ip netns exec ns0 sysctl net.ipv6.conf.all.forwarding=1 > /dev/null

	ip netns exec ns2 ping -q -c 1 dead:1::99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 via ipv6"
		return 1
		lret=1
	fi

	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns2$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	reset_counters

# add masquerading rule
ip netns exec ns0 nft -f - <<EOF
table $family nat {
	chain postrouting {
		type nat hook postrouting priority 0; policy accept;
		meta oif veth0 masquerade $natflags
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add add $family masquerade hook"
		return $ksft_skip
	fi

	ip netns exec ns2 ping -q -c 1 dead:1::99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
<<<<<<< HEAD
		echo "ERROR: cannot ping ns1 from ns2 with active $family masquerading"
=======
		echo "ERROR: cannot ping ns1 from ns2 with active ipv6 masquerade $natflags"
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1
		lret=1
	fi

	# ns1 should have seen packets from ns0, due to masquerade
	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do

		cnt=$(ip netns exec ns1 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	# ns1 should not have seen packets from ns2, due to masquerade
	expect="packets 0 bytes 0"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

<<<<<<< HEAD
	ip netns exec ns0 nft flush chain $family nat postrouting
=======
	ip netns exec ns2 ping -q -c 1 dead:1::99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 with active ipv6 masquerade $natflags (attempt 2)"
		lret=1
	fi

	ip netns exec ns0 nft flush chain ip6 nat postrouting
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1
	if [ $? -ne 0 ]; then
		echo "ERROR: Could not flush $family nat postrouting" 1>&2
		lret=1
	fi

<<<<<<< HEAD
	test $lret -eq 0 && echo "PASS: $family IPv6 masquerade for ns2"
=======
	test $lret -eq 0 && echo "PASS: IPv6 masquerade $natflags for ns2"
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1

	return $lret
}

test_masquerade()
{
<<<<<<< HEAD
	local family=$1
=======
	local natflags=$1
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1
	local lret=0

	ip netns exec ns0 sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
	ip netns exec ns0 sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null

	ip netns exec ns2 ping -q -c 1 10.0.1.99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 $natflags"
		lret=1
	fi

	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns2$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	reset_counters

# add masquerading rule
ip netns exec ns0 nft -f - <<EOF
table $family nat {
	chain postrouting {
		type nat hook postrouting priority 0; policy accept;
		meta oif veth0 masquerade $natflags
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add add $family masquerade hook"
		return $ksft_skip
	fi

	ip netns exec ns2 ping -q -c 1 10.0.1.99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
<<<<<<< HEAD
		echo "ERROR: cannot ping ns1 from ns2 with active $family masquerading"
=======
		echo "ERROR: cannot ping ns1 from ns2 with active ip masquere $natflags"
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1
		lret=1
	fi

	# ns1 should have seen packets from ns0, due to masquerade
	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns0${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	# ns1 should not have seen packets from ns2, due to masquerade
	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

<<<<<<< HEAD
	ip netns exec ns0 nft flush chain $family nat postrouting
=======
	ip netns exec ns2 ping -q -c 1 10.0.1.99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 with active ip masquerade $natflags (attempt 2)"
		lret=1
	fi

	ip netns exec ns0 nft flush chain ip nat postrouting
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1
	if [ $? -ne 0 ]; then
		echo "ERROR: Could not flush $family nat postrouting" 1>&2
		lret=1
	fi

<<<<<<< HEAD
	test $lret -eq 0 && echo "PASS: $family IP masquerade for ns2"
=======
	test $lret -eq 0 && echo "PASS: IP masquerade $natflags for ns2"
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1

	return $lret
}

test_redirect6()
{
	local family=$1
	local lret=0

	ip netns exec ns0 sysctl net.ipv6.conf.all.forwarding=1 > /dev/null

	ip netns exec ns2 ping -q -c 1 dead:1::99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannnot ping ns1 from ns2 via ipv6"
		lret=1
	fi

	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns2$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	reset_counters

# add redirect rule
ip netns exec ns0 nft -f - <<EOF
table $family nat {
	chain prerouting {
		type nat hook prerouting priority 0; policy accept;
		meta iif veth1 meta l4proto icmpv6 ip6 saddr dead:2::99 ip6 daddr dead:1::99 redirect
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add add $family redirect hook"
		return $ksft_skip
	fi

	ip netns exec ns2 ping -q -c 1 dead:1::99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 via ipv6 with active $family redirect"
		lret=1
	fi

	# ns1 should have seen no packets from ns2, due to redirection
	expect="packets 0 bytes 0"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	# ns0 should have seen packets from ns2, due to masquerade
	expect="packets 1 bytes 104"
	for dir in "in6" "out6" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	ip netns exec ns0 nft delete table $family nat
	if [ $? -ne 0 ]; then
		echo "ERROR: Could not delete $family nat table" 1>&2
		lret=1
	fi

	test $lret -eq 0 && echo "PASS: $family IPv6 redirection for ns2"

	return $lret
}

test_redirect()
{
	local family=$1
	local lret=0

	ip netns exec ns0 sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
	ip netns exec ns0 sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null

	ip netns exec ns2 ping -q -c 1 10.0.1.99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2"
		lret=1
	fi

	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns2$dir "$expect"
			lret=1
		fi

		cnt=$(ip netns exec ns2 nft list counter inet filter ns1${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns2 ns1$dir "$expect"
			lret=1
		fi
	done

	reset_counters

# add redirect rule
ip netns exec ns0 nft -f - <<EOF
table $family nat {
	chain prerouting {
		type nat hook prerouting priority 0; policy accept;
		meta iif veth1 ip protocol icmp ip saddr 10.0.2.99 ip daddr 10.0.1.99 redirect
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: Could not add add $family redirect hook"
		return $ksft_skip
	fi

	ip netns exec ns2 ping -q -c 1 10.0.1.99 > /dev/null # ping ns2->ns1
	if [ $? -ne 0 ] ; then
		echo "ERROR: cannot ping ns1 from ns2 with active $family ip redirect"
		lret=1
	fi

	# ns1 should have seen no packets from ns2, due to redirection
	expect="packets 0 bytes 0"
	for dir in "in" "out" ; do

		cnt=$(ip netns exec ns1 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	# ns0 should have seen packets from ns2, due to masquerade
	expect="packets 1 bytes 84"
	for dir in "in" "out" ; do
		cnt=$(ip netns exec ns0 nft list counter inet filter ns2${dir} | grep -q "$expect")
		if [ $? -ne 0 ]; then
			bad_counter ns1 ns0$dir "$expect"
			lret=1
		fi
	done

	ip netns exec ns0 nft delete table $family nat
	if [ $? -ne 0 ]; then
		echo "ERROR: Could not delete $family nat table" 1>&2
		lret=1
	fi

	test $lret -eq 0 && echo "PASS: $family IP redirection for ns2"

	return $lret
}


# ip netns exec ns0 ping -c 1 -q 10.0.$i.99
for i in 0 1 2; do
ip netns exec ns$i nft -f - <<EOF
table inet filter {
	counter ns0in {}
	counter ns1in {}
	counter ns2in {}

	counter ns0out {}
	counter ns1out {}
	counter ns2out {}

	counter ns0in6 {}
	counter ns1in6 {}
	counter ns2in6 {}

	counter ns0out6 {}
	counter ns1out6 {}
	counter ns2out6 {}

	map nsincounter {
		type ipv4_addr : counter
		elements = { 10.0.1.1 : "ns0in",
			     10.0.2.1 : "ns0in",
			     10.0.1.99 : "ns1in",
			     10.0.2.99 : "ns2in" }
	}

	map nsincounter6 {
		type ipv6_addr : counter
		elements = { dead:1::1 : "ns0in6",
			     dead:2::1 : "ns0in6",
			     dead:1::99 : "ns1in6",
			     dead:2::99 : "ns2in6" }
	}

	map nsoutcounter {
		type ipv4_addr : counter
		elements = { 10.0.1.1 : "ns0out",
			     10.0.2.1 : "ns0out",
			     10.0.1.99: "ns1out",
			     10.0.2.99: "ns2out" }
	}

	map nsoutcounter6 {
		type ipv6_addr : counter
		elements = { dead:1::1 : "ns0out6",
			     dead:2::1 : "ns0out6",
			     dead:1::99 : "ns1out6",
			     dead:2::99 : "ns2out6" }
	}

	chain input {
		type filter hook input priority 0; policy accept;
		counter name ip saddr map @nsincounter
		icmpv6 type { "echo-request", "echo-reply" } counter name ip6 saddr map @nsincounter6
	}
	chain output {
		type filter hook output priority 0; policy accept;
		counter name ip daddr map @nsoutcounter
		icmpv6 type { "echo-request", "echo-reply" } counter name ip6 daddr map @nsoutcounter6
	}
}
EOF
done

sleep 3
# test basic connectivity
for i in 1 2; do
  ip netns exec ns0 ping -c 1 -q 10.0.$i.99 > /dev/null
  if [ $? -ne 0 ];then
  	echo "ERROR: Could not reach other namespace(s)" 1>&2
	ret=1
  fi

  ip netns exec ns0 ping -c 1 -q dead:$i::99 > /dev/null
  if [ $? -ne 0 ];then
	echo "ERROR: Could not reach other namespace(s) via ipv6" 1>&2
	ret=1
  fi
  check_counters ns$i
  if [ $? -ne 0 ]; then
	ret=1
  fi

  check_ns0_counters ns$i
  if [ $? -ne 0 ]; then
	ret=1
  fi
  reset_counters
done

if [ $ret -eq 0 ];then
	echo "PASS: netns routing/connectivity: ns0 can reach ns1 and ns2"
fi

reset_counters
test_local_dnat ip
test_local_dnat6 ip6
reset_counters
$test_inet_nat && test_local_dnat inet
$test_inet_nat && test_local_dnat6 inet

reset_counters
<<<<<<< HEAD
test_masquerade ip
test_masquerade6 ip6
reset_counters
$test_inet_nat && test_masquerade inet
$test_inet_nat && test_masquerade6 inet
=======
test_masquerade ""
test_masquerade6 ""

reset_counters
test_masquerade "fully-random"
test_masquerade6 "fully-random"
>>>>>>> cd8dead0c39457e58ec1d36db93aedca811d48f1

reset_counters
test_redirect ip
test_redirect6 ip6
reset_counters
$test_inet_nat && test_redirect inet
$test_inet_nat && test_redirect6 inet

for i in 0 1 2; do ip netns del ns$i;done

exit $ret
