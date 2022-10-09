// SPDX-License-Identifier: GPL-2.0
/*
 * Assorted bcachefs debug code
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "buckets.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "fsck.h"
#include "inode.h"
#include "io.h"
#include "super.h"

#include <linux/console.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>

static struct dentry *bch_debug;

static bool bch2_btree_verify_replica(struct bch_fs *c, struct btree *b,
				      struct extent_ptr_decoded pick)
{
	struct btree *v = c->verify_data;
	struct btree_node *n_ondisk = c->verify_ondisk;
	struct btree_node *n_sorted = c->verify_data->data;
	struct bset *sorted, *inmemory = &b->data->keys;
	struct bch_dev *ca = bch_dev_bkey_exists(c, pick.ptr.dev);
	struct bio *bio;
	bool failed = false;

	if (!bch2_dev_get_ioref(ca, READ))
		return false;

	bio = bio_alloc_bioset(ca->disk_sb.bdev,
			       buf_pages(n_sorted, btree_bytes(c)),
			       REQ_OP_READ|REQ_META,
			       GFP_NOIO,
			       &c->btree_bio);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bch2_bio_map(bio, n_sorted, btree_bytes(c));

	submit_bio_wait(bio);

	bio_put(bio);
	percpu_ref_put(&ca->io_ref);

	memcpy(n_ondisk, n_sorted, btree_bytes(c));

	v->written = 0;
	if (bch2_btree_node_read_done(c, ca, v, false))
		return false;

	n_sorted = c->verify_data->data;
	sorted = &n_sorted->keys;

	if (inmemory->u64s != sorted->u64s ||
	    memcmp(inmemory->start,
		   sorted->start,
		   vstruct_end(inmemory) - (void *) inmemory->start)) {
		unsigned offset = 0, sectors;
		struct bset *i;
		unsigned j;

		console_lock();

		printk(KERN_ERR "*** in memory:\n");
		bch2_dump_bset(c, b, inmemory, 0);

		printk(KERN_ERR "*** read back in:\n");
		bch2_dump_bset(c, v, sorted, 0);

		while (offset < v->written) {
			if (!offset) {
				i = &n_ondisk->keys;
				sectors = vstruct_blocks(n_ondisk, c->block_bits) <<
					c->block_bits;
			} else {
				struct btree_node_entry *bne =
					(void *) n_ondisk + (offset << 9);
				i = &bne->keys;

				sectors = vstruct_blocks(bne, c->block_bits) <<
					c->block_bits;
			}

			printk(KERN_ERR "*** on disk block %u:\n", offset);
			bch2_dump_bset(c, b, i, offset);

			offset += sectors;
		}

		for (j = 0; j < le16_to_cpu(inmemory->u64s); j++)
			if (inmemory->_data[j] != sorted->_data[j])
				break;

		console_unlock();
		bch_err(c, "verify failed at key %u", j);

		failed = true;
	}

	if (v->written != b->written) {
		bch_err(c, "written wrong: expected %u, got %u",
			b->written, v->written);
		failed = true;
	}

	return failed;
}

void __bch2_btree_verify(struct bch_fs *c, struct btree *b)
{
	struct bkey_ptrs_c ptrs;
	struct extent_ptr_decoded p;
	const union bch_extent_entry *entry;
	struct btree *v;
	struct bset *inmemory = &b->data->keys;
	struct bkey_packed *k;
	bool failed = false;

	if (c->opts.nochanges)
		return;

	bch2_btree_node_io_lock(b);
	mutex_lock(&c->verify_lock);

	if (!c->verify_ondisk) {
		c->verify_ondisk = kvpmalloc(btree_bytes(c), GFP_KERNEL);
		if (!c->verify_ondisk)
			goto out;
	}

	if (!c->verify_data) {
		c->verify_data = __bch2_btree_node_mem_alloc(c);
		if (!c->verify_data)
			goto out;

		list_del_init(&c->verify_data->list);
	}

	BUG_ON(b->nsets != 1);

	for (k = inmemory->start; k != vstruct_last(inmemory); k = bkey_next(k))
		if (k->type == KEY_TYPE_btree_ptr_v2) {
			struct bch_btree_ptr_v2 *v = (void *) bkeyp_val(&b->format, k);
			v->mem_ptr = 0;
		}

	v = c->verify_data;
	bkey_copy(&v->key, &b->key);
	v->c.level	= b->c.level;
	v->c.btree_id	= b->c.btree_id;
	bch2_btree_keys_init(v);

	ptrs = bch2_bkey_ptrs_c(bkey_i_to_s_c(&b->key));
	bkey_for_each_ptr_decode(&b->key.k, ptrs, p, entry)
		failed |= bch2_btree_verify_replica(c, b, p);

	if (failed) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		bch2_fs_fatal_error(c, "btree node verify failed for : %s\n", buf.buf);
		printbuf_exit(&buf);
	}
out:
	mutex_unlock(&c->verify_lock);
	bch2_btree_node_io_unlock(b);
}

#ifdef CONFIG_DEBUG_FS

/* XXX: bch_fs refcounting */

