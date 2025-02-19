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
#include "btree_update_interior.h"
#include "buckets.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "fsck.h"
#include "inode.h"
#include "journal_reclaim.h"
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
	struct bio *bio;
	bool failed = false, saw_error = false;

	struct bch_dev *ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ);
	if (!ca)
		return false;

	bio = bio_alloc_bioset(ca->disk_sb.bdev,
			       buf_pages(n_sorted, btree_buf_bytes(b)),
			       REQ_OP_READ|REQ_META,
			       GFP_NOFS,
			       &c->btree_bio);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bch2_bio_map(bio, n_sorted, btree_buf_bytes(b));

	submit_bio_wait(bio);

	bio_put(bio);
	percpu_ref_put(&ca->io_ref);

	memcpy(n_ondisk, n_sorted, btree_buf_bytes(b));

	v->written = 0;
	if (bch2_btree_node_read_done(c, ca, v, false, &saw_error) || saw_error)
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
		c->verify_ondisk = kvmalloc(btree_buf_bytes(b), GFP_KERNEL);
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

	for (k = inmemory->start; k != vstruct_last(inmemory); k = bkey_p_next(k))
		if (k->type == KEY_TYPE_btree_ptr_v2)
			((struct bch_btree_ptr_v2 *) bkeyp_val(&b->format, k))->mem_ptr = 0;

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
		bch2_fs_fatal_error(c, ": btree node verify failed for: %s\n", buf.buf);
		printbuf_exit(&buf);
	}
out:
	mutex_unlock(&c->verify_lock);
	bch2_btree_node_io_unlock(b);
}

void bch2_btree_node_ondisk_to_text(struct printbuf *out, struct bch_fs *c,
				    const struct btree *b)
{
	struct btree_node *n_ondisk = NULL;
	struct extent_ptr_decoded pick;
	struct bch_dev *ca;
	struct bio *bio = NULL;
	unsigned offset = 0;
	int ret;

	if (bch2_bkey_pick_read_device(c, bkey_i_to_s_c(&b->key), NULL, &pick) <= 0) {
		prt_printf(out, "error getting device to read from: invalid device\n");
		return;
	}

	ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ);
	if (!ca) {
		prt_printf(out, "error getting device to read from: not online\n");
		return;
	}

	n_ondisk = kvmalloc(btree_buf_bytes(b), GFP_KERNEL);
	if (!n_ondisk) {
		prt_printf(out, "memory allocation failure\n");
		goto out;
	}

	bio = bio_alloc_bioset(ca->disk_sb.bdev,
			       buf_pages(n_ondisk, btree_buf_bytes(b)),
			       REQ_OP_READ|REQ_META,
			       GFP_NOFS,
			       &c->btree_bio);
	bio->bi_iter.bi_sector	= pick.ptr.offset;
	bch2_bio_map(bio, n_ondisk, btree_buf_bytes(b));

	ret = submit_bio_wait(bio);
	if (ret) {
		prt_printf(out, "IO error reading btree node: %s\n", bch2_err_str(ret));
		goto out;
	}

	while (offset < btree_sectors(c)) {
		struct bset *i;
		struct nonce nonce;
		struct bch_csum csum;
		struct bkey_packed *k;
		unsigned sectors;

		if (!offset) {
			i = &n_ondisk->keys;

			if (!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i))) {
				prt_printf(out, "unknown checksum type at offset %u: %llu\n",
					   offset, BSET_CSUM_TYPE(i));
				goto out;
			}

			nonce = btree_nonce(i, offset << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, n_ondisk);

			if (bch2_crc_cmp(csum, n_ondisk->csum)) {
				prt_printf(out, "invalid checksum\n");
				goto out;
			}

			bset_encrypt(c, i, offset << 9);

			sectors = vstruct_sectors(n_ondisk, c->block_bits);
		} else {
			struct btree_node_entry *bne = (void *) n_ondisk + (offset << 9);

			i = &bne->keys;

			if (i->seq != n_ondisk->keys.seq)
				break;

			if (!bch2_checksum_type_valid(c, BSET_CSUM_TYPE(i))) {
				prt_printf(out, "unknown checksum type at offset %u: %llu\n",
					   offset, BSET_CSUM_TYPE(i));
				goto out;
			}

			nonce = btree_nonce(i, offset << 9);
			csum = csum_vstruct(c, BSET_CSUM_TYPE(i), nonce, bne);

			if (bch2_crc_cmp(csum, bne->csum)) {
				prt_printf(out, "invalid checksum");
				goto out;
			}

			bset_encrypt(c, i, offset << 9);

			sectors = vstruct_sectors(bne, c->block_bits);
		}

		prt_printf(out, "  offset %u version %u, journal seq %llu\n",
			   offset,
			   le16_to_cpu(i->version),
			   le64_to_cpu(i->journal_seq));
		offset += sectors;

		printbuf_indent_add(out, 4);

		for (k = i->start; k != vstruct_last(i); k = bkey_p_next(k)) {
			struct bkey u;

			bch2_bkey_val_to_text(out, c, bkey_disassemble(b, k, &u));
			prt_newline(out);
		}

		printbuf_indent_sub(out, 4);
	}
