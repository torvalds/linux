#!/bin/sh

# Copyright (C) 2012 ADARA Networks.  All rights reserved.
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

tap_exists()
{
	ls -1 /dev | grep -q "$1"
}

if [ $(id -u) -ne 0 ]; then
	echo "Must be root" >&2
	exit 1
fi

set -e

# Base case create & destroy
tap=$(ifconfig tap create)
tap_exists $tap
ifconfig $tap destroy
! tap_exists $tap

# kern/172075: INVARIANTS kernel panicked when destroying an in-use tap(4) 
# Fixed in HEAD r240938.
tap=$(ifconfig tap create)
tap_exists $tap
cat /dev/$tap > /dev/null &
catpid=$!
sleep 0.1
ifconfig $tap destroy &
sleep 0.1
kill $catpid
! tap_exists $tap

echo PASS
exit 0
