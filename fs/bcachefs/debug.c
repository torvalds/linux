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
		char buf[200];

		bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(&b->key));
		bch2_fs_fatal_error(c, "btree node verify failed for : %s\n", buf);
	}
out:
	mutex_unlock(&c->verify_lock);
	bch2_btree_node_io_unlock(b);
}

#ifdef CONFIG_DEBUG_FS

/* XXX: bch_fs refcounting */

struct dump_iter {
	struct bpos		from;
	struct bch_fs	*c;
	enum btree_id		id;

	char			buf[1 << 12];
	size_t			bytes;	/* what's currently in buf */

	char __user		*ubuf;	/* destination user buffer */
	size_t			size;	/* size of requested read */
	ssize_t			ret;	/* bytes read so far */
};

static int flush_buf(struct dump_iter *i)
{
	if (i->bytes) {
		size_t bytes = min(i->bytes, i->size);
		int err = copy_to_user(i->ubuf, i->buf, bytes);

		if (err)
			return err;

		i->ret	 += bytes;
		i->ubuf	 += bytes;
		i->size	 -= bytes;
		i->bytes -= bytes;
		memmove(i->buf, i->buf + bytes, i->bytes);
	}

	return 0;
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
	i->c	= container_of(bd, struct bch_fs, btree_debug[bd->id]);
	i->id	= bd->id;

	return 0;
}

static int bch2_dump_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t bch2_read_btree(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int err;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	err = flush_buf(i);
	if (err)
		return err;

	if (!i->size)
		return i->ret;

	bch2_trans_init(&trans, i->c, 0, 0);

	bch2_trans_iter_init(&trans, &iter, i->id, i->from,
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);
	k = bch2_btree_iter_peek(&iter);

	while (k.k && !(err = bkey_err(k))) {
		bch2_bkey_val_to_text(&PBUF(i->buf), i->c, k);
		i->bytes = strlen(i->buf);
		BUG_ON(i->bytes >= sizeof(i->buf));
		i->buf[i->bytes] = '\n';
		i->bytes++;

		k = bch2_btree_iter_next(&iter);
		i->from = iter.pos;

		err = flush_buf(i);
		if (err)
			break;

		if (!i->size)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	return err < 0 ? err : i->ret;
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
	int err;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	err = flush_buf(i);
	if (err)
		return err;

	if (!i->size || !bpos_cmp(SPOS_MAX, i->from))
		return i->ret;

	bch2_trans_init(&trans, i->c, 0, 0);

	for_each_btree_node(&trans, iter, i->id, i->from, 0, b, err) {
		bch2_btree_node_to_text(&PBUF(i->buf), i->c, b);
		i->bytes = strlen(i->buf);
		err = flush_buf(i);
		if (err)
			break;

		/*
		 * can't easily correctly restart a btree node traversal across
		 * all nodes, meh
		 */
		i->from = bpos_cmp(SPOS_MAX, b->key.k.p)
			? bpos_successor(b->key.k.p)
			: b->key.k.p;

		if (!i->size)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	return err < 0 ? err : i->ret;
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
	struct btree *prev_node = NULL;
	int err;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	err = flush_buf(i);
	if (err)
		return err;

	if (!i->size)
		return i->ret;

	bch2_trans_init(&trans, i->c, 0, 0);

	bch2_trans_iter_init(&trans, &iter, i->id, i->from,
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);

	while ((k = bch2_btree_iter_peek(&iter)).k &&
	       !(err = bkey_err(k))) {
		struct btree_path_level *l = &iter.path->l[0];
		struct bkey_packed *_k =
			bch2_btree_node_iter_peek(&l->iter, l->b);

		if (l->b != prev_node) {
			bch2_btree_node_to_text(&PBUF(i->buf), i->c, l->b);
			i->bytes = strlen(i->buf);
			err = flush_buf(i);
			if (err)
				break;
		}
		prev_node = l->b;

		bch2_bfloat_to_text(&PBUF(i->buf), l->b, _k);
		i->bytes = strlen(i->buf);
		err = flush_buf(i);
		if (err)
			break;

		bch2_btree_iter_advance(&iter);
		i->from = iter.pos;

		err = flush_buf(i);
		if (err)
			break;

		if (!i->size)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);

	return err < 0 ? err : i->ret;
}

static const struct file_operations bfloat_failed_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_dump_open,
	.release	= bch2_dump_release,
	.read		= bch2_read_bfloat_failed,
};

void bch2_fs_debug_exit(struct bch_fs *c)
{
	if (!IS_ERR_OR_NULL(c->debug))
		debugfs_remove_recursive(c->debug);
}

void bch2_fs_debug_init(struct bch_fs *c)
{
	struct btree_debug *bd;
	char name[100];

	if (IS_ERR_OR_NULL(bch_debug))
		return;

	snprintf(name, sizeof(name), "%pU", c->sb.user_uuid.b);
	c->debug = debugfs_create_dir(name, bch_debug);
	if (IS_ERR_OR_NULL(c->debug))
		return;

	for (bd = c->btree_debug;
	     bd < c->btree_debug + ARRAY_SIZE(c->btree_debug);
	     bd++) {
		bd->id = bd - c->btree_debug;
		bd->btree = debugfs_create_file(bch2_btree_ids[bd->id],
						0400, c->debug, bd,
						&btree_debug_ops);

		snprintf(name, sizeof(name), "%s-formats",
			 bch2_btree_ids[bd->id]);

		bd->btree_format = debugfs_create_file(name, 0400, c->debug, bd,
						       &btree_format_debug_ops);

		snprintf(name, sizeof(name), "%s-bfloat-failed",
			 bch2_btree_ids[bd->id]);

		bd->failed = debugfs_create_file(name, 0400, c->debug, bd,
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
