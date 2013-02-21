#!/bin/bash

# This example script activates an interface based on the specified
# configuration.
#
# In the interest of keeping the KVP daemon code free of distro specific
# information; the kvp daemon code invokes this external script to configure
# the interface.
#
# The only argument to this script is the configuration file that is to
# be used to configure the interface.
#
# Each Distro is expected to implement this script in a distro specific
# fashion. For instance on Distros that ship with Network Manager enabled,
# this script can be based on the Network Manager APIs for configuring the
# interface.
#
# This example script is based on a RHEL environment.
#
# Here is the format of the ip configuration file:
#
# HWADDR=macaddr
# DEVICE=interface name
# BOOTPROTO=<protocol> (where <protocol> is "dhcp" if DHCP is configured
#                       or "none" if no boot-time protocol should be used)
#
# IPADDR0=ipaddr1
# IPADDR1=ipaddr2
# IPADDRx=ipaddry (where y = x + 1)
#
# NETMASK0=netmask1
# NETMASKx=netmasky (where y = x + 1)
#
# GATEWAY=ipaddr1
# GATEWAYx=ipaddry (where y = x + 1)
#
# DNSx=ipaddrx (where first DNS address is tagged as DNS1 etc)
#
# IPV6 addresses will be tagged as IPV6ADDR, IPV6 gateway will be
# tagged as IPV6_DEFAULTGW and IPV6 NETMASK will be tagged as
# IPV6NETMASK.
#
# The host can specify multiple ipv4 and ipv6 addresses to be
# configured for the interface. Furthermore, the configuration
# needs to be persistent. A subsequent GET call on the interface
# is expected to return the configuration that is set via the SET
# call.
#



echo "IPV6INIT=yes" >> $1
echo "NM_CONTROLLED=no" >> $1
echo "PEERDNS=yes" >> $1
echo "ONBOOT=yes" >> $1


cp $1 /etc/sysconfig/network-scripts/


interface=$(echo $1 | awk -F - '{ print $2 }')

/sbin/ifdown $interface 2>/dev/null
/sbin/ifup $interface 2>/dev/null
