#	$OpenBSD: devlist2h.awk,v 1.6 2004/04/07 18:24:19 mickey Exp $

#
# Copyright (c) 1998-2003 Michael Shalayeff
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
# IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.
#

BEGIN	{
	ncpu = 0;
	cpuh="cpudevs.h";
	cpud="cpudevs_data.h";
	SUBSEP = "_";
}

/^[ \t]*$/	{next}

/^[ \t]*\/\*/	{busted++}

/^[ \t]*#/	{next}

busted	{
	cp = match($0, /\*\//);
	if(!cp) {
		next;
	} else if (cp + 1 == length($0)) {
		busted = 0;
		next;
	} else {
		sub(/.*\*\//, "");
		busted = 0;
	}
}

# first line is rcsid, beware
NR == 1	{
	VERSION = $0;
	gsub("\\$", "", VERSION);

	printf("/*\n * THIS FILE AUTOMATICALLY GENERATED. DO NOT EDIT.\n" \
	       " * generated from:\n *\t%s\n */\n\n", VERSION) > cpud;
	printf("/*\n * THIS FILE AUTOMATICALLY GENERATED. DO NOT EDIT.\n" \
	       " * generated from:\n *\t%s\n */\n\n", VERSION) > cpuh;
}

$1 == "type"	{
	printf("#define\tHPPA_TYPE_%s\t%s\n", toupper($2), $3) > cpuh;
	types[tolower($2)] = toupper($2);
	next;
}

NR > 1 {
	if (tolower($1) in types) {
		printf("#define\tHPPA_%s_%s\t%s\n", toupper($1),
		       toupper($2), $3) > cpuh;
		printf("{HPPA_TYPE_%s,\tHPPA_%s_%s,\t\"", toupper($1),
		       toupper($1), toupper($2), $3) > cpud;
		f = 4;
		while (f <= NF) {
			sub(/[ \t]*/, "", $f);
			ep = match($f, /\*\//);
			if (busted && !ep) {
				f++;
				continue;
			}
			if (match($f, /\/\*/)) {
				if (ep) {
					sub(/\/\*/, "", $f);
				} else {
					sub(/\/\*.*$/, "", $f);
					busted++;
				}
			}
			if (ep) {
				gsub(/^.*\*\//, "", $f);
				busted = 0;
			}
			if (length($f)) {
				if (f > 4)
					printf (" ") > cpud;
				printf ("%s", $f) > cpud;
			}
			f++;
		}
		printf("\" },\n") > cpud;
	} else {
		printf("WHA at line %d\n", NR);
		exit(1);
	}
}

END	{
	if (busted) {
		print("unterminated comment at the EOF\n");
		exit(1);
	}
	printf("{ -1 }\n") > cpud;
}

