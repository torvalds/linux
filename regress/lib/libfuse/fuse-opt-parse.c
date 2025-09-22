/* $OpenBSD: fuse-opt-parse.c,v 1.4 2018/07/20 12:05:08 helg Exp $ */
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

#include <assert.h>
#include <fuse_opt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct data {
	int	port;
	char	*fsname;
	char	*x;
	char	*optstring;
	int	debug;
	int	foreground;
	int	noatime;
	int	ssh_ver;
	int	count;
	int	cache;
	int	set_gid;
	int	gid;
	int	bad_opt;
};

enum {
	KEY_DEBUG,
	KEY_NOATIME,
	KEY_PORT
};

#define DATA_OPT(o,m,v) {o, offsetof(struct data, m), v}

struct fuse_opt opts[] = {
	FUSE_OPT_KEY("noatime",		KEY_NOATIME	 ),

	DATA_OPT("optstring=%s",	optstring,	0),
	DATA_OPT("-f=%s",		fsname, 	0),
	DATA_OPT("-x %s",		x,		0),
	DATA_OPT("--count=%u",		count, 		0),
	DATA_OPT("-1",			ssh_ver,	5),
	DATA_OPT("cache=yes",		cache,		1),
	DATA_OPT("cache=no",		cache,		0),
	DATA_OPT("debug",		debug,		1),
	DATA_OPT("debug",		foreground,	1),
	FUSE_OPT_KEY("debug",		KEY_DEBUG	 ),
	FUSE_OPT_KEY("debug",		FUSE_OPT_KEY_KEEP),
	DATA_OPT("-p",			port,	       25),
	FUSE_OPT_KEY("-p ",		KEY_PORT	 ),
	DATA_OPT("gid=",		set_gid,	1),
	DATA_OPT("gid=%o",		gid,		1),

	FUSE_OPT_END
};

int
proc(void *data, const char *arg, int key, struct fuse_args *args)
{
	struct data *conf = data;

	if (conf == NULL)
		return (1);

	switch (key)
	{
	case KEY_PORT:
		conf->port = atoi(&arg[2]);
		return (0);
	case KEY_DEBUG:
		conf->debug = 2;
		return (0);
	case KEY_NOATIME:
		conf->noatime = 1;
		return (1);
	}

	if (strcmp("bad_opt", arg) == 0) {
		conf->bad_opt = 1;
		return (0);
	}

	return (1);
}

/*
 * A NULL 'args' is equivalent to an empty argument vector.
 */
void
test_null_args(void) {
	struct data data;
	struct fuse_args args;

	bzero(&data, sizeof(data));

	assert(fuse_opt_parse(NULL, &data, opts, proc) == 0);

	assert(data.port == 0);
	assert(data.fsname == NULL);
	assert(data.x == NULL);
	assert(data.optstring == NULL);
	assert(data.debug == 0);
	assert(data.noatime == 0);
	assert(data.ssh_ver == 0);
	assert(data.count == 0);
	assert(data.cache == 0);
	assert(data.bad_opt == 0);
}

/*
 * A NULL 'opts' is equivalent to an 'opts' array containing a single
 * end marker.
 */
void
test_null_opts(void)
{
	struct data data;
	struct fuse_args args;

	char *argv_null_opts[] = {
		"progname",
		"/mnt",
		"bad_opt"
	};

	args.argc = sizeof(argv_null_opts) / sizeof(argv_null_opts[0]);
	args.argv = argv_null_opts;
	args.allocated = 0;

	bzero(&data, sizeof(data));

	assert(fuse_opt_parse(&args, &data, NULL, proc) == 0);

	assert(data.port == 0);
	assert(data.fsname == NULL);
	assert(data.x == NULL);
	assert(data.optstring == NULL);
	assert(data.debug == 0);
	assert(data.noatime == 0);
	assert(data.ssh_ver == 0);
	assert(data.count == 0);
	assert(data.cache == 0);
	assert(data.set_gid == 0);
	assert(data.gid == 0);
	assert(data.bad_opt == 1);

	assert(args.argc == 2);
	assert(strcmp(args.argv[0], "progname") == 0);
	assert(strcmp(args.argv[1], "/mnt") == 0);
	assert(args.allocated);

	fuse_opt_free_args(&args);
}

