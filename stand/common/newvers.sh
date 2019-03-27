#!/bin/sh -
#
# $FreeBSD$
#	$NetBSD: newvers.sh,v 1.1 1997/07/26 01:50:38 thorpej Exp $
#
# Copyright (c) 1984, 1986, 1990, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#	@(#)newvers.sh	8.1 (Berkeley) 4/20/94

tempfile=$(mktemp tmp.XXXXXX) || exit
trap "rm -f $tempfile" EXIT INT TERM

include_metadata=true
while getopts r opt; do
	case "$opt" in
	r)
		include_metadata=
		;;
	esac
done
shift $((OPTIND - 1))

LC_ALL=C; export LC_ALL
u=${USER-root} h=${HOSTNAME-`hostname`} t=`date`
#r=`head -n 6 $1 | tail -n 1 | awk -F: ' { print $1 } '`
r=`awk -F: ' /^[0-9]\.[0-9]+:/ { print $1; exit }' $1`

bootprog_info="FreeBSD/${3} ${2}, Revision ${r}\\n"
if [ -n "${include_metadata}" ]; then
	bootprog_info="$bootprog_info(${t} ${u}@${h})\\n"
fi

echo "char bootprog_info[] = \"$bootprog_info\";" > $tempfile
echo "unsigned bootprog_rev = ${r%%.*}${r##*.};" >> $tempfile
mv $tempfile vers.c
