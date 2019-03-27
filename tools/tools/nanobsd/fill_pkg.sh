#!/bin/sh
#
# Copyright (c) 2009 Poul-Henning Kamp.
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
# Usage:
# 	$0 PACKAGE_DUMP NANO_PACKAGE_DIR /usr/ports/foo/bar ...
#
# Will symlink the packages listed, including their runtime dependencies,
# from the PACKAGE_DUMP to the NANO_PACKAGE_DIR.
#

NANO_PKG_DUMP=$1
shift;
if [ ! -d $NANO_PKG_DUMP ] ; then
	echo "$NANO_PKG_DUMP not a directory" 1>&2
	exit 1
fi

NANO_PACKAGE_DIR=$1
shift;

ports_recurse() (
	of=$1
	shift
	for d
	do
		if [ ! -d $d ] ; then
			echo "Missing port $d" 1>&2
			exit 2
		fi
		if grep -q "^$d\$" $of ; then
			true
		else
			(
			cd $d
			rd=`make -V RUN_DEPENDS ${PORTS_OPTS}`
			ld=`make -V LIB_DEPENDS ${PORTS_OPTS}`
			
			for x in $rd $ld
			do
				ports_recurse $of `echo $x |
				    sed 's/^[^:]*:\([^:]*\).*$/\1/'`
			done
			)
			echo $d >> $of
		fi
	done
)

rm -rf $NANO_PACKAGE_DIR
mkdir -p $NANO_PACKAGE_DIR

PL=$NANO_PACKAGE_DIR/_list
true > $PL
for i 
do
	ports_recurse `pwd`/$PL $i
done

for i in `cat $PL`
do
	p=`(cd $i && make -V PKGNAME)`
	if [ -f $NANO_PKG_DUMP/$p.t[bx]z ] ; then
		ln -s $NANO_PKG_DUMP/$p.t[bx]z $NANO_PACKAGE_DIR
	else
		echo "Package $p misssing in $NANO_PKG_DUMP" 1>&2
		exit 1
	fi
done

rm -f $PL
exit 0
