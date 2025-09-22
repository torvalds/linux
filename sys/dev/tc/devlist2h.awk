#! /usr/bin/awk -f
#	$OpenBSD: devlist2h.awk,v 1.6 2006/03/13 22:00:31 miod Exp $
#	$NetBSD: devlist2h.awk,v 1.3 1996/06/05 18:32:19 cgd Exp $
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
	dfile="tcdevs_data.h"
}
NR == 1 {
	VERSION = $0
	gsub("\\$", "", VERSION)

	printf("/*\t\$OpenBSD\$\t*/\n\n") > dfile
	printf("/*\n") > dfile
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n") \
	    > dfile
	printf(" *\n") > dfile
	printf(" * generated from:\n") > dfile
	printf(" *\t%s\n", VERSION) > dfile
	printf(" */\n") > dfile

	next
}
$1 == "device" {
	ndevices++

	devices[ndevices] = $2;		# devices id
	description[ndevices] = $4

	f = 5;
	while (f <= NF) {
		description[ndevices] = sprintf("%s %s", description[ndevices], $f)
		f++;
	}

	next
}
END {
	# print out the match tables

	printf("\n") > dfile

	printf("struct tc_knowndev tc_knowndevs[] = {\n") > dfile
	for (i = 1; i <= ndevices; i++) {
		printf("\t{\n") > dfile
		printf("\t    \"%-8s\",\n", devices[i]) \
		    > dfile
		printf("\t    \"%s\"\n", description[i]) \
		    > dfile

		printf("\t},\n") > dfile
	}
	printf("\t{ NULL, NULL }\n") > dfile
	printf("};\n") > dfile
}
