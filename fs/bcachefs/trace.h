/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcachefs

#if !defined(_TRACE_BCACHEFS_H) || defined(TRACE_HEADER_MULTI_READ)

#include <linux/tracepoint.h>

#define TRACE_BPOS_entries(name)				\
	__field(u64,			name##_inode	)	\
	__field(u64,			name##_offset	)	\
	__field(u32,			name##_snapshot	)

#define TRACE_BPOS_assign(dst, src)				\
	__entry->dst##_inode		= (src).inode;		\
	__entry->dst##_offset		= (src).offset;		\
	__entry->dst##_snapshot		= (src).snapshot

DECLARE_EVENT_CLASS(bpos,
	TP_PROTO(const struct bpos *p),
	TP_ARGS(p),

	TP_STRUCT__entry(
		TRACE_BPOS_entries(p)
	),

	TP_fast_assign(
		TRACE_BPOS_assign(p, *p);
	),

	TP_printk("%llu:%llu:%u", __entry->p_inode, __entry->p_offset, __entry->p_snapshot)
);

DECLARE_EVENT_CLASS(fs_str,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__string(str,		str			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__assign_str(str);
	),

	TP_printk("%d,%d\n%s", MAJOR(__entry->dev), MINOR(__entry->dev), __get_str(str))
);

DECLARE_EVENT_CLASS(trans_str,
	TP_PROTO(struct btree_trans *trans, unsigned long caller_ip, const char *str),
	TP_ARGS(trans, caller_ip, str),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__array(char,		trans_fn, 32		)
		__field(unsigned long,	caller_ip		)
		__string(str,		str			)
	),

	TP_fast_assign(
		__entry->dev		= trans->c->dev;
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__assign_str(str);
	),

	TP_printk("%d,%d %s %pS %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->trans_fn, (void *) __entry->caller_ip, __get_str(str))
);

DECLARE_EVENT_CLASS(trans_str_nocaller,
	TP_PROTO(struct btree_trans *trans, const char *str),
	TP_ARGS(trans, str),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__array(char,		trans_fn, 32		)
		__string(str,		str			)
	),

	TP_fast_assign(
		__entry->dev		= trans->c->dev;
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__assign_str(str);
	),

	TP_printk("%d,%d %s %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->trans_fn, __get_str(str))
);

DECLARE_EVENT_CLASS(btree_node_nofs,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u8,		level			)
		__field(u8,		btree_id		)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->level		= b->c.level;
		__entry->btree_id	= b->c.btree_id;
		TRACE_BPOS_assign(pos, b->key.k.p);
	),

	TP_printk("%d,%d %u %s %llu:%llu:%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->level,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode, __entry->pos_offset, __entry->pos_snapshot)
);

DECLARE_EVENT_CLASS(btree_node,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__array(char,		trans_fn, 32		)
		__field(u8,		level			)
		__field(u8,		btree_id		)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->dev		= trans->c->dev;
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->level		= b->c.level;
		__entry->btree_id	= b->c.btree_id;
		TRACE_BPOS_assign(pos, b->key.k.p);
	),

	TP_printk("%d,%d %s %u %s %llu:%llu:%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->trans_fn,
		  __entry->level,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode, __entry->pos_offset, __entry->pos_snapshot)
);

DECLARE_EVENT_CLASS(bch_fs,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
	),

	TP_printk("%d,%d", MAJOR(__entry->dev), MINOR(__entry->dev))
);

DECLARE_EVENT_CLASS(btree_trans,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__array(char,		trans_fn, 32		)
	),

	TP_fast_assign(
		__entry->dev		= trans->c->dev;
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
	),

	TP_printk("%d,%d %s", MAJOR(__entry->dev), MINOR(__entry->dev), __entry->trans_fn)
);

DECLARE_EVENT_CLASS(bio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(sector_t,	sector			)
		__field(unsigned int,	nr_sector		)
		__array(char,		rwbs,	6		)
	),

	TP_fast_assign(
		__entry->dev		= bio->bi_bdev ? bio_dev(bio) : 0;
		__entry->sector		= bio->bi_iter.bi_sector;
		__entry->nr_sector	= bio->bi_iter.bi_size >> 9;
		blk_fill_rwbs(__entry->rwbs, bio->bi_opf);
	),

	TP_printk("%d,%d  %s %llu + %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->rwbs,
		  (unsigned long long)__entry->sector, __entry->nr_sector)
);

/* disk_accounting.c */

TRACE_EVENT(accounting_mem_insert,
	TP_PROTO(struct bch_fs *c, const char *acc),
	TP_ARGS(c, acc),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(unsigned,	new_nr			)
		__string(acc,		acc			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->new_nr		= c->accounting.k.nr;
		__assign_str(acc);
	),

	TP_printk("%d,%d entries %u added %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->new_nr,
		  __get_str(acc))
);