/*
 * A NULL 'proc' is equivalent to a processing function always returning '1'.
 */
void
test_null_proc(void)
{
	struct data data;
	struct fuse_args args;

        char *argv_null_proc[] = {
                "progname",
                "-odebug,noatime,gid=077",
                "-d",
                "-p", "22",
                "/mnt",
                "-f=filename",
                "-1",
                "-x", "xanadu",
                "-o", "optstring=",
                "-o", "optstring=optstring",
                "--count=10",
		"bad_opt"
      };

        args.argc = sizeof(argv_null_proc) / sizeof(argv_null_proc[0]);
        args.argv = argv_null_proc;
        args.allocated = 0;

        bzero(&data, sizeof(data));

	assert(fuse_opt_parse(&args, &data, opts, NULL) == 0);

	assert(data.port == 25);
	assert(strcmp(data.fsname, "filename") == 0);
	assert(strcmp(data.x, "xanadu") == 0);
	assert(strcmp(data.optstring, "optstring") == 0);
	assert(data.debug == 1);
	assert(data.noatime == 0);
	assert(data.ssh_ver == 5);
	assert(data.count == 10);
	assert(data.cache == 0);
	assert(data.set_gid == 1);
	assert(data.gid == 077);
	assert(data.bad_opt == 0);

	assert(args.argc == 9);
	assert(strcmp(args.argv[0], "progname") == 0);
	assert(strcmp(args.argv[1], "-o") == 0);
	assert(strcmp(args.argv[2], "debug") == 0);
	assert(strcmp(args.argv[3], "-o") == 0);
	assert(strcmp(args.argv[4], "noatime") == 0);
	assert(strcmp(args.argv[5], "-d") == 0);
	assert(strcmp(args.argv[6], "-p22") == 0);
	assert(strcmp(args.argv[7], "/mnt") == 0);
	assert(strcmp(args.argv[8], "bad_opt") == 0);
	assert(args.allocated);

	fuse_opt_free_args(&args);
}

/*
 * Test with all args supplied to fuse_opt_parse.
 */
void
test_all_args(void)
{
	struct data data;
	struct fuse_args args;

	char *argv[] = {
		"progname",
		"-odebug,noatime,gid=077",
		"-d",
		"-p", "22",
		"/mnt",
		"-f=filename",
		"-1",
		"-x", "xanadu",
		"-o", "optstring=optstring,cache=no",
		"--count=10",
		"bad_opt"
	};

	args.argc = sizeof(argv) / sizeof(argv[0]);
	args.argv = argv;
	args.allocated = 0;

	bzero(&data, sizeof(data));

	assert(fuse_opt_parse(&args, &data, opts, proc) == 0);

	assert(data.port == 22);
	assert(strcmp(data.fsname, "filename") == 0);
	assert(strcmp(data.x, "xanadu") == 0);
	assert(strcmp(data.optstring, "optstring") == 0);
	assert(data.debug == 2);
	assert(data.noatime == 1);
	assert(data.ssh_ver == 5);
	assert(data.count == 10);
	assert(data.cache == 0);
	assert(data.set_gid == 1);
	assert(data.gid == 077);
	assert(data.bad_opt == 1);

	assert(args.argc == 7);
	assert(strcmp(args.argv[0], "progname") == 0);
	assert(strcmp(args.argv[1], "-o") == 0);
	assert(strcmp(args.argv[2], "debug") == 0);
	assert(strcmp(args.argv[3], "-o") == 0);
	assert(strcmp(args.argv[4], "noatime") == 0);
	assert(strcmp(args.argv[5], "-d") == 0);
	assert(strcmp(args.argv[6], "/mnt") == 0);
	assert(args.allocated);

	fuse_opt_free_args(&args);
}

int
main(void)
{
	test_null_opts();
	test_null_args();
	test_null_proc();
	test_all_args();

	return (0);
}
