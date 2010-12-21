/*
 * Copyright (c) 2009, Christoph Hellwig
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfs

#if !defined(_TRACE_XFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XFS_H

#include <linux/tracepoint.h>

struct xfs_agf;
struct xfs_alloc_arg;
struct xfs_attr_list_context;
struct xfs_buf_log_item;
struct xfs_da_args;
struct xfs_da_node_entry;
struct xfs_dquot;
struct xlog_ticket;
struct log;
struct xlog_recover;
struct xlog_recover_item;
struct xfs_buf_log_format;
struct xfs_inode_log_format;

DECLARE_EVENT_CLASS(xfs_attr_list_class,
	TP_PROTO(struct xfs_attr_list_context *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, alist)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor->hashval;
		__entry->blkno = ctx->cursor->blkno;
		__entry->offset = ctx->cursor->offset;
		__entry->alist = ctx->alist;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->flags = ctx->flags;
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "alist 0x%p size %u count %u firstu %u flags %d %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->alist,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __entry->flags,
		   __print_flags(__entry->flags, "|", XFS_ATTR_FLAGS)
	)
)

#define DEFINE_ATTR_LIST_EVENT(name) \
DEFINE_EVENT(xfs_attr_list_class, name, \
	TP_PROTO(struct xfs_attr_list_context *ctx), \
	TP_ARGS(ctx))
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_sf_all);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_leaf_end);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_full);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_add);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_wrong_blk);
DEFINE_ATTR_LIST_EVENT(xfs_attr_list_notfound);

DECLARE_EVENT_CLASS(xfs_perag_class,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int refcount,
		 unsigned long caller_ip),
	TP_ARGS(mp, agno, refcount, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, refcount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->refcount = refcount;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno %u refcount %d caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->refcount,
		  (char *)__entry->caller_ip)
);

#define DEFINE_PERAG_REF_EVENT(name)	\
DEFINE_EVENT(xfs_perag_class, name,	\
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, int refcount,	\
		 unsigned long caller_ip),					\
	TP_ARGS(mp, agno, refcount, caller_ip))
DEFINE_PERAG_REF_EVENT(xfs_perag_get);
DEFINE_PERAG_REF_EVENT(xfs_perag_get_tag);
DEFINE_PERAG_REF_EVENT(xfs_perag_put);
DEFINE_PERAG_REF_EVENT(xfs_perag_set_reclaim);
DEFINE_PERAG_REF_EVENT(xfs_perag_clear_reclaim);

TRACE_EVENT(xfs_attr_list_node_descend,
	TP_PROTO(struct xfs_attr_list_context *ctx,
		 struct xfs_da_node_entry *btree),
	TP_ARGS(ctx, btree),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(u32, hashval)
		__field(u32, blkno)
		__field(u32, offset)
		__field(void *, alist)
		__field(int, bufsize)
		__field(int, count)
		__field(int, firstu)
		__field(int, dupcnt)
		__field(int, flags)
		__field(u32, bt_hashval)
		__field(u32, bt_before)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ctx->dp)->i_sb->s_dev;
		__entry->ino = ctx->dp->i_ino;
		__entry->hashval = ctx->cursor->hashval;
		__entry->blkno = ctx->cursor->blkno;
		__entry->offset = ctx->cursor->offset;
		__entry->alist = ctx->alist;
		__entry->bufsize = ctx->bufsize;
		__entry->count = ctx->count;
		__entry->firstu = ctx->firstu;
		__entry->flags = ctx->flags;
		__entry->bt_hashval = be32_to_cpu(btree->hashval);
		__entry->bt_before = be32_to_cpu(btree->before);
	),
	TP_printk("dev %d:%d ino 0x%llx cursor h/b/o 0x%x/0x%x/%u dupcnt %u "
		  "alist 0x%p size %u count %u firstu %u flags %d %s "
		  "node hashval %u, node before %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		   __entry->ino,
		   __entry->hashval,
		   __entry->blkno,
		   __entry->offset,
		   __entry->dupcnt,
		   __entry->alist,
		   __entry->bufsize,
		   __entry->count,
		   __entry->firstu,
		   __entry->flags,
		   __print_flags(__entry->flags, "|", XFS_ATTR_FLAGS),
		   __entry->bt_hashval,
		   __entry->bt_before)
);

TRACE_EVENT(xfs_iext_insert,
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx,
		 struct xfs_bmbt_irec *r, int state, unsigned long caller_ip),
	TP_ARGS(ip, idx, r, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_extnum_t, idx)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(int, bmap_state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->idx = idx;
		__entry->startoff = r->br_startoff;
		__entry->startblock = r->br_startblock;
		__entry->blockcount = r->br_blockcount;
		__entry->state = r->br_state;
		__entry->bmap_state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx state %s idx %ld "
		  "offset %lld block %lld count %lld flag %d caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->bmap_state, "|", XFS_BMAP_EXT_FLAGS),
		  (long)__entry->idx,
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  (char *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_bmap_class,
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx, int state,
		 unsigned long caller_ip),
	TP_ARGS(ip, idx, state, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_extnum_t, idx)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
		__field(xfs_exntst_t, state)
		__field(int, bmap_state)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		struct xfs_ifork	*ifp = (state & BMAP_ATTRFORK) ?
						ip->i_afp : &ip->i_df;
		struct xfs_bmbt_irec	r;

		xfs_bmbt_get_all(xfs_iext_get_ext(ifp, idx), &r);
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->idx = idx;
		__entry->startoff = r.br_startoff;
		__entry->startblock = r.br_startblock;
		__entry->blockcount = r.br_blockcount;
		__entry->state = r.br_state;
		__entry->bmap_state = state;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx state %s idx %ld "
		  "offset %lld block %lld count %lld flag %d caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->bmap_state, "|", XFS_BMAP_EXT_FLAGS),
		  (long)__entry->idx,
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount,
		  __entry->state,
		  (char *)__entry->caller_ip)
)

#define DEFINE_BMAP_EVENT(name) \
DEFINE_EVENT(xfs_bmap_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_extnum_t idx, int state, \
		 unsigned long caller_ip), \
	TP_ARGS(ip, idx, state, caller_ip))
DEFINE_BMAP_EVENT(xfs_iext_remove);
DEFINE_BMAP_EVENT(xfs_bmap_pre_update);
DEFINE_BMAP_EVENT(xfs_bmap_post_update);
DEFINE_BMAP_EVENT(xfs_extlist);

DECLARE_EVENT_CLASS(xfs_buf_class,
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip),
	TP_ARGS(bp, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(size_t, buffer_length)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->buffer_length = bp->b_buffer_length;
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = xfs_buf_lock_value(bp);
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d flags %s caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->buffer_length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_EVENT(name) \
DEFINE_EVENT(xfs_buf_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned long caller_ip), \
	TP_ARGS(bp, caller_ip))
DEFINE_BUF_EVENT(xfs_buf_init);
DEFINE_BUF_EVENT(xfs_buf_free);
DEFINE_BUF_EVENT(xfs_buf_hold);
DEFINE_BUF_EVENT(xfs_buf_rele);
DEFINE_BUF_EVENT(xfs_buf_iodone);
DEFINE_BUF_EVENT(xfs_buf_iorequest);
DEFINE_BUF_EVENT(xfs_buf_bawrite);
DEFINE_BUF_EVENT(xfs_buf_bdwrite);
DEFINE_BUF_EVENT(xfs_buf_lock);
DEFINE_BUF_EVENT(xfs_buf_lock_done);
DEFINE_BUF_EVENT(xfs_buf_cond_lock);
DEFINE_BUF_EVENT(xfs_buf_unlock);
DEFINE_BUF_EVENT(xfs_buf_iowait);
DEFINE_BUF_EVENT(xfs_buf_iowait_done);
DEFINE_BUF_EVENT(xfs_buf_delwri_queue);
DEFINE_BUF_EVENT(xfs_buf_delwri_dequeue);
DEFINE_BUF_EVENT(xfs_buf_delwri_split);
DEFINE_BUF_EVENT(xfs_buf_get_uncached);
DEFINE_BUF_EVENT(xfs_bdstrat_shut);
DEFINE_BUF_EVENT(xfs_buf_item_relse);
DEFINE_BUF_EVENT(xfs_buf_item_iodone);
DEFINE_BUF_EVENT(xfs_buf_item_iodone_async);
DEFINE_BUF_EVENT(xfs_buf_error_relse);
DEFINE_BUF_EVENT(xfs_trans_read_buf_io);
DEFINE_BUF_EVENT(xfs_trans_read_buf_shut);

/* not really buffer traces, but the buf provides useful information */
DEFINE_BUF_EVENT(xfs_btree_corrupt);
DEFINE_BUF_EVENT(xfs_da_btree_corrupt);
DEFINE_BUF_EVENT(xfs_reset_dqcounts);
DEFINE_BUF_EVENT(xfs_inode_item_push);