/* fs.c: */
TRACE_EVENT(bch2_sync_fs,
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

	TP_printk("dev %d,%d wait %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->wait)
);

/* fs-io.c: */
TRACE_EVENT(bch2_fsync,
	TP_PROTO(struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry(
		__field(	dev_t,	dev			)
		__field(	ino_t,	ino			)
		__field(	ino_t,	parent			)
		__field(	int,	datasync		)
	),

	TP_fast_assign(
		struct dentry *dentry = file->f_path.dentry;

		__entry->dev		= dentry->d_sb->s_dev;
		__entry->ino		= d_inode(dentry)->i_ino;
		__entry->parent		= d_inode(dentry->d_parent)->i_ino;
		__entry->datasync	= datasync;
	),

	TP_printk("dev %d,%d ino %lu parent %lu datasync %d ",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (unsigned long) __entry->ino,
		  (unsigned long) __entry->parent, __entry->datasync)
);

/* super-io.c: */
TRACE_EVENT(write_super,
	TP_PROTO(struct bch_fs *c, unsigned long ip),
	TP_ARGS(c, ip),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(unsigned long,	ip	)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->ip		= ip;
	),

	TP_printk("%d,%d for %pS",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  (void *) __entry->ip)
);

/* io.c: */

DEFINE_EVENT(bio, read_promote,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

TRACE_EVENT(read_nopromote,
	TP_PROTO(struct bch_fs *c, int ret),
	TP_ARGS(c, ret),

	TP_STRUCT__entry(
		__field(dev_t,		dev		)
		__array(char,		ret, 32		)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		strscpy(__entry->ret, bch2_err_str(ret), sizeof(__entry->ret));
	),

	TP_printk("%d,%d ret %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ret)
);

DEFINE_EVENT(bio, read_bounce,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_split,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_retry,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

DEFINE_EVENT(bio, read_reuse_race,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

/* Journal */

DEFINE_EVENT(bch_fs, journal_full,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(fs_str, journal_entry_full,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, journal_entry_close,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(bio, journal_write,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio)
);

TRACE_EVENT(journal_reclaim_start,
	TP_PROTO(struct bch_fs *c, bool direct, bool kicked,
		 u64 min_nr, u64 min_key_cache,
		 u64 btree_cache_dirty, u64 btree_cache_total,
		 u64 btree_key_cache_dirty, u64 btree_key_cache_total),
	TP_ARGS(c, direct, kicked, min_nr, min_key_cache,
		btree_cache_dirty, btree_cache_total,
		btree_key_cache_dirty, btree_key_cache_total),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(bool,		direct			)
		__field(bool,		kicked			)
		__field(u64,		min_nr			)
		__field(u64,		min_key_cache		)
		__field(u64,		btree_cache_dirty	)
		__field(u64,		btree_cache_total	)
		__field(u64,		btree_key_cache_dirty	)
		__field(u64,		btree_key_cache_total	)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->direct			= direct;
		__entry->kicked			= kicked;
		__entry->min_nr			= min_nr;
		__entry->min_key_cache		= min_key_cache;
		__entry->btree_cache_dirty	= btree_cache_dirty;
		__entry->btree_cache_total	= btree_cache_total;
		__entry->btree_key_cache_dirty	= btree_key_cache_dirty;
		__entry->btree_key_cache_total	= btree_key_cache_total;
	),

	TP_printk("%d,%d direct %u kicked %u min %llu key cache %llu btree cache %llu/%llu key cache %llu/%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->direct,
		  __entry->kicked,
		  __entry->min_nr,
		  __entry->min_key_cache,
		  __entry->btree_cache_dirty,
		  __entry->btree_cache_total,
		  __entry->btree_key_cache_dirty,
		  __entry->btree_key_cache_total)
);

TRACE_EVENT(journal_reclaim_finish,
	TP_PROTO(struct bch_fs *c, u64 nr_flushed),
	TP_ARGS(c, nr_flushed),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		nr_flushed		)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->nr_flushed	= nr_flushed;
	),

	TP_printk("%d,%d flushed %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->nr_flushed)
);

/* bset.c: */

DEFINE_EVENT(bpos, bkey_pack_pos_fail,
	TP_PROTO(const struct bpos *p),
	TP_ARGS(p)
);

/* Btree cache: */

TRACE_EVENT(btree_cache_scan,
	TP_PROTO(long nr_to_scan, long can_free, long ret),
	TP_ARGS(nr_to_scan, can_free, ret),

	TP_STRUCT__entry(
		__field(long,	nr_to_scan		)
		__field(long,	can_free		)
		__field(long,	ret			)
	),

	TP_fast_assign(
		__entry->nr_to_scan	= nr_to_scan;
		__entry->can_free	= can_free;
		__entry->ret		= ret;
	),

	TP_printk("scanned for %li nodes, can free %li, ret %li",
		  __entry->nr_to_scan, __entry->can_free, __entry->ret)
);

DEFINE_EVENT(btree_node_nofs, btree_cache_reap,
	TP_PROTO(struct bch_fs *c, struct btree *b),
	TP_ARGS(c, b)
);

DEFINE_EVENT(btree_trans, btree_cache_cannibalize_lock_fail,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans)
);

DEFINE_EVENT(btree_trans, btree_cache_cannibalize_lock,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans)
);

DEFINE_EVENT(btree_trans, btree_cache_cannibalize,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans)
);

DEFINE_EVENT(btree_trans, btree_cache_cannibalize_unlock,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans)
);

/* Btree */

