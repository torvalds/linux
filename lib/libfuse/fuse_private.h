/* $OpenBSD: fuse_private.h,v 1.24 2025/09/20 15:01:23 helg Exp $ */
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

#ifndef _FUSE_SUBR_H_
#define _FUSE_SUBR_H_

#include <sys/dirent.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/tree.h>
#include <sys/fusebuf.h>
#include <limits.h>

#include "fuse.h"

struct fuse_args;

struct fuse_vnode {
	ino_t ino;
	struct fuse_vnode *parent;
	unsigned int ref;

	char path[NAME_MAX + 1];

	SIMPLEQ_ENTRY(fuse_vnode) node; /* for dict */
};

struct fuse_dirhandle {
	struct fuse *fuse;
	fuse_fill_dir_t filler;
	void *buf;
	int full;
	uint32_t size;
	uint32_t start;
	uint32_t idx;
	off_t off;
};

SIMPLEQ_HEAD(fuse_vn_head, fuse_vnode);
SPLAY_HEAD(dict, dictentry);
SPLAY_HEAD(tree, treeentry);

struct fuse_session {
	void *args;
};

struct fuse_chan {
	char *dir;
	struct fuse_args *args;

	int fd;
	int init;
	int dead;

	/* kqueue stuff */
	int kq;
};

struct fuse_config {
	uid_t			uid;
	gid_t			gid;
	pid_t			pid;
	mode_t			umask;
	int			set_mode;
	int			set_uid;
	int			set_gid;
	int			use_ino;
};

struct fuse_core_opts {
	char			*mp;
	int			foreground;
};

struct fuse_mount_opts {
	char			*fsname;
	int			allow_other;
	int			def_perms;
	int			max_read;
	int			noatime;
	int			nonempty;
	int			rdonly;
};

struct fuse {
	struct fuse_chan	*fc;
	struct fuse_operations	op;

	int			compat;

	struct tree		vnode_tree;
	struct dict		name_tree;
	uint64_t		max_ino;
	void			*private_data;

	struct fuse_config	conf;
	struct fuse_session	se;
};

#define	FUSE_MAX_OPS	39
#define FUSE_ROOT_INO ((ino_t)1)

/* fuse_ops.c */
int	ifuse_exec_opcode(struct fuse *, struct fusebuf *);

/* fuse_subr.c */
struct fuse_vnode	*alloc_vn(struct fuse *, const char *, ino_t, ino_t);
void			 ref_vn(struct fuse_vnode *);
void			 unref_vn(struct fuse *, struct fuse_vnode *);
struct fuse_vnode	*get_vn_by_name_and_parent(struct fuse *, uint8_t *,
    ino_t);
void			remove_vnode_from_name_tree(struct fuse *,
    struct fuse_vnode *);
int			set_vn(struct fuse *, struct fuse_vnode *);
char			*build_realname(struct fuse *, ino_t);

/* tree.c */
#define tree_init(t)	SPLAY_INIT((t))
#define tree_empty(t)	SPLAY_EMPTY((t))
int			tree_check(struct tree *, uint64_t);
void			*tree_set(struct tree *, uint64_t, void *);
void			*tree_get(struct tree *, uint64_t);
void			*tree_pop(struct tree *, uint64_t);

/* dict.c */
int			dict_check(struct dict *, const char *);
void			*dict_set(struct dict *, const char *, void *);
void			*dict_get(struct dict *, const char *);
void			*dict_pop(struct dict *, const char *);

#define FUSE_VERSION_PKG_INFO "2.8.0"
#define unused __attribute__ ((unused))

#define	PROTO(x)	__dso_hidden typeof(x) x asm("__"#x)
#define	DEF(x)		__strong_alias(x, __##x)

PROTO(fuse_daemonize);
PROTO(fuse_destroy);
PROTO(fuse_get_context);
PROTO(fuse_get_session);
PROTO(fuse_loop);
PROTO(fuse_mount);
PROTO(fuse_new);
PROTO(fuse_opt_add_arg);
PROTO(fuse_opt_free_args);
PROTO(fuse_opt_insert_arg);
PROTO(fuse_opt_match);
PROTO(fuse_opt_parse);
PROTO(fuse_parse_cmdline);
PROTO(fuse_remove_signal_handlers);
PROTO(fuse_setup);
PROTO(fuse_unmount);

#endif /* _FUSE_SUBR_ */