struct dump_iter {
	struct bch_fs		*c;
	enum btree_id		id;
	struct bpos		from;
	struct bpos		prev_node;
	u64			iter;

	struct printbuf		buf;

	char __user		*ubuf;	/* destination user buffer */
	size_t			size;	/* size of requested read */
	ssize_t			ret;	/* bytes read so far */
};

static ssize_t flush_buf(struct dump_iter *i)
{
	if (i->buf.pos) {
		size_t bytes = min_t(size_t, i->buf.pos, i->size);
		int err = copy_to_user(i->ubuf, i->buf.buf, bytes);

		if (err)
			return err;

		i->ret	 += bytes;
		i->ubuf	 += bytes;
		i->size	 -= bytes;
		i->buf.pos -= bytes;
		memmove(i->buf.buf, i->buf.buf + bytes, i->buf.pos);
	}

	return i->size ? 0 : i->ret;
}

static int bch2_dump_open(struct inode *inode, struct file *file)
{
	struct btree_debug *bd = inode->i_private;
	struct dump_iter *i;

	i = kzalloc(sizeof(struct dump_iter), GFP_KERNEL);
	if (!i)
		return -ENOMEM;

	file->private_data = i;
	i->from = POS_MIN;
	i->iter	= 0;
	i->c	= container_of(bd, struct bch_fs, btree_debug[bd->id]);
	i->id	= bd->id;
	i->buf	= PRINTBUF;

	return 0;
}

static int bch2_dump_release(struct inode *inode, struct file *file)
{
	struct dump_iter *i = file->private_data;

	printbuf_exit(&i->buf);
	kfree(i);
	return 0;
}

static ssize_t bch2_read_btree(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	ssize_t ret;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	bch2_trans_init(&trans, i->c, 0, 0);

	ret = for_each_btree_key2(&trans, iter, i->id, i->from,
				  BTREE_ITER_PREFETCH|
				  BTREE_ITER_ALL_SNAPSHOTS, k, ({
		ret = flush_buf(i);
		if (ret)
			break;

		bch2_bkey_val_to_text(&i->buf, i->c, k);
		prt_newline(&i->buf);
		0;
	}));
	i->from = iter.pos;

	if (!ret)
		ret = flush_buf(i);

	bch2_trans_exit(&trans);

	return ret ?: i->ret;
}

static const struct file_operations btree_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_read_btree,
};

