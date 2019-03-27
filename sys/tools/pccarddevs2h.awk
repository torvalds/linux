#! /usr/bin/awk -f
#	$NetBSD: devlist2h.awk,v 1.3 1998/09/05 14:42:06 christos Exp $
# $FreeBSD$

#-
# SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-4-Clause
#
# Copyright (c) 1998 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas.
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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
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
#      This product includes software developed by Christos Zoulas
# 4. The name of the author(s) may not be used to endorse or promote products
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
function collectline(f, line) {
	oparen = 0
	line = ""
	while (f <= NF) {
		if ($f == "#") {
			line = line "("
			oparen = 1
			f++
			continue
		}
		if (oparen) {
			line = line $f
			if (f < NF)
				line = line " "
			f++
			continue
		}
		line = line $f
		if (f < NF)
			line = line " "
		f++
	}
	if (oparen)
		line = line ")"
	return line
}
BEGIN {
	nproducts = nvendors = 0
	hfile="pccarddevs.h"
}
NR == 1 {
	VERSION = $0
	gsub("\\$", "", VERSION)

	printf("/*\t\$FreeBSD\$\t*/\n\n") > hfile
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

	vendorindex[$2] = nvendors;		# record index for this name, for later.
	vendors[nvendors, 1] = $2;		# name
	if ($3 == "-1")
		$3 = "0xffffffff";
	vendors[nvendors, 2] = $3;		# id
	printf("#define\tPCMCIA_VENDOR_%s\t%s\t", vendors[nvendors, 1],
	    vendors[nvendors, 2]) > hfile
	vendors[nvendors, 3] = collectline(4, line)
	printf("/* %s */\n", vendors[nvendors, 3]) > hfile
	next
}
$1 == "product" {
	nproducts++

	products[nproducts, 1] = $2;		# vendor name
	if ($3 == "-1")
		$3 = "0xffffffff";
	products[nproducts, 2] = $3;		# product id
	products[nproducts, 3] = $4;		# id

	f = 5;

	if ($4 == "{") {
		products[nproducts, 3] = "0xffffffff";
		z = "{ "
		for (i = 0; i < 4; i++) {
			if (f <= NF) {
				gsub("&sp", " ", $f)
				gsub("&tab", "\t", $f)
				gsub("&nl", "\n", $f)
				z = z $f " "
				f++
			}
			else {
				if (i == 3)
					z = z "NULL "
				else
					z = z "NULL, "
			}
		}
		products[nproducts, 4] = z $f
		f++
	}
	else {
		products[nproducts, 4] = "{ NULL, NULL, NULL, NULL }"
	}
	printf("#define\tPCMCIA_CIS_%s_%s\t%s\n",
	    products[nproducts, 1], products[nproducts, 2],
	    products[nproducts, 4]) > hfile
	printf("#define\tPCMCIA_PRODUCT_%s_%s\t%s\n", products[nproducts, 1],
	    products[nproducts, 2], products[nproducts, 3]) > hfile

	products[nproducts, 5] = collectline(f, line)

	printf("#define\tPCMCIA_STR_%s_%s\t\"%s\"\n",
	    products[nproducts, 1], products[nproducts, 2],
	    products[nproducts, 5]) > hfile

	next
}
{
	print $0 > hfile
}