/* pass flags explicitly */
DECLARE_EVENT_CLASS(xfs_buf_flags_class,
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip),
	TP_ARGS(bp, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(size_t, buffer_length)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(unsigned, flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->buffer_length = bp->b_buffer_length;
		__entry->flags = flags;
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = xfs_buf_lock_value(bp);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d flags %s caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->buffer_length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_BUF_FLAGS_EVENT(name) \
DEFINE_EVENT(xfs_buf_flags_class, name, \
	TP_PROTO(struct xfs_buf *bp, unsigned flags, unsigned long caller_ip), \
	TP_ARGS(bp, flags, caller_ip))
DEFINE_BUF_FLAGS_EVENT(xfs_buf_find);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_get);
DEFINE_BUF_FLAGS_EVENT(xfs_buf_read);

TRACE_EVENT(xfs_buf_ioerror,
	TP_PROTO(struct xfs_buf *bp, int error, unsigned long caller_ip),
	TP_ARGS(bp, error, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, bno)
		__field(size_t, buffer_length)
		__field(unsigned, flags)
		__field(int, hold)
		__field(int, pincount)
		__field(unsigned, lockval)
		__field(int, error)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = bp->b_target->bt_dev;
		__entry->bno = bp->b_bn;
		__entry->buffer_length = bp->b_buffer_length;
		__entry->hold = atomic_read(&bp->b_hold);
		__entry->pincount = atomic_read(&bp->b_pin_count);
		__entry->lockval = xfs_buf_lock_value(bp);
		__entry->error = error;
		__entry->flags = bp->b_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d error %d flags %s caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->bno,
		  __entry->buffer_length,
		  __entry->hold,
		  __entry->pincount,
		  __entry->lockval,
		  __entry->error,
		  __print_flags(__entry->flags, "|", XFS_BUF_FLAGS),
		  (void *)__entry->caller_ip)
);

DECLARE_EVENT_CLASS(xfs_buf_item_class,
	TP_PROTO(struct xfs_buf_log_item *bip),
	TP_ARGS(bip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_daddr_t, buf_bno)
		__field(size_t, buf_len)
		__field(int, buf_hold)
		__field(int, buf_pincount)
		__field(int, buf_lockval)
		__field(unsigned, buf_flags)
		__field(unsigned, bli_recur)
		__field(int, bli_refcount)
		__field(unsigned, bli_flags)
		__field(void *, li_desc)
		__field(unsigned, li_flags)
	),
	TP_fast_assign(
		__entry->dev = bip->bli_buf->b_target->bt_dev;
		__entry->bli_flags = bip->bli_flags;
		__entry->bli_recur = bip->bli_recur;
		__entry->bli_refcount = atomic_read(&bip->bli_refcount);
		__entry->buf_bno = bip->bli_buf->b_bn;
		__entry->buf_len = bip->bli_buf->b_buffer_length;
		__entry->buf_flags = bip->bli_buf->b_flags;
		__entry->buf_hold = atomic_read(&bip->bli_buf->b_hold);
		__entry->buf_pincount = atomic_read(&bip->bli_buf->b_pin_count);
		__entry->buf_lockval = xfs_buf_lock_value(bip->bli_buf);
		__entry->li_desc = bip->bli_item.li_desc;
		__entry->li_flags = bip->bli_item.li_flags;
	),
	TP_printk("dev %d:%d bno 0x%llx len 0x%zx hold %d pincount %d "
		  "lock %d flags %s recur %d refcount %d bliflags %s "
		  "lidesc 0x%p liflags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long long)__entry->buf_bno,
		  __entry->buf_len,
		  __entry->buf_hold,
		  __entry->buf_pincount,
		  __entry->buf_lockval,
		  __print_flags(__entry->buf_flags, "|", XFS_BUF_FLAGS),
		  __entry->bli_recur,
		  __entry->bli_refcount,
		  __print_flags(__entry->bli_flags, "|", XFS_BLI_FLAGS),
		  __entry->li_desc,
		  __print_flags(__entry->li_flags, "|", XFS_LI_FLAGS))
)

#define DEFINE_BUF_ITEM_EVENT(name) \
DEFINE_EVENT(xfs_buf_item_class, name, \
	TP_PROTO(struct xfs_buf_log_item *bip), \
	TP_ARGS(bip))
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_size_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_format_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_pin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unpin_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_trylock);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unlock);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_unlock_stale);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_committed);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_push);
DEFINE_BUF_ITEM_EVENT(xfs_buf_item_pushbuf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_get_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb);
DEFINE_BUF_ITEM_EVENT(xfs_trans_getsb_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_read_buf_recur);
DEFINE_BUF_ITEM_EVENT(xfs_trans_log_buf);
DEFINE_BUF_ITEM_EVENT(xfs_trans_brelse);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bjoin);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold);
DEFINE_BUF_ITEM_EVENT(xfs_trans_bhold_release);
DEFINE_BUF_ITEM_EVENT(xfs_trans_binval);

