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
device="/dev/hast/${resource}"

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

# If there is secondary worker process, it means that remote primary process is
# still running. We have to wait for it to terminate.
for i in `jot 30`; do
	pgrep -f "hastd: ${resource} \(secondary\)" >/dev/null 2>&1 || break
	sleep 1
done
if pgrep -f "hastd: ${resource} \(secondary\)" >/dev/null 2>&1; then
	logger -p local0.error -t hast "Secondary process for resource ${resource} is still running after 30 seconds."
	exit 1
fi
logger -p local0.debug -t hast "Secondary process in not running."

# Change role to primary for our resource.
out=`hastctl role primary "${resource}" 2>&1`
if [ $? -ne 0 ]; then
	logger -p local0.error -t hast "Unable to change to role to primary for resource ${resource}: ${out}."
	exit 1
fi
# Wait few seconds for provider to appear.
for i in `jot 50`; do
	[ -c "${device}" ] && break
	sleep 0.1
done
if [ ! -c "${device}" ]; then
	logger -p local0.error -t hast "Device ${device} didn't appear."
	exit 1
fi
logger -p local0.debug -t hast "Role for resource ${resource} changed to primary."

case "${fstype}" in
UFS)
	# Check the file system.
	fsck -y -t ufs "${device}" >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		logger -p local0.error -t hast "File system check for resource ${resource} failed."
		exit 1
	fi
	logger -p local0.debug -t hast "File system check for resource ${resource} finished."
	# Mount the file system.
	out=`mount -t ufs "${device}" "${mountpoint}" 2>&1`
	if [ $? -ne 0 ]; then
		logger -p local0.error -t hast "File system mount for resource ${resource} failed: ${out}."
		exit 1
	fi
	logger -p local0.debug -t hast "File system for resource ${resource} mounted."
	;;
ZFS)
	# Import ZFS pool. Do it forcibly as it remembers hostid of
	# the other cluster node.
	out=`zpool import -f "${pool}" 2>&1`
	if [ $? -ne 0 ]; then
		logger -p local0.error -t hast "ZFS pool import for resource ${resource} failed: ${out}."
		exit 1
	fi
	logger -p local0.debug -t hast "ZFS pool for resource ${resource} imported."
	;;
esac

logger -p local0.info -t hast "Successfully switched to primary for resource ${resource}."

exit 0
