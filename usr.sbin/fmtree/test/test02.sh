#!/bin/sh
#
# Copyright (c) 2003 Dan Nelson
# All rights reserved.
#
# Please see src/share/examples/etc/bsd-style-copyright.
#
# $FreeBSD$
#

set -e

TMP=/tmp/mtree.$$

rm -rf ${TMP}
mkdir -p ${TMP} ${TMP}/mr ${TMP}/mt

touch -t 199901020304 ${TMP}/mr/oldfile
touch ${TMP}/mt/oldfile

mtree -c -p ${TMP}/mr > ${TMP}/_ 

mtree -U -r -p ${TMP}/mt < ${TMP}/_ > /dev/null

x=x`(cd ${TMP}/mr ; ls -l 2>&1) || true`
y=x`(cd ${TMP}/mt ; ls -l 2>&1) || true`

if [ "$x" != "$y" ] ; then
	echo "ERROR Update of mtime failed" 1>&2
	rm -rf ${TMP}
	exit 1
fi

rm -rf ${TMP}
exit 0

