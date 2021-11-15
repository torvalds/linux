/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs

#if !defined(_TRACE_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_F2FS_H

#include <linux/tracepoint.h>
#include <uapi/linux/f2fs.h>

#define show_dev(dev)		MAJOR(dev), MINOR(dev)
#define show_dev_ino(entry)	show_dev(entry->dev), (unsigned long)entry->ino

TRACE_DEFINE_ENUM(NODE);
TRACE_DEFINE_ENUM(DATA);
TRACE_DEFINE_ENUM(META);
TRACE_DEFINE_ENUM(META_FLUSH);
TRACE_DEFINE_ENUM(INMEM);
TRACE_DEFINE_ENUM(INMEM_DROP);
TRACE_DEFINE_ENUM(INMEM_INVALIDATE);
TRACE_DEFINE_ENUM(INMEM_REVOKE);
TRACE_DEFINE_ENUM(IPU);
TRACE_DEFINE_ENUM(OPU);
TRACE_DEFINE_ENUM(HOT);
TRACE_DEFINE_ENUM(WARM);
TRACE_DEFINE_ENUM(COLD);
TRACE_DEFINE_ENUM(CURSEG_HOT_DATA);
TRACE_DEFINE_ENUM(CURSEG_WARM_DATA);
TRACE_DEFINE_ENUM(CURSEG_COLD_DATA);
TRACE_DEFINE_ENUM(CURSEG_HOT_NODE);
TRACE_DEFINE_ENUM(CURSEG_WARM_NODE);
TRACE_DEFINE_ENUM(CURSEG_COLD_NODE);
TRACE_DEFINE_ENUM(NO_CHECK_TYPE);
TRACE_DEFINE_ENUM(GC_GREEDY);
TRACE_DEFINE_ENUM(GC_CB);
TRACE_DEFINE_ENUM(FG_GC);
TRACE_DEFINE_ENUM(BG_GC);
TRACE_DEFINE_ENUM(LFS);
TRACE_DEFINE_ENUM(SSR);
TRACE_DEFINE_ENUM(__REQ_RAHEAD);
TRACE_DEFINE_ENUM(__REQ_SYNC);
TRACE_DEFINE_ENUM(__REQ_IDLE);
TRACE_DEFINE_ENUM(__REQ_PREFLUSH);
TRACE_DEFINE_ENUM(__REQ_FUA);
TRACE_DEFINE_ENUM(__REQ_PRIO);
TRACE_DEFINE_ENUM(__REQ_META);
TRACE_DEFINE_ENUM(CP_UMOUNT);
TRACE_DEFINE_ENUM(CP_FASTBOOT);
TRACE_DEFINE_ENUM(CP_SYNC);
TRACE_DEFINE_ENUM(CP_RECOVERY);
TRACE_DEFINE_ENUM(CP_DISCARD);
TRACE_DEFINE_ENUM(CP_TRIMMED);
TRACE_DEFINE_ENUM(CP_PAUSE);
TRACE_DEFINE_ENUM(CP_RESIZE);

#define show_block_type(type)						\
	__print_symbolic(type,						\
		{ NODE,		"NODE" },				\
		{ DATA,		"DATA" },				\
		{ META,		"META" },				\
		{ META_FLUSH,	"META_FLUSH" },				\
		{ INMEM,	"INMEM" },				\
		{ INMEM_DROP,	"INMEM_DROP" },				\
		{ INMEM_INVALIDATE,	"INMEM_INVALIDATE" },		\
		{ INMEM_REVOKE,	"INMEM_REVOKE" },			\
		{ IPU,		"IN-PLACE" },				\
		{ OPU,		"OUT-OF-PLACE" })

#define show_block_temp(temp)						\
	__print_symbolic(temp,						\
		{ HOT,		"HOT" },				\
		{ WARM,		"WARM" },				\
		{ COLD,		"COLD" })

#define F2FS_OP_FLAGS (REQ_RAHEAD | REQ_SYNC | REQ_META | REQ_PRIO |	\
			REQ_PREFLUSH | REQ_FUA)
#define F2FS_BIO_FLAG_MASK(t)	(t & F2FS_OP_FLAGS)

#define show_bio_type(op,op_flags)	show_bio_op(op),		\
						show_bio_op_flags(op_flags)

#define show_bio_op(op)		blk_op_str(op)

#define show_bio_op_flags(flags)					\
	__print_flags(F2FS_BIO_FLAG_MASK(flags), "|",			\
		{ REQ_RAHEAD,		"R" },				\
		{ REQ_SYNC,		"S" },				\
		{ REQ_META,		"M" },				\
		{ REQ_PRIO,		"P" },				\
		{ REQ_PREFLUSH,		"PF" },				\
		{ REQ_FUA,		"FUA" })

#define show_data_type(type)						\
	__print_symbolic(type,						\
		{ CURSEG_HOT_DATA, 	"Hot DATA" },			\
		{ CURSEG_WARM_DATA, 	"Warm DATA" },			\
		{ CURSEG_COLD_DATA, 	"Cold DATA" },			\
		{ CURSEG_HOT_NODE, 	"Hot NODE" },			\
		{ CURSEG_WARM_NODE, 	"Warm NODE" },			\
		{ CURSEG_COLD_NODE, 	"Cold NODE" },			\
		{ NO_CHECK_TYPE, 	"No TYPE" })

#define show_file_type(type)						\
	__print_symbolic(type,						\
		{ 0,		"FILE" },				\
		{ 1,		"DIR" })

#define show_gc_type(type)						\
	__print_symbolic(type,						\
		{ FG_GC,	"Foreground GC" },			\
		{ BG_GC,	"Background GC" })

#define show_alloc_mode(type)						\
	__print_symbolic(type,						\
		{ LFS,		"LFS-mode" },				\
		{ SSR,		"SSR-mode" },				\
		{ AT_SSR,	"AT_SSR-mode" })

#define show_victim_policy(type)					\
	__print_symbolic(type,						\
		{ GC_GREEDY,	"Greedy" },				\
		{ GC_CB,	"Cost-Benefit" },			\
		{ GC_AT,	"Age-threshold" })

#define show_cpreason(type)						\
	__print_flags(type, "|",					\
		{ CP_UMOUNT,	"Umount" },				\
		{ CP_FASTBOOT,	"Fastboot" },				\
		{ CP_SYNC,	"Sync" },				\
		{ CP_RECOVERY,	"Recovery" },				\
		{ CP_DISCARD,	"Discard" },				\
		{ CP_PAUSE,	"Pause" },				\
		{ CP_TRIMMED,	"Trimmed" },				\
		{ CP_RESIZE,	"Resize" })

