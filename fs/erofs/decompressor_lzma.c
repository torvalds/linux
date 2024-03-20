// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/xz.h>
#include <linux/module.h>
#include "compress.h"

struct z_erofs_lzma {
	struct z_erofs_lzma *next;
	struct xz_dec_microlzma *state;
	struct xz_buf buf;
	u8 bounce[PAGE_SIZE];
};

/* considering the LZMA performance, no need to use a lockless list for now */
static DEFINE_SPINLOCK(z_erofs_lzma_lock);
static unsigned int z_erofs_lzma_max_dictsize;
static unsigned int z_erofs_lzma_nstrms, z_erofs_lzma_avail_strms;
static struct z_erofs_lzma *z_erofs_lzma_head;
static DECLARE_WAIT_QUEUE_HEAD(z_erofs_lzma_wq);

module_param_named(lzma_streams, z_erofs_lzma_nstrms, uint, 0444);

void z_erofs_lzma_exit(void)
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

int __init z_erofs_lzma_init(void)
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

int z_erofs_load_lzma_config(struct super_block *sb,
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

	erofs_info(sb, "EXPERIMENTAL MicroLZMA in use. Use at your own risk!");

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

int z_erofs_lzma_decompress(struct z_erofs_decompress_req *rq,
			    struct page **pagepool)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int nrpages_in =
		PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT;
	unsigned int inlen, outlen, pageofs;
	struct z_erofs_lzma *strm;
	u8 *kin;
	bool bounced = false;
	int no, ni, j, err = 0;

	/* 1. get the exact LZMA compressed size */
	kin = kmap(*rq->in);
	err = z_erofs_fixup_insize(rq, kin + rq->pageofs_in,
			min_t(unsigned int, rq->inputsize,
			      rq->sb->s_blocksize - rq->pageofs_in));
	if (err) {
		kunmap(*rq->in);
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
	inlen = rq->inputsize;
	outlen = rq->outputsize;
	xz_dec_microlzma_reset(strm->state, inlen, outlen,
			       !rq->partial_decoding);
	pageofs = rq->pageofs_out;
	strm->buf.in = kin + rq->pageofs_in;
	strm->buf.in_pos = 0;
	strm->buf.in_size = min_t(u32, inlen, PAGE_SIZE - rq->pageofs_in);
	inlen -= strm->buf.in_size;
	strm->buf.out = NULL;
	strm->buf.out_pos = 0;
	strm->buf.out_size = 0;

	for (ni = 0, no = -1;;) {
		enum xz_ret xz_err;

		if (strm->buf.out_pos == strm->buf.out_size) {
			if (strm->buf.out) {
				kunmap(rq->out[no]);
				strm->buf.out = NULL;
			}

			if (++no >= nrpages_out || !outlen) {
				erofs_err(rq->sb, "decompressed buf out of bound");
				err = -EFSCORRUPTED;
				break;
			}
			strm->buf.out_pos = 0;
			strm->buf.out_size = min_t(u32, outlen,
						   PAGE_SIZE - pageofs);
			outlen -= strm->buf.out_size;
			if (!rq->out[no] && rq->fillgaps) {	/* deduped */
				rq->out[no] = erofs_allocpage(pagepool,
						GFP_KERNEL | __GFP_NOFAIL);
				set_page_private(rq->out[no],
						 Z_EROFS_SHORTLIVED_PAGE);
			}
			if (rq->out[no])
				strm->buf.out = kmap(rq->out[no]) + pageofs;
			pageofs = 0;
		} else if (strm->buf.in_pos == strm->buf.in_size) {
			kunmap(rq->in[ni]);

			if (++ni >= nrpages_in || !inlen) {
				erofs_err(rq->sb, "compressed buf out of bound");
				err = -EFSCORRUPTED;
				break;
			}
			strm->buf.in_pos = 0;
			strm->buf.in_size = min_t(u32, inlen, PAGE_SIZE);
			inlen -= strm->buf.in_size;
			kin = kmap(rq->in[ni]);
			strm->buf.in = kin;
			bounced = false;
		}

		/*
		 * Handle overlapping: Use bounced buffer if the compressed
		 * data is under processing; Otherwise, Use short-lived pages
		 * from the on-stack pagepool where pages share with the same
		 * request.
		 */
		if (!bounced && rq->out[no] == rq->in[ni]) {
			memcpy(strm->bounce, strm->buf.in, strm->buf.in_size);
			strm->buf.in = strm->bounce;
			bounced = true;
		}
		for (j = ni + 1; j < nrpages_in; ++j) {
			struct page *tmppage;

			if (rq->out[no] != rq->in[j])
				continue;

			DBG_BUGON(erofs_page_is_managed(EROFS_SB(rq->sb),
							rq->in[j]));
			tmppage = erofs_allocpage(pagepool,
						  GFP_KERNEL | __GFP_NOFAIL);
			set_page_private(tmppage, Z_EROFS_SHORTLIVED_PAGE);
			copy_highpage(tmppage, rq->in[j]);
			rq->in[j] = tmppage;
		}
		xz_err = xz_dec_microlzma_run(strm->state, &strm->buf);
		DBG_BUGON(strm->buf.out_pos > strm->buf.out_size);
		DBG_BUGON(strm->buf.in_pos > strm->buf.in_size);

		if (xz_err != XZ_OK) {
			if (xz_err == XZ_STREAM_END && !outlen)
				break;
			erofs_err(rq->sb, "failed to decompress %d in[%u] out[%u]",
				  xz_err, rq->inputsize, rq->outputsize);
			err = -EFSCORRUPTED;
			break;
		}
	}
	if (no < nrpages_out && strm->buf.out)
		kunmap(rq->out[no]);
	if (ni < nrpages_in)
		kunmap(rq->in[ni]);
	/* 4. push back LZMA stream context to the global list */
	spin_lock(&z_erofs_lzma_lock);
	strm->next = z_erofs_lzma_head;
	z_erofs_lzma_head = strm;
	spin_unlock(&z_erofs_lzma_lock);
	wake_up(&z_erofs_lzma_wq);
	return err;
}
