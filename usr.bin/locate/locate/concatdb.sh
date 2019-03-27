#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) September 1995 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
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
# concatdb - concatenate locate databases
# 
# usage: concatdb database1 ... databaseN > newdb
#
# Sequence of databases is important.
#
# $FreeBSD$

# The directory containing locate subprograms
: ${LIBEXECDIR:=/usr/libexec}; export LIBEXECDIR

PATH=$LIBEXECDIR:/bin:/usr/bin:$PATH; export PATH

umask 077			# protect temp files

: ${TMPDIR:=/var/tmp}; export TMPDIR;
test -d "$TMPDIR" || TMPDIR=/var/tmp

# utilities to built locate database
: ${bigram:=locate.bigram}
: ${code:=locate.code}
: ${sort:=sort}
: ${locate:=locate}


case $# in 
        [01]) 	echo 'usage: concatdb databases1 ... databaseN > newdb'
		exit 1
		;;
esac


bigrams=`mktemp ${TMPDIR=/tmp}/_bigrams.XXXXXXXXXX` || exit 1
trap 'rm -f $bigrams' 0 1 2 3 5 10 15

for db 
do
       $locate -d $db /
done | $bigram | $sort -nr | awk 'NR <= 128 { printf $2 }' > $bigrams

for db
do
	$locate -d $db /
done | $code $bigrams