DECLARE_EVENT_CLASS(xfs_lock_class,
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags,
		 unsigned long caller_ip),
	TP_ARGS(ip,  lock_flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, lock_flags)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->lock_flags = lock_flags;
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx flags %s caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->lock_flags, "|", XFS_LOCK_FLAGS),
		  (void *)__entry->caller_ip)
)

#define DEFINE_LOCK_EVENT(name) \
DEFINE_EVENT(xfs_lock_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned lock_flags, \
		 unsigned long caller_ip), \
	TP_ARGS(ip,  lock_flags, caller_ip))
DEFINE_LOCK_EVENT(xfs_ilock);
DEFINE_LOCK_EVENT(xfs_ilock_nowait);
DEFINE_LOCK_EVENT(xfs_ilock_demote);
DEFINE_LOCK_EVENT(xfs_iunlock);

DECLARE_EVENT_CLASS(xfs_inode_class,
	TP_PROTO(struct xfs_inode *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
	),
	TP_printk("dev %d:%d ino 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino)
)

#define DEFINE_INODE_EVENT(name) \
DEFINE_EVENT(xfs_inode_class, name, \
	TP_PROTO(struct xfs_inode *ip), \
	TP_ARGS(ip))
DEFINE_INODE_EVENT(xfs_iget_skip);
DEFINE_INODE_EVENT(xfs_iget_reclaim);
DEFINE_INODE_EVENT(xfs_iget_reclaim_fail);
DEFINE_INODE_EVENT(xfs_iget_hit);
DEFINE_INODE_EVENT(xfs_iget_miss);

DEFINE_INODE_EVENT(xfs_getattr);
DEFINE_INODE_EVENT(xfs_setattr);
DEFINE_INODE_EVENT(xfs_readlink);
DEFINE_INODE_EVENT(xfs_alloc_file_space);
DEFINE_INODE_EVENT(xfs_free_file_space);
DEFINE_INODE_EVENT(xfs_readdir);
#ifdef CONFIG_XFS_POSIX_ACL
DEFINE_INODE_EVENT(xfs_check_acl);
#endif
DEFINE_INODE_EVENT(xfs_vm_bmap);
DEFINE_INODE_EVENT(xfs_file_ioctl);
DEFINE_INODE_EVENT(xfs_file_compat_ioctl);
DEFINE_INODE_EVENT(xfs_ioctl_setattr);
DEFINE_INODE_EVENT(xfs_file_fsync);
DEFINE_INODE_EVENT(xfs_destroy_inode);
DEFINE_INODE_EVENT(xfs_write_inode);
DEFINE_INODE_EVENT(xfs_evict_inode);

DEFINE_INODE_EVENT(xfs_dquot_dqalloc);
DEFINE_INODE_EVENT(xfs_dquot_dqdetach);

DECLARE_EVENT_CLASS(xfs_iref_class,
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip),
	TP_ARGS(ip, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, count)
		__field(int, pincount)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->count = atomic_read(&VFS_I(ip)->i_count);
		__entry->pincount = atomic_read(&ip->i_pincount);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d ino 0x%llx count %d pincount %d caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->count,
		  __entry->pincount,
		  (char *)__entry->caller_ip)
)

#define DEFINE_IREF_EVENT(name) \
DEFINE_EVENT(xfs_iref_class, name, \
	TP_PROTO(struct xfs_inode *ip, unsigned long caller_ip), \
	TP_ARGS(ip, caller_ip))
DEFINE_IREF_EVENT(xfs_ihold);
DEFINE_IREF_EVENT(xfs_irele);
DEFINE_IREF_EVENT(xfs_inode_pin);
DEFINE_IREF_EVENT(xfs_inode_unpin);
DEFINE_IREF_EVENT(xfs_inode_unpin_nowait);

DECLARE_EVENT_CLASS(xfs_namespace_class,
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name),
	TP_ARGS(dp, name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, dp_ino)
		__dynamic_array(char, name, name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(dp)->i_sb->s_dev;
		__entry->dp_ino = dp->i_ino;
		memcpy(__get_str(name), name->name, name->len);
	),
	TP_printk("dev %d:%d dp ino 0x%llx name %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dp_ino,
		  __get_str(name))
)

#define DEFINE_NAMESPACE_EVENT(name) \
DEFINE_EVENT(xfs_namespace_class, name, \
	TP_PROTO(struct xfs_inode *dp, struct xfs_name *name), \
	TP_ARGS(dp, name))
DEFINE_NAMESPACE_EVENT(xfs_remove);
DEFINE_NAMESPACE_EVENT(xfs_link);
DEFINE_NAMESPACE_EVENT(xfs_lookup);
DEFINE_NAMESPACE_EVENT(xfs_create);
DEFINE_NAMESPACE_EVENT(xfs_symlink);

TRACE_EVENT(xfs_rename,
	TP_PROTO(struct xfs_inode *src_dp, struct xfs_inode *target_dp,
		 struct xfs_name *src_name, struct xfs_name *target_name),
	TP_ARGS(src_dp, target_dp, src_name, target_name),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, src_dp_ino)
		__field(xfs_ino_t, target_dp_ino)
		__dynamic_array(char, src_name, src_name->len)
		__dynamic_array(char, target_name, target_name->len)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(src_dp)->i_sb->s_dev;
		__entry->src_dp_ino = src_dp->i_ino;
		__entry->target_dp_ino = target_dp->i_ino;
		memcpy(__get_str(src_name), src_name->name, src_name->len);
		memcpy(__get_str(target_name), target_name->name, target_name->len);
	),
	TP_printk("dev %d:%d src dp ino 0x%llx target dp ino 0x%llx"
		  " src name %s target name %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->src_dp_ino,
		  __entry->target_dp_ino,
		  __get_str(src_name),
		  __get_str(target_name))
)

