/* $OpenBSD: fuse-opt-match.c,v 1.5 2018/07/20 12:05:08 helg Exp $ */
/*
 * Copyright (c) 2017 Helg Bredow <xx404@msn.com>
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
#include <stddef.h>
#include <fuse_opt.h>

static const struct fuse_opt emptyopts[] = {
	FUSE_OPT_END
};

static const struct fuse_opt opts[] = {
	FUSE_OPT_KEY("-p ",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-C",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-V",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("--version",	FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-h",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("const=false",	FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("cache=no",	FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("cache=yes",	FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("debug",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("ro",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("--foo=",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("bars=%s",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("--fool=%lu",	FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-x ",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-n %u",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_KEY("-P",		FUSE_OPT_KEY_DISCARD),
	FUSE_OPT_END
};

int
main(void)
{
	assert(fuse_opt_match(emptyopts, "debug")	==	0);

	assert(fuse_opt_match(opts, NULL)		==	0);
	assert(fuse_opt_match(opts, "-p ")		==	1);
	assert(fuse_opt_match(opts, "-C")		==	1);
	assert(fuse_opt_match(opts, "-c")		==	0);
	assert(fuse_opt_match(opts, "-V")		==	1);
	assert(fuse_opt_match(opts, "--version")	==	1);
	assert(fuse_opt_match(opts, "-h")		==	1);
	assert(fuse_opt_match(opts, "const=false")	==	1);
	assert(fuse_opt_match(opts, "const=falsefalse")	==	0);
	assert(fuse_opt_match(opts, "cache=no")		==	1);
	assert(fuse_opt_match(opts, "cache=yes")	==	1);
	assert(fuse_opt_match(opts, "debug")		==	1);
	assert(fuse_opt_match(opts, "ro")		==	1);
	assert(fuse_opt_match(opts, "ro_fallback")	==	0);
	assert(fuse_opt_match(opts, "--foo=bar")	==	1);
	assert(fuse_opt_match(opts, "bars=foo")		==	1);
	assert(fuse_opt_match(opts, "--fool=bool")	==	1);
	assert(fuse_opt_match(opts, "--fool=1")		==	1);
	assert(fuse_opt_match(opts, "-x bar")		==	1);
	assert(fuse_opt_match(opts, "-xbar")		==	1);
	assert(fuse_opt_match(opts, "-n 100")		==	1);
	assert(fuse_opt_match(opts, "-n100")		==	1);
	assert(fuse_opt_match(opts, "-P")		==	1);

	return (0);
}
