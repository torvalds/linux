#!/bin/sh

set -eu

ping_once()
{
	ping -${1} -q -c 1 -W 1 ${2%%/*} >/dev/null 2>&1
}

wait_for_ip()
{
	local _i
	echo -n "Wait for testing IPv4/IPv6 to become available "
	for _i in $(seq ${MAX_PING_TRIES}); do
		echo -n "."
		if ping_once 4 ${TEST_IPv4} && ping_once 6 ${TEST_IPv6}; then
			echo " OK"
			return
		fi
	done
	echo 1>&2 "ERROR: Timeout waiting for test IP to become available."
	exit 1
}

setup()
{
	# Create testing interfaces not to interfere with current environment.
	ip link add dev ${TEST_IF} type veth peer name ${TEST_IF_PEER}
	ip link set ${TEST_IF} up
	ip link set ${TEST_IF_PEER} up

	ip -4 addr add ${TEST_IPv4} dev ${TEST_IF}
	ip -6 addr add ${TEST_IPv6} dev ${TEST_IF}
	wait_for_ip
}

cleanup()
{
	ip link del ${TEST_IF} 2>/dev/null || :
	ip link del ${TEST_IF_PEER} 2>/dev/null || :
}

main()
{
	trap cleanup EXIT 2 3 6 15
	setup
	./test_sock_addr setup_done
}

BASENAME=$(basename $0 .sh)
TEST_IF="${BASENAME}1"
TEST_IF_PEER="${BASENAME}2"
TEST_IPv4="127.0.0.4/8"
TEST_IPv6="::6/128"
MAX_PING_TRIES=5

main
