#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by Pawel Jakub Dawidek under sponsorship from
# the FreeBSD Foundation.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# Shared IP address, unused for now.
addr="10.99.0.3"
# Password for UCARP communication.
pass="password"
# First node IP and interface for UCARP communication.
nodea_srcip="10.99.0.1"
nodea_ifnet="bge0"
# Second node IP and interface for UCARP communication.
nodeb_srcip="10.99.0.2"
nodeb_ifnet="em3"

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

vhid="1"
upscript="/root/hast/sbin/hastd/vip-up.sh"
downscript="/root/hast/sbin/hastd/vip-down.sh"

ifconfig "${nodea_ifnet}" 2>/dev/null | grep -q "inet ${nodea_srcip} "
if [ $? -eq 0 ]; then
	srcip="${nodea_srcip}"
	ifnet="${nodea_ifnet}"
	node="node A"
fi
ifconfig "${nodeb_ifnet}" 2>/dev/null | grep -q "inet ${nodeb_srcip} "
if [ $? -eq 0 ]; then
	if [ -n "${srcip}" -o -n "${ifnet}" ]; then
		echo "Unable to determine which node is this (both match)." >/dev/stderr
		exit 1
	fi
	srcip="${nodeb_srcip}"
	ifnet="${nodeb_ifnet}"
	node="node B"
fi
if [ -z "${srcip}" -o -z "${ifnet}" ]; then
	echo "Unable to determine which node is this (none match)." >/dev/stderr
	exit 1
fi
ucarp -i ${ifnet} -s ${srcip} -v ${vhid} -a ${addr} -p ${pass} -u "${upscript}" -d "${downscript}"
