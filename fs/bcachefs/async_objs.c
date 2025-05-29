// SPDX-License-Identifier: GPL-2.0
/*
 * Async obj debugging: keep asynchronous objects on (very fast) lists, make
 * them visibile in debugfs:
 */

#include "bcachefs.h"
#include "async_objs.h"
#include "btree_io.h"
#include "debug.h"
#include "io_read.h"
#include "io_write.h"

#include <linux/debugfs.h>

static void promote_obj_to_text(struct printbuf *out, void *obj)
{
	bch2_promote_op_to_text(out, obj);
}

static void rbio_obj_to_text(struct printbuf *out, void *obj)
{
	bch2_read_bio_to_text(out, obj);
}

static void write_op_obj_to_text(struct printbuf *out, void *obj)
{
	bch2_write_op_to_text(out, obj);
}

static void btree_read_bio_obj_to_text(struct printbuf *out, void *obj)
{
	struct btree_read_bio *rbio = obj;
	bch2_btree_read_bio_to_text(out, rbio);
}

static void btree_write_bio_obj_to_text(struct printbuf *out, void *obj)
{
	struct btree_write_bio *wbio = obj;
	bch2_bio_to_text(out, &wbio->wbio.bio);
}

static int bch2_async_obj_list_open(struct inode *inode, struct file *file)
{
	struct async_obj_list *list = inode->i_private;
	struct dump_iter *i;

	i = kzalloc(sizeof(struct dump_iter), GFP_KERNEL);
	if (!i)
		return -ENOMEM;

	file->private_data = i;
	i->from = POS_MIN;
	i->iter	= 0;
	i->c	= container_of(list, struct bch_fs, async_objs[list->idx]);
	i->list	= list;
	i->buf	= PRINTBUF;
	return 0;
}

static ssize_t bch2_async_obj_list_read(struct file *file, char __user *buf,
					size_t size, loff_t *ppos)
{
	struct dump_iter *i = file->private_data;
	struct async_obj_list *list = i->list;
	ssize_t ret = 0;

	i->ubuf = buf;
	i->size	= size;
	i->ret	= 0;

	struct genradix_iter iter;
	void *obj;
	fast_list_for_each_from(&list->list, iter, obj, i->iter) {
		ret = bch2_debugfs_flush_buf(i);
		if (ret)
			return ret;

		if (!i->size)
			break;

		list->obj_to_text(&i->buf, obj);
	}

	if (i->buf.allocation_failure)
		ret = -ENOMEM;
	else
		i->iter = iter.pos;

	if (!ret)
		ret = bch2_debugfs_flush_buf(i);

	return ret ?: i->ret;
}

static const struct file_operations async_obj_ops = {
	.owner		= THIS_MODULE,
	.open		= bch2_async_obj_list_open,
	.release	= bch2_dump_release,
	.read		= bch2_async_obj_list_read,
};

void bch2_fs_async_obj_debugfs_init(struct bch_fs *c)
{
	c->async_obj_dir = debugfs_create_dir("async_objs", c->fs_debug_dir);

#define x(n) debugfs_create_file(#n, 0400, c->async_obj_dir,		\
			    &c->async_objs[BCH_ASYNC_OBJ_LIST_##n], &async_obj_ops);
	BCH_ASYNC_OBJ_LISTS()
#undef x
}

void bch2_fs_async_obj_exit(struct bch_fs *c)
{
	for (unsigned i = 0; i < ARRAY_SIZE(c->async_objs); i++)
		fast_list_exit(&c->async_objs[i].list);
}

int bch2_fs_async_obj_init(struct bch_fs *c)
{
	for (unsigned i = 0; i < ARRAY_SIZE(c->async_objs); i++) {
		if (fast_list_init(&c->async_objs[i].list))
			return -BCH_ERR_ENOMEM_async_obj_init;
		c->async_objs[i].idx = i;
	}

#define x(n) c->async_objs[BCH_ASYNC_OBJ_LIST_##n].obj_to_text = n##_obj_to_text;
	BCH_ASYNC_OBJ_LISTS()
#undef x

	return 0;
}