#define show_fsync_cpreason(type)					\
	__print_symbolic(type,						\
		{ CP_NO_NEEDED,		"no needed" },			\
		{ CP_NON_REGULAR,	"non regular" },		\
		{ CP_COMPRESSED,	"compressed" },			\
		{ CP_HARDLINK,		"hardlink" },			\
		{ CP_SB_NEED_CP,	"sb needs cp" },		\
		{ CP_WRONG_PINO,	"wrong pino" },			\
		{ CP_NO_SPC_ROLL,	"no space roll forward" },	\
		{ CP_NODE_NEED_CP,	"node needs cp" },		\
		{ CP_FASTBOOT_MODE,	"fastboot mode" },		\
		{ CP_SPEC_LOG_NUM,	"log type is 2" },		\
		{ CP_RECOVER_DIR,	"dir needs recovery" })

#define show_shutdown_mode(type)					\
	__print_symbolic(type,						\
		{ F2FS_GOING_DOWN_FULLSYNC,	"full sync" },		\
		{ F2FS_GOING_DOWN_METASYNC,	"meta sync" },		\
		{ F2FS_GOING_DOWN_NOSYNC,	"no sync" },		\
		{ F2FS_GOING_DOWN_METAFLUSH,	"meta flush" },		\
		{ F2FS_GOING_DOWN_NEED_FSCK,	"need fsck" })

#define show_compress_algorithm(type)					\
	__print_symbolic(type,						\
		{ COMPRESS_LZO,		"LZO" },			\
		{ COMPRESS_LZ4,		"LZ4" },			\
		{ COMPRESS_ZSTD,	"ZSTD" },			\
		{ COMPRESS_LZORLE,	"LZO-RLE" })

struct f2fs_sb_info;
struct f2fs_io_info;
struct extent_info;
struct victim_sel_policy;
struct f2fs_map_blocks;

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

	TP_PROTO(struct inode *inode, int cp_reason, int datasync, int ret),

	TP_ARGS(inode, cp_reason, datasync, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	cp_reason)
		__field(int,	datasync)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->cp_reason	= cp_reason;
		__entry->datasync	= datasync;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, cp_reason: %s, "
		"datasync = %d, ret = %d",
		show_dev_ino(__entry),
		show_fsync_cpreason(__entry->cp_reason),
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
		__entry->dirty	= is_sbi_flag_set(F2FS_SB(sb), SBI_IS_DIRTY);
		__entry->wait	= wait;
	),

	TP_printk("dev = (%d,%d), superblock is %s, wait = %d",
		show_dev(__entry->dev),
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

DEFINE_EVENT(f2fs__inode_exit, f2fs_new_inode,

	TP_PROTO(struct inode *inode, int ret),

	TP_ARGS(inode, ret)
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

DEFINE_EVENT(f2fs__inode_exit, f2fs_drop_inode,

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

	TP_PROTO(struct inode *inode, nid_t *nid, int depth, int err),

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

TRACE_EVENT(f2fs_file_write_iter,

	TP_PROTO(struct inode *inode, unsigned long offset,
		unsigned long length, int ret),

	TP_ARGS(inode, offset, length, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(unsigned long, offset)
		__field(unsigned long, length)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->offset	= offset;
		__entry->length	= length;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, "
		"offset = %lu, length = %lu, written(err) = %d",
		show_dev_ino(__entry),
		__entry->offset,
		__entry->length,
		__entry->ret)
);

TRACE_EVENT(f2fs_map_blocks,
	TP_PROTO(struct inode *inode, struct f2fs_map_blocks *map,
				int create, int flag, int ret),

	TP_ARGS(inode, map, create, flag, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(block_t,	m_lblk)
		__field(block_t,	m_pblk)
		__field(unsigned int,	m_len)
		__field(unsigned int,	m_flags)
		__field(int,	m_seg_type)
		__field(bool,	m_may_create)
		__field(bool,	m_multidev_dio)
		__field(int,	create)
		__field(int,	flag)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev		= map->m_bdev->bd_dev;
		__entry->ino		= inode->i_ino;
		__entry->m_lblk		= map->m_lblk;
		__entry->m_pblk		= map->m_pblk;
		__entry->m_len		= map->m_len;
		__entry->m_flags	= map->m_flags;
		__entry->m_seg_type	= map->m_seg_type;
		__entry->m_may_create	= map->m_may_create;
		__entry->m_multidev_dio	= map->m_multidev_dio;
		__entry->create		= create;
		__entry->flag		= flag;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, file offset = %llu, "
		"start blkaddr = 0x%llx, len = 0x%llx, flags = %u, "
		"seg_type = %d, may_create = %d, multidevice = %d, "
		"create = %d, flag = %d, err = %d",
		show_dev_ino(__entry),
		(unsigned long long)__entry->m_lblk,
		(unsigned long long)__entry->m_pblk,
		(unsigned long long)__entry->m_len,
		__entry->m_flags,
		__entry->m_seg_type,
		__entry->m_may_create,
		__entry->m_multidev_dio,
		__entry->create,
		__entry->flag,
		__entry->ret)
);

TRACE_EVENT(f2fs_background_gc,

	TP_PROTO(struct super_block *sb, unsigned int wait_ms,
			unsigned int prefree, unsigned int free),

	TP_ARGS(sb, wait_ms, prefree, free),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned int,	wait_ms)
		__field(unsigned int,	prefree)
		__field(unsigned int,	free)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->wait_ms	= wait_ms;
		__entry->prefree	= prefree;
		__entry->free		= free;
	),

	TP_printk("dev = (%d,%d), wait_ms = %u, prefree = %u, free = %u",
		show_dev(__entry->dev),
		__entry->wait_ms,
		__entry->prefree,
		__entry->free)
);

TRACE_EVENT(f2fs_gc_begin,

	TP_PROTO(struct super_block *sb, bool sync, bool background,
			long long dirty_nodes, long long dirty_dents,
			long long dirty_imeta, unsigned int free_sec,
			unsigned int free_seg, int reserved_seg,
			unsigned int prefree_seg),

	TP_ARGS(sb, sync, background, dirty_nodes, dirty_dents, dirty_imeta,
		free_sec, free_seg, reserved_seg, prefree_seg),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(bool,		sync)
		__field(bool,		background)
		__field(long long,	dirty_nodes)
		__field(long long,	dirty_dents)
		__field(long long,	dirty_imeta)
		__field(unsigned int,	free_sec)
		__field(unsigned int,	free_seg)
		__field(int,		reserved_seg)
		__field(unsigned int,	prefree_seg)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->sync		= sync;
		__entry->background	= background;
		__entry->dirty_nodes	= dirty_nodes;
		__entry->dirty_dents	= dirty_dents;
		__entry->dirty_imeta	= dirty_imeta;
		__entry->free_sec	= free_sec;
		__entry->free_seg	= free_seg;
		__entry->reserved_seg	= reserved_seg;
		__entry->prefree_seg	= prefree_seg;
	),

	TP_printk("dev = (%d,%d), sync = %d, background = %d, nodes = %lld, "
		"dents = %lld, imeta = %lld, free_sec:%u, free_seg:%u, "
		"rsv_seg:%d, prefree_seg:%u",
		show_dev(__entry->dev),
		__entry->sync,
		__entry->background,
		__entry->dirty_nodes,
		__entry->dirty_dents,
		__entry->dirty_imeta,
		__entry->free_sec,
		__entry->free_seg,
		__entry->reserved_seg,
		__entry->prefree_seg)
);

