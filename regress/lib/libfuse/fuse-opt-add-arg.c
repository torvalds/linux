/* $OpenBSD: fuse-opt-add-arg.c,v 1.4 2018/07/20 12:05:08 helg Exp $ */
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

#define ADD_ARG(fa, a)	if (fuse_opt_add_arg(fa, a) != 0) \
				return (1);

char *argstest[] = {
	"-d",
	"test",
	"--test",
	"-o foo",
	"barfoo"
};

int
main(int ac, char **av)
{
	struct fuse_args args = FUSE_ARGS_INIT(ac, av);
	int len, i;

	len = sizeof(argstest) / sizeof(*argstest);

	for (i = 0; i < len; i++)
		ADD_ARG(&args, argstest[i]);

	if (!args.allocated)
		return (1);
	if (fuse_opt_add_arg(&args, NULL) != -1)
		return (2);

	for (i = 0; i < len; i++)
		if (strcmp(args.argv[i+1], argstest[i]) != 0)
			return (4);

	if (args.argc != len + 1)
		return (5);

	fuse_opt_free_args(&args);
	if (args.allocated)
		return (6);
	return (0);
}