DECLARE_EVENT_CLASS(xfs_dquot_class,
	TP_PROTO(struct xfs_dquot *dqp),
	TP_ARGS(dqp),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, id)
		__field(unsigned, flags)
		__field(unsigned, nrefs)
		__field(unsigned long long, res_bcount)
		__field(unsigned long long, bcount)
		__field(unsigned long long, icount)
		__field(unsigned long long, blk_hardlimit)
		__field(unsigned long long, blk_softlimit)
		__field(unsigned long long, ino_hardlimit)
		__field(unsigned long long, ino_softlimit)
	), \
	TP_fast_assign(
		__entry->dev = dqp->q_mount->m_super->s_dev;
		__entry->id = be32_to_cpu(dqp->q_core.d_id);
		__entry->flags = dqp->dq_flags;
		__entry->nrefs = dqp->q_nrefs;
		__entry->res_bcount = dqp->q_res_bcount;
		__entry->bcount = be64_to_cpu(dqp->q_core.d_bcount);
		__entry->icount = be64_to_cpu(dqp->q_core.d_icount);
		__entry->blk_hardlimit =
			be64_to_cpu(dqp->q_core.d_blk_hardlimit);
		__entry->blk_softlimit =
			be64_to_cpu(dqp->q_core.d_blk_softlimit);
		__entry->ino_hardlimit =
			be64_to_cpu(dqp->q_core.d_ino_hardlimit);
		__entry->ino_softlimit =
			be64_to_cpu(dqp->q_core.d_ino_softlimit);
	),
	TP_printk("dev %d:%d id 0x%x flags %s nrefs %u res_bc 0x%llx "
		  "bcnt 0x%llx bhardlimit 0x%llx bsoftlimit 0x%llx "
		  "icnt 0x%llx ihardlimit 0x%llx isoftlimit 0x%llx]",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->id,
		  __print_flags(__entry->flags, "|", XFS_DQ_FLAGS),
		  __entry->nrefs,
		  __entry->res_bcount,
		  __entry->bcount,
		  __entry->blk_hardlimit,
		  __entry->blk_softlimit,
		  __entry->icount,
		  __entry->ino_hardlimit,
		  __entry->ino_softlimit)
)

#define DEFINE_DQUOT_EVENT(name) \
DEFINE_EVENT(xfs_dquot_class, name, \
	TP_PROTO(struct xfs_dquot *dqp), \
	TP_ARGS(dqp))
DEFINE_DQUOT_EVENT(xfs_dqadjust);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_want);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_dirty);
DEFINE_DQUOT_EVENT(xfs_dqreclaim_unlink);
DEFINE_DQUOT_EVENT(xfs_dqattach_found);
DEFINE_DQUOT_EVENT(xfs_dqattach_get);
DEFINE_DQUOT_EVENT(xfs_dqinit);
DEFINE_DQUOT_EVENT(xfs_dqreuse);
DEFINE_DQUOT_EVENT(xfs_dqalloc);
DEFINE_DQUOT_EVENT(xfs_dqtobp_read);
DEFINE_DQUOT_EVENT(xfs_dqread);
DEFINE_DQUOT_EVENT(xfs_dqread_fail);
DEFINE_DQUOT_EVENT(xfs_dqlookup_found);
DEFINE_DQUOT_EVENT(xfs_dqlookup_want);
DEFINE_DQUOT_EVENT(xfs_dqlookup_freelist);
DEFINE_DQUOT_EVENT(xfs_dqlookup_done);
DEFINE_DQUOT_EVENT(xfs_dqget_hit);
DEFINE_DQUOT_EVENT(xfs_dqget_miss);
DEFINE_DQUOT_EVENT(xfs_dqput);
DEFINE_DQUOT_EVENT(xfs_dqput_wait);
DEFINE_DQUOT_EVENT(xfs_dqput_free);
DEFINE_DQUOT_EVENT(xfs_dqrele);
DEFINE_DQUOT_EVENT(xfs_dqflush);
DEFINE_DQUOT_EVENT(xfs_dqflush_force);
DEFINE_DQUOT_EVENT(xfs_dqflush_done);

DECLARE_EVENT_CLASS(xfs_loggrant_class,
	TP_PROTO(struct log *log, struct xlog_ticket *tic),
	TP_ARGS(log, tic),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned, trans_type)
		__field(char, ocnt)
		__field(char, cnt)
		__field(int, curr_res)
		__field(int, unit_res)
		__field(unsigned int, flags)
		__field(int, reserveq)
		__field(int, writeq)
		__field(int, grant_reserve_cycle)
		__field(int, grant_reserve_bytes)
		__field(int, grant_write_cycle)
		__field(int, grant_write_bytes)
		__field(int, curr_cycle)
		__field(int, curr_block)
		__field(xfs_lsn_t, tail_lsn)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->trans_type = tic->t_trans_type;
		__entry->ocnt = tic->t_ocnt;
		__entry->cnt = tic->t_cnt;
		__entry->curr_res = tic->t_curr_res;
		__entry->unit_res = tic->t_unit_res;
		__entry->flags = tic->t_flags;
		__entry->reserveq = list_empty(&log->l_reserveq);
		__entry->writeq = list_empty(&log->l_writeq);
		xlog_crack_grant_head(&log->l_grant_reserve_head,
				&__entry->grant_reserve_cycle,
				&__entry->grant_reserve_bytes);
		xlog_crack_grant_head(&log->l_grant_write_head,
				&__entry->grant_write_cycle,
				&__entry->grant_write_bytes);
		__entry->curr_cycle = log->l_curr_cycle;
		__entry->curr_block = log->l_curr_block;
		__entry->tail_lsn = log->l_tail_lsn;
	),
	TP_printk("dev %d:%d type %s t_ocnt %u t_cnt %u t_curr_res %u "
		  "t_unit_res %u t_flags %s reserveq %s "
		  "writeq %s grant_reserve_cycle %d "
		  "grant_reserve_bytes %d grant_write_cycle %d "
		  "grant_write_bytes %d curr_cycle %d curr_block %d "
		  "tail_cycle %d tail_block %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __print_symbolic(__entry->trans_type, XFS_TRANS_TYPES),
		  __entry->ocnt,
		  __entry->cnt,
		  __entry->curr_res,
		  __entry->unit_res,
		  __print_flags(__entry->flags, "|", XLOG_TIC_FLAGS),
		  __entry->reserveq ? "empty" : "active",
		  __entry->writeq ? "empty" : "active",
		  __entry->grant_reserve_cycle,
		  __entry->grant_reserve_bytes,
		  __entry->grant_write_cycle,
		  __entry->grant_write_bytes,
		  __entry->curr_cycle,
		  __entry->curr_block,
		  CYCLE_LSN(__entry->tail_lsn),
		  BLOCK_LSN(__entry->tail_lsn)
	)
)

#define DEFINE_LOGGRANT_EVENT(name) \
DEFINE_EVENT(xfs_loggrant_class, name, \
	TP_PROTO(struct log *log, struct xlog_ticket *tic), \
	TP_ARGS(log, tic))
DEFINE_LOGGRANT_EVENT(xfs_log_done_nonperm);
DEFINE_LOGGRANT_EVENT(xfs_log_done_perm);
DEFINE_LOGGRANT_EVENT(xfs_log_reserve);
DEFINE_LOGGRANT_EVENT(xfs_log_umount_write);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_error);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_sleep1);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake1);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_sleep2);
DEFINE_LOGGRANT_EVENT(xfs_log_grant_wake2);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_error);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_sleep1);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_wake1);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_sleep2);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_write_wake2);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_regrant_reserve_sub);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_enter);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_exit);
DEFINE_LOGGRANT_EVENT(xfs_log_ungrant_sub);

