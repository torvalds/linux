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

# Resource name as defined in /etc/hast.conf.
resource="test"
# Supported file system types: UFS, ZFS
fstype="UFS"
# ZFS pool name. Required only when fstype == ZFS.
pool="test"
# File system mount point. Required only when fstype == UFS.
mountpoint="/mnt/test"
# Name of HAST provider as defined in /etc/hast.conf.
# Required only when fstype == UFS.
device="/dev/hast/${resource}"

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

# KIll UP script if it still runs in the background.
sig="TERM"
for i in `jot 30`; do
	pgid=`pgrep -f ucarp_up.sh | head -1`
	[ -n "${pgid}" ] || break
	kill -${sig} -- -${pgid}
	sig="KILL"
	sleep 1
done
if [ -n "${pgid}" ]; then
	logger -p local0.error -t hast "UCARP UP process for resource ${resource} is still running after 30 seconds."
	exit 1
fi
logger -p local0.debug -t hast "UCARP UP is not running."

case "${fstype}" in
UFS)
	mount | egrep -q "^${device} on "
	if [ $? -eq 0 ]; then
		# Forcibly unmount file system.
		out=`umount -f "${mountpoint}" 2>&1`
		if [ $? -ne 0 ]; then
			logger -p local0.error -t hast "Unable to unmount file system for resource ${resource}: ${out}."
			exit 1
		fi
		logger -p local0.debug -t hast "File system for resource ${resource} unmounted."
	fi
	;;
ZFS)
	zpool list | egrep -q "^${pool} "
	if [ $? -eq 0 ]; then
		# Forcibly export file pool.
		out=`zpool export -f "${pool}" 2>&1`
		if [ $? -ne 0 ]; then
			logger -p local0.error -t hast "Unable to export pool for resource ${resource}: ${out}."
			exit 1
		fi
		logger -p local0.debug -t hast "ZFS pool for resource ${resource} exported."
	fi
	;;
esac

# Change role to secondary for our resource.
out=`hastctl role secondary "${resource}" 2>&1`
if [ $? -ne 0 ]; then
	logger -p local0.error -t hast "Unable to change to role to secondary for resource ${resource}: ${out}."
	exit 1
fi
logger -p local0.debug -t hast "Role for resource ${resource} changed to secondary."

logger -p local0.info -t hast "Successfully switched to secondary for resource ${resource}."

exit 0
