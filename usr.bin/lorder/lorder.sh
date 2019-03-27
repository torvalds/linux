#!/bin/sh -
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 1990, 1993
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
#	@(#)lorder.sh	8.1 (Berkeley) 6/6/93
#
# $FreeBSD$
#

# only one argument is a special case, just output the name twice
case $# in
	0)
		echo "usage: lorder file ...";
		exit ;;
	1)
		echo $1 $1;
		exit ;;
esac

# temporary files
R=$(mktemp -t _reference_)
S=$(mktemp -t _symbol_)
NM=${NM:-nm}

# remove temporary files on HUP, INT, QUIT, PIPE, TERM
trap "rm -f $R $S $T; exit 1" 1 2 3 13 15

# make sure all the files get into the output
for i in $*; do
	echo $i $i
done

# if the line has " [TDW] " it's a globally defined symbol, put it
# into the symbol file.
#
# if the line has " U " it's a globally undefined symbol, put it into
# the reference file.
${NM} ${NMFLAGS} -go $* | sed "
	/ [TDW] / {
		s/:.* [TDW] / /
		w $S
		d
	}
	/ U / {
		s/:.* U / /
		w $R
	}
	d
"

export LC_ALL=C
# eliminate references that can be resolved by the same library.
if [ $(expr "$*" : '.*\.a[[:>:]]') -ne 0 ]; then
	sort -u -o $S $S
	sort -u -o $R $R
	T=$(mktemp -t _temp_)
	comm -23 $R $S >$T
	mv $T $R
fi

# sort references and symbols on the second field (the symbol),
# join on that field, and print out the file names.
sort -k 2 -o $R $R
sort -k 2 -o $S $S
join -j 2 -o 1.1 2.1 $R $S
rm -f $R $S
