#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NR_FILES=32768
SAVED_NR_FILES=$(ulimit -n)

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
    if [[ "$use_v6" == true ]]; then
	ip addr add $addr_v6 nodad dev eth0
    else
	ip addr add $addr_v4 dev lo
    fi
	ulimit -n $NR_FILES
}

cleanup() {
    if [[ "$use_v6" == true ]]; then
	ip addr del $addr_v6 dev eth0
    else
	ip addr del $addr_v4/32 dev lo
    fi
    ulimit -n $SAVED_NR_FILES
}

if [[ "$addr" != "" ]]; then
    addr_v4=$addr;
    addr_v6=$addr;
fi
setup
if [[ "$use_v6" == true ]] ; then
    ./bind_bhash $port "ipv6" $addr_v6
else
    ./bind_bhash $port "ipv4" $addr_v4
fi
cleanup
