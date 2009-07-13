#if !defined(_TRACE_EXT4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EXT4_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext4

#include <linux/writeback.h>
#include "../../../fs/ext4/ext4.h"
#include "../../../fs/ext4/mballoc.h"
#include <linux/tracepoint.h>

TRACE_EVENT(ext4_free_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	uid_t,	uid			)
		__field(	gid_t,	gid			)
		__field(	blkcnt_t, blocks		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->uid	= inode->i_uid;
		__entry->gid	= inode->i_gid;
		__entry->blocks	= inode->i_blocks;
	),

	TP_printk("dev %s ino %lu mode %d uid %u gid %u blocks %llu",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->mode,
		  __entry->uid, __entry->gid,
		  (unsigned long long) __entry->blocks)
);

TRACE_EVENT(ext4_request_inode,
	TP_PROTO(struct inode *dir, int mode),

	TP_ARGS(dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %s dir %lu mode %d",
		  jbd2_dev_to_name(__entry->dev), __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_allocate_inode,
	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %s ino %lu dir %lu mode %d",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev %s ino %lu pos %llu len %u flags %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->flags)
);

TRACE_EVENT(ext4_ordered_write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %s ino %lu pos %llu len %u copied %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->copied)
);

TRACE_EVENT(ext4_writeback_write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %s ino %lu pos %llu len %u copied %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->copied)
);

TRACE_EVENT(ext4_journalled_write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),
	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %s ino %lu pos %llu len %u copied %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->copied)
);

TRACE_EVENT(ext4_writepage,
	TP_PROTO(struct inode *inode, struct page *page),

	TP_ARGS(inode, page),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)

	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->index	= page->index;
	),

	TP_printk("dev %s ino %lu page_index %lu",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->index)
);

TRACE_EVENT(ext4_da_writepages,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	long,	nr_to_write		)
		__field(	long,	pages_skipped		)
		__field(	loff_t,	range_start		)
		__field(	loff_t,	range_end		)
		__field(	char,	nonblocking		)
		__field(	char,	for_kupdate		)
		__field(	char,	for_reclaim		)
		__field(	char,	for_writepages		)
		__field(	char,	range_cyclic		)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->nonblocking	= wbc->nonblocking;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->for_writepages	= wbc->for_writepages;
		__entry->range_cyclic	= wbc->range_cyclic;
	),

	TP_printk("dev %s ino %lu nr_t_write %ld pages_skipped %ld range_start %llu range_end %llu nonblocking %d for_kupdate %d for_reclaim %d for_writepages %d range_cyclic %d",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->nr_to_write,
		  __entry->pages_skipped, __entry->range_start,
		  __entry->range_end, __entry->nonblocking,
		  __entry->for_kupdate, __entry->for_reclaim,
		  __entry->for_writepages, __entry->range_cyclic)
);

TRACE_EVENT(ext4_da_writepages_result,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc,
			int ret, int pages_written),

	TP_ARGS(inode, wbc, ret, pages_written),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
		__field(	int,	pages_written		)
		__field(	long,	pages_skipped		)
		__field(	char,	encountered_congestion	)
		__field(	char,	more_io			)	
		__field(	char,	no_nrwrite_index_update )
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->ret		= ret;
		__entry->pages_written	= pages_written;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->encountered_congestion	= wbc->encountered_congestion;
		__entry->more_io	= wbc->more_io;
		__entry->no_nrwrite_index_update = wbc->no_nrwrite_index_update;
	),

	TP_printk("dev %s ino %lu ret %d pages_written %d pages_skipped %ld congestion %d more_io %d no_nrwrite_index_update %d",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->encountered_congestion, __entry->more_io,
		  __entry->no_nrwrite_index_update)
);

TRACE_EVENT(ext4_da_write_begin,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev %s ino %lu pos %llu len %u flags %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->flags)
);

TRACE_EVENT(ext4_da_write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %s ino %lu pos %llu len %u copied %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pos, __entry->len,
		  __entry->copied)
);

TRACE_EVENT(ext4_discard_blocks,
	TP_PROTO(struct super_block *sb, unsigned long long blk,
			unsigned long long count),

	TP_ARGS(sb, blk, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	__u64,	blk			)
		__field(	__u64,	count			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->blk	= blk;
		__entry->count	= count;
	),

	TP_printk("dev %s blk %llu count %llu",
		  jbd2_dev_to_name(__entry->dev), __entry->blk, __entry->count)
);

TRACE_EVENT(ext4_mb_new_inode_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)
		__field(	__u64,	pa_lstart		)

	),

	TP_fast_assign(
		__entry->dev		= ac->ac_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
		__entry->pa_lstart	= pa->pa_lstart;
	),

	TP_printk("dev %s ino %lu pstart %llu len %u lstart %llu",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pa_pstart,
		  __entry->pa_len, __entry->pa_lstart)
);

TRACE_EVENT(ext4_mb_new_group_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)
		__field(	__u64,	pa_lstart		)

	),

	TP_fast_assign(
		__entry->dev		= ac->ac_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
		__entry->pa_lstart	= pa->pa_lstart;
	),

	TP_printk("dev %s ino %lu pstart %llu len %u lstart %llu",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->pa_pstart,
		  __entry->pa_len, __entry->pa_lstart)
);