TRACE_EVENT(f2fs_gc_end,

	TP_PROTO(struct super_block *sb, int ret, int seg_freed,
			int sec_freed, long long dirty_nodes,
			long long dirty_dents, long long dirty_imeta,
			unsigned int free_sec, unsigned int free_seg,
			int reserved_seg, unsigned int prefree_seg),

	TP_ARGS(sb, ret, seg_freed, sec_freed, dirty_nodes, dirty_dents,
		dirty_imeta, free_sec, free_seg, reserved_seg, prefree_seg),

	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(int,		ret)
		__field(int,		seg_freed)
		__field(int,		sec_freed)
		__field(long long,	dirty_nodes)
		__field(long long,	dirty_dents)
		__field(long long,	dirty_imeta)
		__field(unsigned int,	free_sec)
		__field(unsigned int,	free_seg)
		__field(int,		reserved_seg)
		__field(unsigned int,	prefree_seg)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->ret		= ret;
		__entry->seg_freed	= seg_freed;
		__entry->sec_freed	= sec_freed;
		__entry->dirty_nodes	= dirty_nodes;
		__entry->dirty_dents	= dirty_dents;
		__entry->dirty_imeta	= dirty_imeta;
		__entry->free_sec	= free_sec;
		__entry->free_seg	= free_seg;
		__entry->reserved_seg	= reserved_seg;
		__entry->prefree_seg	= prefree_seg;
	),

	TP_printk("dev = (%d,%d), ret = %d, seg_freed = %d, sec_freed = %d, "
		"nodes = %lld, dents = %lld, imeta = %lld, free_sec:%u, "
		"free_seg:%u, rsv_seg:%d, prefree_seg:%u",
		show_dev(__entry->dev),
		__entry->ret,
		__entry->seg_freed,
		__entry->sec_freed,
		__entry->dirty_nodes,
		__entry->dirty_dents,
		__entry->dirty_imeta,
		__entry->free_sec,
		__entry->free_seg,
		__entry->reserved_seg,
		__entry->prefree_seg)
);

TRACE_EVENT(f2fs_get_victim,

	TP_PROTO(struct super_block *sb, int type, int gc_type,
			struct victim_sel_policy *p, unsigned int pre_victim,
			unsigned int prefree, unsigned int free),

	TP_ARGS(sb, type, gc_type, p, pre_victim, prefree, free),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	type)
		__field(int,	gc_type)
		__field(int,	alloc_mode)
		__field(int,	gc_mode)
		__field(unsigned int,	victim)
		__field(unsigned int,	cost)
		__field(unsigned int,	ofs_unit)
		__field(unsigned int,	pre_victim)
		__field(unsigned int,	prefree)
		__field(unsigned int,	free)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->type		= type;
		__entry->gc_type	= gc_type;
		__entry->alloc_mode	= p->alloc_mode;
		__entry->gc_mode	= p->gc_mode;
		__entry->victim		= p->min_segno;
		__entry->cost		= p->min_cost;
		__entry->ofs_unit	= p->ofs_unit;
		__entry->pre_victim	= pre_victim;
		__entry->prefree	= prefree;
		__entry->free		= free;
	),

	TP_printk("dev = (%d,%d), type = %s, policy = (%s, %s, %s), "
		"victim = %u, cost = %u, ofs_unit = %u, "
		"pre_victim_secno = %d, prefree = %u, free = %u",
		show_dev(__entry->dev),
		show_data_type(__entry->type),
		show_gc_type(__entry->gc_type),
		show_alloc_mode(__entry->alloc_mode),
		show_victim_policy(__entry->gc_mode),
		__entry->victim,
		__entry->cost,
		__entry->ofs_unit,
		(int)__entry->pre_victim,
		__entry->prefree,
		__entry->free)
);

TRACE_EVENT(f2fs_lookup_start,

	TP_PROTO(struct inode *dir, struct dentry *dentry, unsigned int flags),

	TP_ARGS(dir, dentry, flags),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__string(name,	dentry->d_name.name)
		__field(unsigned int, flags)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__assign_str(name, dentry->d_name.name);
		__entry->flags	= flags;
	),

	TP_printk("dev = (%d,%d), pino = %lu, name:%s, flags:%u",
		show_dev_ino(__entry),
		__get_str(name),
		__entry->flags)
);

TRACE_EVENT(f2fs_lookup_end,

	TP_PROTO(struct inode *dir, struct dentry *dentry, nid_t ino,
		int err),

	TP_ARGS(dir, dentry, ino, err),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__string(name,	dentry->d_name.name)
		__field(nid_t,	cino)
		__field(int,	err)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__assign_str(name, dentry->d_name.name);
		__entry->cino	= ino;
		__entry->err	= err;
	),

	TP_printk("dev = (%d,%d), pino = %lu, name:%s, ino:%u, err:%d",
		show_dev_ino(__entry),
		__get_str(name),
		__entry->cino,
		__entry->err)
);

TRACE_EVENT(f2fs_readdir,

	TP_PROTO(struct inode *dir, loff_t start_pos, loff_t end_pos, int err),

	TP_ARGS(dir, start_pos, end_pos, err),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	start)
		__field(loff_t,	end)
		__field(int,	err)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->ino	= dir->i_ino;
		__entry->start	= start_pos;
		__entry->end	= end_pos;
		__entry->err	= err;
	),

	TP_printk("dev = (%d,%d), ino = %lu, start_pos:%llu, end_pos:%llu, err:%d",
		show_dev_ino(__entry),
		__entry->start,
		__entry->end,
		__entry->err)
);

