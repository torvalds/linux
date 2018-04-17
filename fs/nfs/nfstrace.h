/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfs

#if !defined(_TRACE_NFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NFS_H

#include <linux/tracepoint.h>
#include <linux/iversion.h>

#define nfs_show_file_type(ftype) \
	__print_symbolic(ftype, \
			{ DT_UNKNOWN, "UNKNOWN" }, \
			{ DT_FIFO, "FIFO" }, \
			{ DT_CHR, "CHR" }, \
			{ DT_DIR, "DIR" }, \
			{ DT_BLK, "BLK" }, \
			{ DT_REG, "REG" }, \
			{ DT_LNK, "LNK" }, \
			{ DT_SOCK, "SOCK" }, \
			{ DT_WHT, "WHT" })

#define nfs_show_cache_validity(v) \
	__print_flags(v, "|", \
			{ NFS_INO_INVALID_ATTR, "INVALID_ATTR" }, \
			{ NFS_INO_INVALID_DATA, "INVALID_DATA" }, \
			{ NFS_INO_INVALID_ATIME, "INVALID_ATIME" }, \
			{ NFS_INO_INVALID_ACCESS, "INVALID_ACCESS" }, \
			{ NFS_INO_INVALID_ACL, "INVALID_ACL" }, \
			{ NFS_INO_REVAL_PAGECACHE, "REVAL_PAGECACHE" }, \
			{ NFS_INO_REVAL_FORCED, "REVAL_FORCED" }, \
			{ NFS_INO_INVALID_LABEL, "INVALID_LABEL" })

#define nfs_show_nfsi_flags(v) \
	__print_flags(v, "|", \
			{ 1 << NFS_INO_ADVISE_RDPLUS, "ADVISE_RDPLUS" }, \
			{ 1 << NFS_INO_STALE, "STALE" }, \
			{ 1 << NFS_INO_INVALIDATING, "INVALIDATING" }, \
			{ 1 << NFS_INO_FSCACHE, "FSCACHE" }, \
			{ 1 << NFS_INO_LAYOUTCOMMIT, "NEED_LAYOUTCOMMIT" }, \
			{ 1 << NFS_INO_LAYOUTCOMMITTING, "LAYOUTCOMMIT" })

DECLARE_EVENT_CLASS(nfs_inode_event,
		TP_PROTO(
			const struct inode *inode
		),

		TP_ARGS(inode),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(u64, version)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
			__entry->version = inode_peek_iversion_raw(inode);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x version=%llu ",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(unsigned long long)__entry->version
		)
);

DECLARE_EVENT_CLASS(nfs_inode_event_done,
		TP_PROTO(
			const struct inode *inode,
			int error
		),

		TP_ARGS(inode, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(unsigned char, type)
			__field(u64, fileid)
			__field(u64, version)
			__field(loff_t, size)
			__field(unsigned long, nfsi_flags)
			__field(unsigned long, cache_validity)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);
			__entry->error = error;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
			__entry->type = nfs_umode_to_dtype(inode->i_mode);
			__entry->version = inode_peek_iversion_raw(inode);
			__entry->size = i_size_read(inode);
			__entry->nfsi_flags = nfsi->flags;
			__entry->cache_validity = nfsi->cache_validity;
		),

		TP_printk(
			"error=%d fileid=%02x:%02x:%llu fhandle=0x%08x "
			"type=%u (%s) version=%llu size=%lld "
			"cache_validity=%lu (%s) nfs_flags=%ld (%s)",
			__entry->error,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->type,
			nfs_show_file_type(__entry->type),
			(unsigned long long)__entry->version,
			(long long)__entry->size,
			__entry->cache_validity,
			nfs_show_cache_validity(__entry->cache_validity),
			__entry->nfsi_flags,
			nfs_show_nfsi_flags(__entry->nfsi_flags)
		)
);

#define DEFINE_NFS_INODE_EVENT(name) \
	DEFINE_EVENT(nfs_inode_event, name, \
			TP_PROTO( \
				const struct inode *inode \
			), \
			TP_ARGS(inode))
#define DEFINE_NFS_INODE_EVENT_DONE(name) \
	DEFINE_EVENT(nfs_inode_event_done, name, \
			TP_PROTO( \
				const struct inode *inode, \
				int error \
			), \
			TP_ARGS(inode, error))
