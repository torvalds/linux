#! /usr/bin/awk -f
#	$NetBSD: devlist2h.awk,v 1.9 2005/12/11 12:21:20 christos Exp $
#	$FreeBSD$
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
	nproducts = nvendors = blanklines = 0
	dfile="ediddevs_data.h"
	hfile="ediddevs.h"
}
NR == 1 {
	VERSION = $0
	gsub("\\$", "", VERSION)
	gsub(/ $/, "", VERSION)

	printf("/*\t$FreeBSD" "$\t*/\n\n") > dfile
	printf("/*\n") > dfile
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n") \
	    > dfile
	printf(" *\n") > dfile
	printf(" * generated from:\n") > dfile
	printf(" *\t%s\n", VERSION) > dfile
	printf(" */\n") > dfile

	printf("/*\t$NetBSD" "$\t*/\n\n") > hfile
	printf("/*\n") > hfile
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n") \
	    > hfile
	printf(" *\n") > hfile
	printf(" * generated from:\n") > hfile
	printf(" *\t%s\n", VERSION) > hfile
	printf(" */\n") > hfile

	next
}

NF > 0 && $1 == "vendor" {
	nvendors++

	vendorindex[$2] = nvendors;	# record index for this name, for later.
	vendors[nvendors, 1] = $2;	# name/ID
	i = 2; f = 3;

	printf("#define\tEDID_VENDOR_%s\t\"", vendors[nvendors, 1]) > hfile

	# comments
	oparen = 0
	while (f <= NF) {
		if ($f == "#") {
			printf("(") > hfile
			oparen = 1
			f++
			continue
		}
		if (oparen) {
			printf("%s", $f) > hfile
			f++
			continue
		}
		vendors[nvendors, i] = $f
		printf("%s", vendors[nvendors, i]) > hfile
		if (f < NF)
			printf(" ") > hfile
		i++; f++;
	}
	if (oparen)
		printf(")") > hfile
	printf("\"") > hfile
	printf("\n") > hfile

	next
}

NF > 0 && $1 == "product" {
	nproducts++

	products[nproducts, 1] = $2;		# vendor name
	products[nproducts, 2] = $3;		# product id
	products[nproducts, 3] = $4;		# id
	printf("#define\tEDID_PRODUCT_%s_%s\t%s", products[nproducts, 1],
	    products[nproducts, 2], products[nproducts, 3]) > hfile

	i = 4; f = 5;
	
	ocomment = oparen = 0
	if (f <= NF) {
		printf("\t\t/* ") > hfile
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
		printf(" */") > hfile
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
	printf("const struct edid_vendor edid_vendors[] = {\n") > dfile

	for (i = 1; i <= nvendors; i++) {
		printf("\t{") > dfile
		printf(" \"%s\", EDID_VENDOR_%s", vendors[i, 1], \
		    vendors[i, 1]) > dfile
		printf(" },\n") > dfile
	}
	printf("};\n") > dfile
	printf("const int edid_nvendors = %d;\n", nvendors) > dfile

	printf("\n") > dfile

	printf("const struct edid_product edid_products[] = {\n") > dfile
	for (i = 1; i <= nproducts; i++) {
		printf("\t{\n") > dfile
		printf("\t    \"%s\", EDID_PRODUCT_%s_%s,\n", \
		    products[i, 1], products[i, 1], products[i, 2]) > dfile
		printf("\t    \"") > dfile
		j = 4
		needspace = 0
		while ((i, j) in products) {
			if (needspace)
				printf(" ") > dfile
			printf("%s", products[i, j]) > dfile
			needspace = 1
			j++
		}
		printf("\",\n") > dfile
		printf("\t},\n") > dfile
	}
	printf("};\n") > dfile
	printf("const int edid_nproducts = %d;\n", nproducts) >dfile

	close(dfile)
	close(hfile)
}
