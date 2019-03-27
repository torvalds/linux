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

# Create partitions on a target disk
#############################

. ${PROGDIR}/backend/functions.sh

if [ -z "${1}" ] ; then
  echo "Error: No disk specified!"
  exit 1
fi

if [ -z "${2}" ] ; then
  echo "Error: No size specified!"
  exit 1
fi

if [ ! -e "/dev/${1}" ] ; then
  echo "Error: Disk /dev/${1} does not exist!"
  exit 1
fi

DISK="${1}"
MB="${2}"
TYPE="${3}"
STARTBLOCK="${4}"

TOTALBLOCKS="`expr $MB \* 2048`"

# If no TYPE specified, default to MBR
if [ -z "$TYPE" ] ; then TYPE="mbr" ; fi

# Sanity check the gpart type
case $TYPE in
	apm|APM) ;;
	bsd|BSD) ;;
	ebr|EBR) ;;
	gpt|GPT) ;;
	mbr|MBR) ;;
    vtoc8|VTOC8) ;;
	*) echo "Error: Unknown gpart type: $TYPE" ; exit 1 ;;
esac

# Lets figure out what number this partition will be
LASTSLICE="`gpart show $DISK | grep -v -e $DISK -e '\- free \-' -e '^$' | awk 'END {print $3}'`"
if [ -z "${LASTSLICE}" ] ; then
  LASTSLICE="1"
else
  LASTSLICE="`expr $LASTSLICE + 1`"
fi

SLICENUM="${LASTSLICE}"

# Set a 4k Aligned start block if none specified
if [ "${SLICENUM}" = "1" -a -z "$STARTBLOCK" ] ; then
  STARTBLOCK="2016"
fi


# If this is an empty disk, see if we need to create a new scheme for it
gpart show ${DISK} >/dev/null 2>/dev/null
if [ $? -eq 0 -a "${SLICENUM}" = "1" ] ; then
  if [ "${TYPE}" = "mbr" -o "${TYPE}" = "MBR" ] ; then 
    flags="-s ${TYPE} -f active"
  else
    flags="-s ${TYPE}"
  fi
  gpart create ${flags} ${DISK}
fi

# If we have a starting block, use it
if [ -n "$STARTBLOCK" ] ; then
  sBLOCK="-b $STARTBLOCK"
fi

gpart add ${sBLOCK} -s ${TOTALBLOCKS} -t freebsd -i ${SLICENUM} ${DISK}
exit "$?"
