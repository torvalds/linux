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

# Delete a specified partition, takes effect immediately
########################################################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/backend/functions-disk.sh

if [ -z "${1}" ]
then
  echo "Error: No partition specified!"
  exit 1
fi

if [ ! -e "/dev/${1}" ]
then
  echo "Error: Partition /dev/${1} does not exist!"
  exit 1
fi

PARTITION="${1}"

# First lets figure out the partition number for the given device
##################################################################

# Get the number of characters in this dev
CHARS="`echo $PARTITION | wc -c`"

PARTINDEX=""

# Lets read through backwards until we get the part number
while 
z=1
do
  CHARS=$((CHARS-1))
  LAST_CHAR=`echo "${PARTITION}" | cut -c $CHARS`
  echo "${LAST_CHAR}" | grep -q "^[0-9]$" 2>/dev/null
  if [ $? -eq 0 ] ; then
    PARTINDEX="${LAST_CHAR}${PARTINDEX}"
  else
    break
  fi
done

# Now get current disk we are working on
CHARS=`expr $CHARS - 1`
DISK="`echo $PARTITION | cut -c 1-${CHARS}`"

# Make sure we have a valid disk name still
if [ ! -e "/dev/${DISK}" ] ; then
  echo "Error: Disk: ${DISK} doesn't exist!"
  exit 1
fi

echo "Running: gpart delete -i ${PARTINDEX} ${DISK}"
gpart delete -i ${PARTINDEX} ${DISK} >/dev/null 2>/dev/null

# Check if this was the last partition and destroy the disk geom if so
get_disk_partitions "${DISK}"
if [ -z "${VAL}" ] ; then
  gpart destroy ${DISK}  
fi

exit "$?"