static ssize_t bch2_read_btree_formats(struct file *file, char __user *buf,
				       size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree *b;
	ssize_t ret;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	ret = flush_buf(i);
	if (ret)
		return ret;

	if (!bpos_cmp(SPOS_MAX, i->from))
		return i->ret;

	bch2_trans_init(&trans, i->c, 0, 0);

	for_each_btree_node(&trans, iter, i->id, i->from, 0, b, ret) {
		ret = flush_buf(i);
		if (ret)
			break;

		bch2_btree_node_to_text(&i->buf, i->c, b);
		i->from = bpos_cmp(SPOS_MAX, b->key.k.p)
			? bpos_successor(b->key.k.p)
			: b->key.k.p;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	if (!ret)
		ret = flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations btree_format_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_read_btree_formats,
};

static ssize_t bch2_read_bfloat_failed(struct file *file, char __user *buf,
				       size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	ssize_t ret;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	ret = flush_buf(i);
	if (ret)
		return ret;

	bch2_trans_init(&trans, i->c, 0, 0);

	ret = for_each_btree_key2(&trans, iter, i->id, i->from,
				  BTREE_ITER_PREFETCH|
				  BTREE_ITER_ALL_SNAPSHOTS, k, ({
		struct btree_path_level *l = &iter.path->l[0];
		struct bkey_packed *_k =
			bch2_btree_node_iter_peek(&l->iter, l->b);

		ret = flush_buf(i);
		if (ret)
			break;

		if (bpos_cmp(l->b->key.k.p, i->prev_node) > 0) {
			bch2_btree_node_to_text(&i->buf, i->c, l->b);
			i->prev_node = l->b->key.k.p;
		}

		bch2_bfloat_to_text(&i->buf, l->b, _k);
		0;
	}));
	i->from = iter.pos;

	bch2_trans_exit(&trans);

	if (!ret)
		ret = flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations bfloat_failed_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_read_bfloat_failed,
};

static void bch2_cached_btree_node_to_text(struct printbuf *out, struct bch_fs *c,
					   struct btree *b)
{
	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 32);

	prt_printf(out, "%px btree=%s l=%u ",
	       b,
	       bch2_btree_ids[b->c.btree_id],
	       b->c.level);
	prt_newline(out);

	printbuf_indent_add(out, 2);

	bch2_bkey_val_to_text(out, c, bkey_i_to_s_c(&b->key));
	prt_newline(out);

	prt_printf(out, "flags: ");
	prt_tab(out);
	prt_bitflags(out, bch2_btree_node_flags, b->flags);
	prt_newline(out);

	prt_printf(out, "pcpu read locks: ");
	prt_tab(out);
	prt_printf(out, "%u", b->c.lock.readers != NULL);
	prt_newline(out);

	prt_printf(out, "written:");
	prt_tab(out);
	prt_printf(out, "%u", b->written);
	prt_newline(out);

	prt_printf(out, "writes blocked:");
	prt_tab(out);
	prt_printf(out, "%u", !list_empty_careful(&b->write_blocked));
	prt_newline(out);

	prt_printf(out, "will make reachable:");
	prt_tab(out);
	prt_printf(out, "%lx", b->will_make_reachable);
	prt_newline(out);

	prt_printf(out, "journal pin %px:", &b->writes[0].journal);
	prt_tab(out);
	prt_printf(out, "%llu", b->writes[0].journal.seq);
	prt_newline(out);

	prt_printf(out, "journal pin %px:", &b->writes[1].journal);
	prt_tab(out);
	prt_printf(out, "%llu", b->writes[1].journal.seq);
	prt_newline(out);

	printbuf_indent_sub(out, 2);
}

static ssize_t bch2_cached_btree_nodes_read(struct file *file, char __user *buf,
					    size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	bool done = false;
	ssize_t ret = 0;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	do {
		struct bucket_table *tbl;
		struct rhash_head *pos;
		struct btree *b;

		ret = flush_buf(i);
		if (ret)
			return ret;

		rcu_read_lock();
		i->buf.atomic++;
		tbl = rht_dereference_rcu(c->btree_cache.table.tbl,
					  &c->btree_cache.table);
		if (i->iter < tbl->size) {
			rht_for_each_entry_rcu(b, pos, tbl, i->iter, hash)
				bch2_cached_btree_node_to_text(&i->buf, c, b);
			i->iter++;;
		} else {
			done = true;
		}
		--i->buf.atomic;
		rcu_read_unlock();
	} while (!done);

	if (i->buf.allocation_failure)
		ret = -ENOMEM;

	if (!ret)
		ret = flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations cached_btree_nodes_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_cached_btree_nodes_read,
};

