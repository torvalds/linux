// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/xz.h>
#include "compress.h"

struct z_erofs_lzma {
	struct z_erofs_lzma *next;
	struct xz_dec_microlzma *state;
	u8 bounce[PAGE_SIZE];
};

/* considering the LZMA performance, no need to use a lockless list for now */
static DEFINE_SPINLOCK(z_erofs_lzma_lock);
static unsigned int z_erofs_lzma_max_dictsize;
static unsigned int z_erofs_lzma_nstrms, z_erofs_lzma_avail_strms;
static struct z_erofs_lzma *z_erofs_lzma_head;
static DECLARE_WAIT_QUEUE_HEAD(z_erofs_lzma_wq);

module_param_named(lzma_streams, z_erofs_lzma_nstrms, uint, 0444);

static void z_erofs_lzma_exit(void)
{
	/* there should be no running fs instance */
	while (z_erofs_lzma_avail_strms) {
		struct z_erofs_lzma *strm;

		spin_lock(&z_erofs_lzma_lock);
		strm = z_erofs_lzma_head;
		if (!strm) {
			spin_unlock(&z_erofs_lzma_lock);
			DBG_BUGON(1);
			return;
		}
		z_erofs_lzma_head = NULL;
		spin_unlock(&z_erofs_lzma_lock);

		while (strm) {
			struct z_erofs_lzma *n = strm->next;

			if (strm->state)
				xz_dec_microlzma_end(strm->state);
			kfree(strm);
			--z_erofs_lzma_avail_strms;
			strm = n;
		}
	}
}

static int __init z_erofs_lzma_init(void)
{
	unsigned int i;

	/* by default, use # of possible CPUs instead */
	if (!z_erofs_lzma_nstrms)
		z_erofs_lzma_nstrms = num_possible_cpus();

	for (i = 0; i < z_erofs_lzma_nstrms; ++i) {
		struct z_erofs_lzma *strm = kzalloc(sizeof(*strm), GFP_KERNEL);

		if (!strm) {
			z_erofs_lzma_exit();
			return -ENOMEM;
		}
		spin_lock(&z_erofs_lzma_lock);
		strm->next = z_erofs_lzma_head;
		z_erofs_lzma_head = strm;
		spin_unlock(&z_erofs_lzma_lock);
		++z_erofs_lzma_avail_strms;
	}
	return 0;
}

static int z_erofs_load_lzma_config(struct super_block *sb,
			struct erofs_super_block *dsb, void *data, int size)
{
	static DEFINE_MUTEX(lzma_resize_mutex);
	struct z_erofs_lzma_cfgs *lzma = data;
	unsigned int dict_size, i;
	struct z_erofs_lzma *strm, *head = NULL;
	int err;

	if (!lzma || size < sizeof(struct z_erofs_lzma_cfgs)) {
		erofs_err(sb, "invalid lzma cfgs, size=%u", size);
		return -EINVAL;
	}
	if (lzma->format) {
		erofs_err(sb, "unidentified lzma format %x, please check kernel version",
			  le16_to_cpu(lzma->format));
		return -EINVAL;
	}
	dict_size = le32_to_cpu(lzma->dict_size);
	if (dict_size > Z_EROFS_LZMA_MAX_DICT_SIZE || dict_size < 4096) {
		erofs_err(sb, "unsupported lzma dictionary size %u",
			  dict_size);
		return -EINVAL;
	}

	/* in case 2 z_erofs_load_lzma_config() race to avoid deadlock */
	mutex_lock(&lzma_resize_mutex);

	if (z_erofs_lzma_max_dictsize >= dict_size) {
		mutex_unlock(&lzma_resize_mutex);
		return 0;
	}

	/* 1. collect/isolate all streams for the following check */
	for (i = 0; i < z_erofs_lzma_avail_strms; ++i) {
		struct z_erofs_lzma *last;

again:
		spin_lock(&z_erofs_lzma_lock);
		strm = z_erofs_lzma_head;
		if (!strm) {
			spin_unlock(&z_erofs_lzma_lock);
			wait_event(z_erofs_lzma_wq,
				   READ_ONCE(z_erofs_lzma_head));
			goto again;
		}
		z_erofs_lzma_head = NULL;
		spin_unlock(&z_erofs_lzma_lock);

		for (last = strm; last->next; last = last->next)
			++i;
		last->next = head;
		head = strm;
	}

