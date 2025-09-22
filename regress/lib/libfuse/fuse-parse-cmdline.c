/* $OpenBSD: fuse-parse-cmdline.c,v 1.3 2018/07/20 12:05:08 helg Exp $ */
/*
 * Copyright (c) 2017 Helg Bredow <helg@openbsd.org>
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

#include <fuse.h>
#include <stdlib.h>
#include <string.h>

static int
test_null_args(void)
{
	if (fuse_parse_cmdline(NULL, NULL, NULL, NULL) == 0)
		exit(__LINE__);

	return (0);
}

static int
test_all_args(char **dir, int *multithreaded, int *foreground)
{
	char *argv[] = {
		"progname",
		"-odebug",
		/* read-only mount not supported yet */
		/*fuse_opt_add_arg(args, "-odebug,ro");*/
		/*fuse_opt_add_arg(args, "-r");*/
		"-d",
		"-f",
		"/mnt",
		"-s"
	};
	struct fuse_args args = FUSE_ARGS_INIT(6, argv);

	if (dir != NULL)
		*dir = NULL;

	if (multithreaded != NULL)
		*multithreaded = 0;

	if (foreground != NULL)
		*foreground = 0;

	if (fuse_parse_cmdline(&args, dir, multithreaded, foreground) != 0)
		exit (__LINE__);

	if (dir != NULL && strcmp(*dir, "/mnt") != 0)
		exit(__LINE__);
	if (multithreaded != NULL && *multithreaded == 1)
		exit(__LINE__);
	if (foreground != NULL && *foreground == 0)
		exit(__LINE__);

	if (args.argc != 1)
		exit(__LINE__);
	if (strcmp(args.argv[0], "progname") != 0)
		exit(__LINE__);

	return (0);
}

int
main(void)
{
	char *dir;
	int multithreaded, foreground;

	test_null_args();
	test_all_args(NULL, NULL, NULL);
	test_all_args(&dir, NULL, NULL);
	test_all_args(&dir, &multithreaded, NULL);
	test_all_args(&dir, &multithreaded, &foreground);

	return (0);
}
