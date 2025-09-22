#!/bin/sh -
#	$OpenBSD: genassym.sh,v 1.4 2020/04/02 06:06:22 otto Exp $
#	$NetBSD: genassym.sh,v 1.8 2014/01/06 22:43:15 christos Exp $
#
# Copyright (c) 1997 Matthias Pfaller.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

progname="$(basename "${0}")"
: ${AWK:=awk}

ccode=0		# generate temporary C file, compile it, execute result
fcode=0		# generate Forth code

usage()
{

	echo "usage: ${progname} [-c | -f] -- compiler command" >&2
}

set -e

while getopts cf i
do
	case "$i" in
	c)
		ccode=1
		;;
	f)
		fcode=1
		;;
	esac
done
shift "$(($OPTIND - 1))"
if [ $# -eq 0 ]; then
	usage
	exit 1
fi

# Deal with any leading environment settings..

while [ -n "$1" ]
do
	case "$1" in
	*=*)
		eval export "$1"
		shift
		;;
	*)
		break
		;;
	esac
done

genassym_temp="$(mktemp -d "${TMPDIR-/tmp}/genassym.XXXXXX")"


if [ ! -d $genassym_temp ]; then
	echo "${progname}: unable to create temporary directory" >&2
	exit 1
fi
trap "rm -rf $genassym_temp" 0 1 2 3 15

$AWK '
BEGIN {
	printf("#if __GNUC__ >= 4\n");
	printf("#define	offsetof(type, member) __builtin_offsetof(type, member)\n");
	printf("#else\n");
	printf("#define	offsetof(type, member) ((size_t)(&((type *)0)->member))\n");
	printf("#endif\n");
	defining = 0;
	type = "long";
	asmtype = "n";
	asmprint = "";
}

{
	doing_member = 0;
}

$0 ~ /^[ \t]*#.*/ || $0 ~ /^[ \t]*$/ {
	# Just ignore comments and empty lines
	next;
}

$0 ~ /^config[ \t]/ {
	type = $2;
	asmtype = $3;
	asmprint = $4;
	next;
}

/^include[ \t]/ {
	if (defining != 0) {
		defining = 0;
		printf("}\n");
	}
	printf("#%s\n", $0);
	next;
}

$0 ~ /^if[ \t]/ ||
$0 ~ /^ifdef[ \t]/ ||
$0 ~ /^ifndef[ \t]/ ||
$0 ~ /^else/ ||
$0 ~ /^elif[ \t]/ ||
$0 ~ /^endif/ {
	printf("#%s\n", $0);
	next;
}

/^struct[ \t]/ {
	structname = $2;
	$0 = "define " structname "_SIZEOF sizeof(struct " structname ")";
	# fall through
}

/^member[ \t]/ {
	if (NF > 2)
		$0 = "define " $2 " offsetof(struct " structname ", " $3 ")";
	else
		$0 = "define " $2 " offsetof(struct " structname ", " $2 ")";
	doing_member = 1;
	# fall through
}

/^export[ \t]/ {
	$0 = "define " $2 " " $2;
	# fall through
}

/^define[ \t]/ {
	if (defining == 0) {
		defining = 1;
		printf("void f" FNR "(void);\n");
		printf("void f" FNR "(void) {\n");
		if (ccode)
			call[FNR] = "f" FNR;
		defining = 1;
	}
	value = $0
	gsub("^define[ \t]+[A-Za-z_][A-Za-z_0-9]*[ \t]+", "", value)
	if (ccode)
		printf("printf(\"#define " $2 " %%ld\\n\", (%s)" value ");\n", type);
	else if (fcode) {
		if (doing_member)
			printf("__asm(\"XYZZY : %s d# %%%s0 + ;\" : : \"%s\" (%s));\n", $2, asmprint, asmtype, value);
		else
			printf("__asm(\"XYZZY d# %%%s0 constant %s\" : : \"%s\" (%s));\n", asmprint, $2, asmtype, value);
	} else
		printf("__asm(\"XYZZY %s %%%s0\" : : \"%s\" (%s));\n", $2, asmprint, asmtype, value);
	next;
}

/^quote[ \t]/ {
	gsub("^quote[ \t]+", "");
	print;
	next;
}

{
	printf("syntax error in line %d\n", FNR) >"/dev/stderr";
	exit(1);
}

END {
	if (defining != 0) {
		defining = 0;
		printf("}\n");
	}
	if (ccode) {
		printf("int main(int argc, char **argv) {");
		for (i in call)
			printf(call[i] "();");
		printf("return(0); }\n");
	}
}
' ccode="$ccode" fcode="$fcode" > "${genassym_temp}/assym.c" || exit 1

if [ "$ccode" = 1 ]; then
	"$@" "${genassym_temp}/assym.c" -o "${genassym_temp}/genassym" && \
	    "${genassym_temp}/genassym"
elif [ "$fcode" = 1 ]; then
	# Kill all of the "#" and "$" modifiers; locore.s already
	# prepends the correct "constant" modifier.
	"$@" -S "${genassym_temp}/assym.c" -o - | sed -e 's/\$//g' | \
	    sed -n 's/.*XYZZY//gp'
else
	# Kill all of the "#" and "$" modifiers; locore.s already
	# prepends the correct "constant" modifier.
	"$@" -S "${genassym_temp}/assym.c" -o - > \
	    "${genassym_temp}/genassym.out" && \
	    sed -e 's/#//g' -e 's/\$//g' < "${genassym_temp}/genassym.out" | \
	    sed -n 's/.*XYZZY/#define/gp'
fi
