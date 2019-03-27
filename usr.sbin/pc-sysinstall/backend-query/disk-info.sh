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
# $FreeBSD$

# Query a disk for partitions and display them
#############################################################################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/backend/functions-disk.sh

DISK="${1}"

[ -z "${DISK}" ] && { echo 'Error: No disk specified!'; exit 1; }
[ ! -e "/dev/${DISK}" ] && \
	{ echo "Error: Disk /dev/${DISK} does not exist!"; exit 1; }

get_disk_cyl "${DISK}"
CYLS="${VAL}"

get_disk_heads "${DISK}"
HEADS="${VAL}"

get_disk_sectors "${DISK}"
SECS="${VAL}"

# Now get the disks size in MB
KB="`diskinfo -v ${1} | grep 'bytes' | cut -d '#' -f 1 | tr -s '\t' ' ' | tr -d ' '`"
MB=$(convert_byte_to_megabyte ${KB})

# Now get the Controller Type
CTYPE="`dmesg | grep "^${1}:" | grep "B <" | cut -d '>' -f 2 | cut -d ' ' -f 3-10`"

echo "cylinders=${CYLS}"
echo "heads=${HEADS}"
echo "sectors=${SECS}"
echo "size=${MB}"
echo "type=${CTYPE}"
