#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs

#if !defined(_TRACE_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_F2FS_H

#include <linux/tracepoint.h>

#define show_dev(entry)		MAJOR(entry->dev), MINOR(entry->dev)
#define show_dev_ino(entry)	show_dev(entry), (unsigned long)entry->ino

DECLARE_EVENT_CLASS(f2fs__inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(ino_t,	pino)
		__field(umode_t, mode)
		__field(loff_t,	size)
		__field(unsigned int, nlink)
		__field(blkcnt_t, blocks)
		__field(__u8,	advise)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pino	= F2FS_I(inode)->i_pino;
		__entry->mode	= inode->i_mode;
		__entry->nlink	= inode->i_nlink;
		__entry->size	= inode->i_size;
		__entry->blocks	= inode->i_blocks;
		__entry->advise	= F2FS_I(inode)->i_advise;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pino = %lu, i_mode = 0x%hx, "
		"i_size = %lld, i_nlink = %u, i_blocks = %llu, i_advise = 0x%x",
		show_dev_ino(__entry),
		(unsigned long)__entry->pino,
		__entry->mode,
		__entry->size,
		(unsigned int)__entry->nlink,
		(unsigned long long)__entry->blocks,
		(unsigned char)__entry->advise)
);

DECLARE_EVENT_CLASS(f2fs__inode_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, ret = %d",
		show_dev_ino(__entry),
		__entry->ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_sync_file_enter,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(f2fs_sync_file_exit,

	TP_PROTO(struct inode *inode, bool need_cp, int datasync, int ret),

	TP_ARGS(inode, need_cp, datasync, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(bool,	need_cp)
		__field(int,	datasync)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->need_cp	= need_cp;
		__entry->datasync	= datasync;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, checkpoint is %s, "
		"datasync = %d, ret = %d",
		show_dev_ino(__entry),
		__entry->need_cp ? "needed" : "not needed",
		__entry->datasync,
		__entry->ret)
);

TRACE_EVENT(f2fs_sync_fs,

	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	dirty)
		__field(int,	wait)
	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->dirty	= F2FS_SB(sb)->s_dirty;
		__entry->wait	= wait;
	),

	TP_printk("dev = (%d,%d), superblock is %s, wait = %d",
		show_dev(__entry),
		__entry->dirty ? "dirty" : "not dirty",
		__entry->wait)
);

DEFINE_EVENT(f2fs__inode, f2fs_iget,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_iget_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_evict_inode,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(f2fs_unlink_enter,

	TP_PROTO(struct inode *dir, struct dentry *dentry),

	TP_ARGS(dir, dentry),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	size)
		__field(blkcnt_t, blocks)
		__field(const char *,	name)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__entry->size	= dir->i_size;
		__entry->blocks	= dir->i_blocks;
		__entry->name	= dentry->d_name.name;
	),

	TP_printk("dev = (%d,%d), dir ino = %lu, i_size = %lld, "
		"i_blocks = %llu, name = %s",
		show_dev_ino(__entry),
		__entry->size,
		(unsigned long long)__entry->blocks,
		__entry->name)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_unlink_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__inode, f2fs_truncate,

	TP_PROTO(struct inode *inode),

	TP_ARGS(inode)
);

TRACE_EVENT(f2fs_truncate_data_blocks_range,

	TP_PROTO(struct inode *inode, nid_t nid, unsigned int ofs, int free),

	TP_ARGS(inode, nid,  ofs, free),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid)
		__field(unsigned int,	ofs)
		__field(int,	free)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->nid	= nid;
		__entry->ofs	= ofs;
		__entry->free	= free;
	),

	TP_printk("dev = (%d,%d), ino = %lu, nid = %u, offset = %u, freed = %d",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid,
		__entry->ofs,
		__entry->free)
);

DECLARE_EVENT_CLASS(f2fs__truncate_op,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	size)
		__field(blkcnt_t, blocks)
		__field(u64,	from)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->size	= inode->i_size;
		__entry->blocks	= inode->i_blocks;
		__entry->from	= from;
	),

	TP_printk("dev = (%d,%d), ino = %lu, i_size = %lld, i_blocks = %llu, "
		"start file offset = %llu",
		show_dev_ino(__entry),
		__entry->size,
		(unsigned long long)__entry->blocks,
		(unsigned long long)__entry->from)
);

DEFINE_EVENT(f2fs__truncate_op, f2fs_truncate_blocks_enter,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_blocks_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__truncate_op, f2fs_truncate_inode_blocks_enter,

	TP_PROTO(struct inode *inode, u64 from),

	TP_ARGS(inode, from)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_inode_blocks_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DECLARE_EVENT_CLASS(f2fs__truncate_node,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid)
		__field(block_t,	blk_addr)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->nid		= nid;
		__entry->blk_addr	= blk_addr;
	),

	TP_printk("dev = (%d,%d), ino = %lu, nid = %u, block_address = 0x%llx",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid,
		(unsigned long long)__entry->blk_addr)
);

DEFINE_EVENT(f2fs__truncate_node, f2fs_truncate_nodes_enter,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr)
);

DEFINE_EVENT(f2fs__inode_exit, f2fs_truncate_nodes_exit,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
);

DEFINE_EVENT(f2fs__truncate_node, f2fs_truncate_node,

	TP_PROTO(struct inode *inode, nid_t nid, block_t blk_addr),

	TP_ARGS(inode, nid, blk_addr)
);

TRACE_EVENT(f2fs_truncate_partial_nodes,

	TP_PROTO(struct inode *inode, nid_t nid[], int depth, int err),

	TP_ARGS(inode, nid, depth, err),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(nid_t,	nid[3])
		__field(int,	depth)
		__field(int,	err)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->nid[0]	= nid[0];
		__entry->nid[1]	= nid[1];
		__entry->nid[2]	= nid[2];
		__entry->depth	= depth;
		__entry->err	= err;
	),

	TP_printk("dev = (%d,%d), ino = %lu, "
		"nid[0] = %u, nid[1] = %u, nid[2] = %u, depth = %d, err = %d",
		show_dev_ino(__entry),
		(unsigned int)__entry->nid[0],
		(unsigned int)__entry->nid[1],
		(unsigned int)__entry->nid[2],
		__entry->depth,
		__entry->err)
);

#endif /* _TRACE_F2FS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>
