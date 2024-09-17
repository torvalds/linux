/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hugetlbfs

#if !defined(_TRACE_HUGETLBFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HUGETLBFS_H

#include <linux/tracepoint.h>

TRACE_EVENT(hugetlbfs_alloc_inode,

	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(ino_t,		dir)
		__field(__u16,		mode)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->dir		= dir->i_ino;
		__entry->mode		= mode;
	),

	TP_printk("dev %d,%d ino %lu dir %lu mode 0%o",
		MAJOR(__entry->dev), MINOR(__entry->dev),
		(unsigned long) __entry->ino,
		(unsigned long) __entry->dir, __entry->mode)
);

DECLARE_EVENT_CLASS(hugetlbfs__inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(__u16,		mode)
		__field(loff_t,		size)
		__field(unsigned int,	nlink)
		__field(unsigned int,	seals)
		__field(blkcnt_t,	blocks)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->mode		= inode->i_mode;
		__entry->size		= inode->i_size;
		__entry->nlink		= inode->i_nlink;
		__entry->seals		= HUGETLBFS_I(inode)->seals;
		__entry->blocks		= inode->i_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o size %lld nlink %u seals %u blocks %llu",
		MAJOR(__entry->dev), MINOR(__entry->dev), (unsigned long) __entry->ino,
		__entry->mode, __entry->size, __entry->nlink, __entry->seals,
		(unsigned long long)__entry->blocks)
);

DEFINE_EVENT(hugetlbfs__inode, hugetlbfs_evict_inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(hugetlbfs__inode, hugetlbfs_free_inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(hugetlbfs_setattr,

	TP_PROTO(struct inode *inode, struct dentry *dentry,
		struct iattr *attr),

	TP_ARGS(inode, dentry, attr),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(unsigned int,	d_len)
		__string(d_name,	dentry->d_name.name)
		__field(unsigned int,	ia_valid)
		__field(unsigned int,	ia_mode)
		__field(loff_t,		old_size)
		__field(loff_t,		ia_size)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->d_len		= dentry->d_name.len;
		__assign_str(d_name);
		__entry->ia_valid	= attr->ia_valid;
		__entry->ia_mode	= attr->ia_mode;
		__entry->old_size	= inode->i_size;
		__entry->ia_size	= attr->ia_size;
	),

	TP_printk("dev %d,%d ino %lu name %.*s valid %#x mode 0%o old_size %lld size %lld",
		MAJOR(__entry->dev), MINOR(__entry->dev), (unsigned long)__entry->ino,
		__entry->d_len, __get_str(d_name), __entry->ia_valid, __entry->ia_mode,
		__entry->old_size, __entry->ia_size)
);

TRACE_EVENT(hugetlbfs_fallocate,

	TP_PROTO(struct inode *inode, int mode,
		loff_t offset, loff_t len, int ret),

	TP_ARGS(inode, mode, offset, len, ret),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(int,		mode)
		__field(loff_t,		offset)
		__field(loff_t,		len)
		__field(loff_t,		size)
		__field(int,		ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->mode		= mode;
		__entry->offset		= offset;
		__entry->len		= len;
		__entry->size		= inode->i_size;
		__entry->ret		= ret;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o offset %lld len %lld size %lld ret %d",
		MAJOR(__entry->dev), MINOR(__entry->dev),
		(unsigned long)__entry->ino, __entry->mode,
		(unsigned long long)__entry->offset,
		(unsigned long long)__entry->len,
		(unsigned long long)__entry->size,
		__entry->ret)
);

#endif /* _TRACE_HUGETLBFS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>
