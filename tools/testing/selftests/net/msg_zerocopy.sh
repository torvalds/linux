#!/bin/bash
#
# Send data between two processes across namespaces
# Run twice: once without and once with zerocopy

set -e

readonly DEV="veth0"
readonly DEV_MTU=65535
readonly BIN="./msg_zerocopy"

readonly RAND="$(mktemp -u XXXXXX)"
readonly NSPREFIX="ns-${RAND}"
readonly NS1="${NSPREFIX}1"
readonly NS2="${NSPREFIX}2"

readonly SADDR4='192.168.1.1'
readonly DADDR4='192.168.1.2'
readonly SADDR6='fd::1'
readonly DADDR6='fd::2'

readonly path_sysctl_mem="net.core.optmem_max"

# No arguments: automated test
if [[ "$#" -eq "0" ]]; then
	$0 4 tcp -t 1
	$0 6 tcp -t 1
	echo "OK. All tests passed"
	exit 0
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
	readonly SADDR="${SADDR4}"
	readonly DADDR="${DADDR4}"
elif [[ "${IP}" == "6" ]]; then
	readonly SADDR="${SADDR6}"
	readonly DADDR="${DADDR6}"
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
save_sysctl_mem="$(sysctl -n ${path_sysctl_mem})"

cleanup() {
	ip netns del "${NS2}"
	ip netns del "${NS1}"
	sysctl -w -q "${path_sysctl_mem}=${save_sysctl_mem}"
}

trap cleanup EXIT

# Configure system settings
sysctl -w -q "${path_sysctl_mem}=1000000"

# Create virtual ethernet pair between network namespaces
ip netns add "${NS1}"
ip netns add "${NS2}"

ip link add "${DEV}" mtu "${DEV_MTU}" netns "${NS1}" type veth \
  peer name "${DEV}" mtu "${DEV_MTU}" netns "${NS2}"

# Bring the devices up
ip -netns "${NS1}" link set "${DEV}" up
ip -netns "${NS2}" link set "${DEV}" up

# Set fixed MAC addresses on the devices
ip -netns "${NS1}" link set dev "${DEV}" address 02:02:02:02:02:02
ip -netns "${NS2}" link set dev "${DEV}" address 06:06:06:06:06:06

# Add fixed IP addresses to the devices
ip -netns "${NS1}" addr add 192.168.1.1/24 dev "${DEV}"
ip -netns "${NS2}" addr add 192.168.1.2/24 dev "${DEV}"
ip -netns "${NS1}" addr add       fd::1/64 dev "${DEV}" nodad
ip -netns "${NS2}" addr add       fd::2/64 dev "${DEV}" nodad

# Optionally disable sg or csum offload to test edge cases
# ip netns exec "${NS1}" ethtool -K "${DEV}" sg off

do_test() {
	local readonly ARGS="$1"

	echo "ipv${IP} ${TXMODE} ${ARGS}"
	ip netns exec "${NS2}" "${BIN}" "-${IP}" -i "${DEV}" -t 2 -C 2 -S "${SADDR}" -D "${DADDR}" ${ARGS} -r "${RXMODE}" &
	sleep 0.2
	ip netns exec "${NS1}" "${BIN}" "-${IP}" -i "${DEV}" -t 1 -C 3 -S "${SADDR}" -D "${DADDR}" ${ARGS} "${TXMODE}"
	wait
}

do_test "${EXTRA_ARGS}"
do_test "-z ${EXTRA_ARGS}"
echo ok