DEFINE_NFS_INODE_EVENT(nfs_refresh_inode_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_refresh_inode_exit);
DEFINE_NFS_INODE_EVENT(nfs_revalidate_inode_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_revalidate_inode_exit);
DEFINE_NFS_INODE_EVENT(nfs_invalidate_mapping_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_invalidate_mapping_exit);
DEFINE_NFS_INODE_EVENT(nfs_getattr_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_getattr_exit);
DEFINE_NFS_INODE_EVENT(nfs_setattr_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_setattr_exit);
DEFINE_NFS_INODE_EVENT(nfs_writeback_page_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_writeback_page_exit);
DEFINE_NFS_INODE_EVENT(nfs_writeback_inode_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_writeback_inode_exit);
DEFINE_NFS_INODE_EVENT(nfs_fsync_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_fsync_exit);
DEFINE_NFS_INODE_EVENT(nfs_access_enter);
DEFINE_NFS_INODE_EVENT_DONE(nfs_access_exit);

#define show_lookup_flags(flags) \
	__print_flags((unsigned long)flags, "|", \
			{ LOOKUP_AUTOMOUNT, "AUTOMOUNT" }, \
			{ LOOKUP_DIRECTORY, "DIRECTORY" }, \
			{ LOOKUP_OPEN, "OPEN" }, \
			{ LOOKUP_CREATE, "CREATE" }, \
			{ LOOKUP_EXCL, "EXCL" })

