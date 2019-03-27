#! /bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 1992, 1993
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
#	@(#)systags.sh	8.1 (Berkeley) 6/10/93
# $FreeBSD$
#
# systags.sh - construct a system tags file using dependence relations
#	in a .depend file
#
# First written May 16, 1992 by Van Jacobson, Lawrence Berkeley Laboratory.

rm -f tags tags.tmp tags.cfiles tags.sfiles tags.hfiles
sed -e "s, machine/, ../../include/,g" \
	-e 's,[a-z][^/    ]*/\.\./,,g' .depend | awk '{
		for (i = 1; i <= NF; ++i) {
			t = substr($i, length($i) - 1)
			if (t == ".c")
				cfiles[$i] = 1;
			else if (t == ".h")
				hfiles[$i] = 1;
			else if (t == ".s")
				sfiles[$i] = 1;
		}
	};
	END {
		for (i in cfiles)
			print i > "tags.cfiles";
		for (i in sfiles)
			print i > "tags.sfiles";
		for (i in hfiles)
			print i > "tags.hfiles";
	}'

ctags -t -d -w `cat tags.cfiles tags.hfiles tags.sfiles`
egrep "^ENTRY\(.*\)|^ALTENTRY\(.*\)" `cat tags.sfiles` | \
    sed "s;\([^:]*\):\([^(]*\)(\([^, )]*\)\(.*\);\3	\1	/^\2(\3\4$/;" >> tags

mv tags tags.tmp
sort -u tags.tmp > tags
rm tags.tmp tags.cfiles tags.sfiles tags.hfiles
