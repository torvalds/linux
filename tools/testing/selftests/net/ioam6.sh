#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Author: Justin Iurman <justin.iurman@uliege.be>
#
# This test evaluates the IOAM insertion for IPv6 by checking the IOAM data
# integrity on the receiver.
#
# The topology is formed by 3 nodes: Alpha (sender), Beta (router in-between)
# and Gamma (receiver). An IOAM domain is configured from Alpha to Gamma only,
# which means not on the reverse path. When Gamma is the destination, Alpha
# adds an IOAM option (Pre-allocated Trace) inside a Hop-by-hop and fills the
# trace with its own IOAM data. Beta and Gamma also fill the trace. The IOAM
# data integrity is checked on Gamma, by comparing with the pre-defined IOAM
# configuration (see below).
#
#     +-------------------+            +-------------------+
#     |                   |            |                   |
#     |    alpha netns    |            |    gamma netns    |
#     |                   |            |                   |
#     |  +-------------+  |            |  +-------------+  |
#     |  |    veth0    |  |            |  |    veth0    |  |
#     |  |  db01::2/64 |  |            |  |  db02::2/64 |  |
#     |  +-------------+  |            |  +-------------+  |
#     |         .         |            |         .         |
#     +-------------------+            +-------------------+
#               .                                .
#               .                                .
#               .                                .
#     +----------------------------------------------------+
#     |         .                                .         |
#     |  +-------------+                  +-------------+  |
#     |  |    veth0    |                  |    veth1    |  |
#     |  |  db01::1/64 | ................ |  db02::1/64 |  |
#     |  +-------------+                  +-------------+  |
#     |                                                    |
#     |                      beta netns                    |
#     |                                                    |
#     +--------------------------+-------------------------+
#
#
# ~~~~~~~~~~~~~~~~~~~~~~
# | IOAM configuration |
# ~~~~~~~~~~~~~~~~~~~~~~
#
# Alpha
# +-----------------------------------------------------------+
# | Type                | Value                               |
# +-----------------------------------------------------------+
# | Node ID             | 1                                   |
# +-----------------------------------------------------------+
# | Node Wide ID        | 11111111                            |
# +-----------------------------------------------------------+
# | Ingress ID          | 0xffff (default value)              |
# +-----------------------------------------------------------+
# | Ingress Wide ID     | 0xffffffff (default value)          |
# +-----------------------------------------------------------+
# | Egress ID           | 101                                 |
# +-----------------------------------------------------------+
# | Egress Wide ID      | 101101                              |
# +-----------------------------------------------------------+
# | Namespace Data      | 0xdeadbee0                          |
# +-----------------------------------------------------------+
# | Namespace Wide Data | 0xcafec0caf00dc0de                  |
# +-----------------------------------------------------------+
# | Schema ID           | 777                                 |
# +-----------------------------------------------------------+
# | Schema Data         | something that will be 4n-aligned   |
# +-----------------------------------------------------------+
#
# Note: When Gamma is the destination, Alpha adds an IOAM Pre-allocated Trace
#       option inside a Hop-by-hop, where 164 bytes are pre-allocated for the
#       trace, with 123 as the IOAM-Namespace and with 0xfff00200 as the trace
#       type (= all available options at this time). As a result, and based on
#       IOAM configurations here, only both Alpha and Beta should be capable of
#       inserting their IOAM data while Gamma won't have enough space and will
#       set the overflow bit.
#
# Beta
# +-----------------------------------------------------------+
# | Type                | Value                               |
# +-----------------------------------------------------------+
# | Node ID             | 2                                   |
# +-----------------------------------------------------------+
# | Node Wide ID        | 22222222                            |
# +-----------------------------------------------------------+
# | Ingress ID          | 201                                 |
# +-----------------------------------------------------------+
# | Ingress Wide ID     | 201201                              |
# +-----------------------------------------------------------+
# | Egress ID           | 202                                 |
# +-----------------------------------------------------------+
# | Egress Wide ID      | 202202                              |
# +-----------------------------------------------------------+
# | Namespace Data      | 0xdeadbee1                          |
# +-----------------------------------------------------------+
# | Namespace Wide Data | 0xcafec0caf11dc0de                  |
# +-----------------------------------------------------------+
# | Schema ID           | 0xffffff (= None)                   |
# +-----------------------------------------------------------+
# | Schema Data         |                                     |
# +-----------------------------------------------------------+
#
# Gamma
# +-----------------------------------------------------------+
# | Type                | Value                               |
# +-----------------------------------------------------------+
# | Node ID             | 3                                   |
# +-----------------------------------------------------------+
# | Node Wide ID        | 33333333                            |
# +-----------------------------------------------------------+
# | Ingress ID          | 301                                 |
# +-----------------------------------------------------------+
# | Ingress Wide ID     | 301301                              |
# +-----------------------------------------------------------+
# | Egress ID           | 0xffff (default value)              |
# +-----------------------------------------------------------+
# | Egress Wide ID      | 0xffffffff (default value)          |
# +-----------------------------------------------------------+
# | Namespace Data      | 0xdeadbee2                          |
# +-----------------------------------------------------------+
# | Namespace Wide Data | 0xcafec0caf22dc0de                  |
# +-----------------------------------------------------------+
# | Schema ID           | 0xffffff (= None)                   |
# +-----------------------------------------------------------+
# | Schema Data         |                                     |
# +-----------------------------------------------------------+

