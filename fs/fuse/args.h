/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FS_FUSE_ARGS_H
#define _FS_FUSE_ARGS_H

#include <linux/types.h>

struct fuse_mount;

/** One input argument of a request */
struct fuse_in_arg {
	unsigned size;
	const void *value;
};

/** One output argument of a request */
struct fuse_arg {
	unsigned size;
	void *value;
};

struct fuse_args {
	u64 nodeid;
	u32 opcode;
	u32 uid;
	u32 gid;
	u32 pid;
	u8 in_numargs;
	u8 out_numargs;
	u8 ext_idx;
	bool force:1;
	bool noreply:1;
	bool nocreds:1;
	bool in_pages:1;
	bool out_pages:1;
	bool user_pages:1;
	bool out_argvar:1;
	bool page_zeroing:1;
	bool page_replace:1;
	bool may_block:1;
	bool is_ext:1;
	bool is_pinned:1;
	bool invalidate_vmap:1;
	bool abort_on_kill:1;
	struct fuse_in_arg in_args[4];
	struct fuse_arg out_args[2];
	void (*end)(struct fuse_args *args, int error);
	/* Used for kvec iter backed by vmalloc address */
	void *vmap_base;
};

/** FUSE folio descriptor */
struct fuse_folio_desc {
	unsigned int length;
	unsigned int offset;
};

struct fuse_args_pages {
	struct fuse_args args;
	struct folio **folios;
	struct fuse_folio_desc *descs;
	unsigned int num_folios;
};

#endif /* _FS_FUSE_ARGS_H */