TRACE_EVENT(f2fs_fallocate,

	TP_PROTO(struct inode *inode, int mode,
				loff_t offset, loff_t len, int ret),

	TP_ARGS(inode, mode, offset, len, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	mode)
		__field(loff_t,	offset)
		__field(loff_t,	len)
		__field(loff_t, size)
		__field(blkcnt_t, blocks)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= mode;
		__entry->offset	= offset;
		__entry->len	= len;
		__entry->size	= inode->i_size;
		__entry->blocks = inode->i_blocks;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, mode = %x, offset = %lld, "
		"len = %lld,  i_size = %lld, i_blocks = %llu, ret = %d",
		show_dev_ino(__entry),
		__entry->mode,
		(unsigned long long)__entry->offset,
		(unsigned long long)__entry->len,
		(unsigned long long)__entry->size,
		(unsigned long long)__entry->blocks,
		__entry->ret)
);

TRACE_EVENT(f2fs_direct_IO_enter,

	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len, int rw),

	TP_ARGS(inode, offset, len, rw),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	pos)
		__field(unsigned long,	len)
		__field(int,	rw)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= offset;
		__entry->len	= len;
		__entry->rw	= rw;
	),

	TP_printk("dev = (%d,%d), ino = %lu pos = %lld len = %lu rw = %d",
		show_dev_ino(__entry),
		__entry->pos,
		__entry->len,
		__entry->rw)
);

TRACE_EVENT(f2fs_direct_IO_exit,

	TP_PROTO(struct inode *inode, loff_t offset, unsigned long len,
		 int rw, int ret),

	TP_ARGS(inode, offset, len, rw, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	pos)
		__field(unsigned long,	len)
		__field(int,	rw)
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= offset;
		__entry->len	= len;
		__entry->rw	= rw;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu pos = %lld len = %lu "
		"rw = %d ret = %d",
		show_dev_ino(__entry),
		__entry->pos,
		__entry->len,
		__entry->rw,
		__entry->ret)
);

TRACE_EVENT(f2fs_reserve_new_blocks,

	TP_PROTO(struct inode *inode, nid_t nid, unsigned int ofs_in_node,
							blkcnt_t count),

	TP_ARGS(inode, nid, ofs_in_node, count),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(nid_t, nid)
		__field(unsigned int, ofs_in_node)
		__field(blkcnt_t, count)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->nid	= nid;
		__entry->ofs_in_node = ofs_in_node;
		__entry->count = count;
	),

	TP_printk("dev = (%d,%d), nid = %u, ofs_in_node = %u, count = %llu",
		show_dev(__entry->dev),
		(unsigned int)__entry->nid,
		__entry->ofs_in_node,
		(unsigned long long)__entry->count)
);

DECLARE_EVENT_CLASS(f2fs__submit_page_bio,

	TP_PROTO(struct page *page, struct f2fs_io_info *fio),

	TP_ARGS(page, fio),

	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(pgoff_t, index)
		__field(block_t, old_blkaddr)
		__field(block_t, new_blkaddr)
		__field(int, op)
		__field(int, op_flags)
		__field(int, temp)
		__field(int, type)
	),

	TP_fast_assign(
		__entry->dev		= page_file_mapping(page)->host->i_sb->s_dev;
		__entry->ino		= page_file_mapping(page)->host->i_ino;
		__entry->index		= page->index;
		__entry->old_blkaddr	= fio->old_blkaddr;
		__entry->new_blkaddr	= fio->new_blkaddr;
		__entry->op		= fio->op;
		__entry->op_flags	= fio->op_flags;
		__entry->temp		= fio->temp;
		__entry->type		= fio->type;
	),

	TP_printk("dev = (%d,%d), ino = %lu, page_index = 0x%lx, "
		"oldaddr = 0x%llx, newaddr = 0x%llx, rw = %s(%s), type = %s_%s",
		show_dev_ino(__entry),
		(unsigned long)__entry->index,
		(unsigned long long)__entry->old_blkaddr,
		(unsigned long long)__entry->new_blkaddr,
		show_bio_type(__entry->op, __entry->op_flags),
		show_block_temp(__entry->temp),
		show_block_type(__entry->type))
);

DEFINE_EVENT_CONDITION(f2fs__submit_page_bio, f2fs_submit_page_bio,

	TP_PROTO(struct page *page, struct f2fs_io_info *fio),

	TP_ARGS(page, fio),

	TP_CONDITION(page->mapping)
);

DEFINE_EVENT_CONDITION(f2fs__submit_page_bio, f2fs_submit_page_write,

	TP_PROTO(struct page *page, struct f2fs_io_info *fio),

	TP_ARGS(page, fio),

	TP_CONDITION(page->mapping)
);

DECLARE_EVENT_CLASS(f2fs__bio,

	TP_PROTO(struct super_block *sb, int type, struct bio *bio),

	TP_ARGS(sb, type, bio),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(dev_t,	target)
		__field(int,	op)
		__field(int,	op_flags)
		__field(int,	type)
		__field(sector_t,	sector)
		__field(unsigned int,	size)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->target		= bio_dev(bio);
		__entry->op		= bio_op(bio);
		__entry->op_flags	= bio->bi_opf;
		__entry->type		= type;
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->size		= bio->bi_iter.bi_size;
	),

	TP_printk("dev = (%d,%d)/(%d,%d), rw = %s(%s), %s, sector = %lld, size = %u",
		show_dev(__entry->target),
		show_dev(__entry->dev),
		show_bio_type(__entry->op, __entry->op_flags),
		show_block_type(__entry->type),
		(unsigned long long)__entry->sector,
		__entry->size)
);

DEFINE_EVENT_CONDITION(f2fs__bio, f2fs_prepare_write_bio,

	TP_PROTO(struct super_block *sb, int type, struct bio *bio),

	TP_ARGS(sb, type, bio),

	TP_CONDITION(bio)
);

DEFINE_EVENT_CONDITION(f2fs__bio, f2fs_prepare_read_bio,

	TP_PROTO(struct super_block *sb, int type, struct bio *bio),

	TP_ARGS(sb, type, bio),

	TP_CONDITION(bio)
);

DEFINE_EVENT_CONDITION(f2fs__bio, f2fs_submit_read_bio,

	TP_PROTO(struct super_block *sb, int type, struct bio *bio),

	TP_ARGS(sb, type, bio),

	TP_CONDITION(bio)
);

DEFINE_EVENT_CONDITION(f2fs__bio, f2fs_submit_write_bio,

	TP_PROTO(struct super_block *sb, int type, struct bio *bio),

	TP_ARGS(sb, type, bio),

	TP_CONDITION(bio)
);

