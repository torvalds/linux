#! /usr/bin/awk -f
#	$OpenBSD: devlist2h.awk,v 1.4 2003/03/29 00:17:44 mickey Exp $
#	$NetBSD: devlist2h.awk,v 1.2 1996/04/09 20:07:16 cgd Exp $
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
	nproducts = nvendors = 0
	dfile="eisadevs_data.h"
	hfile="eisadevs.h"
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

	printf("/*\n") > hfile
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n") \
	    > hfile
	printf(" *\n") > hfile
	printf(" * generated from:\n") > hfile
	printf(" *\t%s\n", VERSION) > hfile
	printf(" */\n") > hfile

	next
}
$1 == "vendor" {
	nvendors++

	vendorindex[$2] = nvendors;	# record index for this name, for later.
	vendors[nvendors, 1] = $2;	# name/ID
	i = 2; f = 3;

	# comments
	ocomment = oparen = 0
	if (f <= NF) {
		ocomment = 1;
	}
	while (f <= NF) {
		if ($f == "#") {
			oparen = 1
			f++
			continue
		}
		if (oparen) {
			f++
			continue
		}
		vendors[nvendors, i] = $f
		i++; f++;
	}

	next
}
$1 == "product" {
	nproducts++

	products[nproducts, 1] = $2;		# vendor name
	products[nproducts, 2] = $3;		# product id
	printf("#define\tEISA_PRODUCT_%s%s\t\"", products[nproducts, 1],
	    products[nproducts, 2]) > hfile

	i = vendorindex[products[nproducts, 1]]; j = 2;
	needspace = 0;
	while (vendors[i, j] != "") {
		if (needspace)
			printf(" ") > hfile
		printf("%s", vendors[i, j]) > hfile
		needspace = 1
		j++
	}

	if (needspace)
		printf(" ") > hfile

	i=3; f = 4;

	# comments
	ocomment = oparen = 0
	if (f <= NF) {
		ocomment = 1;
	}
	while (f <= NF) {
		if ($f == "#") {
			printf("(") > hfile
			oparen = 1
			f++
			continue
		}
		if (oparen) {
			printf("%s", $f) > hfile
			if (f < NF)
				printf(" ") > hfile
			f++
			continue
		}
		products[nproducts, i] = $f
		printf("%s", products[nproducts, i]) > hfile
		if (f < NF)
			printf(" ") > hfile
		i++; f++;
	}
	if (oparen)
		printf(")") > hfile
	if (ocomment)
		printf("\"") > hfile
	printf("\n") > hfile

	next
}
{
	if ($0 == "")
		blanklines++
	if (blanklines != 2 && blanklines != 3)
		print $0 > hfile
	if (blanklines < 2)
		print $0 > dfile
}
END {
	# print out the match tables

	printf("\n") > dfile

	printf("static const struct eisa_knowndev eisa_knowndevs[] = {\n") > dfile
	for (i = 1; i <= nproducts; i++) {
		printf("\t{\n") > dfile
		printf("\t    0,\n") > dfile
		printf("\t    \"%s%s\",\n", products[i, 1], products[i, 2]) \
		    > dfile
		printf("\t    EISA_PRODUCT_%s%s,\n", \
		    products[i, 1], products[i, 2]) \
		    > dfile

		printf("\t},\n") > dfile
	}
	for (i = 1; i <= nvendors; i++) {
		printf("\t{\n") > dfile
		printf("\t    EISA_KNOWNDEV_NOPROD,\n") \
		    > dfile
		printf("\t    \"%s\",\n", vendors[i, 1]) \
		    > dfile
		printf("\t    \"") > dfile
		j = 2;
		needspace = 0;
		while (vendors[i, j] != "") {
			if (needspace)
				printf(" ") > dfile
			printf("%s", vendors[i, j]) > dfile
			needspace = 1
			j++
		}
		printf("\",\n") > dfile
		printf("\t},\n") > dfile
	}
	printf("\t{ 0, \"\", NULL, }\n") > dfile
	printf("};\n") > dfile
}