DEFINE_EVENT(btree_node, btree_node_read,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

TRACE_EVENT(btree_node_write,
	TP_PROTO(struct btree *b, unsigned bytes, unsigned sectors),
	TP_ARGS(b, bytes, sectors),

	TP_STRUCT__entry(
		__field(enum btree_node_type,	type)
		__field(unsigned,	bytes			)
		__field(unsigned,	sectors			)
	),

	TP_fast_assign(
		__entry->type	= btree_node_type(b);
		__entry->bytes	= bytes;
		__entry->sectors = sectors;
	),

	TP_printk("bkey type %u bytes %u sectors %u",
		  __entry->type , __entry->bytes, __entry->sectors)
);

DEFINE_EVENT(btree_node, btree_node_alloc,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

DEFINE_EVENT(btree_node, btree_node_free,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

TRACE_EVENT(btree_reserve_get_fail,
	TP_PROTO(const char *trans_fn,
		 unsigned long caller_ip,
		 size_t required,
		 int ret),
	TP_ARGS(trans_fn, caller_ip, required, ret),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(size_t,			required	)
		__array(char,			ret, 32		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans_fn, sizeof(__entry->trans_fn));
		__entry->caller_ip	= caller_ip;
		__entry->required	= required;
		strscpy(__entry->ret, bch2_err_str(ret), sizeof(__entry->ret));
	),

	TP_printk("%s %pS required %zu ret %s",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->required,
		  __entry->ret)
);

DEFINE_EVENT(btree_node, btree_node_compact,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

DEFINE_EVENT(btree_node, btree_node_merge,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

DEFINE_EVENT(btree_node, btree_node_split,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

DEFINE_EVENT(btree_node, btree_node_rewrite,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

DEFINE_EVENT(btree_node, btree_node_set_root,
	TP_PROTO(struct btree_trans *trans, struct btree *b),
	TP_ARGS(trans, b)
);

TRACE_EVENT(btree_path_relock_fail,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned level),
	TP_ARGS(trans, caller_ip, path, level),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			level		)
		__field(u8,			path_idx)
		TRACE_BPOS_entries(pos)
		__array(char,			node, 24	)
		__field(u8,			self_read_count	)
		__field(u8,			self_intent_count)
		__field(u8,			read_count	)
		__field(u8,			intent_count	)
		__field(u32,			iter_lock_seq	)
		__field(u32,			node_lock_seq	)
	),

	TP_fast_assign(
		struct btree *b = btree_path_node(path, level);
		struct six_lock_count c;

		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->level			= level;
		__entry->path_idx		= path - trans->paths;
		TRACE_BPOS_assign(pos, path->pos);

		c = bch2_btree_node_lock_counts(trans, NULL, &path->l[level].b->c, level);
		__entry->self_read_count	= c.n[SIX_LOCK_read];
		__entry->self_intent_count	= c.n[SIX_LOCK_intent];

		if (IS_ERR(b)) {
			strscpy(__entry->node, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node));
		} else {
			c = six_lock_counts(&path->l[level].b->c.lock);
			__entry->read_count	= c.n[SIX_LOCK_read];
			__entry->intent_count	= c.n[SIX_LOCK_intent];
			scnprintf(__entry->node, sizeof(__entry->node), "%px", &b->c);
		}
		__entry->iter_lock_seq		= path->l[level].lock_seq;
		__entry->node_lock_seq		= is_btree_node(path, level)
			? six_lock_seq(&path->l[level].b->c.lock)
			: 0;
	),

	TP_printk("%s %pS\nidx %2u btree %s pos %llu:%llu:%u level %u node %s held %u:%u lock count %u:%u iter seq %u lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->path_idx,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->level,
		  __entry->node,
		  __entry->self_read_count,
		  __entry->self_intent_count,
		  __entry->read_count,
		  __entry->intent_count,
		  __entry->iter_lock_seq,
		  __entry->node_lock_seq)
);

TRACE_EVENT(btree_path_upgrade_fail,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned level),
	TP_ARGS(trans, caller_ip, path, level),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			level		)
		__field(u8,			path_idx)
		TRACE_BPOS_entries(pos)
		__field(u8,			locked		)
		__field(u8,			self_read_count	)
		__field(u8,			self_intent_count)
		__field(u8,			read_count	)
		__field(u8,			intent_count	)
		__field(u32,			iter_lock_seq	)
		__field(u32,			node_lock_seq	)
	),

	TP_fast_assign(
		struct six_lock_count c;

		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->level			= level;
		__entry->path_idx		= path - trans->paths;
		TRACE_BPOS_assign(pos, path->pos);
		__entry->locked			= btree_node_locked(path, level);

		c = bch2_btree_node_lock_counts(trans, NULL, &path->l[level].b->c, level),
		__entry->self_read_count	= c.n[SIX_LOCK_read];
		__entry->self_intent_count	= c.n[SIX_LOCK_intent];
		c = six_lock_counts(&path->l[level].b->c.lock);
		__entry->read_count		= c.n[SIX_LOCK_read];
		__entry->intent_count		= c.n[SIX_LOCK_intent];
		__entry->iter_lock_seq		= path->l[level].lock_seq;
		__entry->node_lock_seq		= is_btree_node(path, level)
			? six_lock_seq(&path->l[level].b->c.lock)
			: 0;
	),

	TP_printk("%s %pS\nidx %2u btree %s pos %llu:%llu:%u level %u locked %u held %u:%u lock count %u:%u iter seq %u lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->path_idx,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->level,
		  __entry->locked,
		  __entry->self_read_count,
		  __entry->self_intent_count,
		  __entry->read_count,
		  __entry->intent_count,
		  __entry->iter_lock_seq,
		  __entry->node_lock_seq)
);

/* Garbage collection */

DEFINE_EVENT(bch_fs, gc_gens_start,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

DEFINE_EVENT(bch_fs, gc_gens_end,
	TP_PROTO(struct bch_fs *c),
	TP_ARGS(c)
);

/* Allocator */

DEFINE_EVENT(fs_str, bucket_alloc,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, bucket_alloc_fail,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DECLARE_EVENT_CLASS(discard_buckets_class,
	TP_PROTO(struct bch_fs *c, u64 seen, u64 open,
		 u64 need_journal_commit, u64 discarded, const char *err),
	TP_ARGS(c, seen, open, need_journal_commit, discarded, err),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		seen			)
		__field(u64,		open			)
		__field(u64,		need_journal_commit	)
		__field(u64,		discarded		)
		__array(char,		err,	16		)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->seen			= seen;
		__entry->open			= open;
		__entry->need_journal_commit	= need_journal_commit;
		__entry->discarded		= discarded;
		strscpy(__entry->err, err, sizeof(__entry->err));
	),

	TP_printk("%d%d seen %llu open %llu need_journal_commit %llu discarded %llu err %s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->seen,
		  __entry->open,
		  __entry->need_journal_commit,
		  __entry->discarded,
		  __entry->err)
);

