// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include "util/string2.h"

#include "demangle-ocaml.h"

#include <linux/ctype.h>

static const char *caml_prefix = "caml";
static const size_t caml_prefix_len = 4;

/* mangled OCaml symbols start with "caml" followed by an upper-case letter */
static bool
ocaml_is_mangled(const char *sym)
{
	return 0 == strncmp(sym, caml_prefix, caml_prefix_len)
		&& isupper(sym[caml_prefix_len]);
}

/*
 * input:
 *     sym: a symbol which may have been mangled by the OCaml compiler
 * return:
 *     if the input doesn't look like a mangled OCaml symbol, NULL is returned
 *     otherwise, a newly allocated string containing the demangled symbol is returned
 */
char *
ocaml_demangle_sym(const char *sym)
{
	char *result;
	int j = 0;
	int i;
	int len;

	if (!ocaml_is_mangled(sym)) {
		return NULL;
	}

	len = strlen(sym);

	/* the demangled symbol is always smaller than the mangled symbol */
	result = malloc(len + 1);
	if (!result)
		return NULL;

	/* skip "caml" prefix */
	i = caml_prefix_len;

	while (i < len) {
		if (sym[i] == '_' && sym[i + 1] == '_') {
			/* "__" -> "." */
			result[j++] = '.';
			i += 2;
		}
		else if (sym[i] == '$' && isxdigit(sym[i + 1]) && isxdigit(sym[i + 2])) {
			/* "$xx" is a hex-encoded character */
			result[j++] = (hex(sym[i + 1]) << 4) | hex(sym[i + 2]);
			i += 3;
		}
		else {
			result[j++] = sym[i++];
		}
	}
	result[j] = '\0';

	return result;
}