	err = 0;
	/* 2. walk each isolated stream and grow max dict_size if needed */
	for (strm = head; strm; strm = strm->next) {
		if (strm->state)
			xz_dec_microlzma_end(strm->state);
		strm->state = xz_dec_microlzma_alloc(XZ_PREALLOC, dict_size);
		if (!strm->state)
			err = -ENOMEM;
	}

	/* 3. push back all to the global list and update max dict_size */
	spin_lock(&z_erofs_lzma_lock);
	DBG_BUGON(z_erofs_lzma_head);
	z_erofs_lzma_head = head;
	spin_unlock(&z_erofs_lzma_lock);
	wake_up_all(&z_erofs_lzma_wq);

	z_erofs_lzma_max_dictsize = dict_size;
	mutex_unlock(&lzma_resize_mutex);
	return err;
}

static int z_erofs_lzma_decompress(struct z_erofs_decompress_req *rq,
				   struct page **pgpl)
{
	struct super_block *sb = rq->sb;
	struct z_erofs_stream_dctx dctx = {
		.rq = rq,
		.inpages = PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT,
		.outpages = PAGE_ALIGN(rq->pageofs_out + rq->outputsize)
				>> PAGE_SHIFT,
		.no = -1, .ni = 0,
	};
	struct xz_buf buf = {};
	struct z_erofs_lzma *strm;
	enum xz_ret xz_err;
	int err;

	/* 1. get the exact LZMA compressed size */
	dctx.kin = kmap_local_page(*rq->in);
	err = z_erofs_fixup_insize(rq, dctx.kin + rq->pageofs_in,
			min(rq->inputsize, sb->s_blocksize - rq->pageofs_in));
	if (err) {
		kunmap_local(dctx.kin);
		return err;
	}

	/* 2. get an available lzma context */
again:
	spin_lock(&z_erofs_lzma_lock);
	strm = z_erofs_lzma_head;
	if (!strm) {
		spin_unlock(&z_erofs_lzma_lock);
		wait_event(z_erofs_lzma_wq, READ_ONCE(z_erofs_lzma_head));
		goto again;
	}
	z_erofs_lzma_head = strm->next;
	spin_unlock(&z_erofs_lzma_lock);

	/* 3. multi-call decompress */
	xz_dec_microlzma_reset(strm->state, rq->inputsize, rq->outputsize,
			       !rq->partial_decoding);
	buf.in_size = min(rq->inputsize, PAGE_SIZE - rq->pageofs_in);
	rq->inputsize -= buf.in_size;
	buf.in = dctx.kin + rq->pageofs_in;
	dctx.bounce = strm->bounce;
	do {
		dctx.avail_out = buf.out_size - buf.out_pos;
		dctx.inbuf_sz = buf.in_size;
		dctx.inbuf_pos = buf.in_pos;
		err = z_erofs_stream_switch_bufs(&dctx, (void **)&buf.out,
						 (void **)&buf.in, pgpl);
		if (err)
			break;

		if (buf.out_size == buf.out_pos) {
			buf.out_size = dctx.avail_out;
			buf.out_pos = 0;
		}
		buf.in_size = dctx.inbuf_sz;
		buf.in_pos = dctx.inbuf_pos;

		xz_err = xz_dec_microlzma_run(strm->state, &buf);
		DBG_BUGON(buf.out_pos > buf.out_size);
		DBG_BUGON(buf.in_pos > buf.in_size);

		if (xz_err != XZ_OK) {
			if (xz_err == XZ_STREAM_END && !rq->outputsize)
				break;
			erofs_err(sb, "failed to decompress %d in[%u] out[%u]",
				  xz_err, rq->inputsize, rq->outputsize);
			err = -EFSCORRUPTED;
			break;
		}
	} while (1);

	if (dctx.kout)
		kunmap_local(dctx.kout);
	kunmap_local(dctx.kin);
	/* 4. push back LZMA stream context to the global list */
	spin_lock(&z_erofs_lzma_lock);
	strm->next = z_erofs_lzma_head;
	z_erofs_lzma_head = strm;
	spin_unlock(&z_erofs_lzma_lock);
	wake_up(&z_erofs_lzma_wq);
	return err;
}

const struct z_erofs_decompressor z_erofs_lzma_decomp = {
	.config = z_erofs_load_lzma_config,
	.decompress = z_erofs_lzma_decompress,
	.init = z_erofs_lzma_init,
	.exit = z_erofs_lzma_exit,
	.name = "lzma"
};
