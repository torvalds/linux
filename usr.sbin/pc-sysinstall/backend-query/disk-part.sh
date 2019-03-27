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
#############################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/backend/functions-disk.sh

if [ -z "${1}" ]
then
  echo "Error: No disk specified!"
  exit 1
fi

if [ ! -e "/dev/${1}" ]
then
  echo "Error: Disk /dev/${1} does not exist!"
  exit 1
fi

DISK="${1}"

# Now get the disks size in MB
KB="`diskinfo -v ${1} | grep 'bytes' | cut -d '#' -f 1 | tr -s '\t' ' ' | tr -d ' '`"
MB=$(convert_byte_to_megabyte ${KB})
TOTALSIZE="$MB"
TOTALB="`diskinfo -v ${1} | grep 'in sectors' | tr -s '\t' ' ' | cut -d ' ' -f 2`"

gpart show ${1} >/dev/null 2>/dev/null
if [ "$?" != "0" ] ; then
  # No partitions on this disk, display entire disk size and exit
  echo "${1}-freemb: ${TOTALSIZE}"
  echo "${1}-freeblocks: ${TOTALB}"
  exit
fi

# Display if this is GPT or MBR formatted
TYPE=`gpart show ${1} | awk '/^=>/ { printf("%s",$5); }'`
echo "${1}-format: $TYPE"

# Set some search flags
PART="0"
EXTENDED="0"
START="0"
SIZEB="0"

# Get a listing of partitions on this disk
get_disk_partitions "${DISK}"
PARTS="${VAL}"
for curpart in $PARTS
do

  # First get the sysid / label for this partition
  if [ "$TYPE" = "MBR" ] ; then
    get_partition_sysid_mbr "${DISK}" "${curpart}"
    echo "${curpart}-sysid: ${VAL}"
    get_partition_label_mbr "${DISK}" "${curpart}"
    echo "${curpart}-label: ${VAL}"
  else
    get_partition_label_gpt "${DISK}" "${curpart}"
    echo "${curpart}-sysid: ${VAL}"
    echo "${curpart}-label: ${VAL}"
  fi

  # Now get the startblock, blocksize and MB size of this partition

  get_partition_startblock "${DISK}" "${curpart}"
  START="${VAL}"
  echo "${curpart}-blockstart: ${START}"

  get_partition_blocksize "${DISK}" "${curpart}"
  SIZEB="${VAL}"
  echo "${curpart}-blocksize: ${SIZEB}"

  SIZEMB=$(convert_blocks_to_megabyte ${SIZEB})
  echo "${curpart}-sizemb: ${SIZEMB}"

done


# Now calculate any free space
LASTB="`expr $SIZEB + $START`"
FREEB="`expr $TOTALB - $LASTB`"
FREEMB="`expr ${FREEB} / 2048`"
echo "${1}-freemb: $FREEMB"
echo "${1}-freeblocks: $FREEB"
