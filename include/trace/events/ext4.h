#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext4

#if !defined(_TRACE_EXT4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EXT4_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>

struct ext4_allocation_context;
struct ext4_allocation_request;
struct ext4_prealloc_space;
struct ext4_inode_info;
struct mpage_da_data;

#define EXT4_I(inode) (container_of(inode, struct ext4_inode_info, vfs_inode))

TRACE_EVENT(ext4_free_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	uid_t,	uid			)
		__field(	gid_t,	gid			)
		__field(	blkcnt_t, blocks		)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->uid	= inode->i_uid;
		__entry->gid	= inode->i_gid;
		__entry->blocks	= inode->i_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o uid %u gid %u blocks %llu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->mode,
		  __entry->uid, __entry->gid,
		  (unsigned long long) __entry->blocks)
);

TRACE_EVENT(ext4_request_inode,
	TP_PROTO(struct inode *dir, int mode),

	TP_ARGS(dir, mode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(dir->i_sb->s_dev);
		__entry->dev_minor = MINOR(dir->i_sb->s_dev);
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %d,%d dir %lu mode 0%o",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_allocate_inode,
	TP_PROTO(struct inode *inode, struct inode *dir, int mode),

	TP_ARGS(inode, dir, mode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	ino_t,	dir			)
		__field(	umode_t, mode			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->dir	= dir->i_ino;
		__entry->mode	= mode;
	),

	TP_printk("dev %d,%d ino %lu dir %lu mode 0%o",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->dir, __entry->mode)
);

TRACE_EVENT(ext4_evict_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	int,	nlink			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->nlink	= inode->i_nlink;
	),

	TP_printk("dev %d,%d ino %lu nlink %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->nlink)
);

TRACE_EVENT(ext4_drop_inode,
	TP_PROTO(struct inode *inode, int drop),

	TP_ARGS(inode, drop),

	TP_STRUCT__entry(
		__field(	int,	dev_major		)
		__field(	int,	dev_minor		)
		__field(	ino_t,	ino			)
		__field(	int,	drop			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->drop	= drop;
	),

	TP_printk("dev %d,%d ino %lu drop %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->drop)
);

TRACE_EVENT(ext4_mark_inode_dirty,
	TP_PROTO(struct inode *inode, unsigned long IP),

	TP_ARGS(inode, IP),

	TP_STRUCT__entry(
		__field(	int,	dev_major		)
		__field(	int,	dev_minor		)
		__field(	ino_t,	ino			)
		__field(unsigned long,	ip			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->ip	= IP;
	),

	TP_printk("dev %d,%d ino %lu caller %pF",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, (void *)__entry->ip)
);

TRACE_EVENT(ext4_begin_ordered_truncate,
	TP_PROTO(struct inode *inode, loff_t new_size),

	TP_ARGS(inode, new_size),

	TP_STRUCT__entry(
		__field(	int,	dev_major		)
		__field(	int,	dev_minor		)
		__field(	ino_t,	ino			)
		__field(	loff_t,	new_size		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(inode->i_sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->new_size	= new_size;
	),

	TP_printk("dev %d,%d ino %lu new_size %lld",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  (long long) __entry->new_size)
);

DECLARE_EVENT_CLASS(ext4__write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, flags		)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->flags	= flags;
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %u flags %u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->pos, __entry->len, __entry->flags)
);

DEFINE_EVENT(ext4__write_begin, ext4_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
);

DEFINE_EVENT(ext4__write_begin, ext4_da_write_begin,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int flags),

	TP_ARGS(inode, pos, len, flags)
);

DECLARE_EVENT_CLASS(ext4__write_end,
	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
			unsigned int copied),

	TP_ARGS(inode, pos, len, copied),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	loff_t,	pos			)
		__field(	unsigned int, len		)
		__field(	unsigned int, copied		)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->pos	= pos;
		__entry->len	= len;
		__entry->copied	= copied;
	),

	TP_printk("dev %d,%d ino %lu pos %llu len %u copied %u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->pos,
		  __entry->len, __entry->copied)
);