out:
	if (bio)
		bio_put(bio);
	kvfree(n_ondisk);
	percpu_ref_put(&ca->io_ref);
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
		int copied = bytes - copy_to_user(i->ubuf, i->buf.buf, bytes);

		i->ret	 += copied;
		i->ubuf	 += copied;
		i->size	 -= copied;
		i->buf.pos -= copied;
		memmove(i->buf.buf, i->buf.buf + copied, i->buf.pos);

		if (copied != bytes)
			return -EFAULT;
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

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	return flush_buf(i) ?:
		bch2_trans_run(i->c,
			for_each_btree_key(trans, iter, i->id, i->from,
					   BTREE_ITER_prefetch|
					   BTREE_ITER_all_snapshots, k, ({
				bch2_bkey_val_to_text(&i->buf, i->c, k);
				prt_newline(&i->buf);
				bch2_trans_unlock(trans);
				i->from = bpos_successor(iter.pos);
				flush_buf(i);
			}))) ?:
		i->ret;
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

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	ssize_t ret = flush_buf(i);
	if (ret)
		return ret;

	if (bpos_eq(SPOS_MAX, i->from))
		return i->ret;

	return bch2_trans_run(i->c,
		for_each_btree_node(trans, iter, i->id, i->from, 0, b, ({
			bch2_btree_node_to_text(&i->buf, i->c, b);
			i->from = !bpos_eq(SPOS_MAX, b->key.k.p)
				? bpos_successor(b->key.k.p)
				: b->key.k.p;

			drop_locks_do(trans, flush_buf(i));
		}))) ?: i->ret;
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

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	return flush_buf(i) ?:
		bch2_trans_run(i->c,
			for_each_btree_key(trans, iter, i->id, i->from,
					   BTREE_ITER_prefetch|
					   BTREE_ITER_all_snapshots, k, ({
				struct btree_path_level *l =
					&btree_iter_path(trans, &iter)->l[0];
				struct bkey_packed *_k =
					bch2_btree_node_iter_peek(&l->iter, l->b);

				if (bpos_gt(l->b->key.k.p, i->prev_node)) {
					bch2_btree_node_to_text(&i->buf, i->c, l->b);
					i->prev_node = l->b->key.k.p;
				}

				bch2_bfloat_to_text(&i->buf, l->b, _k);
				bch2_trans_unlock(trans);
				i->from = bpos_successor(iter.pos);
				flush_buf(i);
			}))) ?:
		i->ret;
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

	prt_printf(out, "%px ", b);
	bch2_btree_id_level_to_text(out, b->c.btree_id, b->c.level);
	prt_printf(out, "\n");

	printbuf_indent_add(out, 2);

	bch2_bkey_val_to_text(out, c, bkey_i_to_s_c(&b->key));
	prt_newline(out);

	prt_printf(out, "flags:\t");
	prt_bitflags(out, bch2_btree_node_flags, b->flags);
	prt_newline(out);

	prt_printf(out, "pcpu read locks:\t%u\n",	b->c.lock.readers != NULL);
	prt_printf(out, "written:\t%u\n",		b->written);
	prt_printf(out, "writes blocked:\t%u\n",	!list_empty_careful(&b->write_blocked));
	prt_printf(out, "will make reachable:\t%lx\n",	b->will_make_reachable);

	prt_printf(out, "journal pin %px:\t%llu\n",
		   &b->writes[0].journal, b->writes[0].journal.seq);
	prt_printf(out, "journal pin %px:\t%llu\n",
		   &b->writes[1].journal, b->writes[1].journal.seq);

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
			i->iter++;
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