DEFINE_EVENT(discard_buckets_class, discard_buckets,
	TP_PROTO(struct bch_fs *c, u64 seen, u64 open,
		 u64 need_journal_commit, u64 discarded, const char *err),
	TP_ARGS(c, seen, open, need_journal_commit, discarded, err)
);

DEFINE_EVENT(discard_buckets_class, discard_buckets_fast,
	TP_PROTO(struct bch_fs *c, u64 seen, u64 open,
		 u64 need_journal_commit, u64 discarded, const char *err),
	TP_ARGS(c, seen, open, need_journal_commit, discarded, err)
);

TRACE_EVENT(bucket_invalidate,
	TP_PROTO(struct bch_fs *c, unsigned dev, u64 bucket, u32 sectors),
	TP_ARGS(c, dev, bucket, sectors),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u32,		dev_idx			)
		__field(u32,		sectors			)
		__field(u64,		bucket			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->dev_idx	= dev;
		__entry->sectors	= sectors;
		__entry->bucket		= bucket;
	),

	TP_printk("%d:%d invalidated %u:%llu cached sectors %u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dev_idx, __entry->bucket,
		  __entry->sectors)
);

/* Moving IO */

TRACE_EVENT(bucket_evacuate,
	TP_PROTO(struct bch_fs *c, struct bpos *bucket),
	TP_ARGS(c, bucket),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u32,		dev_idx			)
		__field(u64,		bucket			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->dev_idx	= bucket->inode;
		__entry->bucket		= bucket->offset;
	),

	TP_printk("%d:%d %u:%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dev_idx, __entry->bucket)
);

DEFINE_EVENT(fs_str, move_extent,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, move_extent_read,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, move_extent_write,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, move_extent_finish,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, move_extent_fail,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, move_extent_start_fail,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

TRACE_EVENT(move_data,
	TP_PROTO(struct bch_fs *c,
		 struct bch_move_stats *stats),
	TP_ARGS(c, stats),

	TP_STRUCT__entry(
		__field(dev_t,		dev		)
		__field(u64,		keys_moved	)
		__field(u64,		keys_raced	)
		__field(u64,		sectors_seen	)
		__field(u64,		sectors_moved	)
		__field(u64,		sectors_raced	)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->keys_moved	= atomic64_read(&stats->keys_moved);
		__entry->keys_raced	= atomic64_read(&stats->keys_raced);
		__entry->sectors_seen	= atomic64_read(&stats->sectors_seen);
		__entry->sectors_moved	= atomic64_read(&stats->sectors_moved);
		__entry->sectors_raced	= atomic64_read(&stats->sectors_raced);
	),

	TP_printk("%d,%d keys moved %llu raced %llu"
		  "sectors seen %llu moved %llu raced %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->keys_moved,
		  __entry->keys_raced,
		  __entry->sectors_seen,
		  __entry->sectors_moved,
		  __entry->sectors_raced)
);

TRACE_EVENT(evacuate_bucket,
	TP_PROTO(struct bch_fs *c, struct bpos *bucket,
		 unsigned sectors, unsigned bucket_size,
		 int ret),
	TP_ARGS(c, bucket, sectors, bucket_size, ret),

	TP_STRUCT__entry(
		__field(dev_t,		dev		)
		__field(u64,		member		)
		__field(u64,		bucket		)
		__field(u32,		sectors		)
		__field(u32,		bucket_size	)
		__field(int,		ret		)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->member			= bucket->inode;
		__entry->bucket			= bucket->offset;
		__entry->sectors		= sectors;
		__entry->bucket_size		= bucket_size;
		__entry->ret			= ret;
	),

	TP_printk("%d,%d %llu:%llu sectors %u/%u ret %i",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->member, __entry->bucket,
		  __entry->sectors, __entry->bucket_size,
		  __entry->ret)
);

TRACE_EVENT(copygc,
	TP_PROTO(struct bch_fs *c,
		 u64 buckets,
		 u64 sectors_seen,
		 u64 sectors_moved),
	TP_ARGS(c, buckets, sectors_seen, sectors_moved),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		buckets			)
		__field(u64,		sectors_seen		)
		__field(u64,		sectors_moved		)
	),

	TP_fast_assign(
		__entry->dev			= c->dev;
		__entry->buckets		= buckets;
		__entry->sectors_seen		= sectors_seen;
		__entry->sectors_moved		= sectors_moved;
	),

	TP_printk("%d,%d buckets %llu sectors seen %llu moved %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->buckets,
		  __entry->sectors_seen,
		  __entry->sectors_moved)
);

TRACE_EVENT(copygc_wait,
	TP_PROTO(struct bch_fs *c,
		 u64 wait_amount, u64 until),
	TP_ARGS(c, wait_amount, until),

	TP_STRUCT__entry(
		__field(dev_t,		dev			)
		__field(u64,		wait_amount		)
		__field(u64,		until			)
	),

	TP_fast_assign(
		__entry->dev		= c->dev;
		__entry->wait_amount	= wait_amount;
		__entry->until		= until;
	),

	TP_printk("%d,%u waiting for %llu sectors until %llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->wait_amount, __entry->until)
);

/* btree transactions: */

DECLARE_EVENT_CLASS(transaction_event,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
	),

	TP_printk("%s %pS", __entry->trans_fn, (void *) __entry->caller_ip)
);

