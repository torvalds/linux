/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __DIR_DOT_H__
#define __DIR_DOT_H__

#include <linux/dcache.h>
#include <linux/crc32.h>

struct inode;
struct gfs2_inode;
struct gfs2_inum;
struct buffer_head;
struct gfs2_dirent;

struct gfs2_diradd {
	unsigned nr_blocks;
	struct gfs2_dirent *dent;
	struct buffer_head *bh;
	int save_loc;
};

extern struct inode *gfs2_dir_search(struct inode *dir,
				     const struct qstr *filename,
				     bool fail_on_exist);
extern int gfs2_dir_check(struct inode *dir, const struct qstr *filename,
			  const struct gfs2_inode *ip);
extern int gfs2_dir_add(struct inode *inode, const struct qstr *filename,
			const struct gfs2_inode *ip, struct gfs2_diradd *da);
static inline void gfs2_dir_no_add(struct gfs2_diradd *da)
{
	brelse(da->bh);
	da->bh = NULL;
}
extern int gfs2_dir_del(struct gfs2_inode *dip, const struct dentry *dentry);
extern int gfs2_dir_read(struct inode *inode, struct dir_context *ctx,
			 struct file_ra_state *f_ra);
extern int gfs2_dir_mvino(struct gfs2_inode *dip, const struct qstr *filename,
			  const struct gfs2_inode *nip, unsigned int new_type);

extern int gfs2_dir_exhash_dealloc(struct gfs2_inode *dip);

extern int gfs2_diradd_alloc_required(struct inode *dir,
				      const struct qstr *filename,
				      struct gfs2_diradd *da);
extern int gfs2_dir_get_new_buffer(struct gfs2_inode *ip, u64 block,
				   struct buffer_head **bhp);
extern void gfs2_dir_hash_inval(struct gfs2_inode *ip);

static inline u32 gfs2_disk_hash(const char *data, int len)
{
        return crc32_le((u32)~0, data, len) ^ (u32)~0;
}


static inline void gfs2_str2qstr(struct qstr *name, const char *fname)
{
	name->name = fname;
	name->len = strlen(fname);
	name->hash = gfs2_disk_hash(name->name, name->len);
}

/* N.B. This probably ought to take inum & type as args as well */
static inline void gfs2_qstr2dirent(const struct qstr *name, u16 reclen, struct gfs2_dirent *dent)
{
	dent->de_inum.no_addr = cpu_to_be64(0);
	dent->de_inum.no_formal_ino = cpu_to_be64(0);
	dent->de_hash = cpu_to_be32(name->hash);
	dent->de_rec_len = cpu_to_be16(reclen);
	dent->de_name_len = cpu_to_be16(name->len);
	dent->de_type = cpu_to_be16(0);
	memset(dent->__pad, 0, sizeof(dent->__pad));
	memcpy(dent + 1, name->name, name->len);
}

extern struct qstr gfs2_qdot;
extern struct qstr gfs2_qdotdot;

#endif /* __DIR_DOT_H__ */