typedef int (*list_cmp_fn)(const struct list_head *l, const struct list_head *r);

static void list_sort(struct list_head *head, list_cmp_fn cmp)
{
	struct list_head *pos;

	list_for_each(pos, head)
		while (!list_is_last(pos, head) &&
		       cmp(pos, pos->next) > 0) {
			struct list_head *pos2, *next = pos->next;

			list_del(next);
			list_for_each(pos2, head)
				if (cmp(next, pos2) < 0)
					goto pos_found;
			BUG();
pos_found:
			list_add_tail(next, pos2);
		}
}

static int list_ptr_order_cmp(const struct list_head *l, const struct list_head *r)
{
	return cmp_int(l, r);
}

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
restart:
	seqmutex_lock(&c->btree_trans_lock);
	list_sort(&c->btree_trans_list, list_ptr_order_cmp);

	list_for_each_entry(trans, &c->btree_trans_list, list) {
		if ((ulong) trans <= i->iter)
			continue;

		i->iter = (ulong) trans;

		if (!closure_get_not_zero(&trans->ref))
			continue;

		u32 seq = seqmutex_unlock(&c->btree_trans_lock);

		bch2_btree_trans_to_text(&i->buf, trans);

		prt_printf(&i->buf, "backtrace:\n");
		printbuf_indent_add(&i->buf, 2);
		bch2_prt_task_backtrace(&i->buf, trans->locking_wait.task, 0, GFP_KERNEL);
		printbuf_indent_sub(&i->buf, 2);
		prt_newline(&i->buf);

		closure_put(&trans->ref);

		ret = flush_buf(i);
		if (ret)
			goto unlocked;

		if (!seqmutex_relock(&c->btree_trans_lock, seq))
			goto restart;
	}
	seqmutex_unlock(&c->btree_trans_lock);
unlocked:
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

	while (1) {
		err = flush_buf(i);
		if (err)
			return err;

		if (!i->size)
			break;

		if (done)
			break;

		done = bch2_journal_seq_pins_to_text(&i->buf, &c->journal, &i->iter);
		i->iter++;
	}

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

static ssize_t bch2_btree_updates_read(struct file *file, char __user *buf,
				       size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	int err;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	if (!i->iter) {
		bch2_btree_updates_to_text(&i->buf, c);
		i->iter++;
	}

	err = flush_buf(i);
	if (err)
		return err;

	if (i->buf.allocation_failure)
		return -ENOMEM;

	return i->ret;
}

static const struct file_operations btree_updates_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_btree_updates_read,
};

static int btree_transaction_stats_open(struct inode *inode, struct file *file)
{
	struct bch_fs *c = inode->i_private;
	struct dump_iter *i;

	i = kzalloc(sizeof(struct dump_iter), GFP_KERNEL);
	if (!i)
		return -ENOMEM;

	i->iter = 1;
	i->c    = c;
	i->buf  = PRINTBUF;
	file->private_data = i;

	return 0;
}

static int btree_transaction_stats_release(struct inode *inode, struct file *file)
{
	struct dump_iter *i = file->private_data;

	printbuf_exit(&i->buf);
	kfree(i);

	return 0;
}