DECLARE_EVENT_CLASS(nfs_lookup_event,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry,
			unsigned int flags
		),

		TP_ARGS(dir, dentry, flags),

		TP_STRUCT__entry(
			__field(unsigned int, flags)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->flags = flags;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"flags=%u (%s) name=%02x:%02x:%llu/%s",
			__entry->flags,
			show_lookup_flags(__entry->flags),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

#define DEFINE_NFS_LOOKUP_EVENT(name) \
	DEFINE_EVENT(nfs_lookup_event, name, \
			TP_PROTO( \
				const struct inode *dir, \
				const struct dentry *dentry, \
				unsigned int flags \
			), \
			TP_ARGS(dir, dentry, flags))

DECLARE_EVENT_CLASS(nfs_lookup_event_done,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry,
			unsigned int flags,
			int error
		),

		TP_ARGS(dir, dentry, flags, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(unsigned int, flags)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->error = error;
			__entry->flags = flags;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"error=%d flags=%u (%s) name=%02x:%02x:%llu/%s",
			__entry->error,
			__entry->flags,
			show_lookup_flags(__entry->flags),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

#define DEFINE_NFS_LOOKUP_EVENT_DONE(name) \
	DEFINE_EVENT(nfs_lookup_event_done, name, \
			TP_PROTO( \
				const struct inode *dir, \
				const struct dentry *dentry, \
				unsigned int flags, \
				int error \
			), \
			TP_ARGS(dir, dentry, flags, error))

DEFINE_NFS_LOOKUP_EVENT(nfs_lookup_enter);
DEFINE_NFS_LOOKUP_EVENT_DONE(nfs_lookup_exit);
DEFINE_NFS_LOOKUP_EVENT(nfs_lookup_revalidate_enter);
DEFINE_NFS_LOOKUP_EVENT_DONE(nfs_lookup_revalidate_exit);

#define show_open_flags(flags) \
	__print_flags((unsigned long)flags, "|", \
		{ O_CREAT, "O_CREAT" }, \
		{ O_EXCL, "O_EXCL" }, \
		{ O_TRUNC, "O_TRUNC" }, \
		{ O_APPEND, "O_APPEND" }, \
		{ O_DSYNC, "O_DSYNC" }, \
		{ O_DIRECT, "O_DIRECT" }, \
		{ O_DIRECTORY, "O_DIRECTORY" })

#define show_fmode_flags(mode) \
	__print_flags(mode, "|", \
		{ ((__force unsigned long)FMODE_READ), "READ" }, \
		{ ((__force unsigned long)FMODE_WRITE), "WRITE" }, \
		{ ((__force unsigned long)FMODE_EXEC), "EXEC" })

TRACE_EVENT(nfs_atomic_open_enter,
		TP_PROTO(
			const struct inode *dir,
			const struct nfs_open_context *ctx,
			unsigned int flags
		),

		TP_ARGS(dir, ctx, flags),

		TP_STRUCT__entry(
			__field(unsigned int, flags)
			__field(unsigned int, fmode)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, ctx->dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->flags = flags;
			__entry->fmode = (__force unsigned int)ctx->mode;
			__assign_str(name, ctx->dentry->d_name.name);
		),

		TP_printk(
			"flags=%u (%s) fmode=%s name=%02x:%02x:%llu/%s",
			__entry->flags,
			show_open_flags(__entry->flags),
			show_fmode_flags(__entry->fmode),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

TRACE_EVENT(nfs_atomic_open_exit,
		TP_PROTO(
			const struct inode *dir,
			const struct nfs_open_context *ctx,
			unsigned int flags,
			int error
		),

		TP_ARGS(dir, ctx, flags, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(unsigned int, flags)
			__field(unsigned int, fmode)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, ctx->dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->error = error;
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->flags = flags;
			__entry->fmode = (__force unsigned int)ctx->mode;
			__assign_str(name, ctx->dentry->d_name.name);
		),

		TP_printk(
			"error=%d flags=%u (%s) fmode=%s "
			"name=%02x:%02x:%llu/%s",
			__entry->error,
			__entry->flags,
			show_open_flags(__entry->flags),
			show_fmode_flags(__entry->fmode),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

TRACE_EVENT(nfs_create_enter,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry,
			unsigned int flags
		),

		TP_ARGS(dir, dentry, flags),

		TP_STRUCT__entry(
			__field(unsigned int, flags)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->flags = flags;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"flags=%u (%s) name=%02x:%02x:%llu/%s",
			__entry->flags,
			show_open_flags(__entry->flags),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

TRACE_EVENT(nfs_create_exit,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry,
			unsigned int flags,
			int error
		),

		TP_ARGS(dir, dentry, flags, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(unsigned int, flags)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->error = error;
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->flags = flags;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"error=%d flags=%u (%s) name=%02x:%02x:%llu/%s",
			__entry->error,
			__entry->flags,
			show_open_flags(__entry->flags),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

DECLARE_EVENT_CLASS(nfs_directory_event,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry
		),

		TP_ARGS(dir, dentry),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"name=%02x:%02x:%llu/%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

#define DEFINE_NFS_DIRECTORY_EVENT(name) \
	DEFINE_EVENT(nfs_directory_event, name, \
			TP_PROTO( \
				const struct inode *dir, \
				const struct dentry *dentry \
			), \
			TP_ARGS(dir, dentry))

DECLARE_EVENT_CLASS(nfs_directory_event_done,
		TP_PROTO(
			const struct inode *dir,
			const struct dentry *dentry,
			int error
		),

		TP_ARGS(dir, dentry, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(dev_t, dev)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->error = error;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"error=%d name=%02x:%02x:%llu/%s",
			__entry->error,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

#define DEFINE_NFS_DIRECTORY_EVENT_DONE(name) \
	DEFINE_EVENT(nfs_directory_event_done, name, \
			TP_PROTO( \
				const struct inode *dir, \
				const struct dentry *dentry, \
				int error \
			), \
			TP_ARGS(dir, dentry, error))

DEFINE_NFS_DIRECTORY_EVENT(nfs_mknod_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_mknod_exit);
DEFINE_NFS_DIRECTORY_EVENT(nfs_mkdir_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_mkdir_exit);
DEFINE_NFS_DIRECTORY_EVENT(nfs_rmdir_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_rmdir_exit);
DEFINE_NFS_DIRECTORY_EVENT(nfs_remove_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_remove_exit);
DEFINE_NFS_DIRECTORY_EVENT(nfs_unlink_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_unlink_exit);
DEFINE_NFS_DIRECTORY_EVENT(nfs_symlink_enter);
DEFINE_NFS_DIRECTORY_EVENT_DONE(nfs_symlink_exit);

TRACE_EVENT(nfs_link_enter,
		TP_PROTO(
			const struct inode *inode,
			const struct inode *dir,
			const struct dentry *dentry
		),

		TP_ARGS(inode, dir, dentry),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, fileid)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->dir = NFS_FILEID(dir);
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu name=%02x:%02x:%llu/%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			__entry->fileid,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

TRACE_EVENT(nfs_link_exit,
		TP_PROTO(
			const struct inode *inode,
			const struct inode *dir,
			const struct dentry *dentry,
			int error
		),

		TP_ARGS(inode, dir, dentry, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(dev_t, dev)
			__field(u64, fileid)
			__field(u64, dir)
			__string(name, dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->dir = NFS_FILEID(dir);
			__entry->error = error;
			__assign_str(name, dentry->d_name.name);
		),

		TP_printk(
			"error=%d fileid=%02x:%02x:%llu name=%02x:%02x:%llu/%s",
			__entry->error,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			__entry->fileid,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

DECLARE_EVENT_CLASS(nfs_rename_event,
		TP_PROTO(
			const struct inode *old_dir,
			const struct dentry *old_dentry,
			const struct inode *new_dir,
			const struct dentry *new_dentry
		),

		TP_ARGS(old_dir, old_dentry, new_dir, new_dentry),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, old_dir)
			__field(u64, new_dir)
			__string(old_name, old_dentry->d_name.name)
			__string(new_name, new_dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = old_dir->i_sb->s_dev;
			__entry->old_dir = NFS_FILEID(old_dir);
			__entry->new_dir = NFS_FILEID(new_dir);
			__assign_str(old_name, old_dentry->d_name.name);
			__assign_str(new_name, new_dentry->d_name.name);
		),

		TP_printk(
			"old_name=%02x:%02x:%llu/%s new_name=%02x:%02x:%llu/%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->old_dir,
			__get_str(old_name),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->new_dir,
			__get_str(new_name)
		)
);
#define DEFINE_NFS_RENAME_EVENT(name) \
	DEFINE_EVENT(nfs_rename_event, name, \
			TP_PROTO( \
				const struct inode *old_dir, \
				const struct dentry *old_dentry, \
				const struct inode *new_dir, \
				const struct dentry *new_dentry \
			), \
			TP_ARGS(old_dir, old_dentry, new_dir, new_dentry))

DECLARE_EVENT_CLASS(nfs_rename_event_done,
		TP_PROTO(
			const struct inode *old_dir,
			const struct dentry *old_dentry,
			const struct inode *new_dir,
			const struct dentry *new_dentry,
			int error
		),

		TP_ARGS(old_dir, old_dentry, new_dir, new_dentry, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(int, error)
			__field(u64, old_dir)
			__string(old_name, old_dentry->d_name.name)
			__field(u64, new_dir)
			__string(new_name, new_dentry->d_name.name)
		),

		TP_fast_assign(
			__entry->dev = old_dir->i_sb->s_dev;
			__entry->old_dir = NFS_FILEID(old_dir);
			__entry->new_dir = NFS_FILEID(new_dir);
			__entry->error = error;
			__assign_str(old_name, old_dentry->d_name.name);
			__assign_str(new_name, new_dentry->d_name.name);
		),

		TP_printk(
			"error=%d old_name=%02x:%02x:%llu/%s "
			"new_name=%02x:%02x:%llu/%s",
			__entry->error,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->old_dir,
			__get_str(old_name),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->new_dir,
			__get_str(new_name)
		)
);
#define DEFINE_NFS_RENAME_EVENT_DONE(name) \
	DEFINE_EVENT(nfs_rename_event_done, name, \
			TP_PROTO( \
				const struct inode *old_dir, \
				const struct dentry *old_dentry, \
				const struct inode *new_dir, \
				const struct dentry *new_dentry, \
				int error \
			), \
			TP_ARGS(old_dir, old_dentry, new_dir, \
				new_dentry, error))

DEFINE_NFS_RENAME_EVENT(nfs_rename_enter);
DEFINE_NFS_RENAME_EVENT_DONE(nfs_rename_exit);

DEFINE_NFS_RENAME_EVENT_DONE(nfs_sillyrename_rename);

TRACE_EVENT(nfs_sillyrename_unlink,
		TP_PROTO(
			const struct nfs_unlinkdata *data,
			int error
		),

		TP_ARGS(data, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(int, error)
			__field(u64, dir)
			__dynamic_array(char, name, data->args.name.len + 1)
		),

		TP_fast_assign(
			struct inode *dir = d_inode(data->dentry->d_parent);
			size_t len = data->args.name.len;
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->error = error;
			memcpy(__get_str(name),
				data->args.name.name, len);
			__get_str(name)[len] = 0;
		),

		TP_printk(
			"error=%d name=%02x:%02x:%llu/%s",
			__entry->error,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

TRACE_EVENT(nfs_initiate_read,
		TP_PROTO(
			const struct inode *inode,
			loff_t offset, unsigned long count
		),

		TP_ARGS(inode, offset, count),

		TP_STRUCT__entry(
			__field(loff_t, offset)
			__field(unsigned long, count)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->offset = offset;
			__entry->count = count;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%lu",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->count
		)
);

TRACE_EVENT(nfs_readpage_done,
		TP_PROTO(
			const struct inode *inode,
			int status, loff_t offset, bool eof
		),

		TP_ARGS(inode, status, offset, eof),

		TP_STRUCT__entry(
			__field(int, status)
			__field(loff_t, offset)
			__field(bool, eof)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->status = status;
			__entry->offset = offset;
			__entry->eof = eof;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld status=%d%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->status,
			__entry->eof ? " eof" : ""
		)
);

TRACE_DEFINE_ENUM(NFS_UNSTABLE);
TRACE_DEFINE_ENUM(NFS_DATA_SYNC);
TRACE_DEFINE_ENUM(NFS_FILE_SYNC);

#define nfs_show_stable(stable) \
	__print_symbolic(stable, \
			{ NFS_UNSTABLE, "UNSTABLE" }, \
			{ NFS_DATA_SYNC, "DATA_SYNC" }, \
			{ NFS_FILE_SYNC, "FILE_SYNC" })

TRACE_EVENT(nfs_initiate_write,
		TP_PROTO(
			const struct inode *inode,
			loff_t offset, unsigned long count,
			enum nfs3_stable_how stable
		),

		TP_ARGS(inode, offset, count, stable),

		TP_STRUCT__entry(
			__field(loff_t, offset)
			__field(unsigned long, count)
			__field(enum nfs3_stable_how, stable)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->offset = offset;
			__entry->count = count;
			__entry->stable = stable;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%lu stable=%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->count,
			nfs_show_stable(__entry->stable)
		)
);

TRACE_EVENT(nfs_writeback_done,
		TP_PROTO(
			const struct inode *inode,
			int status,
			loff_t offset,
			struct nfs_writeverf *writeverf
		),

		TP_ARGS(inode, status, offset, writeverf),

		TP_STRUCT__entry(
			__field(int, status)
			__field(loff_t, offset)
			__field(enum nfs3_stable_how, stable)
			__field(unsigned long long, verifier)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->status = status;
			__entry->offset = offset;
			__entry->stable = writeverf->committed;
			memcpy(&__entry->verifier, &writeverf->verifier,
			       sizeof(__entry->verifier));
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld status=%d stable=%s "
			"verifier 0x%016llx",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->status,
			nfs_show_stable(__entry->stable),
			__entry->verifier
		)
);

TRACE_EVENT(nfs_initiate_commit,
		TP_PROTO(
			const struct nfs_commit_data *data
		),

		TP_ARGS(data),

		TP_STRUCT__entry(
			__field(loff_t, offset)
			__field(unsigned long, count)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct inode *inode = data->inode;
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->offset = data->args.offset;
			__entry->count = data->args.count;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%lu",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->count
		)
);

TRACE_EVENT(nfs_commit_done,
		TP_PROTO(
			const struct nfs_commit_data *data
		),

		TP_ARGS(data),

		TP_STRUCT__entry(
			__field(int, status)
			__field(loff_t, offset)
			__field(unsigned long long, verifier)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
		),

		TP_fast_assign(
			const struct inode *inode = data->inode;
			const struct nfs_inode *nfsi = NFS_I(inode);

			__entry->status = data->res.op_status;
			__entry->offset = data->args.offset;
			memcpy(&__entry->verifier, &data->verf.verifier,
			       sizeof(__entry->verifier));
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(&nfsi->fh);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld status=%d verifier 0x%016llx",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->status,
			__entry->verifier
		)
);

#endif /* _TRACE_NFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE nfstrace
/* This part must be outside protection */
#include <trace/define_trace.h>