#ifdef CONFIG_BCACHEFS_DEBUG_TRANSACTIONS
static ssize_t bch2_btree_transactions_read(struct file *file, char __user *buf,
					    size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	struct btree_trans *trans;
	ssize_t ret = 0;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	mutex_lock(&c->btree_trans_lock);
	list_for_each_entry(trans, &c->btree_trans_list, list) {
		if (trans->locking_wait.task->pid <= i->iter)
			continue;

		ret = flush_buf(i);
		if (ret)
			return ret;

		bch2_btree_trans_to_text(&i->buf, trans);

		prt_printf(&i->buf, "backtrace:");
		prt_newline(&i->buf);
		printbuf_indent_add(&i->buf, 2);
		bch2_prt_backtrace(&i->buf, trans->locking_wait.task);
		printbuf_indent_sub(&i->buf, 2);
		prt_newline(&i->buf);

		i->iter = trans->locking_wait.task->pid;
	}
	mutex_unlock(&c->btree_trans_lock);

	if (i->buf.allocation_failure)
		ret = -ENOMEM;

	if (!ret)
		ret = flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations btree_transactions_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_btree_transactions_read,
};
#endif /* CONFIG_BCACHEFS_DEBUG_TRANSACTIONS */

static ssize_t bch2_journal_pins_read(struct file *file, char __user *buf,
				      size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	bool done = false;
	int err;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	do {
		err = flush_buf(i);
		if (err)
			return err;

		if (!i->size)
			break;

		done = bch2_journal_seq_pins_to_text(&i->buf, &c->journal, &i->iter);
		i->iter++;
	} while (!done);

	if (i->buf.allocation_failure)
		return -ENOMEM;

	return i->ret;
}

static const struct file_operations journal_pins_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_journal_pins_read,
};

static int lock_held_stats_open(struct inode *inode, struct file *file)
{
	struct bch_fs *c = inode->i_private;
	struct dump_iter *i;

	i = kzalloc(sizeof(struct dump_iter), GFP_KERNEL);

	if (!i)
		return -ENOMEM;

	i->iter = 0;
	i->c    = c;
	i->buf  = PRINTBUF;
	file->private_data = i;

	return 0;
}

static int lock_held_stats_release(struct inode *inode, struct file *file)
{
	struct dump_iter *i = file->private_data;

	printbuf_exit(&i->buf);
	kfree(i);

	return 0;
}

static ssize_t lock_held_stats_read(struct file *file, char __user *buf,
				      size_t size, loff_t *ppos)
{
	struct dump_iter        *i = file->private_data;
	struct bch_fs *c = i->c;
	int err;

	i->ubuf = buf;
	i->size = size;
	i->ret  = 0;

	while (1) {
		struct btree_transaction_stats *s = &c->btree_transaction_stats[i->iter];

		err = flush_buf(i);
		if (err)
			return err;

		if (!i->size)
			break;

		if (i->iter == ARRAY_SIZE(c->btree_transaction_fns) ||
		    !c->btree_transaction_fns[i->iter])
			break;

		prt_printf(&i->buf, "%s: ", c->btree_transaction_fns[i->iter]);
		prt_newline(&i->buf);
		printbuf_indent_add(&i->buf, 2);

		mutex_lock(&s->lock);

		prt_printf(&i->buf, "Max mem used: %u", s->max_mem);
		prt_newline(&i->buf);

		if (IS_ENABLED(CONFIG_BCACHEFS_LOCK_TIME_STATS)) {
			prt_printf(&i->buf, "Lock hold times:");
			prt_newline(&i->buf);

			printbuf_indent_add(&i->buf, 2);
			bch2_time_stats_to_text(&i->buf, &s->lock_hold_times);
			printbuf_indent_sub(&i->buf, 2);
		}

		if (s->max_paths_text) {
			prt_printf(&i->buf, "Maximum allocated btree paths (%u):", s->nr_max_paths);
			prt_newline(&i->buf);

			printbuf_indent_add(&i->buf, 2);
			prt_str_indented(&i->buf, s->max_paths_text);
			printbuf_indent_sub(&i->buf, 2);
		}

		mutex_unlock(&s->lock);

		printbuf_indent_sub(&i->buf, 2);
		prt_newline(&i->buf);
		i->iter++;
	}

	if (i->buf.allocation_failure)
		return -ENOMEM;

	return i->ret;
}