TRACE_EVENT(f2fs_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
				unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	pos)
		__field(unsigned int, len)
		__field(unsigned int, flags)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pos = %llu, len = %u, flags = %u",
		show_dev_ino(__entry),
		(unsigned long long)__entry->pos,
		__entry->len,
		__entry->flags)
);

TRACE_EVENT(f2fs_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
				unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	pos)
		__field(unsigned int, len)
		__field(unsigned int, copied)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pos = %llu, len = %u, copied = %u",
		show_dev_ino(__entry),
		(unsigned long long)__entry->pos,
		__entry->len,
		__entry->copied)
);

DECLARE_EVENT_CLASS(f2fs__page,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int, type)
		__field(int, dir)
		__field(pgoff_t, index)
		__field(int, dirty)
		__field(int, uptodate)
	),

	TP_fast_assign(
		__entry->dev	= page_file_mapping(page)->host->i_sb->s_dev;
		__entry->ino	= page_file_mapping(page)->host->i_ino;
		__entry->type	= type;
		__entry->dir	=
			S_ISDIR(page_file_mapping(page)->host->i_mode);
		__entry->index	= page->index;
		__entry->dirty	= PageDirty(page);
		__entry->uptodate = PageUptodate(page);
	),

	TP_printk("dev = (%d,%d), ino = %lu, %s, %s, index = %lu, "
		"dirty = %d, uptodate = %d",
		show_dev_ino(__entry),
		show_block_type(__entry->type),
		show_file_type(__entry->dir),
		(unsigned long)__entry->index,
		__entry->dirty,
		__entry->uptodate)
);

DEFINE_EVENT(f2fs__page, f2fs_writepage,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_do_write_data_page,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_readpage,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_set_page_dirty,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_vm_page_mkwrite,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_register_inmem_page,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

DEFINE_EVENT(f2fs__page, f2fs_commit_inmem_page,

	TP_PROTO(struct page *page, int type),

	TP_ARGS(page, type)
);

TRACE_EVENT(f2fs_filemap_fault,

	TP_PROTO(struct inode *inode, pgoff_t index, unsigned long ret),

	TP_ARGS(inode, index, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t, index)
		__field(unsigned long, ret)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->index	= index;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, index = %lu, ret = %lx",
		show_dev_ino(__entry),
		(unsigned long)__entry->index,
		__entry->ret)
);

TRACE_EVENT(f2fs_writepages,

	TP_PROTO(struct inode *inode, struct writeback_control *wbc, int type),

	TP_ARGS(inode, wbc, type),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(int,	type)
		__field(int,	dir)
		__field(long,	nr_to_write)
		__field(long,	pages_skipped)
		__field(loff_t,	range_start)
		__field(loff_t,	range_end)
		__field(pgoff_t, writeback_index)
		__field(int,	sync_mode)
		__field(char,	for_kupdate)
		__field(char,	for_background)
		__field(char,	tagged_writepages)
		__field(char,	for_reclaim)
		__field(char,	range_cyclic)
		__field(char,	for_sync)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->type		= type;
		__entry->dir		= S_ISDIR(inode->i_mode);
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->writeback_index = inode->i_mapping->writeback_index;
		__entry->sync_mode	= wbc->sync_mode;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_background	= wbc->for_background;
		__entry->tagged_writepages	= wbc->tagged_writepages;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->for_sync	= wbc->for_sync;
	),

	TP_printk("dev = (%d,%d), ino = %lu, %s, %s, nr_to_write %ld, "
		"skipped %ld, start %lld, end %lld, wb_idx %lu, sync_mode %d, "
		"kupdate %u background %u tagged %u reclaim %u cyclic %u sync %u",
		show_dev_ino(__entry),
		show_block_type(__entry->type),
		show_file_type(__entry->dir),
		__entry->nr_to_write,
		__entry->pages_skipped,
		__entry->range_start,
		__entry->range_end,
		(unsigned long)__entry->writeback_index,
		__entry->sync_mode,
		__entry->for_kupdate,
		__entry->for_background,
		__entry->tagged_writepages,
		__entry->for_reclaim,
		__entry->range_cyclic,
		__entry->for_sync)
);

TRACE_EVENT(f2fs_readpages,

	TP_PROTO(struct inode *inode, pgoff_t start, unsigned int nrpage),

	TP_ARGS(inode, start, nrpage),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t,	start)
		__field(unsigned int,	nrpage)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->start	= start;
		__entry->nrpage	= nrpage;
	),

	TP_printk("dev = (%d,%d), ino = %lu, start = %lu nrpage = %u",
		show_dev_ino(__entry),
		(unsigned long)__entry->start,
		__entry->nrpage)
);

TRACE_EVENT(f2fs_write_checkpoint,

	TP_PROTO(struct super_block *sb, int reason, char *msg),

	TP_ARGS(sb, reason, msg),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(int,	reason)
		__field(char *,	msg)
	),

	TP_fast_assign(
		__entry->dev		= sb->s_dev;
		__entry->reason		= reason;
		__entry->msg		= msg;
	),

	TP_printk("dev = (%d,%d), checkpoint for %s, state = %s",
		show_dev(__entry->dev),
		show_cpreason(__entry->reason),
		__entry->msg)
);

DECLARE_EVENT_CLASS(f2fs_discard,

	TP_PROTO(struct block_device *dev, block_t blkstart, block_t blklen),

	TP_ARGS(dev, blkstart, blklen),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(block_t, blkstart)
		__field(block_t, blklen)
	),

	TP_fast_assign(
		__entry->dev	= dev->bd_dev;
		__entry->blkstart = blkstart;
		__entry->blklen = blklen;
	),

	TP_printk("dev = (%d,%d), blkstart = 0x%llx, blklen = 0x%llx",
		show_dev(__entry->dev),
		(unsigned long long)__entry->blkstart,
		(unsigned long long)__entry->blklen)
);

DEFINE_EVENT(f2fs_discard, f2fs_queue_discard,

	TP_PROTO(struct block_device *dev, block_t blkstart, block_t blklen),

	TP_ARGS(dev, blkstart, blklen)
);

DEFINE_EVENT(f2fs_discard, f2fs_issue_discard,

	TP_PROTO(struct block_device *dev, block_t blkstart, block_t blklen),

	TP_ARGS(dev, blkstart, blklen)
);

DEFINE_EVENT(f2fs_discard, f2fs_remove_discard,

	TP_PROTO(struct block_device *dev, block_t blkstart, block_t blklen),

	TP_ARGS(dev, blkstart, blklen)
);

