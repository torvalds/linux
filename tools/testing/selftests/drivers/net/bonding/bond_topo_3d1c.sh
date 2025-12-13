#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Topology for Bond mode 1,5,6 testing
#
#  +-------------------------------------+
#  |                bond0                |
#  |                  +                  |  Server
#  |      eth0        | eth1   eth2      |  192.0.2.1/24
#  |        +-------------------+        |  2001:db8::1/24
#  |        |         |         |        |
#  +-------------------------------------+
#           |         |         |
#  +-------------------------------------+
#  |        |         |         |        |
#  |    +---+---------+---------+---+    |  Gateway
#  |    |            br0            |    |  192.0.2.254/24
#  |    +-------------+-------------+    |  2001:db8::254/24
#  |                  |                  |
#  +-------------------------------------+
#                     |
#  +-------------------------------------+
#  |                  |                  |  Client
#  |                  +                  |  192.0.2.10/24
#  |                eth0                 |  2001:db8::10/24
#  +-------------------------------------+

source bond_topo_2d1c.sh
mac[2]="00:0a:0b:0c:0d:03"

setup_prepare()
{
	gateway_create
	server_create
	client_create

	# Add the extra device as we use 3 down links for bond0
	local i=2
	ip -n ${s_ns} link add eth${i} type veth peer name s${i} netns ${g_ns}
	ip -n "${s_ns}" link set "eth${i}" addr "${mac[$i]}"
	ip -n ${g_ns} link set s${i} up
	ip -n ${g_ns} link set s${i} master br0
	ip -n ${s_ns} link set eth${i} master bond0
	tc -n ${g_ns} qdisc add dev s${i} clsact
}