DEFINE_EVENT(transaction_event,	transaction_commit,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_injected,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

TRACE_EVENT(trans_restart_split_race,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree *b),
	TP_ARGS(trans, caller_ip, b),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			level		)
		__field(u16,			written		)
		__field(u16,			blocks		)
		__field(u16,			u64s_remaining	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->level		= b->c.level;
		__entry->written	= b->written;
		__entry->blocks		= btree_blocks(trans->c);
		__entry->u64s_remaining	= bch2_btree_keys_u64s_remaining(b);
	),

	TP_printk("%s %pS l=%u written %u/%u u64s remaining %u",
		  __entry->trans_fn, (void *) __entry->caller_ip,
		  __entry->level,
		  __entry->written, __entry->blocks,
		  __entry->u64s_remaining)
);

TRACE_EVENT(trans_blocked_journal_reclaim,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)

		__field(unsigned long,		key_cache_nr_keys	)
		__field(unsigned long,		key_cache_nr_dirty	)
		__field(long,			must_wait		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->key_cache_nr_keys	= atomic_long_read(&trans->c->btree_key_cache.nr_keys);
		__entry->key_cache_nr_dirty	= atomic_long_read(&trans->c->btree_key_cache.nr_dirty);
		__entry->must_wait		= __bch2_btree_key_cache_must_wait(trans->c);
	),

	TP_printk("%s %pS key cache keys %lu dirty %lu must_wait %li",
		  __entry->trans_fn, (void *) __entry->caller_ip,
		  __entry->key_cache_nr_keys,
		  __entry->key_cache_nr_dirty,
		  __entry->must_wait)
);

TRACE_EVENT(trans_restart_journal_preres_get,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 unsigned flags),
	TP_ARGS(trans, caller_ip, flags),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned,		flags		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->flags			= flags;
	),

	TP_printk("%s %pS %x", __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->flags)
);

DEFINE_EVENT(transaction_event,	trans_restart_fault_inject,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_traverse_all,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_event,	trans_restart_key_cache_raced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(trans_str, trans_restart_too_many_iters,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 const char *paths),
	TP_ARGS(trans, caller_ip, paths)
);

DECLARE_EVENT_CLASS(transaction_restart_iter,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos)
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_reused,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_btree_node_split,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

TRACE_EVENT(trans_restart_upgrade,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned old_locks_want,
		 unsigned new_locks_want,
		 struct get_locks_fail *f),
	TP_ARGS(trans, caller_ip, path, old_locks_want, new_locks_want, f),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			old_locks_want	)
		__field(u8,			new_locks_want	)
		__field(u8,			level		)
		__field(u32,			path_seq	)
		__field(u32,			node_seq	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= path->btree_id;
		__entry->old_locks_want		= old_locks_want;
		__entry->new_locks_want		= new_locks_want;
		__entry->level			= f->l;
		__entry->path_seq		= path->l[f->l].lock_seq;
		__entry->node_seq		= IS_ERR_OR_NULL(f->b) ? 0 : f->b->c.lock.seq;
		TRACE_BPOS_assign(pos, path->pos)
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u locks_want %u -> %u level %u path seq %u node seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->old_locks_want,
		  __entry->new_locks_want,
		  __entry->level,
		  __entry->path_seq,
		  __entry->node_seq)
);

DEFINE_EVENT(trans_str,	trans_restart_relock,
	TP_PROTO(struct btree_trans *trans, unsigned long caller_ip, const char *str),
	TP_ARGS(trans, caller_ip, str)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_next_node,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_parent_for_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_after_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_event,	trans_restart_key_cache_upgrade,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_key_cache_fill,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_relock_path_intent,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_traverse,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(transaction_restart_iter,	trans_restart_memory_allocation_failure,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path),
	TP_ARGS(trans, caller_ip, path)
);

DEFINE_EVENT(trans_str_nocaller, trans_restart_would_deadlock,
	TP_PROTO(struct btree_trans *trans,
		 const char *cycle),
	TP_ARGS(trans, cycle)
);

DEFINE_EVENT(transaction_event,	trans_restart_would_deadlock_recursion_limit,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

TRACE_EVENT(trans_restart_would_deadlock_write,
	TP_PROTO(struct btree_trans *trans),
	TP_ARGS(trans),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
	),

	TP_printk("%s", __entry->trans_fn)
);

TRACE_EVENT(trans_restart_mem_realloced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 unsigned long bytes),
	TP_ARGS(trans, caller_ip, bytes),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned long,		bytes		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip	= caller_ip;
		__entry->bytes		= bytes;
	),

	TP_printk("%s %pS bytes %lu",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->bytes)
);

TRACE_EVENT(trans_restart_key_cache_key_realloced,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned old_u64s,
		 unsigned new_u64s),
	TP_ARGS(trans, caller_ip, path, old_u64s, new_u64s),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(enum btree_id,		btree_id	)
		TRACE_BPOS_entries(pos)
		__field(u32,			old_u64s	)
		__field(u32,			new_u64s	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;

		__entry->btree_id	= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
		__entry->old_u64s	= old_u64s;
		__entry->new_u64s	= new_u64s;
	),

	TP_printk("%s %pS btree %s pos %llu:%llu:%u old_u64s %u new_u64s %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->old_u64s,
		  __entry->new_u64s)
);

DEFINE_EVENT(transaction_event,	trans_restart_write_buffer_flush,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip),
	TP_ARGS(trans, caller_ip)
);

