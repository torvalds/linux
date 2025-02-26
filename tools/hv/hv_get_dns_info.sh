#!/bin/sh

# This example script parses /etc/resolv.conf to retrive DNS information.
# In the interest of keeping the KVP daemon code free of distro specific
# information; the kvp daemon code invokes this external script to gather
# DNS information.
# This script is expected to print the nameserver values to stdout.
# Each Distro is expected to implement this script in a distro specific
# fashion. For instance on Distros that ship with Network Manager enabled,
# this script can be based on the Network Manager APIs for retrieving DNS
# entries.

exec awk '/^nameserver/ { print $2 }' /etc/resolv.conf 2>/dev/null
