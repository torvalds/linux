#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
# Copyright (c) 2011 The FreeBSD Foundation
# All rights reserved.
#
# Portions of this software were developed by Bjoern Zeeb
# under sponsorship from the FreeBSD Foundation.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# Functions which perform our networking setup

# Function which creates a kde4 .desktop file for the PC-BSD net tray
create_desktop_nettray()
{
  NIC="${1}"
  echo "#!/usr/bin/env xdg-open
[Desktop Entry]
Exec=/usr/local/kde4/bin/pc-nettray ${NIC}
Icon=network
StartupNotify=false
Type=Application" > ${FSMNT}/usr/share/skel/.kde4/Autostart/tray-${NIC}.desktop
  chmod 744 ${FSMNT}/usr/share/skel/.kde4/Autostart/tray-${NIC}.desktop

};

# Function which checks is a nic is wifi or not
check_is_wifi()
{
  NIC="$1"
  ifconfig ${NIC} | grep -q "802.11" 2>/dev/null
  if [ $? -eq 0 ]
  then
    return 0
  else 
    return 1
  fi
};

# Function to get the first available wired nic, used for setup
get_first_wired_nic()
{
  rm ${TMPDIR}/.niclist >/dev/null 2>/dev/null
  # start by getting a list of nics on this system
  ${QUERYDIR}/detect-nics.sh > ${TMPDIR}/.niclist
  if [ -e "${TMPDIR}/.niclist" ]
  then
    while read line
    do
      NIC="`echo $line | cut -d ':' -f 1`"
      check_is_wifi ${NIC}
      if [ $? -ne 0 ]
      then
        export VAL="${NIC}"
        return
      fi
    done < ${TMPDIR}/.niclist
  fi

  export VAL=""
  return
};


# Function which simply enables plain dhcp on all detected nics
enable_dhcp_all()
{
  rm ${TMPDIR}/.niclist >/dev/null 2>/dev/null
  # start by getting a list of nics on this system
  ${QUERYDIR}/detect-nics.sh > ${TMPDIR}/.niclist
  if [ -e "${TMPDIR}/.niclist" ]
  then
    echo "# Auto-Enabled NICs from pc-sysinstall" >>${FSMNT}/etc/rc.conf
    WLANCOUNT="0"
    while read line
    do
      NIC="`echo $line | cut -d ':' -f 1`"
      DESC="`echo $line | cut -d ':' -f 2`"
      echo_log "Setting $NIC to DHCP on the system."
      check_is_wifi ${NIC}
      if [ $? -eq 0 ]
      then
        # We have a wifi device, setup a wlan* entry for it
        WLAN="wlan${WLANCOUNT}"
	cat ${FSMNT}/etc/rc.conf | grep -q "wlans_${NIC}="
	if [ $? -ne 0 ] ; then
          echo "wlans_${NIC}=\"${WLAN}\"" >>${FSMNT}/etc/rc.conf
	fi
        echo "ifconfig_${WLAN}=\"DHCP\"" >>${FSMNT}/etc/rc.conf
        CNIC="${WLAN}"
        WLANCOUNT=$((WLANCOUNT+1))
      else
        echo "ifconfig_${NIC}=\"DHCP\"" >>${FSMNT}/etc/rc.conf
        CNIC="${NIC}"
      fi
 
    done < ${TMPDIR}/.niclist 
  fi
};


# Function which detects available nics, and enables dhcp on them
save_auto_dhcp()
{
  enable_dhcp_all
};

# Function which simply enables iPv6 SLAAC on all detected nics
enable_slaac_all()
{
  rm ${TMPDIR}/.niclist >/dev/null 2>/dev/null
  # start by getting a list of nics on this system
  ${QUERYDIR}/detect-nics.sh > ${TMPDIR}/.niclist
  if [ -e "${TMPDIR}/.niclist" ]
  then
    echo "# Auto-Enabled NICs from pc-sysinstall" >>${FSMNT}/etc/rc.conf
    WLANCOUNT="0"
    while read line
    do
      NIC="`echo $line | cut -d ':' -f 1`"
      DESC="`echo $line | cut -d ':' -f 2`"
      echo_log "Setting $NIC to accepting RAs on the system."
      check_is_wifi ${NIC}
      if [ $? -eq 0 ]
      then
        # We have a wifi device, setup a wlan* entry for it
        # Given we cannot have DHCP and SLAAC the same time currently
	# it's save to just duplicate.
        WLAN="wlan${WLANCOUNT}"
	cat ${FSMNT}/etc/rc.conf | grep -q "wlans_${NIC}="
	if [ $? -ne 0 ] ; then
          echo "wlans_${NIC}=\"${WLAN}\"" >>${FSMNT}/etc/rc.conf
	fi
	#echo "ifconfig_${NIC}=\"up\"" >>${FSMNT}/etc/rc.conf
        echo "ifconfig_${WLAN}_ipv6=\"inet6 accept_rtadv\"" >>${FSMNT}/etc/rc.conf
        CNIC="${WLAN}"
        WLANCOUNT=$((WLANCOUNT+1))
      else
	#echo "ifconfig_${NIC}=\"up\"" >>${FSMNT}/etc/rc.conf
        echo "ifconfig_${NIC}_ipv6=\"inet6 accept_rtadv\"" >>${FSMNT}/etc/rc.conf
        CNIC="${NIC}"
      fi
 
    done < ${TMPDIR}/.niclist 
  fi

  # Given we cannot yet rely on RAs to provide DNS information as much
  # as we can in the DHCP world, we should append a given nameserver.
  : > ${FSMNT}/etc/resolv.conf
  get_value_from_cfg netSaveIPv6NameServer
  NAMESERVER="${VAL}"
  if [ -n "${NAMESERVER}" ]
  then
    echo "nameserver ${NAMESERVER}" >>${FSMNT}/etc/resolv.conf
  fi

};