TRACE_EVENT(path_downgrade,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_path *path,
		 unsigned old_locks_want),
	TP_ARGS(trans, caller_ip, path, old_locks_want),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(unsigned,		old_locks_want	)
		__field(unsigned,		new_locks_want	)
		__field(unsigned,		btree		)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->old_locks_want		= old_locks_want;
		__entry->new_locks_want		= path->locks_want;
		__entry->btree			= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
	),

	TP_printk("%s %pS locks_want %u -> %u %s %llu:%llu:%u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  __entry->old_locks_want,
		  __entry->new_locks_want,
		  bch2_btree_id_str(__entry->btree),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

TRACE_EVENT(key_cache_fill,
	TP_PROTO(struct btree_trans *trans, const char *key),
	TP_ARGS(trans, key),

	TP_STRUCT__entry(
		__array(char,		trans_fn, 32	)
		__string(key,		key			)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__assign_str(key);
	),

	TP_printk("%s %s", __entry->trans_fn, __get_str(key))
);

TRACE_EVENT(write_buffer_flush,
	TP_PROTO(struct btree_trans *trans, size_t nr, size_t skipped, size_t fast, size_t size),
	TP_ARGS(trans, nr, skipped, fast, size),

	TP_STRUCT__entry(
		__field(size_t,		nr		)
		__field(size_t,		skipped		)
		__field(size_t,		fast		)
		__field(size_t,		size		)
	),

	TP_fast_assign(
		__entry->nr	= nr;
		__entry->skipped = skipped;
		__entry->fast	= fast;
		__entry->size	= size;
	),

	TP_printk("%zu/%zu skipped %zu fast %zu",
		  __entry->nr, __entry->size, __entry->skipped, __entry->fast)
);

TRACE_EVENT(write_buffer_flush_sync,
	TP_PROTO(struct btree_trans *trans, unsigned long caller_ip),
	TP_ARGS(trans, caller_ip),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
	),

	TP_printk("%s %pS", __entry->trans_fn, (void *) __entry->caller_ip)
);

TRACE_EVENT(write_buffer_flush_slowpath,
	TP_PROTO(struct btree_trans *trans, size_t slowpath, size_t total),
	TP_ARGS(trans, slowpath, total),

	TP_STRUCT__entry(
		__field(size_t,		slowpath	)
		__field(size_t,		total		)
	),

	TP_fast_assign(
		__entry->slowpath	= slowpath;
		__entry->total		= total;
	),

	TP_printk("%zu/%zu", __entry->slowpath, __entry->total)
);

TRACE_EVENT(write_buffer_maybe_flush,
	TP_PROTO(struct btree_trans *trans, unsigned long caller_ip, const char *key),
	TP_ARGS(trans, caller_ip, key),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__string(key,			key		)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__assign_str(key);
	),

	TP_printk("%s %pS %s", __entry->trans_fn, (void *) __entry->caller_ip, __get_str(key))
);

DEFINE_EVENT(fs_str, rebalance_extent,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

DEFINE_EVENT(fs_str, data_update,
	TP_PROTO(struct bch_fs *c, const char *str),
	TP_ARGS(c, str)
);

TRACE_EVENT(error_downcast,
	TP_PROTO(int bch_err, int std_err, unsigned long ip),
	TP_ARGS(bch_err, std_err, ip),

	TP_STRUCT__entry(
		__array(char,		bch_err, 32		)
		__array(char,		std_err, 32		)
		__array(char,		ip, 32			)
	),

	TP_fast_assign(
		strscpy(__entry->bch_err, bch2_err_str(bch_err), sizeof(__entry->bch_err));
		strscpy(__entry->std_err, bch2_err_str(std_err), sizeof(__entry->std_err));
		snprintf(__entry->ip, sizeof(__entry->ip), "%ps", (void *) ip);
	),

	TP_printk("%s -> %s %s", __entry->bch_err, __entry->std_err, __entry->ip)
);

#ifdef CONFIG_BCACHEFS_PATH_TRACEPOINTS

TRACE_EVENT(update_by_path,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path,
		 struct btree_insert_entry *i, bool overwrite),
	TP_ARGS(trans, path, i, overwrite),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(btree_path_idx_t,	path_idx	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(pos)
		__field(u8,			overwrite	)
		__field(btree_path_idx_t,	update_idx	)
		__field(btree_path_idx_t,	nr_updates	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->path_idx		= path - trans->paths;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
		__entry->overwrite		= overwrite;
		__entry->update_idx		= i - trans->updates;
		__entry->nr_updates		= trans->nr_updates;
	),

	TP_printk("%s path %3u btree %s pos %llu:%llu:%u overwrite %u update %u/%u",
		  __entry->trans_fn,
		  __entry->path_idx,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->overwrite,
		  __entry->update_idx,
		  __entry->nr_updates)
);

TRACE_EVENT(btree_path_lock,
	TP_PROTO(struct btree_trans *trans,
		 unsigned long caller_ip,
		 struct btree_bkey_cached_common *b),
	TP_ARGS(trans, caller_ip, b),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(unsigned long,		caller_ip	)
		__field(u8,			btree_id	)
		__field(u8,			level		)
		__array(char,			node, 24	)
		__field(u32,			lock_seq	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));
		__entry->caller_ip		= caller_ip;
		__entry->btree_id		= b->btree_id;
		__entry->level			= b->level;

		scnprintf(__entry->node, sizeof(__entry->node), "%px", b);
		__entry->lock_seq		= six_lock_seq(&b->lock);
	),

	TP_printk("%s %pS\nbtree %s level %u node %s lock seq %u",
		  __entry->trans_fn,
		  (void *) __entry->caller_ip,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->level,
		  __entry->node,
		  __entry->lock_seq)
);

DECLARE_EVENT_CLASS(btree_path_ev,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path),

	TP_STRUCT__entry(
		__field(u16,			idx		)
		__field(u8,			ref		)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->idx			= path - trans->paths;
		__entry->ref			= path->ref;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
	),

	TP_printk("path %3u ref %u btree %s pos %llu:%llu:%u",
		  __entry->idx, __entry->ref,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

DEFINE_EVENT(btree_path_ev, btree_path_get_ll,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path)
);

