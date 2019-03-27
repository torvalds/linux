#!/bin/sh
#
# Copyright (c) 2011 Edward Tomasz Napierała <trasz@FreeBSD.org>
# All rights reserved.
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
#

# This is a wrapper script to run tools-nfs4-trivial.test on ZFS filesystem.
#
# WARNING: It uses hardcoded ZFS pool name "acltools"

if ! sysctl vfs.zfs.version.spa >/dev/null 2>&1; then
	echo "1..0 # SKIP system doesn't have ZFS loaded"
	exit 0
fi
if [ $(id -u) -ne 0 ]; then
	echo "1..0 # SKIP you must be root"
	exit 0
fi

echo "1..3"

TESTDIR=$(dirname $(realpath $0))

# Set up the test filesystem.
MD=`mdconfig -at swap -s 64m`
MNT=`mktemp -dt acltools`
zpool create -m $MNT acltools /dev/$MD
if [ $? -ne 0 ]; then
	echo "not ok 1 - 'zpool create' failed."
	echo 'Bail out!'
	exit 1
fi

echo "ok 1"

cd $MNT

perl $TESTDIR/run $TESTDIR/tools-nfs4-trivial.test >&2

if [ $? -eq 0 ]; then
	echo "ok 2"
else
	echo "not ok 2"
fi

cd /
zpool destroy -f acltools
rmdir $MNT
mdconfig -du $MD

echo "ok 3"