#===============================================================================
#
# WARNING:
# Do NOT modify the following configuration unless you know what you're doing.
#
IOAM_NAMESPACE=123
IOAM_TRACE_TYPE=0xfff00200
IOAM_PREALLOC_DATA_SIZE=164

ALPHA=(
	1					# ID
	11111111				# Wide ID
	0xffff					# Ingress ID
	0xffffffff				# Ingress Wide ID
	101					# Egress ID
	101101					# Egress Wide ID
	0xdeadbee0				# Namespace Data
	0xcafec0caf00dc0de			# Namespace Wide Data
	777					# Schema ID (0xffffff = None)
	"something that will be 4n-aligned"	# Schema Data
)

BETA=(
	2
	22222222
	201
	201201
	202
	202202
	0xdeadbee1
	0xcafec0caf11dc0de
	0xffffff
	""
)

GAMMA=(
	3
	33333333
	301
	301301
	0xffff
	0xffffffff
	0xdeadbee2
	0xcafec0caf22dc0de
	0xffffff
	""
)
#===============================================================================

if [ "$(id -u)" -ne 0 ]; then
  echo "SKIP: Need root privileges"
  exit 1
fi

if [ ! -x "$(command -v ip)" ]; then
  echo "SKIP: Could not run test without ip tool"
  exit 1
fi

ip ioam &>/dev/null
if [ $? = 1 ]; then
  echo "SKIP: ip tool must include IOAM"
  exit 1
fi

if [ ! -e /proc/sys/net/ipv6/ioam6_id ]; then
  echo "SKIP: ioam6 sysctls do not exist"
  exit 1
fi

cleanup()
{
  ip link del ioam-veth-alpha 2>/dev/null || true
  ip link del ioam-veth-gamma 2>/dev/null || true

  ip netns del ioam-node-alpha || true
  ip netns del ioam-node-beta || true
  ip netns del ioam-node-gamma || true
}

