#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Author: Jesper Dangaard Brouer <hawk@kernel.org>

# Kselftest framework requirement - SKIP code is 4.
readonly KSFT_SKIP=4

# Allow wrapper scripts to name test
if [ -z "$TESTNAME" ]; then
    TESTNAME=xdp_vlan
fi

# Default XDP mode
XDP_MODE=xdpgeneric

usage() {
  echo "Testing XDP + TC eBPF VLAN manipulations: $TESTNAME"
  echo ""
  echo "Usage: $0 [-vfh]"
  echo "  -v | --verbose : Verbose"
  echo "  --flush        : Flush before starting (e.g. after --interactive)"
  echo "  --interactive  : Keep netns setup running after test-run"
  echo "  --mode=XXX     : Choose XDP mode (xdp | xdpgeneric | xdpdrv)"
  echo ""
}

valid_xdp_mode()
{
	local mode=$1

	case "$mode" in
		xdpgeneric | xdpdrv | xdp)
			return 0
			;;
		*)
			return 1
	esac
}

cleanup()
{
	local status=$?

	if [ "$status" = "0" ]; then
		echo "selftests: $TESTNAME [PASS]";
	else
		echo "selftests: $TESTNAME [FAILED]";
	fi

	if [ -n "$INTERACTIVE" ]; then
		echo "Namespace setup still active explore with:"
		echo " ip netns exec ns1 bash"
		echo " ip netns exec ns2 bash"
		exit $status
	fi

	set +e
	ip link del veth1 2> /dev/null
	ip netns del ns1 2> /dev/null
	ip netns del ns2 2> /dev/null
}

# Using external program "getopt" to get --long-options
OPTIONS=$(getopt -o hvfi: \
    --long verbose,flush,help,interactive,debug,mode: -- "$@")
if (( $? != 0 )); then
    usage
    echo "selftests: $TESTNAME [FAILED] Error calling getopt, unknown option?"
    exit 2
fi
eval set -- "$OPTIONS"

##  --- Parse command line arguments / parameters ---
while true; do
	case "$1" in
	    -v | --verbose)
		export VERBOSE=yes
		shift
		;;
	    -i | --interactive | --debug )
		INTERACTIVE=yes
		shift
		;;
	    -f | --flush )
		cleanup
		shift
		;;
	    --mode )
		shift
		XDP_MODE=$1
		shift
		;;
	    -- )
		shift
		break
		;;
	    -h | --help )
		usage;
		echo "selftests: $TESTNAME [SKIP] usage help info requested"
		exit $KSFT_SKIP
		;;
	    * )
		shift
		break
		;;
	esac
done

if [ "$EUID" -ne 0 ]; then
	echo "selftests: $TESTNAME [FAILED] need root privileges"
	exit 1
fi

valid_xdp_mode $XDP_MODE
if [ $? -ne 0 ]; then
	echo "selftests: $TESTNAME [FAILED] unknown XDP mode ($XDP_MODE)"
	exit 1
fi

ip link set dev lo xdpgeneric off 2>/dev/null > /dev/null
if [ $? -ne 0 ]; then
	echo "selftests: $TESTNAME [SKIP] need ip xdp support"
	exit $KSFT_SKIP
fi

# Interactive mode likely require us to cleanup netns
if [ -n "$INTERACTIVE" ]; then
	ip link del veth1 2> /dev/null
	ip netns del ns1 2> /dev/null
	ip netns del ns2 2> /dev/null
fi

# Exit on failure
set -e

# Some shell-tools dependencies
which ip > /dev/null
which tc > /dev/null
which ethtool > /dev/null

# Make rest of shell verbose, showing comments as doc/info
if [ -n "$VERBOSE" ]; then
    set -v
fi

# Create two namespaces
ip netns add ns1
ip netns add ns2

# Run cleanup if failing or on kill
trap cleanup 0 2 3 6 9

# Create veth pair
ip link add veth1 type veth peer name veth2

# Move veth1 and veth2 into the respective namespaces
ip link set veth1 netns ns1
ip link set veth2 netns ns2

# NOTICE: XDP require VLAN header inside packet payload
#  - Thus, disable VLAN offloading driver features
#  - For veth REMEMBER TX side VLAN-offload
#
# Disable rx-vlan-offload (mostly needed on ns1)
ip netns exec ns1 ethtool -K veth1 rxvlan off
ip netns exec ns2 ethtool -K veth2 rxvlan off
#
# Disable tx-vlan-offload (mostly needed on ns2)
ip netns exec ns2 ethtool -K veth2 txvlan off
ip netns exec ns1 ethtool -K veth1 txvlan off

export IPADDR1=100.64.41.1
export IPADDR2=100.64.41.2

# In ns1/veth1 add IP-addr on plain net_device
ip netns exec ns1 ip addr add ${IPADDR1}/24 dev veth1
ip netns exec ns1 ip link set veth1 up

# In ns2/veth2 create VLAN device
export VLAN=4011
export DEVNS2=veth2
ip netns exec ns2 ip link add link $DEVNS2 name $DEVNS2.$VLAN type vlan id $VLAN
ip netns exec ns2 ip addr add ${IPADDR2}/24 dev $DEVNS2.$VLAN
ip netns exec ns2 ip link set $DEVNS2 up
ip netns exec ns2 ip link set $DEVNS2.$VLAN up

# Bringup lo in netns (to avoids confusing people using --interactive)
ip netns exec ns1 ip link set lo up
ip netns exec ns2 ip link set lo up

# At this point, the hosts cannot reach each-other,
# because ns2 are using VLAN tags on the packets.

ip netns exec ns2 sh -c 'ping -W 1 -c 1 100.64.41.1 || echo "Success: First ping must fail"'


# Now we can use the test_xdp_vlan.c program to pop/push these VLAN tags
# ----------------------------------------------------------------------
# In ns1: ingress use XDP to remove VLAN tags
export DEVNS1=veth1
export FILE=test_xdp_vlan.o

# First test: Remove VLAN by setting VLAN ID 0, using "xdp_vlan_change"
export XDP_PROG=xdp_vlan_change
ip netns exec ns1 ip link set $DEVNS1 $XDP_MODE object $FILE section $XDP_PROG

# In ns1: egress use TC to add back VLAN tag 4011
#  (del cmd)
#  tc qdisc del dev $DEVNS1 clsact 2> /dev/null
#
ip netns exec ns1 tc qdisc add dev $DEVNS1 clsact
ip netns exec ns1 tc filter add dev $DEVNS1 egress \
  prio 1 handle 1 bpf da obj $FILE sec tc_vlan_push

# Now the namespaces can reach each-other, test with ping:
ip netns exec ns2 ping -i 0.2 -W 2 -c 2 $IPADDR1
ip netns exec ns1 ping -i 0.2 -W 2 -c 2 $IPADDR2

# Second test: Replace xdp prog, that fully remove vlan header
#
# Catch kernel bug for generic-XDP, that does didn't allow us to
# remove a VLAN header, because skb->protocol still contain VLAN
# ETH_P_8021Q indication, and this cause overwriting of our changes.
#
export XDP_PROG=xdp_vlan_remove_outer2
ip netns exec ns1 ip link set $DEVNS1 $XDP_MODE off
ip netns exec ns1 ip link set $DEVNS1 $XDP_MODE object $FILE section $XDP_PROG

# Now the namespaces should still be able reach each-other, test with ping:
ip netns exec ns2 ping -i 0.2 -W 2 -c 2 $IPADDR1
ip netns exec ns1 ping -i 0.2 -W 2 -c 2 $IPADDR2