static ssize_t btree_transaction_stats_read(struct file *file, char __user *buf,
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

		if (i->iter == ARRAY_SIZE(bch2_btree_transaction_fns) ||
		    !bch2_btree_transaction_fns[i->iter])
			break;

		prt_printf(&i->buf, "%s:\n", bch2_btree_transaction_fns[i->iter]);
		printbuf_indent_add(&i->buf, 2);

		mutex_lock(&s->lock);

		prt_printf(&i->buf, "Max mem used: %u\n", s->max_mem);
		prt_printf(&i->buf, "Transaction duration:\n");

		printbuf_indent_add(&i->buf, 2);
		bch2_time_stats_to_text(&i->buf, &s->duration);
		printbuf_indent_sub(&i->buf, 2);

		if (IS_ENABLED(CONFIG_BCACHEFS_LOCK_TIME_STATS)) {
			prt_printf(&i->buf, "Lock hold times:\n");

			printbuf_indent_add(&i->buf, 2);
			bch2_time_stats_to_text(&i->buf, &s->lock_hold_times);
			printbuf_indent_sub(&i->buf, 2);
		}

		if (s->max_paths_text) {
			prt_printf(&i->buf, "Maximum allocated btree paths (%u):\n", s->nr_max_paths);

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

static const struct file_operations btree_transaction_stats_op = {
	.owner		= THIS_MODULE,
	.open		= btree_transaction_stats_open,
	.release	= btree_transaction_stats_release,
	.read		= btree_transaction_stats_read,
};

/* walk btree transactions until we find a deadlock and print it */
static void btree_deadlock_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct btree_trans *trans;
	ulong iter = 0;
restart:
	seqmutex_lock(&c->btree_trans_lock);
	list_sort(&c->btree_trans_list, list_ptr_order_cmp);

	list_for_each_entry(trans, &c->btree_trans_list, list) {
		if ((ulong) trans <= iter)
			continue;

		iter = (ulong) trans;

		if (!closure_get_not_zero(&trans->ref))
			continue;

		u32 seq = seqmutex_unlock(&c->btree_trans_lock);

		bool found = bch2_check_for_deadlock(trans, out) != 0;

		closure_put(&trans->ref);

		if (found)
			return;

		if (!seqmutex_relock(&c->btree_trans_lock, seq))
			goto restart;
	}
	seqmutex_unlock(&c->btree_trans_lock);
}

static ssize_t bch2_btree_deadlock_read(struct file *file, char __user *buf,
					    size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct bch_fs *c = i->c;
	ssize_t ret = 0;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	if (!i->iter) {
		btree_deadlock_to_text(&i->buf, c);
		i->iter++;
	}

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

static void bch2_fs_debug_btree_init(struct bch_fs *c, struct btree_debug *bd)
{
	struct dentry *d;

	d = debugfs_create_dir(bch2_btree_id_str(bd->id), c->btree_debug_dir);

	debugfs_create_file("keys", 0400, d, bd, &btree_debug_ops);

	debugfs_create_file("formats", 0400, d, bd, &btree_format_debug_ops);

	debugfs_create_file("bfloat-failed", 0400, d, bd,
			    &bfloat_failed_debug_ops);
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

	debugfs_create_file("btree_transactions", 0400, c->fs_debug_dir,
			    c->btree_debug, &btree_transactions_ops);

	debugfs_create_file("journal_pins", 0400, c->fs_debug_dir,
			    c->btree_debug, &journal_pins_ops);

	debugfs_create_file("btree_updates", 0400, c->fs_debug_dir,
			    c->btree_debug, &btree_updates_ops);

	debugfs_create_file("btree_transaction_stats", 0400, c->fs_debug_dir,
			    c, &btree_transaction_stats_op);

	debugfs_create_file("btree_deadlock", 0400, c->fs_debug_dir,
			    c->btree_debug, &btree_deadlock_ops);

	c->btree_debug_dir = debugfs_create_dir("btrees", c->fs_debug_dir);
	if (IS_ERR_OR_NULL(c->btree_debug_dir))
		return;

	for (bd = c->btree_debug;
	     bd < c->btree_debug + ARRAY_SIZE(c->btree_debug);
	     bd++) {
		bd->id = bd - c->btree_debug;
		bch2_fs_debug_btree_init(c, bd);
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
	bch_debug = debugfs_create_dir("bcachefs", NULL);
	return 0;
}
