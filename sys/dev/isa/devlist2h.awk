#! /usr/bin/awk -f
#	$OpenBSD: devlist2h.awk,v 1.6 2001/01/29 06:16:14 mickey Exp $
#	$NetBSD: devlist2h.awk,v 1.2 1996/01/22 21:08:09 cgd Exp $
#
# Copyright (c) 1995, 1996 Christopher G. Demetriou
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
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#      This product includes software developed by Christopher G. Demetriou.
# 4. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission
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
BEGIN {
	nproducts = 0
	dfile="pnpdevs.h"
}
NR == 1 {
	VERSION = $0
	gsub("\\$", "", VERSION)

	printf("/*\n") > dfile
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n") \
	    > dfile
	printf(" *\n") > dfile
	printf(" * generated from:\n") > dfile
	printf(" *\t%s\n", VERSION) > dfile
	printf(" */\n") > dfile

	next
}
{
	if ($1 == "")
		next
	if (substr($1,0,1) == "#")
		next
	if (substr($2,0,1) == "#")
		next
	do {
		nproducts++
		if ((x = index($1, "/")))
			products[nproducts, 1] = substr($1, 1, x - 1);
		else
			products[nproducts, 1] = $1;		# driver name
		products[nproducts, 2] = $2;		# pnp id
#		if ($3 && substr($3,0,1) == "#")
#			products[nproducts, 3] = substr($3, 1);
#		printf("%s %s %s\n", $1, $2, products[nproducts, 3]);
	} while (x && ($1 = substr($1, x + 1, length($1) - x)));

	next
}
END {
	# print out the match tables

	printf("\n") > dfile

	printf("const struct isapnp_knowndev isapnp_knowndevs[] = {\n") > dfile
	for (i = 1; i <= nproducts; i++) {
		printf("\t{ {\"%s\"}, {\"%s\"} },",
		    products[i, 2], products[i, 1]) \
		    > dfile
		if (products[i, 3])
			printf("\t/* %s */", products[i, 3]) > dfile
		printf("\n") > dfile
	}
	printf("};\n") > dfile
}
