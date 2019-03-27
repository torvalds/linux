#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
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
# Script which reads the pc-autoinstall.conf directive, and begins the install
#
# $FreeBSD$

# Source our functions scripts
. ${BACKEND}/functions.sh
. ${BACKEND}/functions-networking.sh
. ${BACKEND}/functions-parse.sh

# Check that the config file exists
if [ ! -e "${1}" ]
then
  echo "ERROR: Install configuration $1 does not exist!"
  exit 1
fi

# Set our config file variable
CONF=${1}
INSTALL_CFG="/tmp/pc-sysinstall.cfg"

# Check if the config file is on disk as well
PCCFG=`grep "pc_config:" ${CONF} | grep -v "^#" | sed "s|pc_config: ||g" | sed "s|pc_config:||g"`
SHUTDOWN_CMD=`grep "shutdown_cmd:" ${CONF} | grep -v "^#" | sed "s|shutdown_cmd: ||g" | sed "s|shutdown_cmd:||g"`
CONFIRM_INS=`grep "confirm_install:" ${CONF} | grep -v "^#" | sed "s|confirm_install: ||g" | sed "s|confirm_install:||g"`

# Check that this isn't a http / ftp file we need to fetch later
echo "${PCCFG}" | grep -q -e "^http" -e "^ftp" 2>/dev/null
if [ $? -ne 0 ]
then
  # Copy over the install cfg file, if not done already
  if [ ! -e "${INSTALL_CFG}" ]
  then
    cp ${PCCFG} ${INSTALL_CFG}
  fi
  # Make sure we have the file which was copied into /tmp previously
  if [ ! -e "${INSTALL_CFG}" ]
  then
    echo "Error: ${INSTALL_CFG} is missing! Exiting in 10 seconds..."
    sleep 10
    exit 150
  fi
else
  # We need to fetch a remote file, check and set any nic options before doing so
  NICCFG=`grep "nic_config:" ${CONF} | grep -v "^#" | sed "s|nic_config: ||g" | sed "s|nic_config:||g"`
  if [ "${NICCFG}" = "dhcp-all" -o "${NICCFG}" = "DHCP-ALL" ]
  then
    # Try to auto-enable dhcp on any nics we find
    enable_auto_dhcp
  else
    echo "Running command \"ifconfig ${NICCFG}\""
    ifconfig ${NICCFG}
    WRKNIC="`echo ${NICCFG} | cut -d ' ' -f 1`"
    NICDNS=`grep "nic_dns:" ${CONF} | grep -v "^#" | sed "s|nic_dns: ||g" | sed "s|nic_dns:||g"`
    NICGATE=`grep "nic_gateway:" ${CONF} | grep -v "^#" | sed "s|nic_gateway: ||g" | sed "s|nic_gateway:||g"`

    echo "nameserver ${NICDNS}" >/etc/resolv.conf

    echo "Running command \"route add default ${NICGATE}\""
    route add default ${NICGATE}
  fi

  get_nic_mac "$WRKNIC"
  nic_mac="${FOUNDMAC}"

  PCCFG=`echo ${PCCFG} | sed "s|%%NIC_MAC%%|${nic_mac}|g"`

  # Now try to fetch the remove file
  echo "Fetching cfg with: \"fetch -o ${INSTALL_CFG} ${PCCFG}\""
  fetch -o "${INSTALL_CFG}" "${PCCFG}"
  if [ $? -ne 0 ]
  then
    echo "ERROR: Failed to fetch ${PCCFG}, install aborted"
    exit 150
  fi

fi

# If we end up with a valid config, lets proccede
if [ -e "${INSTALL_CFG}" ]
then
  
  if [ "${CONFIRM_INS}" != "no" -a "${CONFIRM_INS}" != "NO" ]
  then
    echo "Type in 'install' to begin automated installation. Warning: Data on target disks may be destroyed!"
    read tmp
    case $tmp in
       install|INSTALL) ;;
       *) echo "Install canceled!" ; exit 150 ;;
    esac
  fi

  pc-sysinstall -c ${INSTALL_CFG}
  if [ $? -eq 0 ]
  then
    if [ -n "$SHUTDOWN_CMD" ]
    then
      ${SHUTDOWN_CMD}
    else 
      echo "SUCCESS: Installation finished! Press ENTER to reboot." 
      read tmp
      shutdown -r now
    fi
  else 
    echo "ERROR: Installation failed, press ENTER to drop to shell."
    read tmp
    /bin/csh
  fi
else
  echo "ERROR: Failed to get /tmp/pc-sysinstall.cfg for automated install..."
  exit 150
fi