TRACE_EVENT(f2fs_issue_reset_zone,

	TP_PROTO(struct block_device *dev, block_t blkstart),

	TP_ARGS(dev, blkstart),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(block_t, blkstart)
	),

	TP_fast_assign(
		__entry->dev	= dev->bd_dev;
		__entry->blkstart = blkstart;
	),

	TP_printk("dev = (%d,%d), reset zone at block = 0x%llx",
		show_dev(__entry->dev),
		(unsigned long long)__entry->blkstart)
);

TRACE_EVENT(f2fs_issue_flush,

	TP_PROTO(struct block_device *dev, unsigned int nobarrier,
				unsigned int flush_merge, int ret),

	TP_ARGS(dev, nobarrier, flush_merge, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned int, nobarrier)
		__field(unsigned int, flush_merge)
		__field(int,  ret)
	),

	TP_fast_assign(
		__entry->dev	= dev->bd_dev;
		__entry->nobarrier = nobarrier;
		__entry->flush_merge = flush_merge;
		__entry->ret = ret;
	),

	TP_printk("dev = (%d,%d), %s %s, ret = %d",
		show_dev(__entry->dev),
		__entry->nobarrier ? "skip (nobarrier)" : "issue",
		__entry->flush_merge ? " with flush_merge" : "",
		__entry->ret)
);

TRACE_EVENT(f2fs_lookup_extent_tree_start,

	TP_PROTO(struct inode *inode, unsigned int pgofs),

	TP_ARGS(inode, pgofs),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(unsigned int, pgofs)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->pgofs = pgofs;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pgofs = %u",
		show_dev_ino(__entry),
		__entry->pgofs)
);

TRACE_EVENT_CONDITION(f2fs_lookup_extent_tree_end,

	TP_PROTO(struct inode *inode, unsigned int pgofs,
						struct extent_info *ei),

	TP_ARGS(inode, pgofs, ei),

	TP_CONDITION(ei),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(unsigned int, pgofs)
		__field(unsigned int, fofs)
		__field(u32, blk)
		__field(unsigned int, len)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->pgofs = pgofs;
		__entry->fofs = ei->fofs;
		__entry->blk = ei->blk;
		__entry->len = ei->len;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pgofs = %u, "
		"ext_info(fofs: %u, blk: %u, len: %u)",
		show_dev_ino(__entry),
		__entry->pgofs,
		__entry->fofs,
		__entry->blk,
		__entry->len)
);

TRACE_EVENT(f2fs_update_extent_tree_range,

	TP_PROTO(struct inode *inode, unsigned int pgofs, block_t blkaddr,
						unsigned int len),

	TP_ARGS(inode, pgofs, blkaddr, len),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(unsigned int, pgofs)
		__field(u32, blk)
		__field(unsigned int, len)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->pgofs = pgofs;
		__entry->blk = blkaddr;
		__entry->len = len;
	),

	TP_printk("dev = (%d,%d), ino = %lu, pgofs = %u, "
					"blkaddr = %u, len = %u",
		show_dev_ino(__entry),
		__entry->pgofs,
		__entry->blk,
		__entry->len)
);

TRACE_EVENT(f2fs_shrink_extent_tree,

	TP_PROTO(struct f2fs_sb_info *sbi, unsigned int node_cnt,
						unsigned int tree_cnt),

	TP_ARGS(sbi, node_cnt, tree_cnt),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned int, node_cnt)
		__field(unsigned int, tree_cnt)
	),

	TP_fast_assign(
		__entry->dev = sbi->sb->s_dev;
		__entry->node_cnt = node_cnt;
		__entry->tree_cnt = tree_cnt;
	),

	TP_printk("dev = (%d,%d), shrunk: node_cnt = %u, tree_cnt = %u",
		show_dev(__entry->dev),
		__entry->node_cnt,
		__entry->tree_cnt)
);

TRACE_EVENT(f2fs_destroy_extent_tree,

	TP_PROTO(struct inode *inode, unsigned int node_cnt),

	TP_ARGS(inode, node_cnt),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(unsigned int, node_cnt)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->node_cnt = node_cnt;
	),

	TP_printk("dev = (%d,%d), ino = %lu, destroyed: node_cnt = %u",
		show_dev_ino(__entry),
		__entry->node_cnt)
);

DECLARE_EVENT_CLASS(f2fs_sync_dirty_inodes,

	TP_PROTO(struct super_block *sb, int type, s64 count),

	TP_ARGS(sb, type, count),

	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, type)
		__field(s64, count)
	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->type	= type;
		__entry->count	= count;
	),

	TP_printk("dev = (%d,%d), %s, dirty count = %lld",
		show_dev(__entry->dev),
		show_file_type(__entry->type),
		__entry->count)
);

DEFINE_EVENT(f2fs_sync_dirty_inodes, f2fs_sync_dirty_inodes_enter,

	TP_PROTO(struct super_block *sb, int type, s64 count),

	TP_ARGS(sb, type, count)
);

DEFINE_EVENT(f2fs_sync_dirty_inodes, f2fs_sync_dirty_inodes_exit,

	TP_PROTO(struct super_block *sb, int type, s64 count),

	TP_ARGS(sb, type, count)
);

TRACE_EVENT(f2fs_shutdown,

	TP_PROTO(struct f2fs_sb_info *sbi, unsigned int mode, int ret),

	TP_ARGS(sbi, mode, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned int, mode)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->dev = sbi->sb->s_dev;
		__entry->mode = mode;
		__entry->ret = ret;
	),

	TP_printk("dev = (%d,%d), mode: %s, ret:%d",
		show_dev(__entry->dev),
		show_shutdown_mode(__entry->mode),
		__entry->ret)
);

DECLARE_EVENT_CLASS(f2fs_zip_start,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
			unsigned int cluster_size, unsigned char algtype),

	TP_ARGS(inode, cluster_idx, cluster_size, algtype),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t, idx)
		__field(unsigned int, size)
		__field(unsigned int, algtype)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->idx = cluster_idx;
		__entry->size = cluster_size;
		__entry->algtype = algtype;
	),

	TP_printk("dev = (%d,%d), ino = %lu, cluster_idx:%lu, "
		"cluster_size = %u, algorithm = %s",
		show_dev_ino(__entry),
		__entry->idx,
		__entry->size,
		show_compress_algorithm(__entry->algtype))
);

