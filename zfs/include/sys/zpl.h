/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 */

#ifndef	_SYS_ZPL_H
#define	_SYS_ZPL_H

#include <sys/mntent.h>
#include <sys/vfs.h>
#include <linux/aio.h>
#include <linux/dcache_compat.h>
#include <linux/exportfs.h>
#include <linux/falloc.h>
#include <linux/file_compat.h>
#include <linux/parser.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/vfs_compat.h>
#include <linux/writeback.h>
#include <linux/xattr_compat.h>

/* zpl_inode.c */
extern void zpl_vap_init(vattr_t *vap, struct inode *dir,
    zpl_umode_t mode, cred_t *cr);

extern const struct inode_operations zpl_inode_operations;
extern const struct inode_operations zpl_dir_inode_operations;
extern const struct inode_operations zpl_symlink_inode_operations;
extern const struct inode_operations zpl_special_inode_operations;
extern dentry_operations_t zpl_dentry_operations;

/* zpl_file.c */
extern ssize_t zpl_read_common(struct inode *ip, const char *buf,
    size_t len, loff_t *ppos, uio_seg_t segment, int flags,
    cred_t *cr);
extern ssize_t zpl_write_common(struct inode *ip, const char *buf,
    size_t len, loff_t *ppos, uio_seg_t segment, int flags,
    cred_t *cr);
#if defined(HAVE_FILE_FALLOCATE) || defined(HAVE_INODE_FALLOCATE)
extern long zpl_fallocate_common(struct inode *ip, int mode,
    loff_t offset, loff_t len);
#endif /* defined(HAVE_FILE_FALLOCATE) || defined(HAVE_INODE_FALLOCATE) */

extern const struct address_space_operations zpl_address_space_operations;
extern const struct file_operations zpl_file_operations;
extern const struct file_operations zpl_dir_file_operations;

/* zpl_super.c */
extern void zpl_prune_sb(int64_t nr_to_scan, void *arg);

extern const struct super_operations zpl_super_operations;
extern const struct export_operations zpl_export_operations;
extern struct file_system_type zpl_fs_type;

/* zpl_xattr.c */
extern ssize_t zpl_xattr_list(struct dentry *dentry, char *buf, size_t size);
extern int zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr);
#if defined(CONFIG_FS_POSIX_ACL)
extern int zpl_set_acl(struct inode *ip, struct posix_acl *acl, int type);
extern struct posix_acl *zpl_get_acl(struct inode *ip, int type);
#if !defined(HAVE_GET_ACL)
#if defined(HAVE_CHECK_ACL_WITH_FLAGS)
extern int zpl_check_acl(struct inode *inode, int mask, unsigned int flags);
#elif defined(HAVE_CHECK_ACL)
extern int zpl_check_acl(struct inode *inode, int mask);
#elif defined(HAVE_PERMISSION_WITH_NAMEIDATA)
extern int zpl_permission(struct inode *ip, int mask, struct nameidata *nd);
#elif defined(HAVE_PERMISSION)
extern int zpl_permission(struct inode *ip, int mask);
#endif /*  HAVE_CHECK_ACL | HAVE_PERMISSION */
#endif /* HAVE_GET_ACL */

extern int zpl_init_acl(struct inode *ip, struct inode *dir);
extern int zpl_chmod_acl(struct inode *ip);
#else
static inline int
zpl_init_acl(struct inode *ip, struct inode *dir)
{
	return (0);
}

static inline int
zpl_chmod_acl(struct inode *ip)
{
	return (0);
}
#endif /* CONFIG_FS_POSIX_ACL */

extern xattr_handler_t *zpl_xattr_handlers[];

/* zpl_ctldir.c */
extern const struct file_operations zpl_fops_root;
extern const struct inode_operations zpl_ops_root;

extern const struct file_operations zpl_fops_snapdir;
extern const struct inode_operations zpl_ops_snapdir;
#ifdef HAVE_AUTOMOUNT
extern const struct dentry_operations zpl_dops_snapdirs;
#else
extern const struct inode_operations zpl_ops_snapdirs;
#endif /* HAVE_AUTOMOUNT */

extern const struct file_operations zpl_fops_shares;
extern const struct inode_operations zpl_ops_shares;

#if defined(HAVE_VFS_ITERATE) || defined(HAVE_VFS_ITERATE_SHARED)

#define	DIR_CONTEXT_INIT(_dirent, _actor, _pos) {	\
	.actor = _actor,				\
	.pos = _pos,					\
}

#else

typedef struct dir_context {
	void *dirent;
	const filldir_t actor;
	loff_t pos;
} dir_context_t;

#define	DIR_CONTEXT_INIT(_dirent, _actor, _pos) {	\
	.dirent = _dirent,				\
	.actor = _actor,				\
	.pos = _pos,					\
}

static inline bool
dir_emit(struct dir_context *ctx, const char *name, int namelen,
    uint64_t ino, unsigned type)
{
	return (!ctx->actor(ctx->dirent, name, namelen, ctx->pos, ino, type));
}

static inline bool
dir_emit_dot(struct file *file, struct dir_context *ctx)
{
	return (ctx->actor(ctx->dirent, ".", 1, ctx->pos,
	    file_inode(file)->i_ino, DT_DIR) == 0);
}

static inline bool
dir_emit_dotdot(struct file *file, struct dir_context *ctx)
{
	return (ctx->actor(ctx->dirent, "..", 2, ctx->pos,
	    parent_ino(file_dentry(file)), DT_DIR) == 0);
}

static inline bool
dir_emit_dots(struct file *file, struct dir_context *ctx)
{
	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return (false);
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(file, ctx))
			return (false);
		ctx->pos = 2;
	}
	return (true);
}
#endif /* HAVE_VFS_ITERATE */

#endif	/* _SYS_ZPL_H */
