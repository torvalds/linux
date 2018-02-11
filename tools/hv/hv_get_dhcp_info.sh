#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This example script retrieves the DHCP state of a given interface.
# In the interest of keeping the KVP daemon code free of distro specific
# information; the kvp daemon code invokes this external script to gather
# DHCP setting for the specific interface.
#
# Input: Name of the interface
#
# Output: The script prints the string "Enabled" to stdout to indicate
#	that DHCP is enabled on the interface. If DHCP is not enabled,
#	the script prints the string "Disabled" to stdout.
#
# Each Distro is expected to implement this script in a distro specific
# fashion. For instance on Distros that ship with Network Manager enabled,
# this script can be based on the Network Manager APIs for retrieving DHCP
# information.

if_file="/etc/sysconfig/network-scripts/ifcfg-"$1

dhcp=$(grep "dhcp" $if_file 2>/dev/null)

if [ "$dhcp" != "" ];
then
echo "Enabled"
else
echo "Disabled"
fi
