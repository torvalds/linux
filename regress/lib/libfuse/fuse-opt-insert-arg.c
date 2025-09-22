/* $OpenBSD: fuse-opt-insert-arg.c,v 1.4 2018/07/20 12:05:08 helg Exp $ */
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

	if (fuse_opt_insert_arg(&args, 1, "test") != 0)
		return (1);
	if (fuse_opt_insert_arg(&args, 1, "-d") != 0)
		return (2);
	if (fuse_opt_insert_arg(&args, 3, "barfoo") != 0)
		return (3);
	if (fuse_opt_insert_arg(&args, 3, "--test") != 0)
		return (4);
	if (fuse_opt_insert_arg(&args, 4, "-o foo") != 0)
		return (5);

	if (!args.allocated)
		return (6);
	if (fuse_opt_insert_arg(&args, 1, NULL) != -1)
		return (7);
	if (fuse_opt_insert_arg(&args, -1, "foo") != -1)
		return (9);
	if (fuse_opt_insert_arg(&args, 42, "foo") != -1)
		return (10);

	for (i = 0; i < len; i++)
		if (strcmp(args.argv[i+1], argstest[i]) != 0)
			return (11);

	if (args.argc != len + 1)
		return (12);

	fuse_opt_free_args(&args);
	if (args.allocated)
		return (13);
	return (0);
}