DECLARE_EVENT_CLASS(f2fs_zip_end,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
			unsigned int compressed_size, int ret),

	TP_ARGS(inode, cluster_idx, compressed_size, ret),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(pgoff_t, idx)
		__field(unsigned int, size)
		__field(unsigned int, ret)
	),

	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->idx = cluster_idx;
		__entry->size = compressed_size;
		__entry->ret = ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, cluster_idx:%lu, "
		"compressed_size = %u, ret = %d",
		show_dev_ino(__entry),
		__entry->idx,
		__entry->size,
		__entry->ret)
);

DEFINE_EVENT(f2fs_zip_start, f2fs_compress_pages_start,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
		unsigned int cluster_size, unsigned char algtype),

	TP_ARGS(inode, cluster_idx, cluster_size, algtype)
);

DEFINE_EVENT(f2fs_zip_start, f2fs_decompress_pages_start,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
		unsigned int cluster_size, unsigned char algtype),

	TP_ARGS(inode, cluster_idx, cluster_size, algtype)
);

DEFINE_EVENT(f2fs_zip_end, f2fs_compress_pages_end,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
			unsigned int compressed_size, int ret),

	TP_ARGS(inode, cluster_idx, compressed_size, ret)
);

DEFINE_EVENT(f2fs_zip_end, f2fs_decompress_pages_end,

	TP_PROTO(struct inode *inode, pgoff_t cluster_idx,
			unsigned int compressed_size, int ret),

	TP_ARGS(inode, cluster_idx, compressed_size, ret)
);

#ifdef CONFIG_F2FS_IOSTAT
TRACE_EVENT(f2fs_iostat,

	TP_PROTO(struct f2fs_sb_info *sbi, unsigned long long *iostat),

	TP_ARGS(sbi, iostat),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned long long,	app_dio)
		__field(unsigned long long,	app_bio)
		__field(unsigned long long,	app_wio)
		__field(unsigned long long,	app_mio)
		__field(unsigned long long,	fs_dio)
		__field(unsigned long long,	fs_nio)
		__field(unsigned long long,	fs_mio)
		__field(unsigned long long,	fs_gc_dio)
		__field(unsigned long long,	fs_gc_nio)
		__field(unsigned long long,	fs_cp_dio)
		__field(unsigned long long,	fs_cp_nio)
		__field(unsigned long long,	fs_cp_mio)
		__field(unsigned long long,	app_drio)
		__field(unsigned long long,	app_brio)
		__field(unsigned long long,	app_rio)
		__field(unsigned long long,	app_mrio)
		__field(unsigned long long,	fs_drio)
		__field(unsigned long long,	fs_gdrio)
		__field(unsigned long long,	fs_cdrio)
		__field(unsigned long long,	fs_nrio)
		__field(unsigned long long,	fs_mrio)
		__field(unsigned long long,	fs_discard)
	),

	TP_fast_assign(
		__entry->dev		= sbi->sb->s_dev;
		__entry->app_dio	= iostat[APP_DIRECT_IO];
		__entry->app_bio	= iostat[APP_BUFFERED_IO];
		__entry->app_wio	= iostat[APP_WRITE_IO];
		__entry->app_mio	= iostat[APP_MAPPED_IO];
		__entry->fs_dio		= iostat[FS_DATA_IO];
		__entry->fs_nio		= iostat[FS_NODE_IO];
		__entry->fs_mio		= iostat[FS_META_IO];
		__entry->fs_gc_dio	= iostat[FS_GC_DATA_IO];
		__entry->fs_gc_nio	= iostat[FS_GC_NODE_IO];
		__entry->fs_cp_dio	= iostat[FS_CP_DATA_IO];
		__entry->fs_cp_nio	= iostat[FS_CP_NODE_IO];
		__entry->fs_cp_mio	= iostat[FS_CP_META_IO];
		__entry->app_drio	= iostat[APP_DIRECT_READ_IO];
		__entry->app_brio	= iostat[APP_BUFFERED_READ_IO];
		__entry->app_rio	= iostat[APP_READ_IO];
		__entry->app_mrio	= iostat[APP_MAPPED_READ_IO];
		__entry->fs_drio	= iostat[FS_DATA_READ_IO];
		__entry->fs_gdrio	= iostat[FS_GDATA_READ_IO];
		__entry->fs_cdrio	= iostat[FS_CDATA_READ_IO];
		__entry->fs_nrio	= iostat[FS_NODE_READ_IO];
		__entry->fs_mrio	= iostat[FS_META_READ_IO];
		__entry->fs_discard	= iostat[FS_DISCARD];
	),

	TP_printk("dev = (%d,%d), "
		"app [write=%llu (direct=%llu, buffered=%llu), mapped=%llu], "
		"fs [data=%llu, node=%llu, meta=%llu, discard=%llu], "
		"gc [data=%llu, node=%llu], "
		"cp [data=%llu, node=%llu, meta=%llu], "
		"app [read=%llu (direct=%llu, buffered=%llu), mapped=%llu], "
		"fs [data=%llu, (gc_data=%llu, compr_data=%llu), "
		"node=%llu, meta=%llu]",
		show_dev(__entry->dev), __entry->app_wio, __entry->app_dio,
		__entry->app_bio, __entry->app_mio, __entry->fs_dio,
		__entry->fs_nio, __entry->fs_mio, __entry->fs_discard,
		__entry->fs_gc_dio, __entry->fs_gc_nio, __entry->fs_cp_dio,
		__entry->fs_cp_nio, __entry->fs_cp_mio,
		__entry->app_rio, __entry->app_drio, __entry->app_brio,
		__entry->app_mrio, __entry->fs_drio, __entry->fs_gdrio,
		__entry->fs_cdrio, __entry->fs_nrio, __entry->fs_mrio)
);

#ifndef __F2FS_IOSTAT_LATENCY_TYPE
#define __F2FS_IOSTAT_LATENCY_TYPE
struct f2fs_iostat_latency {
	unsigned int peak_lat;
	unsigned int avg_lat;
	unsigned int cnt;
};
#endif /* __F2FS_IOSTAT_LATENCY_TYPE */

