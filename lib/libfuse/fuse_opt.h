/* $OpenBSD: fuse_opt.h,v 1.5 2018/04/08 20:57:28 jca Exp $ */
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
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

#ifndef _FUSE_OPT_H_
#define _FUSE_OPT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct fuse_args {
	int argc;
	char **argv;
	int allocated;
};

struct fuse_opt {
	const char *templ;
	unsigned long off;
	int val;
};

typedef int (*fuse_opt_proc_t)(void *, const char *, int, struct fuse_args *);
int fuse_opt_add_arg(struct fuse_args *, const char *);
int fuse_opt_insert_arg(struct fuse_args *, int, const char *);
void fuse_opt_free_args(struct fuse_args *);
int fuse_opt_add_opt(char **, const char *);
int fuse_opt_add_opt_escaped(char **, const char *);
int fuse_opt_match(const struct fuse_opt *, const char *);
int fuse_opt_parse(struct fuse_args *, void *, const struct fuse_opt *,
    fuse_opt_proc_t);

#define FUSE_ARGS_INIT(ac, av)	{ ac, av, 0 }

#define FUSE_OPT_IS_OPT_KEY(t)	(t->off == (unsigned long)-1)

#define FUSE_OPT_KEY(t, k)	{ t, (unsigned long)-1, k }
#define FUSE_OPT_END		{ NULL, 0, 0 }
#define FUSE_OPT_KEY_OPT	-1
#define FUSE_OPT_KEY_NONOPT	-2
#define FUSE_OPT_KEY_KEEP	-3
#define FUSE_OPT_KEY_DISCARD	-4

#ifdef __cplusplus
}
#endif

#endif /* _FUSE_OPT_H_ */
