#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2020 Intel Corporation, Weqaar Janjua <weqaar.a.janjua@intel.com>

# AF_XDP selftests based on veth
#
# End-to-end AF_XDP over Veth test
#
# Topology:
# ---------
#                 -----------
#               _ | Process | _
#              /  -----------  \
#             /        |        \
#            /         |         \
#      -----------     |     -----------
#      | Thread1 |     |     | Thread2 |
#      -----------     |     -----------
#           |          |          |
#      -----------     |     -----------
#      |  xskX   |     |     |  xskY   |
#      -----------     |     -----------
#           |          |          |
#      -----------     |     ----------
#      |  vethX  | --------- |  vethY |
#      -----------   peer    ----------
#           |          |          |
#      namespaceX      |     namespaceY
#
# AF_XDP is an address family optimized for high performance packet processing,
# it is XDP’s user-space interface.
#
# An AF_XDP socket is linked to a single UMEM which is a region of virtual
# contiguous memory, divided into equal-sized frames.
#
# Refer to AF_XDP Kernel Documentation for detailed information:
# https://www.kernel.org/doc/html/latest/networking/af_xdp.html
#
# Prerequisites setup by script:
#
#   Set up veth interfaces as per the topology shown ^^:
#   * setup two veth interfaces and one namespace
#   ** veth<xxxx> in root namespace
#   ** veth<yyyy> in af_xdp<xxxx> namespace
#   ** namespace af_xdp<xxxx>
#   * create a spec file veth.spec that includes this run-time configuration
#   *** xxxx and yyyy are randomly generated 4 digit numbers used to avoid
#       conflict with any existing interface
#   * tests the veth and xsk layers of the topology
#
# See the source xdpxceiver.c for information on each test
#
# Kernel configuration:
# ---------------------
# See "config" file for recommended kernel config options.
#
# Turn on XDP sockets and veth support when compiling i.e.
# 	Networking support -->
# 		Networking options -->
# 			[ * ] XDP sockets
#
# Executing Tests:
# ----------------
# Must run with CAP_NET_ADMIN capability.
#
# Run:
#   sudo ./test_xsk.sh
#
# If running from kselftests:
#   sudo make run_tests
#
# Run with verbose output:
#   sudo ./test_xsk.sh -v
#
# Run and dump packet contents:
#   sudo ./test_xsk.sh -D

. xsk_prereqs.sh

while getopts "cvD" flag
do
	case "${flag}" in
		v) verbose=1;;
		D) dump_pkts=1;;
	esac
done

TEST_NAME="PREREQUISITES"

URANDOM=/dev/urandom
[ ! -e "${URANDOM}" ] && { echo "${URANDOM} not found. Skipping tests."; test_exit 1 1; }

VETH0_POSTFIX=$(cat ${URANDOM} | tr -dc '0-9' | fold -w 256 | head -n 1 | head --bytes 4)
VETH0=ve${VETH0_POSTFIX}
VETH1_POSTFIX=$(cat ${URANDOM} | tr -dc '0-9' | fold -w 256 | head -n 1 | head --bytes 4)
VETH1=ve${VETH1_POSTFIX}
NS0=root
NS1=af_xdp${VETH1_POSTFIX}
MTU=1500

setup_vethPairs() {
	if [[ $verbose -eq 1 ]]; then
	        echo "setting up ${VETH0}: namespace: ${NS0}"
	fi
	ip netns add ${NS1}
	ip link add ${VETH0} numtxqueues 4 numrxqueues 4 type veth peer name ${VETH1} numtxqueues 4 numrxqueues 4
	if [ -f /proc/net/if_inet6 ]; then
		echo 1 > /proc/sys/net/ipv6/conf/${VETH0}/disable_ipv6
	fi
	if [[ $verbose -eq 1 ]]; then
	        echo "setting up ${VETH1}: namespace: ${NS1}"
	fi
	ip link set ${VETH1} netns ${NS1}
	ip netns exec ${NS1} ip link set ${VETH1} mtu ${MTU}
	ip link set ${VETH0} mtu ${MTU}
	ip netns exec ${NS1} ip link set ${VETH1} up
	ip netns exec ${NS1} ip link set dev lo up
	ip link set ${VETH0} up
}

validate_root_exec
validate_veth_support ${VETH0}
validate_ip_utility
setup_vethPairs

retval=$?
if [ $retval -ne 0 ]; then
	test_status $retval "${TEST_NAME}"
	cleanup_exit ${VETH0} ${VETH1} ${NS1}
	exit $retval
fi

echo "${VETH0}:${VETH1},${NS1}" > ${SPECFILE}

validate_veth_spec_file

if [[ $verbose -eq 1 ]]; then
        echo "Spec file created: ${SPECFILE}"
	VERBOSE_ARG="-v"
fi

if [[ $dump_pkts -eq 1 ]]; then
	DUMP_PKTS_ARG="-D"
fi

test_status $retval "${TEST_NAME}"

## START TESTS

statusList=()

TEST_NAME="XSK KSELFTESTS"

execxdpxceiver

retval=$?
test_status $retval "${TEST_NAME}"
statusList+=($retval)

## END TESTS

cleanup_exit ${VETH0} ${VETH1} ${NS1}

for _status in "${statusList[@]}"
do
	if [ $_status -ne 0 ]; then
		test_exit $ksft_fail 0
	fi
done

test_exit $ksft_pass 0