TRACE_EVENT(f2fs_iostat_latency,

	TP_PROTO(struct f2fs_sb_info *sbi, struct f2fs_iostat_latency (*iostat_lat)[NR_PAGE_TYPE]),

	TP_ARGS(sbi, iostat_lat),

	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(unsigned int,	d_rd_peak)
		__field(unsigned int,	d_rd_avg)
		__field(unsigned int,	d_rd_cnt)
		__field(unsigned int,	n_rd_peak)
		__field(unsigned int,	n_rd_avg)
		__field(unsigned int,	n_rd_cnt)
		__field(unsigned int,	m_rd_peak)
		__field(unsigned int,	m_rd_avg)
		__field(unsigned int,	m_rd_cnt)
		__field(unsigned int,	d_wr_s_peak)
		__field(unsigned int,	d_wr_s_avg)
		__field(unsigned int,	d_wr_s_cnt)
		__field(unsigned int,	n_wr_s_peak)
		__field(unsigned int,	n_wr_s_avg)
		__field(unsigned int,	n_wr_s_cnt)
		__field(unsigned int,	m_wr_s_peak)
		__field(unsigned int,	m_wr_s_avg)
		__field(unsigned int,	m_wr_s_cnt)
		__field(unsigned int,	d_wr_as_peak)
		__field(unsigned int,	d_wr_as_avg)
		__field(unsigned int,	d_wr_as_cnt)
		__field(unsigned int,	n_wr_as_peak)
		__field(unsigned int,	n_wr_as_avg)
		__field(unsigned int,	n_wr_as_cnt)
		__field(unsigned int,	m_wr_as_peak)
		__field(unsigned int,	m_wr_as_avg)
		__field(unsigned int,	m_wr_as_cnt)
	),

	TP_fast_assign(
		__entry->dev		= sbi->sb->s_dev;
		__entry->d_rd_peak	= iostat_lat[0][DATA].peak_lat;
		__entry->d_rd_avg	= iostat_lat[0][DATA].avg_lat;
		__entry->d_rd_cnt	= iostat_lat[0][DATA].cnt;
		__entry->n_rd_peak	= iostat_lat[0][NODE].peak_lat;
		__entry->n_rd_avg	= iostat_lat[0][NODE].avg_lat;
		__entry->n_rd_cnt	= iostat_lat[0][NODE].cnt;
		__entry->m_rd_peak	= iostat_lat[0][META].peak_lat;
		__entry->m_rd_avg	= iostat_lat[0][META].avg_lat;
		__entry->m_rd_cnt	= iostat_lat[0][META].cnt;
		__entry->d_wr_s_peak	= iostat_lat[1][DATA].peak_lat;
		__entry->d_wr_s_avg	= iostat_lat[1][DATA].avg_lat;
		__entry->d_wr_s_cnt	= iostat_lat[1][DATA].cnt;
		__entry->n_wr_s_peak	= iostat_lat[1][NODE].peak_lat;
		__entry->n_wr_s_avg	= iostat_lat[1][NODE].avg_lat;
		__entry->n_wr_s_cnt	= iostat_lat[1][NODE].cnt;
		__entry->m_wr_s_peak	= iostat_lat[1][META].peak_lat;
		__entry->m_wr_s_avg	= iostat_lat[1][META].avg_lat;
		__entry->m_wr_s_cnt	= iostat_lat[1][META].cnt;
		__entry->d_wr_as_peak	= iostat_lat[2][DATA].peak_lat;
		__entry->d_wr_as_avg	= iostat_lat[2][DATA].avg_lat;
		__entry->d_wr_as_cnt	= iostat_lat[2][DATA].cnt;
		__entry->n_wr_as_peak	= iostat_lat[2][NODE].peak_lat;
		__entry->n_wr_as_avg	= iostat_lat[2][NODE].avg_lat;
		__entry->n_wr_as_cnt	= iostat_lat[2][NODE].cnt;
		__entry->m_wr_as_peak	= iostat_lat[2][META].peak_lat;
		__entry->m_wr_as_avg	= iostat_lat[2][META].avg_lat;
		__entry->m_wr_as_cnt	= iostat_lat[2][META].cnt;
	),

	TP_printk("dev = (%d,%d), "
		"iotype [peak lat.(ms)/avg lat.(ms)/count], "
		"rd_data [%u/%u/%u], rd_node [%u/%u/%u], rd_meta [%u/%u/%u], "
		"wr_sync_data [%u/%u/%u], wr_sync_node [%u/%u/%u], "
		"wr_sync_meta [%u/%u/%u], wr_async_data [%u/%u/%u], "
		"wr_async_node [%u/%u/%u], wr_async_meta [%u/%u/%u]",
		show_dev(__entry->dev),
		__entry->d_rd_peak, __entry->d_rd_avg, __entry->d_rd_cnt,
		__entry->n_rd_peak, __entry->n_rd_avg, __entry->n_rd_cnt,
		__entry->m_rd_peak, __entry->m_rd_avg, __entry->m_rd_cnt,
		__entry->d_wr_s_peak, __entry->d_wr_s_avg, __entry->d_wr_s_cnt,
		__entry->n_wr_s_peak, __entry->n_wr_s_avg, __entry->n_wr_s_cnt,
		__entry->m_wr_s_peak, __entry->m_wr_s_avg, __entry->m_wr_s_cnt,
		__entry->d_wr_as_peak, __entry->d_wr_as_avg, __entry->d_wr_as_cnt,
		__entry->n_wr_as_peak, __entry->n_wr_as_avg, __entry->n_wr_as_cnt,
		__entry->m_wr_as_peak, __entry->m_wr_as_avg, __entry->m_wr_as_cnt)
);
#endif

TRACE_EVENT(f2fs_bmap,

	TP_PROTO(struct inode *inode, sector_t lblock, sector_t pblock),

	TP_ARGS(inode, lblock, pblock),

	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(sector_t, lblock)
		__field(sector_t, pblock)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->lblock		= lblock;
		__entry->pblock		= pblock;
	),

	TP_printk("dev = (%d,%d), ino = %lu, lblock:%lld, pblock:%lld",
		show_dev_ino(__entry),
		(unsigned long long)__entry->lblock,
		(unsigned long long)__entry->pblock)
);

TRACE_EVENT(f2fs_fiemap,

	TP_PROTO(struct inode *inode, sector_t lblock, sector_t pblock,
		unsigned long long len, unsigned int flags, int ret),

	TP_ARGS(inode, lblock, pblock, len, flags, ret),

	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(sector_t, lblock)
		__field(sector_t, pblock)
		__field(unsigned long long, len)
		__field(unsigned int, flags)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->lblock		= lblock;
		__entry->pblock		= pblock;
		__entry->len		= len;
		__entry->flags		= flags;
		__entry->ret		= ret;
	),

	TP_printk("dev = (%d,%d), ino = %lu, lblock:%lld, pblock:%lld, "
		"len:%llu, flags:%u, ret:%d",
		show_dev_ino(__entry),
		(unsigned long long)__entry->lblock,
		(unsigned long long)__entry->pblock,
		__entry->len,
		__entry->flags,
		__entry->ret)
);

#endif /* _TRACE_F2FS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>
