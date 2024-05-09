#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NR_FILES=32768
readonly NETNS="ns-$(mktemp -u XXXXXX)"

# default values
port=443
addr_v6="2001:0db8:0:f101::1"
addr_v4="10.8.8.8"
use_v6=true
addr=""

usage() {
    echo "Usage: $0 [-6 | -4] [-p port] [-a address]"
    echo -e "\t6: use ipv6"
    echo -e "\t4: use ipv4"
    echo -e "\tport: Port number"
    echo -e "\taddress: ip address"
}

while getopts "ha:p:64" opt; do
    case ${opt} in
	h)
	    usage $0
	    exit 0
	    ;;
	a)  addr=$OPTARG;;
	p)
	    port=$OPTARG;;
	6)
	    use_v6=true;;
	4)
	    use_v6=false;;
    esac
done

setup() {
    ip netns add "${NETNS}"
    ip -netns "${NETNS}" link add veth0 type veth peer name veth1
    ip -netns "${NETNS}" link set lo up
    ip -netns "${NETNS}" link set veth0 up
    ip -netns "${NETNS}" link set veth1 up

    if [[ "$use_v6" == true ]]; then
        ip -netns "${NETNS}" addr add $addr_v6 nodad dev veth0
    else
        ip -netns "${NETNS}" addr add $addr_v4 dev lo
    fi
}

cleanup() {
    ip netns del "${NETNS}"
}

if [[ "$addr" != "" ]]; then
    addr_v4=$addr;
    addr_v6=$addr;
fi
setup
if [[ "$use_v6" == true ]] ; then
    ip netns exec "${NETNS}" sh -c \
        "ulimit -n ${NR_FILES};./bind_bhash ${port} ipv6 ${addr_v6}"
else
    ip netns exec "${NETNS}" sh -c \
        "ulimit -n ${NR_FILES};./bind_bhash ${port} ipv4 ${addr_v4}"
fi
cleanup
