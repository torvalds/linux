/*
 * segbuf.h - NILFS Segment buffer prototypes and definitions
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */
#ifndef _NILFS_SEGBUF_H
#define _NILFS_SEGBUF_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/completion.h>

/**
 * struct nilfs_segsum_info - On-memory segment summary
 * @flags: Flags
 * @nfinfo: Number of file information structures
 * @nblocks: Number of blocks included in the partial segment
 * @nsumblk: Number of summary blocks
 * @sumbytes: Byte count of segment summary
 * @nfileblk: Total number of file blocks
 * @seg_seq: Segment sequence number
 * @ctime: Creation time
 * @next: Block number of the next full segment
 */
struct nilfs_segsum_info {
	unsigned int		flags;
	unsigned long		nfinfo;
	unsigned long		nblocks;
	unsigned long		nsumblk;
	unsigned long		sumbytes;
	unsigned long		nfileblk;
	u64			seg_seq;
	time_t			ctime;
	sector_t		next;
};

/* macro for the flags */
#define NILFS_SEG_HAS_SR(sum)    ((sum)->flags & NILFS_SS_SR)
#define NILFS_SEG_LOGBGN(sum)    ((sum)->flags & NILFS_SS_LOGBGN)
#define NILFS_SEG_LOGEND(sum)    ((sum)->flags & NILFS_SS_LOGEND)
#define NILFS_SEG_DSYNC(sum)     ((sum)->flags & NILFS_SS_SYNDT)
#define NILFS_SEG_SIMPLEX(sum) \
	(((sum)->flags & (NILFS_SS_LOGBGN | NILFS_SS_LOGEND)) == \
	 (NILFS_SS_LOGBGN | NILFS_SS_LOGEND))

#define NILFS_SEG_EMPTY(sum)	((sum)->nblocks == (sum)->nsumblk)

/**
 * struct nilfs_segment_buffer - Segment buffer
 * @sb_super: back pointer to a superblock struct
 * @sb_list: List head to chain this structure
 * @sb_sum: On-memory segment summary
 * @sb_segnum: Index number of the full segment
 * @sb_nextnum: Index number of the next full segment
 * @sb_fseg_start: Start block number of the full segment
 * @sb_fseg_end: End block number of the full segment
 * @sb_pseg_start: Disk block number of partial segment
 * @sb_rest_blocks: Number of residual blocks in the current segment
 * @sb_segsum_buffers: List of buffers for segment summaries
 * @sb_payload_buffers: List of buffers for segment payload
 * @sb_nbio: Number of flying bio requests
 * @sb_err: I/O error status
 * @sb_bio_event: Completion event of log writing
 */
struct nilfs_segment_buffer {
	struct super_block     *sb_super;
	struct list_head	sb_list;

	/* Segment information */
	struct nilfs_segsum_info sb_sum;
	__u64			sb_segnum;
	__u64			sb_nextnum;
	sector_t		sb_fseg_start, sb_fseg_end;
	sector_t		sb_pseg_start;
	unsigned		sb_rest_blocks;

	/* Buffers */
	struct list_head	sb_segsum_buffers;
	struct list_head	sb_payload_buffers; /* including super root */

	/* io status */
	int			sb_nbio;
	atomic_t		sb_err;
	struct completion	sb_bio_event;
};

#define NILFS_LIST_SEGBUF(head)  \
	list_entry((head), struct nilfs_segment_buffer, sb_list)
#define NILFS_NEXT_SEGBUF(segbuf)  NILFS_LIST_SEGBUF((segbuf)->sb_list.next)
#define NILFS_PREV_SEGBUF(segbuf)  NILFS_LIST_SEGBUF((segbuf)->sb_list.prev)
#define NILFS_LAST_SEGBUF(head)    NILFS_LIST_SEGBUF((head)->prev)
#define NILFS_FIRST_SEGBUF(head)   NILFS_LIST_SEGBUF((head)->next)
#define NILFS_SEGBUF_IS_LAST(segbuf, head)  ((segbuf)->sb_list.next == (head))

#define nilfs_for_each_segbuf_before(s, t, h) \
	for ((s) = NILFS_FIRST_SEGBUF(h); (s) != (t); \
	     (s) = NILFS_NEXT_SEGBUF(s))

#define NILFS_SEGBUF_FIRST_BH(head)  \
	(list_entry((head)->next, struct buffer_head, b_assoc_buffers))
#define NILFS_SEGBUF_NEXT_BH(bh)  \
	(list_entry((bh)->b_assoc_buffers.next, struct buffer_head, \
		    b_assoc_buffers))
#define NILFS_SEGBUF_BH_IS_LAST(bh, head)  ((bh)->b_assoc_buffers.next == head)


int __init nilfs_init_segbuf_cache(void);
void nilfs_destroy_segbuf_cache(void);
struct nilfs_segment_buffer *nilfs_segbuf_new(struct super_block *);
void nilfs_segbuf_free(struct nilfs_segment_buffer *);
void nilfs_segbuf_map(struct nilfs_segment_buffer *, __u64, unsigned long,
		      struct the_nilfs *);
void nilfs_segbuf_map_cont(struct nilfs_segment_buffer *segbuf,
			   struct nilfs_segment_buffer *prev);
void nilfs_segbuf_set_next_segnum(struct nilfs_segment_buffer *, __u64,
				  struct the_nilfs *);
int nilfs_segbuf_reset(struct nilfs_segment_buffer *, unsigned, time_t);
int nilfs_segbuf_extend_segsum(struct nilfs_segment_buffer *);
int nilfs_segbuf_extend_payload(struct nilfs_segment_buffer *,
				struct buffer_head **);
void nilfs_segbuf_fill_in_segsum(struct nilfs_segment_buffer *);
void nilfs_segbuf_fill_in_segsum_crc(struct nilfs_segment_buffer *, u32);
void nilfs_segbuf_fill_in_data_crc(struct nilfs_segment_buffer *, u32);

static inline void
nilfs_segbuf_add_segsum_buffer(struct nilfs_segment_buffer *segbuf,
			       struct buffer_head *bh)
{
	list_add_tail(&bh->b_assoc_buffers, &segbuf->sb_segsum_buffers);
	segbuf->sb_sum.nblocks++;
	segbuf->sb_sum.nsumblk++;
}

static inline void
nilfs_segbuf_add_payload_buffer(struct nilfs_segment_buffer *segbuf,
				struct buffer_head *bh)
{
	list_add_tail(&bh->b_assoc_buffers, &segbuf->sb_payload_buffers);
	segbuf->sb_sum.nblocks++;
}

static inline void
nilfs_segbuf_add_file_buffer(struct nilfs_segment_buffer *segbuf,
			     struct buffer_head *bh)
{
	get_bh(bh);
	nilfs_segbuf_add_payload_buffer(segbuf, bh);
	segbuf->sb_sum.nfileblk++;
}

void nilfs_clear_logs(struct list_head *logs);
void nilfs_truncate_logs(struct list_head *logs,
			 struct nilfs_segment_buffer *last);
int nilfs_write_logs(struct list_head *logs, struct the_nilfs *nilfs);
int nilfs_wait_on_logs(struct list_head *logs);

static inline void nilfs_destroy_logs(struct list_head *logs)
{
	nilfs_truncate_logs(logs, NULL);
}

#endif /* _NILFS_SEGBUF_H */