DECLARE_EVENT_CLASS(xfs_file_class,
	TP_PROTO(struct xfs_inode *ip, size_t count, loff_t offset, int flags),
	TP_ARGS(ip, count, offset, flags),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fsize_t, new_size)
		__field(loff_t, offset)
		__field(size_t, count)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = ip->i_new_size;
		__entry->offset = offset;
		__entry->count = count;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx new_size 0x%llx "
		  "offset 0x%llx count 0x%zx ioflags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size,
		  __entry->offset,
		  __entry->count,
		  __print_flags(__entry->flags, "|", XFS_IO_FLAGS))
)

#define DEFINE_RW_EVENT(name)		\
DEFINE_EVENT(xfs_file_class, name,	\
	TP_PROTO(struct xfs_inode *ip, size_t count, loff_t offset, int flags),	\
	TP_ARGS(ip, count, offset, flags))
DEFINE_RW_EVENT(xfs_file_read);
DEFINE_RW_EVENT(xfs_file_buffered_write);
DEFINE_RW_EVENT(xfs_file_direct_write);
DEFINE_RW_EVENT(xfs_file_splice_read);
DEFINE_RW_EVENT(xfs_file_splice_write);

DECLARE_EVENT_CLASS(xfs_page_class,
	TP_PROTO(struct inode *inode, struct page *page, unsigned long off),
	TP_ARGS(inode, page, off),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(pgoff_t, pgoff)
		__field(loff_t, size)
		__field(unsigned long, offset)
		__field(int, delalloc)
		__field(int, unwritten)
	),
	TP_fast_assign(
		int delalloc = -1, unwritten = -1;

		if (page_has_buffers(page))
			xfs_count_page_state(page, &delalloc, &unwritten);
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = XFS_I(inode)->i_ino;
		__entry->pgoff = page_offset(page);
		__entry->size = i_size_read(inode);
		__entry->offset = off;
		__entry->delalloc = delalloc;
		__entry->unwritten = unwritten;
	),
	TP_printk("dev %d:%d ino 0x%llx pgoff 0x%lx size 0x%llx offset %lx "
		  "delalloc %d unwritten %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->pgoff,
		  __entry->size,
		  __entry->offset,
		  __entry->delalloc,
		  __entry->unwritten)
)

#define DEFINE_PAGE_EVENT(name)		\
DEFINE_EVENT(xfs_page_class, name,	\
	TP_PROTO(struct inode *inode, struct page *page, unsigned long off),	\
	TP_ARGS(inode, page, off))
DEFINE_PAGE_EVENT(xfs_writepage);
DEFINE_PAGE_EVENT(xfs_releasepage);
DEFINE_PAGE_EVENT(xfs_invalidatepage);

DECLARE_EVENT_CLASS(xfs_imap_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,
		 int type, struct xfs_bmbt_irec *irec),
	TP_ARGS(ip, offset, count, type, irec),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, size)
		__field(loff_t, new_size)
		__field(loff_t, offset)
		__field(size_t, count)
		__field(int, type)
		__field(xfs_fileoff_t, startoff)
		__field(xfs_fsblock_t, startblock)
		__field(xfs_filblks_t, blockcount)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = ip->i_new_size;
		__entry->offset = offset;
		__entry->count = count;
		__entry->type = type;
		__entry->startoff = irec ? irec->br_startoff : 0;
		__entry->startblock = irec ? irec->br_startblock : 0;
		__entry->blockcount = irec ? irec->br_blockcount : 0;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx new_size 0x%llx "
		  "offset 0x%llx count %zd type %s "
		  "startoff 0x%llx startblock %lld blockcount 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size,
		  __entry->offset,
		  __entry->count,
		  __print_symbolic(__entry->type, XFS_IO_TYPES),
		  __entry->startoff,
		  (__int64_t)__entry->startblock,
		  __entry->blockcount)
)

#define DEFINE_IOMAP_EVENT(name)	\
DEFINE_EVENT(xfs_imap_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count,	\
		 int type, struct xfs_bmbt_irec *irec),		\
	TP_ARGS(ip, offset, count, type, irec))
DEFINE_IOMAP_EVENT(xfs_map_blocks_found);
DEFINE_IOMAP_EVENT(xfs_map_blocks_alloc);
DEFINE_IOMAP_EVENT(xfs_get_blocks_found);
DEFINE_IOMAP_EVENT(xfs_get_blocks_alloc);

DECLARE_EVENT_CLASS(xfs_simple_io_class,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),
	TP_ARGS(ip, offset, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(loff_t, size)
		__field(loff_t, new_size)
		__field(loff_t, offset)
		__field(size_t, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = ip->i_new_size;
		__entry->offset = offset;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx new_size 0x%llx "
		  "offset 0x%llx count %zd",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size,
		  __entry->offset,
		  __entry->count)
);

#define DEFINE_SIMPLE_IO_EVENT(name)	\
DEFINE_EVENT(xfs_simple_io_class, name,	\
	TP_PROTO(struct xfs_inode *ip, xfs_off_t offset, ssize_t count),	\
	TP_ARGS(ip, offset, count))
DEFINE_SIMPLE_IO_EVENT(xfs_delalloc_enospc);
DEFINE_SIMPLE_IO_EVENT(xfs_unwritten_convert);
DEFINE_SIMPLE_IO_EVENT(xfs_get_blocks_notfound);


TRACE_EVENT(xfs_itruncate_start,
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size, int flag,
		 xfs_off_t toss_start, xfs_off_t toss_finish),
	TP_ARGS(ip, new_size, flag, toss_start, toss_finish),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fsize_t, new_size)
		__field(xfs_off_t, toss_start)
		__field(xfs_off_t, toss_finish)
		__field(int, flag)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = new_size;
		__entry->toss_start = toss_start;
		__entry->toss_finish = toss_finish;
		__entry->flag = flag;
	),
	TP_printk("dev %d:%d ino 0x%llx %s size 0x%llx new_size 0x%llx "
		  "toss start 0x%llx toss finish 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->flag, "|", XFS_ITRUNC_FLAGS),
		  __entry->size,
		  __entry->new_size,
		  __entry->toss_start,
		  __entry->toss_finish)
);

DECLARE_EVENT_CLASS(xfs_itrunc_class,
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size),
	TP_ARGS(ip, new_size),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fsize_t, new_size)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->new_size = new_size;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx new_size 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->new_size)
)

#define DEFINE_ITRUNC_EVENT(name) \
DEFINE_EVENT(xfs_itrunc_class, name, \
	TP_PROTO(struct xfs_inode *ip, xfs_fsize_t new_size), \
	TP_ARGS(ip, new_size))
DEFINE_ITRUNC_EVENT(xfs_itruncate_finish_start);
DEFINE_ITRUNC_EVENT(xfs_itruncate_finish_end);

