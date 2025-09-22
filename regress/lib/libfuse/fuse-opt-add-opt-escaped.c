/* $OpenBSD: fuse-opt-add-opt-escaped.c,v 1.3 2018/07/20 12:05:08 helg Exp $ */
/*
 * Copyright (c) Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include <fuse_opt.h>

int
main(int ac, char **av)
{
	char *opt = NULL;
	char *opt2;

	opt2 = strdup("-a,\\,a\\,b\\,c,\\\\\\,\\\\\\,\\,\\,\\,\\\\\\\\\\,");
	if (opt2 == NULL)
		return (0);

	if (fuse_opt_add_opt_escaped(&opt2, "test") != 0)
		return (1);

	if (fuse_opt_add_opt_escaped(&opt, "-a") != 0)
		return (2);
	if (fuse_opt_add_opt_escaped(&opt, ",a,b,c") != 0)
		return (3);
	if (fuse_opt_add_opt_escaped(&opt, "\\,\\,,,,\\\\,") != 0)
		return (4);
	if (fuse_opt_add_opt_escaped(&opt, "test") != 0)
		return (5);

	if (fuse_opt_add_opt_escaped(&opt, NULL) != -1)
		return (6);
	if (fuse_opt_add_opt_escaped(&opt, "") != -1)
		return (7);

	return (strcmp(opt, opt2));
}

