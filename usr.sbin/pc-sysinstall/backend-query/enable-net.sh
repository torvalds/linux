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

# Script which enables networking with specified options
###########################################################################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/conf/pc-sysinstall.conf
. ${BACKEND}/functions-networking.sh
. ${BACKEND}/functions-parse.sh


NIC="$1"
IP="$2"
NETMASK="$3"
DNS="$4"
GATEWAY="$5"
MIRRORFETCH="$6"
IPV6="$7"
IPV6GATE="$8"
IPV6DNS="$9"

if [ -z "${NIC}" ]
then
  echo "ERROR: Usage enable-net <nic> <ip> <netmask> <dns> <gateway> <ipv6> " \
	"<ipv6gateway> <ipv6dns>"
  exit 150
fi

if [ "$NIC" = "AUTO-DHCP" ]
then
  enable_auto_dhcp
elif [ "$NIC" = "IPv6-SLAAC" ]
then
  enable_auto_slaac
  # In addition, if static values were defined, add them as well.
  # We might not get DNS information from RAs, for example.
  if [ -n "${IPV6}" ]; then
    VAL=""
    get_first_wired_nic
    if [ -n "${VAL}" ]; then
      ifconfig ${VAL} inet6 ${IPV6} alias
    fi
  fi
  # Append only here.
  if [ -n "${IPV6DNS}" ]; then
    echo "nameserver ${IPV6DNS}" >>/etc/resolv.conf
  fi
  # Do not 
  if [ -n "${IPV6GATE}" ]; then
    # Check if we have a default route already to not overwrite.
    if ! route -n get -inet6 default > /dev/null 2>&1 ; then
      route add -inet6 default ${IPV6GATE}
    fi
  fi
else
  echo "Enabling NIC: $NIC"
  if [ -n "${IP}" ]; then
    ifconfig ${NIC} inet ${IP} ${NETMASK}
  fi
  if [ -n "${IPV6}" ]; then
    ifconfig ${NIC} inet6 ${IPV6} alias
  fi

  # Keep default from IPv4-only support times and clear the resolv.conf file.
  : > /etc/resolv.conf
  if [ -n "${DNS}" ]; then
    echo "nameserver ${DNS}" >>/etc/resolv.conf
  fi
  if [ -n "${IPV6DNS}" ]; then
    echo "nameserver ${IPV6DNS}" >>/etc/resolv.conf
  fi

  if [ -n "${GATE}" ]; then
    route add -inet default ${GATE}
  fi
  if [ -n "${IPV6GATE}" ]; then
    route add -inet6 default ${IPV6GATE}
  fi
fi

case ${MIRRORFETCH} in
  ON|on|yes|YES) fetch -o /tmp/mirrors-list.txt ${MIRRORLIST} >/dev/null 2>/dev/null;;
  *) ;;
esac