TRACE_EVENT(xfs_pagecache_inval,
	TP_PROTO(struct xfs_inode *ip, xfs_off_t start, xfs_off_t finish),
	TP_ARGS(ip, start, finish),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_off_t, start)
		__field(xfs_off_t, finish)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->start = start;
		__entry->finish = finish;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx start 0x%llx finish 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->start,
		  __entry->finish)
);

TRACE_EVENT(xfs_bunmap,
	TP_PROTO(struct xfs_inode *ip, xfs_fileoff_t bno, xfs_filblks_t len,
		 int flags, unsigned long caller_ip),
	TP_ARGS(ip, bno, len, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(xfs_fsize_t, size)
		__field(xfs_fileoff_t, bno)
		__field(xfs_filblks_t, len)
		__field(unsigned long, caller_ip)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->size = ip->i_d.di_size;
		__entry->bno = bno;
		__entry->len = len;
		__entry->caller_ip = caller_ip;
		__entry->flags = flags;
	),
	TP_printk("dev %d:%d ino 0x%llx size 0x%llx bno 0x%llx len 0x%llx"
		  "flags %s caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->bno,
		  __entry->len,
		  __print_flags(__entry->flags, "|", XFS_BMAPI_FLAGS),
		  (void *)__entry->caller_ip)

);

#define XFS_BUSY_SYNC \
	{ 0,	"async" }, \
	{ 1,	"sync" }

TRACE_EVENT(xfs_alloc_busy,
	TP_PROTO(struct xfs_trans *trans, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len, int sync),
	TP_ARGS(trans, agno, agbno, len, sync),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(struct xfs_trans *, tp)
		__field(int, tid)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int, sync)
	),
	TP_fast_assign(
		__entry->dev = trans->t_mountp->m_super->s_dev;
		__entry->tp = trans;
		__entry->tid = trans->t_ticket->t_tid;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->sync = sync;
	),
	TP_printk("dev %d:%d trans 0x%p tid 0x%x agno %u agbno %u len %u %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tp,
		  __entry->tid,
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __print_symbolic(__entry->sync, XFS_BUSY_SYNC))

);

TRACE_EVENT(xfs_alloc_unbusy,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len),
	TP_ARGS(mp, agno, agbno, len),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len)
);

#define XFS_BUSY_STATES \
	{ 0,	"missing" }, \
	{ 1,	"found" }

TRACE_EVENT(xfs_alloc_busysearch,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno,
		 xfs_agblock_t agbno, xfs_extlen_t len, int found),
	TP_ARGS(mp, agno, agbno, len, found),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int, found)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->found = found;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __print_symbolic(__entry->found, XFS_BUSY_STATES))
);

TRACE_EVENT(xfs_trans_commit_lsn,
	TP_PROTO(struct xfs_trans *trans),
	TP_ARGS(trans),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(struct xfs_trans *, tp)
		__field(xfs_lsn_t, lsn)
	),
	TP_fast_assign(
		__entry->dev = trans->t_mountp->m_super->s_dev;
		__entry->tp = trans;
		__entry->lsn = trans->t_commit_lsn;
	),
	TP_printk("dev %d:%d trans 0x%p commit_lsn 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tp,
		  __entry->lsn)
);

TRACE_EVENT(xfs_agf,
	TP_PROTO(struct xfs_mount *mp, struct xfs_agf *agf, int flags,
		 unsigned long caller_ip),
	TP_ARGS(mp, agf, flags, caller_ip),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(int, flags)
		__field(__u32, length)
		__field(__u32, bno_root)
		__field(__u32, cnt_root)
		__field(__u32, bno_level)
		__field(__u32, cnt_level)
		__field(__u32, flfirst)
		__field(__u32, fllast)
		__field(__u32, flcount)
		__field(__u32, freeblks)
		__field(__u32, longest)
		__field(unsigned long, caller_ip)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = be32_to_cpu(agf->agf_seqno),
		__entry->flags = flags;
		__entry->length = be32_to_cpu(agf->agf_length),
		__entry->bno_root = be32_to_cpu(agf->agf_roots[XFS_BTNUM_BNO]),
		__entry->cnt_root = be32_to_cpu(agf->agf_roots[XFS_BTNUM_CNT]),
		__entry->bno_level =
				be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]),
		__entry->cnt_level =
				be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]),
		__entry->flfirst = be32_to_cpu(agf->agf_flfirst),
		__entry->fllast = be32_to_cpu(agf->agf_fllast),
		__entry->flcount = be32_to_cpu(agf->agf_flcount),
		__entry->freeblks = be32_to_cpu(agf->agf_freeblks),
		__entry->longest = be32_to_cpu(agf->agf_longest);
		__entry->caller_ip = caller_ip;
	),
	TP_printk("dev %d:%d agno %u flags %s length %u roots b %u c %u "
		  "levels b %u c %u flfirst %u fllast %u flcount %u "
		  "freeblks %u longest %u caller %pf",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __print_flags(__entry->flags, "|", XFS_AGF_FLAGS),
		  __entry->length,
		  __entry->bno_root,
		  __entry->cnt_root,
		  __entry->bno_level,
		  __entry->cnt_level,
		  __entry->flfirst,
		  __entry->fllast,
		  __entry->flcount,
		  __entry->freeblks,
		  __entry->longest,
		  (void *)__entry->caller_ip)
);

TRACE_EVENT(xfs_free_extent,
	TP_PROTO(struct xfs_mount *mp, xfs_agnumber_t agno, xfs_agblock_t agbno,
		 xfs_extlen_t len, bool isfl, int haveleft, int haveright),
	TP_ARGS(mp, agno, agbno, len, isfl, haveleft, haveright),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, len)
		__field(int, isfl)
		__field(int, haveleft)
		__field(int, haveright)
	),
	TP_fast_assign(
		__entry->dev = mp->m_super->s_dev;
		__entry->agno = agno;
		__entry->agbno = agbno;
		__entry->len = len;
		__entry->isfl = isfl;
		__entry->haveleft = haveleft;
		__entry->haveright = haveright;
	),
	TP_printk("dev %d:%d agno %u agbno %u len %u isfl %d %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->len,
		  __entry->isfl,
		  __entry->haveleft ?
			(__entry->haveright ? "both" : "left") :
			(__entry->haveright ? "right" : "none"))

);

