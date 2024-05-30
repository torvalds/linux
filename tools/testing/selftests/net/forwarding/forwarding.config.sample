#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

##############################################################################
# Topology description. p1 looped back to p2, p3 to p4 and so on.

NETIFS=(
	[p1]=veth0
	[p2]=veth1
	[p3]=veth2
	[p4]=veth3
	[p5]=veth4
	[p6]=veth5
	[p7]=veth6
	[p8]=veth7
	[p9]=veth8
	[p10]=veth9
)

# Port that does not have a cable connected.
NETIF_NO_CABLE=eth8

##############################################################################
# In addition to the topology-related variables, it is also possible to override
# in this file other variables that net/lib.sh, net/forwarding/lib.sh or other
# libraries or selftests use. E.g.:

PING6=ping6
MZ=mausezahn
WAIT_TIME=5