TRACE_EVENT(ext4_mb_release_inode_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa,
		 unsigned long long block, unsigned int count),

	TP_ARGS(ac, pa, block, count),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	__u32,	count			)

	),

	TP_fast_assign(
		__entry->dev		= ac->ac_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->block		= block;
		__entry->count		= count;
	),

	TP_printk("dev %s ino %lu block %llu count %u",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->block,
		  __entry->count)
);

TRACE_EVENT(ext4_mb_release_group_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)

	),

	TP_fast_assign(
		__entry->dev		= ac->ac_sb->s_dev;
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
	),

	TP_printk("dev %s pstart %llu len %u",
		  jbd2_dev_to_name(__entry->dev), __entry->pa_pstart, __entry->pa_len)
);

TRACE_EVENT(ext4_discard_preallocations,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)

	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->ino	= inode->i_ino;
	),

	TP_printk("dev %s ino %lu",
		  jbd2_dev_to_name(__entry->dev), __entry->ino)
);

TRACE_EVENT(ext4_mb_discard_preallocations,
	TP_PROTO(struct super_block *sb, int needed),

	TP_ARGS(sb, needed),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	needed			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->needed	= needed;
	),

	TP_printk("dev %s needed %d",
		  jbd2_dev_to_name(__entry->dev), __entry->needed)
);

TRACE_EVENT(ext4_request_blocks,
	TP_PROTO(struct ext4_allocation_request *ar),

	TP_ARGS(ar),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	unsigned int, flags		)
		__field(	unsigned int, len		)
		__field(	__u64,  logical			)
		__field(	__u64,	goal			)
		__field(	__u64,	lleft			)
		__field(	__u64,	lright			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
	),

	TP_fast_assign(
		__entry->dev	= ar->inode->i_sb->s_dev;
		__entry->ino	= ar->inode->i_ino;
		__entry->flags	= ar->flags;
		__entry->len	= ar->len;
		__entry->logical = ar->logical;
		__entry->goal	= ar->goal;
		__entry->lleft	= ar->lleft;
		__entry->lright	= ar->lright;
		__entry->pleft	= ar->pleft;
		__entry->pright	= ar->pright;
	),

	TP_printk("dev %s ino %lu flags %u len %u lblk %llu goal %llu lleft %llu lright %llu pleft %llu pright %llu ",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->flags,
		  __entry->len,
		  (unsigned long long) __entry->logical,
		  (unsigned long long) __entry->goal,
		  (unsigned long long) __entry->lleft,
		  (unsigned long long) __entry->lright,
		  (unsigned long long) __entry->pleft,
		  (unsigned long long) __entry->pright)
);

TRACE_EVENT(ext4_allocate_blocks,
	TP_PROTO(struct ext4_allocation_request *ar, unsigned long long block),

	TP_ARGS(ar, block),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	unsigned int, flags		)
		__field(	unsigned int, len		)
		__field(	__u64,  logical			)
		__field(	__u64,	goal			)
		__field(	__u64,	lleft			)
		__field(	__u64,	lright			)
		__field(	__u64,	pleft			)
		__field(	__u64,	pright			)
	),

	TP_fast_assign(
		__entry->dev	= ar->inode->i_sb->s_dev;
		__entry->ino	= ar->inode->i_ino;
		__entry->block	= block;
		__entry->flags	= ar->flags;
		__entry->len	= ar->len;
		__entry->logical = ar->logical;
		__entry->goal	= ar->goal;
		__entry->lleft	= ar->lleft;
		__entry->lright	= ar->lright;
		__entry->pleft	= ar->pleft;
		__entry->pright	= ar->pright;
	),

	TP_printk("dev %s ino %lu flags %u len %u block %llu lblk %llu goal %llu lleft %llu lright %llu pleft %llu pright %llu ",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->flags,
		  __entry->len, __entry->block,
		  (unsigned long long) __entry->logical,
		  (unsigned long long) __entry->goal,
		  (unsigned long long) __entry->lleft,
		  (unsigned long long) __entry->lright,
		  (unsigned long long) __entry->pleft,
		  (unsigned long long) __entry->pright)
);

TRACE_EVENT(ext4_free_blocks,
	TP_PROTO(struct inode *inode, __u64 block, unsigned long count,
			int metadata),

	TP_ARGS(inode, block, count, metadata),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	unsigned long,	count		)
		__field(	int,	metadata		)

	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->ino		= inode->i_ino;
		__entry->block		= block;
		__entry->count		= count;
		__entry->metadata	= metadata;
	),

	TP_printk("dev %s ino %lu block %llu count %lu metadata %d",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->block,
		  __entry->count, __entry->metadata)
);

TRACE_EVENT(ext4_sync_file,
	TP_PROTO(struct file *file, struct dentry *dentry, int datasync),

	TP_ARGS(file, dentry, datasync),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
		__entry->dev		= dentry->d_inode->i_sb->s_dev;
		__entry->ino		= dentry->d_inode->i_ino;
		__entry->datasync	= datasync;
		__entry->parent		= dentry->d_parent->d_inode->i_ino;
	),

	TP_printk("dev %s ino %ld parent %ld datasync %d ",
		  jbd2_dev_to_name(__entry->dev), __entry->ino, __entry->parent,
		  __entry->datasync)
);

TRACE_EVENT(ext4_sync_fs,
	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	int,	wait			)

	),

	TP_fast_assign(
		__entry->dev	= sb->s_dev;
		__entry->wait	= wait;
	),

	TP_printk("dev %s wait %d", jbd2_dev_to_name(__entry->dev),
		  __entry->wait)
);

#endif /* _TRACE_EXT4_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
