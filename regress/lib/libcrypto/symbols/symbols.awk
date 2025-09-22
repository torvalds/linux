# $OpenBSD: symbols.awk,v 1.15 2025/08/22 15:52:34 tb Exp $

# Copyright (c) 2018,2020 Theo Buehler <tb@openbsd.org>
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

# usage: awk -f symbols.awk < Symbols.list > symbols.c

BEGIN {
	printf("#include \"include_headers.c\"\n\n")
}

{
	symbols[$0] = $0

	# Undefine aliases, so we don't accidentally leave them in Symbols.list.
	printf("#ifdef %s\n#undef %s\n#endif\n", $0, $0)

	printf("extern typeof(%s) *_libre_%s;\n", $0, $0);
}

END {
	printf("\nint\nmain(void)\n{\n")
	printf("\tsize_t i;\n");

	printf("\tstruct {\n")
	printf("\t\tconst char *const name;\n")
	printf("\t\tconst void *addr;\n")
	printf("\t\tconst void *libre_addr;\n")
	printf("\t} symbols[] = {\n")

	for (symbol in symbols) {
		printf("\t\t{\n")
		printf("\t\t\t.name = \"%s\",\n", symbol)
		printf("\t\t\t.addr = &%s,\n", symbol)
		printf("#if defined(USE_LIBRESSL_NAMESPACE)\n")
		printf("\t\t\t.libre_addr = &_libre_%s,\n", symbol)
		printf("#endif\n")
		printf("\t\t},\n")
	}

	printf("\t\};\n\n")

	printf("\tfor (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {\n")
	printf("\t\tfprintf(stderr, \"%%s: %%p\\n\", symbols[i].name, symbols[i].addr);\n")
	printf("#if defined(USE_LIBRESSL_NAMESPACE)\n")
	printf("\t\tfprintf(stderr, \"_libre_%%s: %%p\\n\", symbols[i].name, symbols[i].libre_addr);\n")
	printf("#endif\n")
	printf("\t}\n")
	printf("\n\tprintf(\"OK\\n\");\n")
	printf("\n\treturn 0;\n}\n")
}