setup()
{
  ip netns add ioam-node-alpha
  ip netns add ioam-node-beta
  ip netns add ioam-node-gamma

  ip link add name ioam-veth-alpha type veth peer name ioam-veth-betaL
  ip link add name ioam-veth-betaR type veth peer name ioam-veth-gamma

  ip link set ioam-veth-alpha netns ioam-node-alpha
  ip link set ioam-veth-betaL netns ioam-node-beta
  ip link set ioam-veth-betaR netns ioam-node-beta
  ip link set ioam-veth-gamma netns ioam-node-gamma

  ip -netns ioam-node-alpha link set ioam-veth-alpha name veth0
  ip -netns ioam-node-beta link set ioam-veth-betaL name veth0
  ip -netns ioam-node-beta link set ioam-veth-betaR name veth1
  ip -netns ioam-node-gamma link set ioam-veth-gamma name veth0

  ip -netns ioam-node-alpha addr add db01::2/64 dev veth0
  ip -netns ioam-node-alpha link set veth0 up
  ip -netns ioam-node-alpha link set lo up
  ip -netns ioam-node-alpha route add default via db01::1

  ip -netns ioam-node-beta addr add db01::1/64 dev veth0
  ip -netns ioam-node-beta addr add db02::1/64 dev veth1
  ip -netns ioam-node-beta link set veth0 up
  ip -netns ioam-node-beta link set veth1 up
  ip -netns ioam-node-beta link set lo up

  ip -netns ioam-node-gamma addr add db02::2/64 dev veth0
  ip -netns ioam-node-gamma link set veth0 up
  ip -netns ioam-node-gamma link set lo up
  ip -netns ioam-node-gamma route add default via db02::1

  # - IOAM config -
  ip netns exec ioam-node-alpha sysctl -wq net.ipv6.ioam6_id=${ALPHA[0]}
  ip netns exec ioam-node-alpha sysctl -wq net.ipv6.ioam6_id_wide=${ALPHA[1]}
  ip netns exec ioam-node-alpha sysctl -wq net.ipv6.conf.veth0.ioam6_id=${ALPHA[4]}
  ip netns exec ioam-node-alpha sysctl -wq net.ipv6.conf.veth0.ioam6_id_wide=${ALPHA[5]}
  ip -netns ioam-node-alpha ioam namespace add ${IOAM_NAMESPACE} data ${ALPHA[6]} wide ${ALPHA[7]}
  ip -netns ioam-node-alpha ioam schema add ${ALPHA[8]} "${ALPHA[9]}"
  ip -netns ioam-node-alpha ioam namespace set ${IOAM_NAMESPACE} schema ${ALPHA[8]}
  ip -netns ioam-node-alpha route add db02::/64 encap ioam6 trace type ${IOAM_TRACE_TYPE:0:-2} ns ${IOAM_NAMESPACE} size ${IOAM_PREALLOC_DATA_SIZE} via db01::1 dev veth0

  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.all.forwarding=1
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.ioam6_id=${BETA[0]}
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.ioam6_id_wide=${BETA[1]}
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=1
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.veth0.ioam6_id=${BETA[2]}
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.veth0.ioam6_id_wide=${BETA[3]}
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.veth1.ioam6_id=${BETA[4]}
  ip netns exec ioam-node-beta sysctl -wq net.ipv6.conf.veth1.ioam6_id_wide=${BETA[5]}
  ip -netns ioam-node-beta ioam namespace add ${IOAM_NAMESPACE} data ${BETA[6]} wide ${BETA[7]}

  ip netns exec ioam-node-gamma sysctl -wq net.ipv6.ioam6_id=${GAMMA[0]}
  ip netns exec ioam-node-gamma sysctl -wq net.ipv6.ioam6_id_wide=${GAMMA[1]}
  ip netns exec ioam-node-gamma sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=1
  ip netns exec ioam-node-gamma sysctl -wq net.ipv6.conf.veth0.ioam6_id=${GAMMA[2]}
  ip netns exec ioam-node-gamma sysctl -wq net.ipv6.conf.veth0.ioam6_id_wide=${GAMMA[3]}
  ip -netns ioam-node-gamma ioam namespace add ${IOAM_NAMESPACE} data ${GAMMA[6]} wide ${GAMMA[7]}
}

run()
{
  echo -n "IOAM test... "

  ip netns exec ioam-node-alpha ping6 -c 5 -W 1 db02::2 &>/dev/null
  if [ $? != 0 ]; then
    echo "FAILED"
    cleanup &>/dev/null
    exit 0
  fi

  ip netns exec ioam-node-gamma ./ioam6_parser veth0 2 ${IOAM_NAMESPACE} ${IOAM_TRACE_TYPE} 64 ${ALPHA[0]} ${ALPHA[1]} ${ALPHA[2]} ${ALPHA[3]} ${ALPHA[4]} ${ALPHA[5]} ${ALPHA[6]} ${ALPHA[7]} ${ALPHA[8]} "${ALPHA[9]}" 63 ${BETA[0]} ${BETA[1]} ${BETA[2]} ${BETA[3]} ${BETA[4]} ${BETA[5]} ${BETA[6]} ${BETA[7]} ${BETA[8]} &

  local spid=$!
  sleep 0.1

  ip netns exec ioam-node-alpha ping6 -c 5 -W 1 db02::2 &>/dev/null

  wait $spid
  [ $? = 0 ] && echo "PASSED" || echo "FAILED"
}

cleanup &>/dev/null
setup
run
cleanup &>/dev/null