DEFINE_EVENT(ext4__write_end, ext4_ordered_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_writeback_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_journalled_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

DEFINE_EVENT(ext4__write_end, ext4_da_write_end,

	TP_PROTO(struct inode *inode, loff_t pos, unsigned int len,
		 unsigned int copied),

	TP_ARGS(inode, pos, len, copied)
);

TRACE_EVENT(ext4_writepage,
	TP_PROTO(struct inode *inode, struct page *page),

	TP_ARGS(inode, page),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	pgoff_t, index			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->index	= page->index;
	),

	TP_printk("dev %d,%d ino %lu page_index %lu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->index)
);

TRACE_EVENT(ext4_da_writepages,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc),

	TP_ARGS(inode, wbc),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	long,	nr_to_write		)
		__field(	long,	pages_skipped		)
		__field(	loff_t,	range_start		)
		__field(	loff_t,	range_end		)
		__field(	char,	for_kupdate		)
		__field(	char,	for_reclaim		)
		__field(	char,	range_cyclic		)
		__field(       pgoff_t,	writeback_index		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(inode->i_sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->writeback_index = inode->i_mapping->writeback_index;
	),

	TP_printk("dev %d,%d ino %lu nr_to_write %ld pages_skipped %ld "
		  "range_start %llu range_end %llu "
		  "for_kupdate %d for_reclaim %d "
		  "range_cyclic %d writeback_index %lu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->nr_to_write,
		  __entry->pages_skipped, __entry->range_start,
		  __entry->range_end,
		  __entry->for_kupdate, __entry->for_reclaim,
		  __entry->range_cyclic,
		  (unsigned long) __entry->writeback_index)
);

TRACE_EVENT(ext4_da_write_pages,
	TP_PROTO(struct inode *inode, struct mpage_da_data *mpd),

	TP_ARGS(inode, mpd),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	__u64,	b_blocknr		)
		__field(	__u32,	b_size			)
		__field(	__u32,	b_state			)
		__field(	unsigned long,	first_page	)
		__field(	int,	io_done			)
		__field(	int,	pages_written		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(inode->i_sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->b_blocknr	= mpd->b_blocknr;
		__entry->b_size		= mpd->b_size;
		__entry->b_state	= mpd->b_state;
		__entry->first_page	= mpd->first_page;
		__entry->io_done	= mpd->io_done;
		__entry->pages_written	= mpd->pages_written;
	),

	TP_printk("dev %d,%d ino %lu b_blocknr %llu b_size %u b_state 0x%04x first_page %lu io_done %d pages_written %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->b_blocknr, __entry->b_size,
		  __entry->b_state, __entry->first_page,
		  __entry->io_done, __entry->pages_written)
);

TRACE_EVENT(ext4_da_writepages_result,
	TP_PROTO(struct inode *inode, struct writeback_control *wbc,
			int ret, int pages_written),

	TP_ARGS(inode, wbc, ret, pages_written),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	int,	ret			)
		__field(	int,	pages_written		)
		__field(	long,	pages_skipped		)
		__field(	char,	more_io			)	
		__field(       pgoff_t,	writeback_index		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(inode->i_sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->ret		= ret;
		__entry->pages_written	= pages_written;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->more_io	= wbc->more_io;
		__entry->writeback_index = inode->i_mapping->writeback_index;
	),

	TP_printk("dev %d,%d ino %lu ret %d pages_written %d pages_skipped %ld more_io %d writeback_index %lu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->ret,
		  __entry->pages_written, __entry->pages_skipped,
		  __entry->more_io,
		  (unsigned long) __entry->writeback_index)
);

TRACE_EVENT(ext4_discard_blocks,
	TP_PROTO(struct super_block *sb, unsigned long long blk,
			unsigned long long count),

	TP_ARGS(sb, blk, count),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	__u64,	blk			)
		__field(	__u64,	count			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(sb->s_dev);
		__entry->dev_minor = MINOR(sb->s_dev);
		__entry->blk	= blk;
		__entry->count	= count;
	),

	TP_printk("dev %d,%d blk %llu count %llu",
		  __entry->dev_major, __entry->dev_minor,
		  __entry->blk, __entry->count)
);

DECLARE_EVENT_CLASS(ext4__mb_new_pa,
	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)
		__field(	__u64,	pa_lstart		)

	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(ac->ac_sb->s_dev);
		__entry->dev_minor	= MINOR(ac->ac_sb->s_dev);
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
		__entry->pa_lstart	= pa->pa_lstart;
	),

	TP_printk("dev %d,%d ino %lu pstart %llu len %u lstart %llu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->pa_pstart,
		  __entry->pa_len, __entry->pa_lstart)
);

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_inode_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
);