DEFINE_EVENT(btree_path_ev, btree_path_put_ll,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path)
);

DEFINE_EVENT(btree_path_ev, btree_path_should_be_locked,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path)
);

TRACE_EVENT(btree_path_alloc,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
		__field(u8,			locks_want	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->idx			= path - trans->paths;
		__entry->locks_want		= path->locks_want;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(pos, path->pos);
	),

	TP_printk("path %3u btree %s locks_want %u pos %llu:%llu:%u",
		  __entry->idx,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->locks_want,
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot)
);

TRACE_EVENT(btree_path_get,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path, struct bpos *new_pos),
	TP_ARGS(trans, path, new_pos),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
		__field(u8,			ref		)
		__field(u8,			preserve	)
		__field(u8,			locks_want	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(old_pos)
		TRACE_BPOS_entries(new_pos)
	),

	TP_fast_assign(
		__entry->idx			= path - trans->paths;
		__entry->ref			= path->ref;
		__entry->preserve		= path->preserve;
		__entry->locks_want		= path->locks_want;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(old_pos, path->pos);
		TRACE_BPOS_assign(new_pos, *new_pos);
	),

	TP_printk("    path %3u ref %u preserve %u btree %s locks_want %u pos %llu:%llu:%u -> %llu:%llu:%u",
		  __entry->idx,
		  __entry->ref,
		  __entry->preserve,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->locks_want,
		  __entry->old_pos_inode,
		  __entry->old_pos_offset,
		  __entry->old_pos_snapshot,
		  __entry->new_pos_inode,
		  __entry->new_pos_offset,
		  __entry->new_pos_snapshot)
);

DECLARE_EVENT_CLASS(btree_path_clone,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path, struct btree_path *new),
	TP_ARGS(trans, path, new),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
		__field(u8,			new_idx		)
		__field(u8,			btree_id	)
		__field(u8,			ref		)
		__field(u8,			preserve	)
		TRACE_BPOS_entries(pos)
	),

	TP_fast_assign(
		__entry->idx			= path - trans->paths;
		__entry->new_idx		= new - trans->paths;
		__entry->btree_id		= path->btree_id;
		__entry->ref			= path->ref;
		__entry->preserve		= path->preserve;
		TRACE_BPOS_assign(pos, path->pos);
	),

	TP_printk("  path %3u ref %u preserve %u btree %s %llu:%llu:%u -> %u",
		  __entry->idx,
		  __entry->ref,
		  __entry->preserve,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->new_idx)
);

DEFINE_EVENT(btree_path_clone, btree_path_clone,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path, struct btree_path *new),
	TP_ARGS(trans, path, new)
);

DEFINE_EVENT(btree_path_clone, btree_path_save_pos,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path, struct btree_path *new),
	TP_ARGS(trans, path, new)
);