# Function which detects available nics, and enables IPv6 SLAAC on them
save_auto_slaac()
{
  enable_slaac_all
};


# Function which saves a manual nic setup to the installed system
save_manual_nic()
{
  # Get the target nic
  NIC="$1"

  get_value_from_cfg netSaveIP_${NIC}
  NETIP="${VAL}"
 
  if [ "$NETIP" = "DHCP" ]
  then
    echo_log "Setting $NIC to DHCP on the system."
    echo "ifconfig_${NIC}=\"DHCP\"" >>${FSMNT}/etc/rc.conf
    return 0
  fi

  # If we get here, we have a manual setup, lets do so now
  IFARGS=""
  IF6ARGS=""

  # Set the manual IP
  if [ -n "${NETIP}" ]
  then
    IFARGS="inet ${NETIP}"

    # Check if we have a netmask to set
    get_value_from_cfg netSaveMask_${NIC}
    NETMASK="${VAL}"
    if [ -n "${NETMASK}" ]
    then
      IFARGS="${IFARGS} netmask ${NETMASK}"
    fi
  fi

  get_value_from_cfg netSaveIPv6_${NIC}
  NETIP6="${VAL}"
  if [ -n "${NETIP6}" ]
  then
    # Make sure we have one inet6 prefix.
    IF6ARGS=`echo "${NETIP6}" | awk '{ if ("^inet6 ") { print $0; } else
      { printf "inet6 %s", $0; } }'`
  fi

  echo "# Auto-Enabled NICs from pc-sysinstall" >>${FSMNT}/etc/rc.conf
  if [ -n "${IFARGS}" ]
  then
    echo "ifconfig_${NIC}=\"${IFARGS}\"" >>${FSMNT}/etc/rc.conf
  fi
  if [ -n "${IF6ARGS}" ]
  then
    echo "ifconfig_${NIC}_ipv6=\"${IF6ARGS}\"" >>${FSMNT}/etc/rc.conf
  fi

};

# Function which saves a manual gateway router setup to the installed system
save_manual_router()
{

  # Check if we have a default router to set
  get_value_from_cfg netSaveDefaultRouter
  NETROUTE="${VAL}"
  if [ -n "${NETROUTE}" ]
  then
    echo "defaultrouter=\"${NETROUTE}\"" >>${FSMNT}/etc/rc.conf
  fi
  get_value_from_cfg netSaveIPv6DefaultRouter
  NETROUTE="${VAL}"
  if [ -n "${NETROUTE}" ]
  then
    echo "ipv6_defaultrouter=\"${NETROUTE}\"" >>${FSMNT}/etc/rc.conf
  fi

};

save_manual_nameserver()
{
  # Check if we have a nameserver to enable
  : > ${FSMNT}/etc/resolv.conf
  get_value_from_cfg_with_spaces netSaveNameServer
  NAMESERVERLIST="${VAL}"
  if [ ! -z "${NAMESERVERLIST}" ]
  then
    for NAMESERVER in ${NAMESERVERLIST}
    do
      echo "nameserver ${NAMESERVER}" >>${FSMNT}/etc/resolv.conf
    done
  fi

  get_value_from_cfg_with_spaces netSaveIPv6NameServer
  NAMESERVERLIST="${VAL}"
  if [ ! -z "${NAMESERVERLIST}" ]
  then
    for NAMESERVER in ${NAMESERVERLIST}
    do
      echo "nameserver ${NAMESERVER}" >>${FSMNT}/etc/resolv.conf
    done
  fi

};

# Function which determines if a nic is active / up
is_nic_active()
{
  ifconfig ${1} | grep -q "status: active" 2>/dev/null
  if [ $? -eq 0 ] ; then
    return 0
  else
    return 1
  fi
};


# Function which detects available nics, and runs DHCP on them until
# a success is found
enable_auto_dhcp()
{
  # start by getting a list of nics on this system
  ${QUERYDIR}/detect-nics.sh > ${TMPDIR}/.niclist
  while read line
  do
    NIC="`echo $line | cut -d ':' -f 1`"
    DESC="`echo $line | cut -d ':' -f 2`"

    is_nic_active "${NIC}"
    if [ $? -eq 0 ] ; then
      echo_log "Trying DHCP on $NIC $DESC"
      dhclient ${NIC} >/dev/null 2>/dev/null
      if [ $? -eq 0 ] ; then
        # Got a valid DHCP IP, we can return now
	    export WRKNIC="$NIC"
   	    return 0
	  fi
    fi
  done < ${TMPDIR}/.niclist 

};