DECLARE_EVENT_CLASS(xfs_alloc_class,
	TP_PROTO(struct xfs_alloc_arg *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_agnumber_t, agno)
		__field(xfs_agblock_t, agbno)
		__field(xfs_extlen_t, minlen)
		__field(xfs_extlen_t, maxlen)
		__field(xfs_extlen_t, mod)
		__field(xfs_extlen_t, prod)
		__field(xfs_extlen_t, minleft)
		__field(xfs_extlen_t, total)
		__field(xfs_extlen_t, alignment)
		__field(xfs_extlen_t, minalignslop)
		__field(xfs_extlen_t, len)
		__field(short, type)
		__field(short, otype)
		__field(char, wasdel)
		__field(char, wasfromfl)
		__field(char, isfl)
		__field(char, userdata)
		__field(xfs_fsblock_t, firstblock)
	),
	TP_fast_assign(
		__entry->dev = args->mp->m_super->s_dev;
		__entry->agno = args->agno;
		__entry->agbno = args->agbno;
		__entry->minlen = args->minlen;
		__entry->maxlen = args->maxlen;
		__entry->mod = args->mod;
		__entry->prod = args->prod;
		__entry->minleft = args->minleft;
		__entry->total = args->total;
		__entry->alignment = args->alignment;
		__entry->minalignslop = args->minalignslop;
		__entry->len = args->len;
		__entry->type = args->type;
		__entry->otype = args->otype;
		__entry->wasdel = args->wasdel;
		__entry->wasfromfl = args->wasfromfl;
		__entry->isfl = args->isfl;
		__entry->userdata = args->userdata;
		__entry->firstblock = args->firstblock;
	),
	TP_printk("dev %d:%d agno %u agbno %u minlen %u maxlen %u mod %u "
		  "prod %u minleft %u total %u alignment %u minalignslop %u "
		  "len %u type %s otype %s wasdel %d wasfromfl %d isfl %d "
		  "userdata %d firstblock 0x%llx",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->agno,
		  __entry->agbno,
		  __entry->minlen,
		  __entry->maxlen,
		  __entry->mod,
		  __entry->prod,
		  __entry->minleft,
		  __entry->total,
		  __entry->alignment,
		  __entry->minalignslop,
		  __entry->len,
		  __print_symbolic(__entry->type, XFS_ALLOC_TYPES),
		  __print_symbolic(__entry->otype, XFS_ALLOC_TYPES),
		  __entry->wasdel,
		  __entry->wasfromfl,
		  __entry->isfl,
		  __entry->userdata,
		  __entry->firstblock)
)

#define DEFINE_ALLOC_EVENT(name) \
DEFINE_EVENT(xfs_alloc_class, name, \
	TP_PROTO(struct xfs_alloc_arg *args), \
	TP_ARGS(args))
DEFINE_ALLOC_EVENT(xfs_alloc_exact_done);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_notfound);
DEFINE_ALLOC_EVENT(xfs_alloc_exact_error);
DEFINE_ALLOC_EVENT(xfs_alloc_near_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_near_first);
DEFINE_ALLOC_EVENT(xfs_alloc_near_greater);
DEFINE_ALLOC_EVENT(xfs_alloc_near_lesser);
DEFINE_ALLOC_EVENT(xfs_alloc_near_error);
DEFINE_ALLOC_EVENT(xfs_alloc_size_neither);
DEFINE_ALLOC_EVENT(xfs_alloc_size_noentry);
DEFINE_ALLOC_EVENT(xfs_alloc_size_nominleft);
DEFINE_ALLOC_EVENT(xfs_alloc_size_done);
DEFINE_ALLOC_EVENT(xfs_alloc_size_error);
DEFINE_ALLOC_EVENT(xfs_alloc_small_freelist);
DEFINE_ALLOC_EVENT(xfs_alloc_small_notenough);
DEFINE_ALLOC_EVENT(xfs_alloc_small_done);
DEFINE_ALLOC_EVENT(xfs_alloc_small_error);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_badargs);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_nofix);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_noagbp);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_loopfailed);
DEFINE_ALLOC_EVENT(xfs_alloc_vextent_allfailed);

DECLARE_EVENT_CLASS(xfs_dir2_class,
	TP_PROTO(struct xfs_da_args *args),
	TP_ARGS(args),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__dynamic_array(char, name, args->namelen)
		__field(int, namelen)
		__field(xfs_dahash_t, hashval)
		__field(xfs_ino_t, inumber)
		__field(int, op_flags)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		if (args->namelen)
			memcpy(__get_str(name), args->name, args->namelen);
		__entry->namelen = args->namelen;
		__entry->hashval = args->hashval;
		__entry->inumber = args->inumber;
		__entry->op_flags = args->op_flags;
	),
	TP_printk("dev %d:%d ino 0x%llx name %.*s namelen %d hashval 0x%x "
		  "inumber 0x%llx op_flags %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->namelen,
		  __entry->namelen ? __get_str(name) : NULL,
		  __entry->namelen,
		  __entry->hashval,
		  __entry->inumber,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS))
)

#define DEFINE_DIR2_EVENT(name) \
DEFINE_EVENT(xfs_dir2_class, name, \
	TP_PROTO(struct xfs_da_args *args), \
	TP_ARGS(args))
DEFINE_DIR2_EVENT(xfs_dir2_sf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_sf_create);
DEFINE_DIR2_EVENT(xfs_dir2_sf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_sf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_sf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino4);
DEFINE_DIR2_EVENT(xfs_dir2_sf_toino8);
DEFINE_DIR2_EVENT(xfs_dir2_sf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_block_addname);
DEFINE_DIR2_EVENT(xfs_dir2_block_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_block_replace);
DEFINE_DIR2_EVENT(xfs_dir2_block_removename);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_sf);
DEFINE_DIR2_EVENT(xfs_dir2_block_to_leaf);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_addname);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_replace);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_removename);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_block);
DEFINE_DIR2_EVENT(xfs_dir2_leaf_to_node);
DEFINE_DIR2_EVENT(xfs_dir2_node_addname);
DEFINE_DIR2_EVENT(xfs_dir2_node_lookup);
DEFINE_DIR2_EVENT(xfs_dir2_node_replace);
DEFINE_DIR2_EVENT(xfs_dir2_node_removename);
DEFINE_DIR2_EVENT(xfs_dir2_node_to_leaf);

DECLARE_EVENT_CLASS(xfs_dir2_space_class,
	TP_PROTO(struct xfs_da_args *args, int idx),
	TP_ARGS(args, idx),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, op_flags)
		__field(int, idx)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->idx = idx;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s index %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->idx)
)

#define DEFINE_DIR2_SPACE_EVENT(name) \
DEFINE_EVENT(xfs_dir2_space_class, name, \
	TP_PROTO(struct xfs_da_args *args, int idx), \
	TP_ARGS(args, idx))
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_add);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_leafn_remove);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_grow_inode);
DEFINE_DIR2_SPACE_EVENT(xfs_dir2_shrink_inode);

TRACE_EVENT(xfs_dir2_leafn_moveents,
	TP_PROTO(struct xfs_da_args *args, int src_idx, int dst_idx, int count),
	TP_ARGS(args, src_idx, dst_idx, count),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(int, op_flags)
		__field(int, src_idx)
		__field(int, dst_idx)
		__field(int, count)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(args->dp)->i_sb->s_dev;
		__entry->ino = args->dp->i_ino;
		__entry->op_flags = args->op_flags;
		__entry->src_idx = src_idx;
		__entry->dst_idx = dst_idx;
		__entry->count = count;
	),
	TP_printk("dev %d:%d ino 0x%llx op_flags %s "
		  "src_idx %d dst_idx %d count %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_flags(__entry->op_flags, "|", XFS_DA_OP_FLAGS),
		  __entry->src_idx,
		  __entry->dst_idx,
		  __entry->count)
);