DEFINE_EVENT(ext4__mb_new_pa, ext4_mb_new_group_pa,

	TP_PROTO(struct ext4_allocation_context *ac,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(ac, pa)
);

TRACE_EVENT(ext4_mb_release_inode_pa,
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 struct ext4_prealloc_space *pa,
		 unsigned long long block, unsigned int count),

	TP_ARGS(sb, inode, pa, block, count),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	__u64,	block			)
		__field(	__u32,	count			)

	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(sb->s_dev);
		__entry->dev_minor	= MINOR(sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->block		= block;
		__entry->count		= count;
	),

	TP_printk("dev %d,%d ino %lu block %llu count %u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->block, __entry->count)
);

TRACE_EVENT(ext4_mb_release_group_pa,
	TP_PROTO(struct super_block *sb,
		 struct ext4_prealloc_space *pa),

	TP_ARGS(sb, pa),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	__u64,	pa_pstart		)
		__field(	__u32,	pa_len			)

	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(sb->s_dev);
		__entry->dev_minor	= MINOR(sb->s_dev);
		__entry->pa_pstart	= pa->pa_pstart;
		__entry->pa_len		= pa->pa_len;
	),

	TP_printk("dev %d,%d pstart %llu len %u",
		  __entry->dev_major, __entry->dev_minor,
		  __entry->pa_pstart, __entry->pa_len)
);

TRACE_EVENT(ext4_discard_preallocations,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
	),

	TP_printk("dev %d,%d ino %lu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino)
);

TRACE_EVENT(ext4_mb_discard_preallocations,
	TP_PROTO(struct super_block *sb, int needed),

	TP_ARGS(sb, needed),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	int,	needed			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(sb->s_dev);
		__entry->dev_minor = MINOR(sb->s_dev);
		__entry->needed	= needed;
	),

	TP_printk("dev %d,%d needed %d",
		  __entry->dev_major, __entry->dev_minor, __entry->needed)
);