# Function which detects available nics, and runs rtsol on them.
enable_auto_slaac()
{

  # start by getting a list of nics on this system
  ${QUERYDIR}/detect-nics.sh > ${TMPDIR}/.niclist
  ALLNICS=""
  while read line
  do
    NIC="`echo $line | cut -d ':' -f 1`"
    DESC="`echo $line | cut -d ':' -f 2`"

    is_nic_active "${NIC}"
    if [ $? -eq 0 ] ; then
      echo_log "Will try IPv6 SLAAC on $NIC $DESC"
      ifconfig ${NIC} inet6 -ifdisabled accept_rtadv up
      ALLNICS="${ALLNICS} ${NIC}"
    fi
  done < ${TMPDIR}/.niclist 

  # XXX once we support it in-tree call /sbin/resovconf here.
  echo_log "Running rtsol on ${ALLNICS}"
  rtsol -F ${ALLNICS} >/dev/null 2>/dev/null
}

# Get the mac address of a target NIC
get_nic_mac()
{
  FOUNDMAC="`ifconfig ${1} | grep 'ether' | tr -d '\t' | cut -d ' ' -f 2`"
  export FOUNDMAC
}

# Function which performs the manual setup of a target nic in the cfg
enable_manual_nic()
{
  # Get the target nic
  NIC="$1"

  # Check that this NIC exists
  rc_halt "ifconfig ${NIC}"

  get_value_from_cfg netIP
  NETIP="${VAL}"
  
  if [ "$NETIP" = "DHCP" ]
  then
    echo_log "Enabling DHCP on $NIC"
    rc_halt "dhclient ${NIC}"
    return 0
  fi

  # If we get here, we have a manual setup, lets do so now

  # IPv4:

  # Set the manual IP
  if [ -n "${NETIP}" ]
  then
    # Check if we have a netmask to set
    get_value_from_cfg netMask
    NETMASK="${VAL}"
    if [ -n "${NETMASK}" ]
    then
      rc_halt "ifconfig inet ${NIC} netmask ${NETMASK}"
    else
      rc_halt "ifconfig inet ${NIC} ${NETIP}"
    fi
  fi

  # Check if we have a default router to set
  get_value_from_cfg netDefaultRouter
  NETROUTE="${VAL}"
  if [ -n "${NETROUTE}" ]
  then
    rc_halt "route add -inet default ${NETROUTE}"
  fi

  # IPv6:

  # Set static IPv6 address
  get_value_from_cfg netIPv6
  NETIP="${VAL}"
  if [ -n ${NETIP} ]
  then
      rc_halt "ifconfig inet6 ${NIC} ${NETIP} -ifdisabled up"
  fi

  # Default router
  get_value_from_cfg netIPv6DefaultRouter
  NETROUTE="${VAL}"
  if [ -n "${NETROUTE}" ]
  then
    rc_halt "route add -inet6 default ${NETROUTE}"
  fi

  # Check if we have a nameserver to enable
  : >/etc/resolv.conf
  get_value_from_cfg netNameServer
  NAMESERVER="${VAL}"
  if [ -n "${NAMESERVER}" ]
  then
    echo "nameserver ${NAMESERVER}" >>/etc/resolv.conf
  fi
  get_value_from_cfg netIPv6NameServer
  NAMESERVER="${VAL}"
  if [ -n "${NAMESERVER}" ]
  then
    echo "nameserver ${NAMESERVER}" >>/etc/resolv.conf
  fi

};


# Function which parses the cfg and enables networking per specified
start_networking()
{
  # Check if we have any networking requested
  get_value_from_cfg netDev
  if [ -z "${VAL}" ]
  then
    return 0
  fi

  NETDEV="${VAL}"
  if [ "$NETDEV" = "AUTO-DHCP" ]
  then
    enable_auto_dhcp
  elif [ "$NETDEV" = "IPv6-SLAAC" ]
  then
    enable_auto_slaac
  elif [ "$NETDEV" = "AUTO-DHCP-SLAAC" ]
  then
    enable_auto_dhcp
    enable_auto_slaac
  else
    enable_manual_nic ${NETDEV}
  fi

};


# Function which checks the cfg and enables the specified networking on
# the installed system
save_networking_install()
{

  # Check if we have any networking requested to save
  get_value_from_cfg_with_spaces netSaveDev
  if [ -z "${VAL}" ]
  then
    return 0
  fi

  NETDEVLIST="${VAL}"
  if [ "$NETDEVLIST" = "AUTO-DHCP" ]
  then
    save_auto_dhcp
  elif [ "$NETDEVLIST" = "IPv6-SLAAC" ]
  then
    save_auto_slaac
  elif [ "$NETDEVLIST" = "AUTO-DHCP-SLAAC" ]
  then
    save_auto_dhcp
    save_auto_slaac
  else
    for NETDEV in ${NETDEVLIST}
    do
      save_manual_nic ${NETDEV}
    done
    save_manual_router
    save_manual_nameserver
  fi

};
