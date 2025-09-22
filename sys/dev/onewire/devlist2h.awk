# $OpenBSD: devlist2h.awk,v 1.4 2007/02/28 22:31:32 deraadt Exp $

#
# Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

BEGIN {
	hfile = "onewiredevs.h"
	dfile = "onewiredevs_data.h"
}

NR == 1	{
	VERSION = $0
	gsub("\\$", "", VERSION)

	printf("/*\t\$OpenBSD\$\t*/\n\n" \
	       "/*\n * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n" \
	       " *\n * Generated from:\n *\t%s\n */\n\n", VERSION) > hfile

	printf("/*\t\$OpenBSD\$\t*/\n\n" \
	       "/*\n * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n" \
	       " *\n * Generated from:\n *\t%s\n */\n\n", VERSION) > dfile

	printf("struct onewire_family {\n") > dfile
	printf("\tint\t\tof_type;\n") > dfile
	printf("\tconst char\t*of_name;\n") > dfile
	printf("};\n\n") > dfile

	printf("static const struct onewire_family " \
	       "onewire_famtab[] = {\n") > dfile
}

$1 == "family" {
	printf("#define ONEWIRE_FAMILY_%s\t%s\n", toupper($2), $3) > hfile
	printf("\t{ ONEWIRE_FAMILY_%s, \"", toupper($2)) > dfile

	f = 4
	while (f <= NF) {
		if (f > 4)
			printf(" ") > dfile
		printf("%s", $f) > dfile
		f++
	}
	printf("\" },\n") > dfile
	next
}

END {
	printf("\t{ 0, NULL }\n};\n") > dfile
}
