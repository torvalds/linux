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

# Script which creates a gzipped log and optionally mails it to the specified address
############################################################################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/conf/pc-sysinstall.conf
. ${BACKEND}/functions-networking.sh
. ${BACKEND}/functions-parse.sh

# Bring up all NICS under DHCP
enable_auto_dhcp

MAILTO="$1"
MAILRESULT="0"

# Set the location of our compressed log
TMPLOG="/tmp/pc-sysinstall.log"

echo "# PC-SYSINSTALL LOG" >${TMPLOG}
cat ${LOGOUT} >> ${TMPLOG}

# Check if we have a GUI generated install cfg
if [ -e "/tmp/sys-install.cfg" ]
then
  echo "" >>${TMPLOG}
  echo "# PC-SYSINSTALL CFG " >>${TMPLOG}
  cat /tmp/sys-install.cfg | grep -vE 'rootPass|userPass' >> ${TMPLOG}
fi

# Save dmesg output
echo "" >>${TMPLOG}
echo "# DMESG OUTPUT " >>${TMPLOG}
dmesg >> ${TMPLOG}

# Get gpart info on all disks
for i in `pc-sysinstall disk-list | cut -d ':' -f 1`
do
  echo "" >>${TMPLOG}
  echo "# DISK INFO $i " >>${TMPLOG}
  ls /dev/${i}* >>${TMPLOG}
  gpart show ${i} >> ${TMPLOG}
done

# Show Mounted volumes
echo "" >>${TMPLOG}
echo "# MOUNT OUTPUT " >>${TMPLOG}
mount >> ${TMPLOG}

echo "Log file saved to ${TMPLOG}"
echo "Warning: This file will be lost once the system is rebooted."

echo "Do you wish to view this logfile now? (Y/N)"
read tmp
if [ "$tmp" = "Y" -o "$tmp" = "y" ]
then
  more ${TMPLOG}
fi
