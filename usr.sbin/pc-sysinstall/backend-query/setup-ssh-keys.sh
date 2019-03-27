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

# Script which sets up password-less logins for ssh host
###########################################################################

. ${PROGDIR}/backend/functions.sh

SSHUSER=$1
SSHHOST=$2
SSHPORT=$3

if [ -z "${SSHUSER}" -o -z "${SSHHOST}" -o -z "${SSHPORT}" ]
then
  echo "ERROR: Usage setup-ssh-keys <user> <host> <port>"
  exit 150
fi

cd ~

echo "Preparing to setup SSH key authorization..."
echo "When prompted, enter your password for ${SSHUSER}@${SSHHOST}"

if [ ! -e ".ssh/id_rsa.pub" ]
then
  mkdir .ssh >/dev/null 2>/dev/null
  ssh-keygen -q -t rsa -N '' -f .ssh/id_rsa
  sleep 1
fi

if [ ! -e ".ssh/id_rsa.pub" ]
then
  echo "ERROR: Failed creating .ssh/id_rsa.pub"
  exit 150
fi

# Get the .pub key
PUBKEY="`cat .ssh/id_rsa.pub`"

ssh -p ${SSHPORT} ${SSHUSER}@${SSHHOST} "mkdir .ssh ; echo $PUBKEY >> .ssh/authorized_keys; chmod 600 .ssh/authorized_keys ; echo $PUBKEY >> .ssh/authorized_keys2; chmod 600 .ssh/authorized_keys2"
