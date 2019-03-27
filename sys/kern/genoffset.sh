#!/bin/sh

# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2000, Bruce Evans <bde@freebsd.org>
# Copyright (c) 2018, Jeff Roberson <jeff@freebsd.org>
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

usage()
{
	echo "usage: genoffset [-o outfile] objfile"
	exit 1
}


work()
{
	echo "#ifndef _OFFSET_INC_"
	echo "#define _OFFSET_INC_"
	echo "#if !defined(GENOFFSET) && (!defined(KLD_MODULE) || defined(KLD_TIED))"
	${NM:='nm'} ${NMFLAGS} "$1" | ${AWK:='awk'} '
	/ C .*_datatype_*/ {
		type = substr($3, match($3, "_datatype_") + length("_datatype_"))
	}
	/ C .*_parenttype_*/ {
		parent = substr($3, match($3, "_parenttype_") + length("_parenttype_"))
	}
	/ C .*sign$/ {
		sign = substr($1, length($1) - 3, 4)
		sub("^0*", "", sign)
		if (sign != "")
			sign = "-"
	}
	/ C .*w0$/ {
		w0 = substr($1, length($1) - 3, 4)
	}
	/ C .*w1$/ {
		w1 = substr($1, length($1) - 3, 4)
	}
	/ C .*w2$/ {
		w2 = substr($1, length($1) - 3, 4)
	}
	/ C .*w3$/ {
		w3 = substr($1, length($1) - 3, 4)
		w = w3 w2 w1 w0
		sub("^0*", "", w)
		if (w == "")
			w = "0"
		hex = ""
		if (w != "0")
			hex = "0x"
		sub("w3$", "", $3)
		member = tolower($3)
		# This still has minor problems representing INT_MIN, etc. 
		# E.g.,
		# with 32-bit 2''s complement ints, this prints -0x80000000,
		# which has the wrong type (unsigned int).
		offset = sprintf("%s%s%s", sign, hex, w)

		structures[parent] = sprintf("%s%s %s %s\n",
		    structures[parent], offset, type, member)
	}
	END {
		for (struct in structures) {
			printf("struct %s_lite {\n", struct);
			n = split(structures[struct], members, "\n")
			for (i = 1; i < n; i++) {
				for (j = i + 1; j < n; j++) {
					split(members[i], ivar, " ")
					split(members[j], jvar, " ")
					if (jvar[1] < ivar[1]) {
						tmp = members[i]
						members[i] = members[j]
						members[j] = tmp
					}
				}
			}
			off = "0"
			for (i = 1; i < n; i++) {
				split(members[i], m, " ")
				printf "\tu_char\tpad_%s[%s - %s];\n", m[3], m[1], off
				printf "\t%s\t%s;\n", m[2], m[3]
				off = sprintf("(%s + sizeof(%s))", m[1], m[2])
			}
			printf("};\n");
		}
	}
	'

	echo "#endif"
	echo "#endif"
}


#
#MAIN PROGGRAM
#
use_outfile="no"
while getopts "o:" option
do
	case "$option" in
	o)	outfile="$OPTARG"
		use_outfile="yes";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
case $# in
1)	;;
*)	usage;;
esac

if [ "$use_outfile" = "yes" ]
then
	work $1  3>"$outfile" >&3 3>&-
else
	work $1
fi

