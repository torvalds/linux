#!/bin/bash
#
# Send data between two processes across namespaces
# Run twice: once without and once with zerocopy

set -e

readonly DEV="veth0"
readonly DUMMY_DEV="dummy0"
readonly DEV_MTU=65535
readonly BIN="./msg_zerocopy"

readonly RAND="$(mktemp -u XXXXXX)"
readonly NSPREFIX="ns-${RAND}"
readonly NS1="${NSPREFIX}1"
readonly NS2="${NSPREFIX}2"

readonly LPREFIX4='192.168.1'
readonly RPREFIX4='192.168.2'
readonly LPREFIX6='fd'
readonly RPREFIX6='fc'


readonly path_sysctl_mem="net.core.optmem_max"

# No arguments: automated test
if [[ "$#" -eq "0" ]]; then
	ret=0

	$0 4 tcp -t 1 || ret=1
	$0 6 tcp -t 1 || ret=1
	$0 4 udp -t 1 || ret=1
	$0 6 udp -t 1 || ret=1

	[[ "$ret" == "0" ]] && echo "OK. All tests passed"
	exit $ret
fi

# Argument parsing
if [[ "$#" -lt "2" ]]; then
	echo "Usage: $0 [4|6] [tcp|udp|raw|raw_hdrincl|packet|packet_dgram] <args>"
	exit 1
fi

readonly IP="$1"
shift
readonly TXMODE="$1"
shift
readonly EXTRA_ARGS="$@"

# Argument parsing: configure addresses
if [[ "${IP}" == "4" ]]; then
	readonly SADDR="${LPREFIX4}.1"
	readonly DADDR="${LPREFIX4}.2"
	readonly DUMMY_ADDR="${RPREFIX4}.1"
	readonly DADDR_TXONLY="${RPREFIX4}.2"
	readonly MASK="24"
elif [[ "${IP}" == "6" ]]; then
	readonly SADDR="${LPREFIX6}::1"
	readonly DADDR="${LPREFIX6}::2"
	readonly DUMMY_ADDR="${RPREFIX6}::1"
	readonly DADDR_TXONLY="${RPREFIX6}::2"
	readonly MASK="64"
	readonly NODAD="nodad"
else
	echo "Invalid IP version ${IP}"
	exit 1
fi

# Argument parsing: select receive mode
#
# This differs from send mode for
# - packet:	use raw recv, because packet receives skb clones
# - raw_hdrinc: use raw recv, because hdrincl is a tx-only option
case "${TXMODE}" in
'packet' | 'packet_dgram' | 'raw_hdrincl')
	RXMODE='raw'
	;;
*)
	RXMODE="${TXMODE}"
	;;
esac

# Start of state changes: install cleanup handler

cleanup() {
	ip netns del "${NS2}"
	ip netns del "${NS1}"
}

trap cleanup EXIT

# Create virtual ethernet pair between network namespaces
ip netns add "${NS1}"
ip netns add "${NS2}"

# Configure system settings
ip netns exec "${NS1}" sysctl -w -q "${path_sysctl_mem}=1000000"
ip netns exec "${NS2}" sysctl -w -q "${path_sysctl_mem}=1000000"

ip link add "${DEV}" mtu "${DEV_MTU}" netns "${NS1}" type veth \
  peer name "${DEV}" mtu "${DEV_MTU}" netns "${NS2}"

ip link add "${DUMMY_DEV}" mtu "${DEV_MTU}" netns "${NS2}" type dummy

# Bring the devices up
ip -netns "${NS1}" link set "${DEV}" up
ip -netns "${NS2}" link set "${DEV}" up
ip -netns "${NS2}" link set "${DUMMY_DEV}" up

# Set fixed MAC addresses on the devices
ip -netns "${NS1}" link set dev "${DEV}" address 02:02:02:02:02:02
ip -netns "${NS2}" link set dev "${DEV}" address 06:06:06:06:06:06

# Add fixed IP addresses to the devices
ip -netns "${NS1}" addr add "${SADDR}/${MASK}" dev "${DEV}" ${NODAD}
ip -netns "${NS2}" addr add "${DADDR}/${MASK}" dev "${DEV}" ${NODAD}
ip -netns "${NS2}" addr add "${DUMMY_ADDR}/${MASK}" dev "${DUMMY_DEV}" ${NODAD}

ip -netns "${NS1}" route add default via "${DADDR}" dev "${DEV}"
ip -netns "${NS2}" route add default via "${DADDR_TXONLY}" dev "${DUMMY_DEV}"

ip netns exec "${NS2}" sysctl -wq net.ipv4.ip_forward=1
ip netns exec "${NS2}" sysctl -wq net.ipv6.conf.all.forwarding=1

# Optionally disable sg or csum offload to test edge cases
# ip netns exec "${NS1}" ethtool -K "${DEV}" sg off

ret=0

do_test() {
	local readonly ARGS="$1"

	# tx-rx test
	# packets queued to a local socket are copied,
	# sender notification has SO_EE_CODE_ZEROCOPY_COPIED.

	echo -e "\nipv${IP} ${TXMODE} ${ARGS} tx-rx\n"
	ip netns exec "${NS2}" "${BIN}" "-${IP}" -i "${DEV}" -t 2 -C 2 \
		-S "${SADDR}" -D "${DADDR}" ${ARGS} -r "${RXMODE}" &
	sleep 0.2
	ip netns exec "${NS1}" "${BIN}" "-${IP}" -i "${DEV}" -t 1 -C 3 \
		-S "${SADDR}" -D "${DADDR}" ${ARGS} "${TXMODE}" -Z 0 || ret=1
	wait

	# next test is unconnected tx to dummy0, cannot exercise with tcp
	[[ "${TXMODE}" == "tcp" ]] && return

	# tx-only test: send out dummy0
	# packets leaving the host are not copied,
	# sender notification does not have SO_EE_CODE_ZEROCOPY_COPIED.

	echo -e "\nipv${IP} ${TXMODE} ${ARGS} tx-only\n"
	ip netns exec "${NS1}" "${BIN}" "-${IP}" -i "${DEV}" -t 1 -C 3 \
		-S "${SADDR}" -D "${DADDR_TXONLY}" ${ARGS} "${TXMODE}" -Z 1 || ret=1
}

do_test "${EXTRA_ARGS}"
do_test "-z ${EXTRA_ARGS}"

[[ "$ret" == "0" ]] && echo "OK"
