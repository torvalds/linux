#! /usr/bin/awk -f
#	$OpenBSD: devlist2h.awk,v 1.4 2006/08/10 23:44:16 miod Exp $
#
# Copyright (c) 2003, Miodrag Vallat.
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
BEGIN {
	header = 0
}
NR == 1 {
	VERSION = $0
	gsub("\\$", "", VERSION)

	printf("/*\t\$OpenBSD\$\t*/\n\n")
	printf("/*\n")
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n")
	printf(" *\n")
	printf(" * generated from:\n")
	printf(" *\t%s\n", VERSION)
	printf(" */\n")

	next
}
$1 == "keyboard" || $1 == "mouse" || $1 == "idmodule" || $1 == "buttonbox" {

	if (header == 0) {
		printf("const struct hildevice hildevs[] = {\n")
		header = 1
	}

	printf("\t{ 0x%s, 0x%s, HIL_DEVICE_%s, \"",
	    $2, $3, toupper($1))

	# description, with optional ``#''-prefixed comments
	comment = 0
	i = 4
	f = i
	while (f <= NF) {
		if ($f == "#") {
			comment = 1
			printf ("\" },\t/*")
		} else {
			if (f > i)
				printf(" ")
			printf("%s", $f)
		}
		f++
	}
	if (comment)
		printf(" */\n");
	else
		printf("\" },\n")

	next
}
{
	if ($0 == "")
		blanklines++
	if (blanklines < 2)
		print $0
}
END {
	printf("\t{ -1, -1, -1, NULL }\n")
	printf("};\n")
}