static const struct file_operations lock_held_stats_op = {
	.owner = THIS_MODULE,
	.open = lock_held_stats_open,
	.release = lock_held_stats_release,
	.read = lock_held_stats_read,
};

static ssize_t bch2_btree_deadlock_read(struct file *file, char __user *buf,
					    size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	struct btree_trans *trans;
	ssize_t ret = 0;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	if (i->iter)
		goto out;

	mutex_lock(&c->btree_trans_lock);
	list_for_each_entry(trans, &c->btree_trans_list, list) {
		if (trans->locking_wait.task->pid <= i->iter)
			continue;

		ret = flush_buf(i);
		if (ret)
			return ret;

		bch2_check_for_deadlock(trans, &i->buf);

		i->iter = trans->locking_wait.task->pid;
	}
	mutex_unlock(&c->btree_trans_lock);
out:
	if (i->buf.allocation_failure)
		ret = -ENOMEM;

	if (!ret)
		ret = flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations btree_deadlock_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_btree_deadlock_read,
};

void bch2_fs_debug_exit(struct bch_fs *c)
{
	if (!IS_ERR_OR_NULL(c->fs_debug_dir))
		debugfs_remove_recursive(c->fs_debug_dir);
}

void bch2_fs_debug_init(struct bch_fs *c)
{
	struct btree_debug *bd;
	char name[100];

	if (IS_ERR_OR_NULL(bch_debug))
		return;

	snprintf(name, sizeof(name), "%pU", c->sb.user_uuid.b);
	c->fs_debug_dir = debugfs_create_dir(name, bch_debug);
	if (IS_ERR_OR_NULL(c->fs_debug_dir))
		return;

	debugfs_create_file("cached_btree_nodes", 0400, c->fs_debug_dir,
			    c->btree_debug, &cached_btree_nodes_ops);

#ifdef CONFIG_BCACHEFS_DEBUG_TRANSACTIONS
	debugfs_create_file("btree_transactions", 0400, c->fs_debug_dir,
			    c->btree_debug, &btree_transactions_ops);
#endif

	debugfs_create_file("journal_pins", 0400, c->fs_debug_dir,
			    c->btree_debug, &journal_pins_ops);

	debugfs_create_file("btree_transaction_stats", 0400, c->fs_debug_dir,
			    c, &lock_held_stats_op);

	debugfs_create_file("btree_deadlock", 0400, c->fs_debug_dir,
			    c->btree_debug, &btree_deadlock_ops);

	c->btree_debug_dir = debugfs_create_dir("btrees", c->fs_debug_dir);
	if (IS_ERR_OR_NULL(c->btree_debug_dir))
		return;

	for (bd = c->btree_debug;
	     bd < c->btree_debug + ARRAY_SIZE(c->btree_debug);
	     bd++) {
		bd->id = bd - c->btree_debug;
		debugfs_create_file(bch2_btree_ids[bd->id],
				    0400, c->btree_debug_dir, bd,
				    &btree_debug_ops);

		snprintf(name, sizeof(name), "%s-formats",
			 bch2_btree_ids[bd->id]);

		debugfs_create_file(name, 0400, c->btree_debug_dir, bd,
				    &btree_format_debug_ops);

		snprintf(name, sizeof(name), "%s-bfloat-failed",
			 bch2_btree_ids[bd->id]);

		debugfs_create_file(name, 0400, c->btree_debug_dir, bd,
				    &bfloat_failed_debug_ops);
	}
}

#endif

void bch2_debug_exit(void)
{
	if (!IS_ERR_OR_NULL(bch_debug))
		debugfs_remove_recursive(bch_debug);
}

int __init bch2_debug_init(void)
{
	int ret = 0;

	bch_debug = debugfs_create_dir("bcachefs", NULL);
	return ret;
}