DECLARE_EVENT_CLASS(btree_path_traverse,
	TP_PROTO(struct btree_trans *trans,
		 struct btree_path *path),
	TP_ARGS(trans, path),

	TP_STRUCT__entry(
		__array(char,			trans_fn, 32	)
		__field(btree_path_idx_t,	idx		)
		__field(u8,			ref		)
		__field(u8,			preserve	)
		__field(u8,			should_be_locked )
		__field(u8,			btree_id	)
		__field(u8,			level		)
		TRACE_BPOS_entries(pos)
		__field(u8,			locks_want	)
		__field(u8,			nodes_locked	)
		__array(char,			node0, 24	)
		__array(char,			node1, 24	)
		__array(char,			node2, 24	)
		__array(char,			node3, 24	)
	),

	TP_fast_assign(
		strscpy(__entry->trans_fn, trans->fn, sizeof(__entry->trans_fn));

		__entry->idx			= path - trans->paths;
		__entry->ref			= path->ref;
		__entry->preserve		= path->preserve;
		__entry->btree_id		= path->btree_id;
		__entry->level			= path->level;
		TRACE_BPOS_assign(pos, path->pos);

		__entry->locks_want		= path->locks_want;
		__entry->nodes_locked		= path->nodes_locked;
		struct btree *b = path->l[0].b;
		if (IS_ERR(b))
			strscpy(__entry->node0, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node0, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[1].b;
		if (IS_ERR(b))
			strscpy(__entry->node1, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node1, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[2].b;
		if (IS_ERR(b))
			strscpy(__entry->node2, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node2, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[3].b;
		if (IS_ERR(b))
			strscpy(__entry->node3, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node3, sizeof(__entry->node0), "%px", &b->c);
	),

	TP_printk("%s\npath %3u ref %u preserve %u btree %s %llu:%llu:%u level %u locks_want %u\n"
		  "locks %u %u %u %u node %s %s %s %s",
		  __entry->trans_fn,
		  __entry->idx,
		  __entry->ref,
		  __entry->preserve,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->pos_inode,
		  __entry->pos_offset,
		  __entry->pos_snapshot,
		  __entry->level,
		  __entry->locks_want,
		  (__entry->nodes_locked >> 6) & 3,
		  (__entry->nodes_locked >> 4) & 3,
		  (__entry->nodes_locked >> 2) & 3,
		  (__entry->nodes_locked >> 0) & 3,
		  __entry->node3,
		  __entry->node2,
		  __entry->node1,
		  __entry->node0)
);

DEFINE_EVENT(btree_path_traverse, btree_path_traverse_start,
	TP_PROTO(struct btree_trans *trans,
		 struct btree_path *path),
	TP_ARGS(trans, path)
);

DEFINE_EVENT(btree_path_traverse, btree_path_traverse_end,
	TP_PROTO(struct btree_trans *trans, struct btree_path *path),
	TP_ARGS(trans, path)
);

TRACE_EVENT(btree_path_set_pos,
	TP_PROTO(struct btree_trans *trans,
		 struct btree_path *path,
		 struct bpos *new_pos),
	TP_ARGS(trans, path, new_pos),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
		__field(u8,			ref		)
		__field(u8,			preserve	)
		__field(u8,			btree_id	)
		TRACE_BPOS_entries(old_pos)
		TRACE_BPOS_entries(new_pos)
		__field(u8,			locks_want	)
		__field(u8,			nodes_locked	)
		__array(char,			node0, 24	)
		__array(char,			node1, 24	)
		__array(char,			node2, 24	)
		__array(char,			node3, 24	)
	),

	TP_fast_assign(
		__entry->idx			= path - trans->paths;
		__entry->ref			= path->ref;
		__entry->preserve		= path->preserve;
		__entry->btree_id		= path->btree_id;
		TRACE_BPOS_assign(old_pos, path->pos);
		TRACE_BPOS_assign(new_pos, *new_pos);

		__entry->nodes_locked		= path->nodes_locked;
		struct btree *b = path->l[0].b;
		if (IS_ERR(b))
			strscpy(__entry->node0, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node0, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[1].b;
		if (IS_ERR(b))
			strscpy(__entry->node1, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node1, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[2].b;
		if (IS_ERR(b))
			strscpy(__entry->node2, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node2, sizeof(__entry->node0), "%px", &b->c);
		b = path->l[3].b;
		if (IS_ERR(b))
			strscpy(__entry->node3, bch2_err_str(PTR_ERR(b)), sizeof(__entry->node0));
		else
			scnprintf(__entry->node3, sizeof(__entry->node0), "%px", &b->c);
	),

	TP_printk("\npath %3u ref %u preserve %u btree %s %llu:%llu:%u -> %llu:%llu:%u\n"
		  "locks %u %u %u %u node %s %s %s %s",
		  __entry->idx,
		  __entry->ref,
		  __entry->preserve,
		  bch2_btree_id_str(__entry->btree_id),
		  __entry->old_pos_inode,
		  __entry->old_pos_offset,
		  __entry->old_pos_snapshot,
		  __entry->new_pos_inode,
		  __entry->new_pos_offset,
		  __entry->new_pos_snapshot,
		  (__entry->nodes_locked >> 6) & 3,
		  (__entry->nodes_locked >> 4) & 3,
		  (__entry->nodes_locked >> 2) & 3,
		  (__entry->nodes_locked >> 0) & 3,
		  __entry->node3,
		  __entry->node2,
		  __entry->node1,
		  __entry->node0)
);

TRACE_EVENT(btree_path_free,
	TP_PROTO(struct btree_trans *trans, btree_path_idx_t path, struct btree_path *dup),
	TP_ARGS(trans, path, dup),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
		__field(u8,			preserve	)
		__field(u8,			should_be_locked)
		__field(s8,			dup		)
		__field(u8,			dup_locked	)
	),

	TP_fast_assign(
		__entry->idx			= path;
		__entry->preserve		= trans->paths[path].preserve;
		__entry->should_be_locked	= trans->paths[path].should_be_locked;
		__entry->dup			= dup ? dup - trans->paths  : -1;
		__entry->dup_locked		= dup ? btree_node_locked(dup, dup->level) : 0;
	),

	TP_printk("   path %3u %c %c dup %2i locked %u", __entry->idx,
		  __entry->preserve ? 'P' : ' ',
		  __entry->should_be_locked ? 'S' : ' ',
		  __entry->dup,
		  __entry->dup_locked)
);

TRACE_EVENT(btree_path_free_trans_begin,
	TP_PROTO(btree_path_idx_t path),
	TP_ARGS(path),

	TP_STRUCT__entry(
		__field(btree_path_idx_t,	idx		)
	),

	TP_fast_assign(
		__entry->idx			= path;
	),

	TP_printk("   path %3u", __entry->idx)
);

#else /* CONFIG_BCACHEFS_PATH_TRACEPOINTS */
#ifndef _TRACE_BCACHEFS_H

static inline void trace_update_by_path(struct btree_trans *trans, struct btree_path *path,
					struct btree_insert_entry *i, bool overwrite) {}
static inline void trace_btree_path_lock(struct btree_trans *trans, unsigned long caller_ip, struct btree_bkey_cached_common *b) {}
static inline void trace_btree_path_get_ll(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_put_ll(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_should_be_locked(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_alloc(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_get(struct btree_trans *trans, struct btree_path *path, struct bpos *new_pos) {}
static inline void trace_btree_path_clone(struct btree_trans *trans, struct btree_path *path, struct btree_path *new) {}
static inline void trace_btree_path_save_pos(struct btree_trans *trans, struct btree_path *path, struct btree_path *new) {}
static inline void trace_btree_path_traverse_start(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_traverse_end(struct btree_trans *trans, struct btree_path *path) {}
static inline void trace_btree_path_set_pos(struct btree_trans *trans, struct btree_path *path, struct bpos *new_pos) {}
static inline void trace_btree_path_free(struct btree_trans *trans, btree_path_idx_t path, struct btree_path *dup) {}
static inline void trace_btree_path_free_trans_begin(btree_path_idx_t path) {}

#endif
#endif /* CONFIG_BCACHEFS_PATH_TRACEPOINTS */

#define _TRACE_BCACHEFS_H
#endif /* _TRACE_BCACHEFS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../fs/bcachefs

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