#define XFS_SWAPEXT_INODES \
	{ 0,	"target" }, \
	{ 1,	"temp" }

#define XFS_INODE_FORMAT_STR \
	{ 0,	"invalid" }, \
	{ 1,	"local" }, \
	{ 2,	"extent" }, \
	{ 3,	"btree" }

DECLARE_EVENT_CLASS(xfs_swap_extent_class,
	TP_PROTO(struct xfs_inode *ip, int which),
	TP_ARGS(ip, which),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, which)
		__field(xfs_ino_t, ino)
		__field(int, format)
		__field(int, nex)
		__field(int, max_nex)
		__field(int, broot_size)
		__field(int, fork_off)
	),
	TP_fast_assign(
		__entry->dev = VFS_I(ip)->i_sb->s_dev;
		__entry->which = which;
		__entry->ino = ip->i_ino;
		__entry->format = ip->i_d.di_format;
		__entry->nex = ip->i_d.di_nextents;
		__entry->max_nex = ip->i_df.if_ext_max;
		__entry->broot_size = ip->i_df.if_broot_bytes;
		__entry->fork_off = XFS_IFORK_BOFF(ip);
	),
	TP_printk("dev %d:%d ino 0x%llx (%s), %s format, num_extents %d, "
		  "Max in-fork extents %d, broot size %d, fork offset %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->which, XFS_SWAPEXT_INODES),
		  __print_symbolic(__entry->format, XFS_INODE_FORMAT_STR),
		  __entry->nex,
		  __entry->max_nex,
		  __entry->broot_size,
		  __entry->fork_off)
)

#define DEFINE_SWAPEXT_EVENT(name) \
DEFINE_EVENT(xfs_swap_extent_class, name, \
	TP_PROTO(struct xfs_inode *ip, int which), \
	TP_ARGS(ip, which))

DEFINE_SWAPEXT_EVENT(xfs_swap_extent_before);
DEFINE_SWAPEXT_EVENT(xfs_swap_extent_after);

DECLARE_EVENT_CLASS(xfs_log_recover_item_class,
	TP_PROTO(struct log *log, struct xlog_recover *trans,
		struct xlog_recover_item *item, int pass),
	TP_ARGS(log, trans, item, pass),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(unsigned long, item)
		__field(xlog_tid_t, tid)
		__field(int, type)
		__field(int, pass)
		__field(int, count)
		__field(int, total)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->item = (unsigned long)item;
		__entry->tid = trans->r_log_tid;
		__entry->type = ITEM_TYPE(item);
		__entry->pass = pass;
		__entry->count = item->ri_cnt;
		__entry->total = item->ri_total;
	),
	TP_printk("dev %d:%d trans 0x%x, pass %d, item 0x%p, item type %s "
		  "item region count/total %d/%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->tid,
		  __entry->pass,
		  (void *)__entry->item,
		  __print_symbolic(__entry->type, XFS_LI_TYPE_DESC),
		  __entry->count,
		  __entry->total)
)

#define DEFINE_LOG_RECOVER_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_item_class, name, \
	TP_PROTO(struct log *log, struct xlog_recover *trans, \
		struct xlog_recover_item *item, int pass), \
	TP_ARGS(log, trans, item, pass))

DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_add_cont);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_head);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_reorder_tail);
DEFINE_LOG_RECOVER_ITEM(xfs_log_recover_item_recover);

DECLARE_EVENT_CLASS(xfs_log_recover_buf_item_class,
	TP_PROTO(struct log *log, struct xfs_buf_log_format *buf_f),
	TP_ARGS(log, buf_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(__int64_t, blkno)
		__field(unsigned short, len)
		__field(unsigned short, flags)
		__field(unsigned short, size)
		__field(unsigned int, map_size)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->blkno = buf_f->blf_blkno;
		__entry->len = buf_f->blf_len;
		__entry->flags = buf_f->blf_flags;
		__entry->size = buf_f->blf_size;
		__entry->map_size = buf_f->blf_map_size;
	),
	TP_printk("dev %d:%d blkno 0x%llx, len %u, flags 0x%x, size %d, "
			"map_size %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->blkno,
		  __entry->len,
		  __entry->flags,
		  __entry->size,
		  __entry->map_size)
)

#define DEFINE_LOG_RECOVER_BUF_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_buf_item_class, name, \
	TP_PROTO(struct log *log, struct xfs_buf_log_format *buf_f), \
	TP_ARGS(log, buf_f))

DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_not_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_add);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_cancel_ref_inc);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_recover);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_inode_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_reg_buf);
DEFINE_LOG_RECOVER_BUF_ITEM(xfs_log_recover_buf_dquot_buf);

DECLARE_EVENT_CLASS(xfs_log_recover_ino_item_class,
	TP_PROTO(struct log *log, struct xfs_inode_log_format *in_f),
	TP_ARGS(log, in_f),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(xfs_ino_t, ino)
		__field(unsigned short, size)
		__field(int, fields)
		__field(unsigned short, asize)
		__field(unsigned short, dsize)
		__field(__int64_t, blkno)
		__field(int, len)
		__field(int, boffset)
	),
	TP_fast_assign(
		__entry->dev = log->l_mp->m_super->s_dev;
		__entry->ino = in_f->ilf_ino;
		__entry->size = in_f->ilf_size;
		__entry->fields = in_f->ilf_fields;
		__entry->asize = in_f->ilf_asize;
		__entry->dsize = in_f->ilf_dsize;
		__entry->blkno = in_f->ilf_blkno;
		__entry->len = in_f->ilf_len;
		__entry->boffset = in_f->ilf_boffset;
	),
	TP_printk("dev %d:%d ino 0x%llx, size %u, fields 0x%x, asize %d, "
			"dsize %d, blkno 0x%llx, len %d, boffset %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __entry->size,
		  __entry->fields,
		  __entry->asize,
		  __entry->dsize,
		  __entry->blkno,
		  __entry->len,
		  __entry->boffset)
)
#define DEFINE_LOG_RECOVER_INO_ITEM(name) \
DEFINE_EVENT(xfs_log_recover_ino_item_class, name, \
	TP_PROTO(struct log *log, struct xfs_inode_log_format *in_f), \
	TP_ARGS(log, in_f))

DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_recover);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_cancel);
DEFINE_LOG_RECOVER_INO_ITEM(xfs_log_recover_inode_skip);

#endif /* _TRACE_XFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE xfs_trace
#include <trace/define_trace.h>