TRACE_EVENT(ext4_request_blocks,
	TP_PROTO(struct ext4_allocation_request *ar),

	TP_ARGS(ar),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
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
		__entry->dev_major = MAJOR(ar->inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(ar->inode->i_sb->s_dev);
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

	TP_printk("dev %d,%d ino %lu flags %u len %u lblk %llu goal %llu lleft %llu lright %llu pleft %llu pright %llu ",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->flags, __entry->len,
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
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
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
		__entry->dev_major = MAJOR(ar->inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(ar->inode->i_sb->s_dev);
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

	TP_printk("dev %d,%d ino %lu flags %u len %u block %llu lblk %llu goal %llu lleft %llu lright %llu pleft %llu pright %llu ",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->flags,
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
		 int flags),

	TP_ARGS(inode, block, count, flags),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(      umode_t, mode			)
		__field(	__u64,	block			)
		__field(	unsigned long,	count		)
		__field(	 int,	flags			)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(inode->i_sb->s_dev);
		__entry->ino		= inode->i_ino;
		__entry->mode		= inode->i_mode;
		__entry->block		= block;
		__entry->count		= count;
		__entry->flags		= flags;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o block %llu count %lu flags %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->mode, __entry->block, __entry->count,
		  __entry->flags)
);

TRACE_EVENT(ext4_sync_file,
	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
		struct dentry *dentry = file->f_path.dentry;

		__entry->dev_major	= MAJOR(dentry->d_inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(dentry->d_inode->i_sb->s_dev);
		__entry->ino		= dentry->d_inode->i_ino;
		__entry->datasync	= datasync;
		__entry->parent		= dentry->d_parent->d_inode->i_ino;
	),

	TP_printk("dev %d,%d ino %ld parent %ld datasync %d ",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->parent, __entry->datasync)
);

TRACE_EVENT(ext4_sync_fs,
	TP_PROTO(struct super_block *sb, int wait),

	TP_ARGS(sb, wait),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	int,	wait			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(sb->s_dev);
		__entry->dev_minor = MINOR(sb->s_dev);
		__entry->wait	= wait;
	),

	TP_printk("dev %d,%d wait %d", __entry->dev_major,
		  __entry->dev_minor, __entry->wait)
);

TRACE_EVENT(ext4_alloc_da_blocks,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field( unsigned int,	data_blocks	)
		__field( unsigned int,	meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu data_blocks %u meta_blocks %u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->data_blocks, __entry->meta_blocks)
);

TRACE_EVENT(ext4_mballoc_alloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	__u16,	found			)
		__field(	__u16,	groups			)
		__field(	__u16,	buddy			)
		__field(	__u16,	flags			)
		__field(	__u16,	tail			)
		__field(	__u8,	cr			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	goal_logical		)
		__field(	  int,	goal_start		)
		__field(	__u32, 	goal_group		)
		__field(	  int,	goal_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(ac->ac_inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(ac->ac_inode->i_sb->s_dev);
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->found		= ac->ac_found;
		__entry->flags		= ac->ac_flags;
		__entry->groups		= ac->ac_groups_scanned;
		__entry->buddy		= ac->ac_buddy;
		__entry->tail		= ac->ac_tail;
		__entry->cr		= ac->ac_criteria;
		__entry->orig_logical	= ac->ac_o_ex.fe_logical;
		__entry->orig_start	= ac->ac_o_ex.fe_start;
		__entry->orig_group	= ac->ac_o_ex.fe_group;
		__entry->orig_len	= ac->ac_o_ex.fe_len;
		__entry->goal_logical	= ac->ac_g_ex.fe_logical;
		__entry->goal_start	= ac->ac_g_ex.fe_start;
		__entry->goal_group	= ac->ac_g_ex.fe_group;
		__entry->goal_len	= ac->ac_g_ex.fe_len;
		__entry->result_logical	= ac->ac_f_ex.fe_logical;
		__entry->result_start	= ac->ac_f_ex.fe_start;
		__entry->result_group	= ac->ac_f_ex.fe_group;
		__entry->result_len	= ac->ac_f_ex.fe_len;
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u goal %u/%d/%u@%u "
		  "result %u/%d/%u@%u blks %u grps %u cr %u flags 0x%04x "
		  "tail %u broken %u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->goal_group, __entry->goal_start,
		  __entry->goal_len, __entry->goal_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical,
		  __entry->found, __entry->groups, __entry->cr,
		  __entry->flags, __entry->tail,
		  __entry->buddy ? 1 << __entry->buddy : 0)
);

TRACE_EVENT(ext4_mballoc_prealloc,
	TP_PROTO(struct ext4_allocation_context *ac),

	TP_ARGS(ac),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	__u32, 	orig_logical		)
		__field(	  int,	orig_start		)
		__field(	__u32, 	orig_group		)
		__field(	  int,	orig_len		)
		__field(	__u32, 	result_logical		)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(ac->ac_inode->i_sb->s_dev);
		__entry->dev_minor	= MINOR(ac->ac_inode->i_sb->s_dev);
		__entry->ino		= ac->ac_inode->i_ino;
		__entry->orig_logical	= ac->ac_o_ex.fe_logical;
		__entry->orig_start	= ac->ac_o_ex.fe_start;
		__entry->orig_group	= ac->ac_o_ex.fe_group;
		__entry->orig_len	= ac->ac_o_ex.fe_len;
		__entry->result_logical	= ac->ac_b_ex.fe_logical;
		__entry->result_start	= ac->ac_b_ex.fe_start;
		__entry->result_group	= ac->ac_b_ex.fe_group;
		__entry->result_len	= ac->ac_b_ex.fe_len;
	),

	TP_printk("dev %d,%d inode %lu orig %u/%d/%u@%u result %u/%d/%u@%u",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->orig_group, __entry->orig_start,
		  __entry->orig_len, __entry->orig_logical,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len, __entry->result_logical)
);

DECLARE_EVENT_CLASS(ext4__mballoc,
	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	  int,	result_start		)
		__field(	__u32, 	result_group		)
		__field(	  int,	result_len		)
	),

	TP_fast_assign(
		__entry->dev_major	= MAJOR(sb->s_dev);
		__entry->dev_minor	= MINOR(sb->s_dev);
		__entry->ino		= inode ? inode->i_ino : 0;
		__entry->result_start	= start;
		__entry->result_group	= group;
		__entry->result_len	= len;
	),

	TP_printk("dev %d,%d inode %lu extent %u/%d/%u ",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->result_group, __entry->result_start,
		  __entry->result_len)
);

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_discard,

	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
);

