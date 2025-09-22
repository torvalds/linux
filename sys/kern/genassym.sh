#	$OpenBSD: genassym.sh,v 1.15 2024/10/07 15:41:46 miod Exp $
#	$NetBSD: genassym.sh,v 1.9 1998/04/25 19:48:27 matthias Exp $

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

# If first argument is -c, create a temporary C file,
# compile it and execute the result.

awk=${AWK:-awk}

if [ "x$1" = "x-c" ] ; then
	shift
	ccode=1
else
	ccode=0
fi

WRKDIR=`mktemp -d /tmp/genassym_XXXXXXXXXX` || exit 1

TMPC=${WRKDIR}/genassym.c
TMP=${WRKDIR}/genassym

trap "rm -rf $WRKDIR" 0 1 2 3 15

$awk '
BEGIN {
	printf("#ifndef _KERNEL\n#define _KERNEL\n#endif\n");
	printf("#define	offsetof(type, member) ((size_t)(&((type *)0)->member))\n");
	defining = 0;
	type = "long";
	asmtype = "n";
	asmprint = "";
}

function start_define() {
	if (defining == 0) {
		defining = 1;
		printf("void f" FNR "(void);\n");
		printf("void f" FNR "(void) {\n");
		if (ccode)
			call[FNR] = "f" FNR;
		defining = 1;
	}
}

function end_define() {
	if (defining != 0) {
		defining = 0;
		printf("}\n");
	}
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
	end_define();
	if (includes[$2] == 0) {
		printf("#%s\n", $0);
		includes[$2] = 1;
	}
	next;
}

$0 ~ /^if[ \t]/ ||
$0 ~ /^ifdef[ \t]/ ||
$0 ~ /^ifndef[ \t]/ ||
$0 ~ /^else/ ||
$0 ~ /^elif[ \t]/ ||
$0 ~ /^endif/ {
	start_define();
	printf("#%s\n", $0);
	next;
}

/^union[ \t]/ {
	structname = $2;
	prefixname = toupper($3);
	structtype = "union"
	if (union[structname] == 1)
		next;
	else {
		union[structname] = 1;
		$0 = "define " toupper(structname) "_SIZEOF sizeof(union " structname ")";
	}
	# fall through
}

/^struct[ \t]/ {
	structname = $2;
	prefixname = toupper($3);
	structtype = "struct"
	if (struct[structname] == 1)
		next;
	else {
		struct[structname] = 1;
		$0 = "define " toupper(structname) "_SIZEOF sizeof(struct " structname ")";
	}
	# fall through
}

/^member[ \t]/ {
	if (NF > 2)
		$0 = "define " prefixname toupper($2) " offsetof(" structtype " " structname ", " $3 ")";
	else
		$0 = "define " prefixname toupper($2) " offsetof(" structtype " " structname ", " $2 ")";
	# fall through
}

/^export[ \t]/ {
	$0 = "define " $2 " " $2;
	# fall through
}

/^define[ \t]/ {
	start_define();
	value = $0
	gsub("^define[ \t]+[A-Za-z_][A-Za-z_0-9]*[ \t]+", "", value)
	if (ccode)
		printf("printf(\"#define " $2 " %%ld\\n\", (%s)" value ");\n", type);
	else
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
	end_define();
	if (ccode) {
		printf("int main(int argc, char **argv) {");
		for (i in call)
			printf(call[i] "();");
		printf("return(0); }\n");
	}
}
' ccode=$ccode > $TMPC || exit 1

if [ $ccode = 1 ] ; then
	"$@" -x c $TMPC -o $TMP && $TMP
else
	# Kill all of the "#" and "$" modifiers; locore.s already
	# prepends the correct "constant" modifier.
	"$@" -x c -S ${TMPC} -o ${TMP} || exit 1
	sed -e 's/#//g' -e 's/\$//g' ${TMP} | sed -n 's/.*XYZZY/#define/gp'
fi
