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

# Script which lists the backups present on a server
###########################################################################

. ${PROGDIR}/backend/functions.sh

SSHUSER=$1
SSHHOST=$2
SSHPORT=$3

if [ -z "${SSHHOST}" -o -z "${SSHPORT}" ]
then
  echo "ERROR: Usage list-rsync-backups.sh <user> <host> <port>"
  exit 150
fi

# Look for full-system backups, needs at minimum a kernel to be bootable
FINDCMD="find . -type d -maxdepth 6 -name 'kernel' | grep '/boot/kernel'"

# Get a listing of the number of full backups saved
OLDBACKUPS=`ssh -o 'BatchMode=yes' -p ${SSHPORT} ${SSHUSER}@${SSHHOST} "${FINDCMD}"`
if [ "$?" = "0" ]
then
  for i in ${OLDBACKUPS}
  do
    BACKPATH="`echo ${i} | sed 's|/boot/.*||g' | sed 's|^./||g'`"
    if [ -z "${BACKLIST}" ]
    then
      BACKLIST="${BACKPATH}"
    else
      BACKLIST="${BACKLIST}:${BACKPATH}"
    fi
  done

  if [ -z "${BACKLIST}" ]
  then
    echo "NONE"
  else
    echo "$BACKLIST"
  fi

else
  echo "FAILED"  
fi
