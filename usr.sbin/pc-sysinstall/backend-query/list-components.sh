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

# Script which lists the available components for this release
###########################################################################

. ${PROGDIR}/backend/functions.sh

echo "Available Components:"

if [ -d "${COMPDIR}" ]
then
  cd ${COMPDIR}
  for i in `ls -d *`
  do
    if [ -e "${i}/component.cfg" -a -e "${i}/install.sh" -a -e "${i}/distfiles" ]
    then
      NAME="`grep 'name:' ${i}/component.cfg | cut -d ':' -f 2`"
      DESC="`grep 'description:' ${i}/component.cfg | cut -d ':' -f 2`"
      TYPE="`grep 'type:' ${i}/component.cfg | cut -d ':' -f 2`"
      echo " "
      echo "name: ${i}"
      echo "desc:${DESC}"
      echo "type:${TYPE}"
      if [ -e "${i}/component.png" ]
      then
        echo "icon: ${COMPDIR}/${i}/component.png"
      fi
    fi
  done
fi
