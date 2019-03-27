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

ARGS=$1
FLAGS_MD=""
FLAGS_CD=""
FLAGS_VERBOSE=""

shift
while [ -n "$1" ]
do
  case "$1" in
    -m)
      FLAGS_MD=1
      ;;
    -v)
      FLAGS_VERBOSE=1
      ;;
    -c)
      FLAGS_CD=1
      ;;
  esac
  shift
done

# Create our device listing
SYSDISK=$(sysctl -n kern.disks)
if [ -n "${FLAGS_MD}" ]
then
  MDS=`mdconfig -l`
  if [ -n "${MDS}" ]
  then
    SYSDISK="${SYSDISK} ${MDS}"
  fi
fi

# Add any RAID devices
if [ -d "/dev/raid" ] ; then
  cd /dev/raid
  for i in `ls`
  do
      SYSDISK="${SYSDISK} ${i}"
  done
fi

# Now loop through these devices, and list the disk drives
for i in ${SYSDISK}
do

  # Get the current device
  DEV="${i}"

  # Make sure we don't find any cd devices
  if [ -z "${FLAGS_CD}" ]
  then
    case "${DEV}" in
      acd[0-9]*|cd[0-9]*|scd[0-9]*) continue ;;
    esac
  fi

  # Try and get some identification information from GEOM
  NEWLINE=$(geom disk list $DEV 2>/dev/null | sed -ne 's/^   descr: *//p')
  if [ -z "$NEWLINE" ]; then
    	NEWLINE=" <Unknown Device>"
  fi

  if [ -n "${FLAGS_MD}" ] && echo "${DEV}" | grep -E '^md[0-9]+' >/dev/null 2>/dev/null
  then
	NEWLINE=" <Memory Disk>"
  fi

  if [ -n "${FLAGS_VERBOSE}" ]
  then
	:
  fi

  # Save the disk list
  if [ ! -z "$DLIST" ]
  then
    DLIST="\n${DLIST}"
  fi

  DLIST="${DEV}:${NEWLINE}${DLIST}"

done

# Echo out the found line
echo -e "$DLIST" | sort
