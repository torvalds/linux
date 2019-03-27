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
# mklocatedb - build locate database
# 
# usage: mklocatedb [-presort] < filelist > database
#
# $FreeBSD$

# The directory containing locate subprograms
: ${LIBEXECDIR:=/usr/libexec}; export LIBEXECDIR

PATH=$LIBEXECDIR:/bin:/usr/bin:$PATH; export PATH

umask 077			# protect temp files

: ${TMPDIR:=/tmp}; export TMPDIR
test -d "$TMPDIR" || TMPDIR=/tmp
if ! TMPDIR=`mktemp -d $TMPDIR/mklocateXXXXXXXXXX`; then
	exit 1
fi


# utilities to built locate database
: ${bigram:=locate.bigram}
: ${code:=locate.code}
: ${sort:=sort}


sortopt="-u -T $TMPDIR"
sortcmd=$sort


bigrams=$TMPDIR/_mklocatedb$$.bigrams
filelist=$TMPDIR/_mklocatedb$$.list

trap 'rm -f $bigrams $filelist; rmdir $TMPDIR' 0 1 2 3 5 10 15


# Input already sorted
if [ X"$1" = "X-presort" ]; then
    shift; 

    # create an empty file
    true > $bigrams
    
    # Locate database bootstrapping
    # 1. first build a temp database without bigram compression
    # 2. create the bigram from the temp database
    # 3. create the real locate database with bigram compression.
    #
    # This scheme avoid large temporary files in /tmp

    $code $bigrams > $filelist || exit 1
    locate -d $filelist / | $bigram | $sort -nr | head -128 |
    awk '{if (/^[ 	]*[0-9]+[ 	]+..$/) {printf("%s",$2)} else {exit 1}}' > $bigrams || exit 1
    locate -d $filelist / | $code $bigrams || exit 1
    exit 	

else
    if $sortcmd $sortopt > $filelist; then
        $bigram < $filelist | $sort -nr | 
	awk '{if (/^[ 	]*[0-9]+[ 	]+..$/) {printf("%s",$2)} else {exit 1}}' > $bigrams || exit 1
        $code $bigrams < $filelist || exit 1
    else
        echo "`basename $0`: cannot build locate database" >&2
        exit 1
    fi
fi
