#!/bin/sh
#
# Copyright (c) 2008, 2009 Edward Tomasz Napierała <trasz@FreeBSD.org>
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

# This is a wrapper script to run tools-crossfs.test between UFS without
# ACLs, UFS with POSIX.1e ACLs, and ZFS with NFSv4 ACLs.
#
# WARNING: It uses hardcoded ZFS pool name "acltools"
#
# Output should be obvious.

if ! sysctl vfs.zfs.version.spa >/dev/null 2>&1; then
	echo "1..0 # SKIP system doesn't have ZFS loaded"
	exit 0
fi
if [ $(id -u) -ne 0 ]; then
	echo "1..0 # SKIP you must be root"
	exit 0
fi

echo "1..5"

TESTDIR=$(dirname $(realpath $0))
MNTROOT=`mktemp -dt acltools`

# Set up the test filesystems.
MD1=`mdconfig -at swap -s 64m`
MNT1=$MNTROOT/nfs4
mkdir $MNT1
zpool create -m $MNT1 acltools /dev/$MD1
if [ $? -ne 0 ]; then
	echo "not ok 1 - 'zpool create' failed."
	echo 'Bail out!'
	exit 1
fi

echo "ok 1"

MD2=`mdconfig -at swap -s 10m`
MNT2=$MNTROOT/posix
mkdir $MNT2
newfs /dev/$MD2 > /dev/null
mount -o acls /dev/$MD2 $MNT2
if [ $? -ne 0 ]; then
	echo "not ok 2 - mount failed."
	echo 'Bail out!'
	exit 1
fi

echo "ok 2"

MD3=`mdconfig -at swap -s 10m`
MNT3=$MNTROOT/none
mkdir $MNT3
newfs /dev/$MD3 > /dev/null
mount /dev/$MD3 $MNT3
if [ $? -ne 0 ]; then
	echo "not ok 3 - mount failed."
	echo 'Bail out!'
	exit 1
fi

echo "ok 3"

cd $MNTROOT

perl $TESTDIR/run $TESTDIR/tools-crossfs.test >&2

if [ $? -eq 0 ]; then
	echo "ok 4"
else
	echo "not ok 4"
fi

cd /

umount -f $MNT3
rmdir $MNT3
mdconfig -du $MD3

umount -f $MNT2
rmdir $MNT2
mdconfig -du $MD2

zpool destroy -f acltools
rmdir $MNT1
mdconfig -du $MD1

rmdir $MNTROOT

echo "ok 5"