DEFINE_EVENT(ext4__mballoc, ext4_mballoc_free,

	TP_PROTO(struct super_block *sb,
		 struct inode *inode,
		 ext4_group_t group,
		 ext4_grpblk_t start,
		 ext4_grpblk_t len),

	TP_ARGS(sb, inode, group, start, len)
);

TRACE_EVENT(ext4_forget,
	TP_PROTO(struct inode *inode, int is_metadata, __u64 block),

	TP_ARGS(inode, is_metadata, block),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	int,	is_metadata		)
		__field(	__u64,	block			)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->is_metadata = is_metadata;
		__entry->block	= block;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o is_metadata %d block %llu",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->mode,
		  __entry->is_metadata, __entry->block)
);

TRACE_EVENT(ext4_da_update_reserve_space,
	TP_PROTO(struct inode *inode, int used_blocks),

	TP_ARGS(inode, used_blocks),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	used_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->used_blocks = used_blocks;
		__entry->reserved_data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
		__entry->allocated_meta_blocks = EXT4_I(inode)->i_allocated_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu used_blocks %d reserved_data_blocks %d reserved_meta_blocks %d allocated_meta_blocks %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino, __entry->mode,
		  (unsigned long long) __entry->i_blocks,
		  __entry->used_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks)
);

TRACE_EVENT(ext4_da_reserve_space,
	TP_PROTO(struct inode *inode, int md_needed),

	TP_ARGS(inode, md_needed),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	md_needed		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->md_needed = md_needed;
		__entry->reserved_data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu md_needed %d reserved_data_blocks %d reserved_meta_blocks %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->mode, (unsigned long long) __entry->i_blocks,
		  __entry->md_needed, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks)
);

TRACE_EVENT(ext4_da_release_space,
	TP_PROTO(struct inode *inode, int freed_blocks),

	TP_ARGS(inode, freed_blocks),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	ino_t,	ino			)
		__field(	umode_t, mode			)
		__field(	__u64,	i_blocks		)
		__field(	int,	freed_blocks		)
		__field(	int,	reserved_data_blocks	)
		__field(	int,	reserved_meta_blocks	)
		__field(	int,	allocated_meta_blocks	)
	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(inode->i_sb->s_dev);
		__entry->dev_minor = MINOR(inode->i_sb->s_dev);
		__entry->ino	= inode->i_ino;
		__entry->mode	= inode->i_mode;
		__entry->i_blocks = inode->i_blocks;
		__entry->freed_blocks = freed_blocks;
		__entry->reserved_data_blocks = EXT4_I(inode)->i_reserved_data_blocks;
		__entry->reserved_meta_blocks = EXT4_I(inode)->i_reserved_meta_blocks;
		__entry->allocated_meta_blocks = EXT4_I(inode)->i_allocated_meta_blocks;
	),

	TP_printk("dev %d,%d ino %lu mode 0%o i_blocks %llu freed_blocks %d reserved_data_blocks %d reserved_meta_blocks %d allocated_meta_blocks %d",
		  __entry->dev_major, __entry->dev_minor,
		  (unsigned long) __entry->ino,
		  __entry->mode, (unsigned long long) __entry->i_blocks,
		  __entry->freed_blocks, __entry->reserved_data_blocks,
		  __entry->reserved_meta_blocks, __entry->allocated_meta_blocks)
);

DECLARE_EVENT_CLASS(ext4__bitmap_load,
	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group),

	TP_STRUCT__entry(
		__field(	int,   dev_major                )
		__field(	int,   dev_minor                )
		__field(	__u32,	group			)

	),

	TP_fast_assign(
		__entry->dev_major = MAJOR(sb->s_dev);
		__entry->dev_minor = MINOR(sb->s_dev);
		__entry->group	= group;
	),

	TP_printk("dev %d,%d group %u",
		  __entry->dev_major, __entry->dev_minor, __entry->group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

DEFINE_EVENT(ext4__bitmap_load, ext4_mb_buddy_bitmap_load,

	TP_PROTO(struct super_block *sb, unsigned long group),

	TP_ARGS(sb, group)
);

#endif /* _TRACE_EXT4_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
