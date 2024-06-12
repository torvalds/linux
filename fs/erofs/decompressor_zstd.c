// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/zstd.h>
#include "compress.h"

struct z_erofs_zstd {
	struct z_erofs_zstd *next;
	u8 bounce[PAGE_SIZE];
	void *wksp;
	unsigned int wkspsz;
};

static DEFINE_SPINLOCK(z_erofs_zstd_lock);
static unsigned int z_erofs_zstd_max_dictsize;
static unsigned int z_erofs_zstd_nstrms, z_erofs_zstd_avail_strms;
static struct z_erofs_zstd *z_erofs_zstd_head;
static DECLARE_WAIT_QUEUE_HEAD(z_erofs_zstd_wq);

module_param_named(zstd_streams, z_erofs_zstd_nstrms, uint, 0444);

static struct z_erofs_zstd *z_erofs_isolate_strms(bool all)
{
	struct z_erofs_zstd *strm;

again:
	spin_lock(&z_erofs_zstd_lock);
	strm = z_erofs_zstd_head;
	if (!strm) {
		spin_unlock(&z_erofs_zstd_lock);
		wait_event(z_erofs_zstd_wq, READ_ONCE(z_erofs_zstd_head));
		goto again;
	}
	z_erofs_zstd_head = all ? NULL : strm->next;
	spin_unlock(&z_erofs_zstd_lock);
	return strm;
}

void z_erofs_zstd_exit(void)
{
	while (z_erofs_zstd_avail_strms) {
		struct z_erofs_zstd *strm, *n;

		for (strm = z_erofs_isolate_strms(true); strm; strm = n) {
			n = strm->next;

			kvfree(strm->wksp);
			kfree(strm);
			--z_erofs_zstd_avail_strms;
		}
	}
}

int __init z_erofs_zstd_init(void)
{
	/* by default, use # of possible CPUs instead */
	if (!z_erofs_zstd_nstrms)
		z_erofs_zstd_nstrms = num_possible_cpus();

	for (; z_erofs_zstd_avail_strms < z_erofs_zstd_nstrms;
	     ++z_erofs_zstd_avail_strms) {
		struct z_erofs_zstd *strm;

		strm = kzalloc(sizeof(*strm), GFP_KERNEL);
		if (!strm) {
			z_erofs_zstd_exit();
			return -ENOMEM;
		}
		spin_lock(&z_erofs_zstd_lock);
		strm->next = z_erofs_zstd_head;
		z_erofs_zstd_head = strm;
		spin_unlock(&z_erofs_zstd_lock);
	}
	return 0;
}

int z_erofs_load_zstd_config(struct super_block *sb,
			struct erofs_super_block *dsb, void *data, int size)
{
	static DEFINE_MUTEX(zstd_resize_mutex);
	struct z_erofs_zstd_cfgs *zstd = data;
	unsigned int dict_size, wkspsz;
	struct z_erofs_zstd *strm, *head = NULL;
	void *wksp;

	if (!zstd || size < sizeof(struct z_erofs_zstd_cfgs) || zstd->format) {
		erofs_err(sb, "unsupported zstd format, size=%u", size);
		return -EINVAL;
	}

	if (zstd->windowlog > ilog2(Z_EROFS_ZSTD_MAX_DICT_SIZE) - 10) {
		erofs_err(sb, "unsupported zstd window log %u", zstd->windowlog);
		return -EINVAL;
	}
	dict_size = 1U << (zstd->windowlog + 10);

	/* in case 2 z_erofs_load_zstd_config() race to avoid deadlock */
	mutex_lock(&zstd_resize_mutex);
	if (z_erofs_zstd_max_dictsize >= dict_size) {
		mutex_unlock(&zstd_resize_mutex);
		return 0;
	}

	/* 1. collect/isolate all streams for the following check */
	while (z_erofs_zstd_avail_strms) {
		struct z_erofs_zstd *n;

		for (strm = z_erofs_isolate_strms(true); strm; strm = n) {
			n = strm->next;
			strm->next = head;
			head = strm;
			--z_erofs_zstd_avail_strms;
		}
	}

	/* 2. walk each isolated stream and grow max dict_size if needed */
	wkspsz = zstd_dstream_workspace_bound(dict_size);
	for (strm = head; strm; strm = strm->next) {
		wksp = kvmalloc(wkspsz, GFP_KERNEL);
		if (!wksp)
			break;
		kvfree(strm->wksp);
		strm->wksp = wksp;
		strm->wkspsz = wkspsz;
	}

	/* 3. push back all to the global list and update max dict_size */
	spin_lock(&z_erofs_zstd_lock);
	DBG_BUGON(z_erofs_zstd_head);
	z_erofs_zstd_head = head;
	spin_unlock(&z_erofs_zstd_lock);
	z_erofs_zstd_avail_strms = z_erofs_zstd_nstrms;
	wake_up_all(&z_erofs_zstd_wq);
	if (!strm)
		z_erofs_zstd_max_dictsize = dict_size;
	mutex_unlock(&zstd_resize_mutex);
	return strm ? -ENOMEM : 0;
}

int z_erofs_zstd_decompress(struct z_erofs_decompress_req *rq,
			    struct page **pgpl)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int nrpages_in =
		PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT;
	zstd_dstream *stream;
	struct super_block *sb = rq->sb;
	unsigned int insz, outsz, pofs;
	struct z_erofs_zstd *strm;
	zstd_in_buffer in_buf = { NULL, 0, 0 };
	zstd_out_buffer out_buf = { NULL, 0, 0 };
	u8 *kin, *kout = NULL;
	bool bounced = false;
	int no = -1, ni = 0, j = 0, zerr, err;

	/* 1. get the exact compressed size */
	kin = kmap_local_page(*rq->in);
	err = z_erofs_fixup_insize(rq, kin + rq->pageofs_in,
			min_t(unsigned int, rq->inputsize,
			      sb->s_blocksize - rq->pageofs_in));
	if (err) {
		kunmap_local(kin);
		return err;
	}

	/* 2. get an available ZSTD context */
	strm = z_erofs_isolate_strms(false);

	/* 3. multi-call decompress */
	insz = rq->inputsize;
	outsz = rq->outputsize;
	stream = zstd_init_dstream(z_erofs_zstd_max_dictsize, strm->wksp, strm->wkspsz);
	if (!stream) {
		err = -EIO;
		goto failed_zinit;
	}

	pofs = rq->pageofs_out;
	in_buf.size = min_t(u32, insz, PAGE_SIZE - rq->pageofs_in);
	insz -= in_buf.size;
	in_buf.src = kin + rq->pageofs_in;
	do {
		if (out_buf.size == out_buf.pos) {
			if (++no >= nrpages_out || !outsz) {
				erofs_err(sb, "insufficient space for decompressed data");
				err = -EFSCORRUPTED;
				break;
			}

			if (kout)
				kunmap_local(kout);
			out_buf.size = min_t(u32, outsz, PAGE_SIZE - pofs);
			outsz -= out_buf.size;
			if (!rq->out[no]) {
				rq->out[no] = erofs_allocpage(pgpl, rq->gfp);
				if (!rq->out[no]) {
					kout = NULL;
					err = -ENOMEM;
					break;
				}
				set_page_private(rq->out[no],
						 Z_EROFS_SHORTLIVED_PAGE);
			}
			kout = kmap_local_page(rq->out[no]);
			out_buf.dst = kout + pofs;
			out_buf.pos = 0;
			pofs = 0;
		}

		if (in_buf.size == in_buf.pos && insz) {
			if (++ni >= nrpages_in) {
				erofs_err(sb, "invalid compressed data");
				err = -EFSCORRUPTED;
				break;
			}

			if (kout) /* unlike kmap(), take care of the orders */
				kunmap_local(kout);
			kunmap_local(kin);
			in_buf.size = min_t(u32, insz, PAGE_SIZE);
			insz -= in_buf.size;
			kin = kmap_local_page(rq->in[ni]);
			in_buf.src = kin;
			in_buf.pos = 0;
			bounced = false;
			if (kout) {
				j = (u8 *)out_buf.dst - kout;
				kout = kmap_local_page(rq->out[no]);
				out_buf.dst = kout + j;
			}
		}

		/*
		 * Handle overlapping: Use bounced buffer if the compressed
		 * data is under processing; Or use short-lived pages from the
		 * on-stack pagepool where pages share among the same request
		 * and not _all_ inplace I/O pages are needed to be doubled.
		 */
		if (!bounced && rq->out[no] == rq->in[ni]) {
			memcpy(strm->bounce, in_buf.src, in_buf.size);
			in_buf.src = strm->bounce;
			bounced = true;
		}

		for (j = ni + 1; j < nrpages_in; ++j) {
			struct page *tmppage;

			if (rq->out[no] != rq->in[j])
				continue;
			tmppage = erofs_allocpage(pgpl, rq->gfp);
			if (!tmppage) {
				err = -ENOMEM;
				goto failed;
			}
			set_page_private(tmppage, Z_EROFS_SHORTLIVED_PAGE);
			copy_highpage(tmppage, rq->in[j]);
			rq->in[j] = tmppage;
		}
		zerr = zstd_decompress_stream(stream, &out_buf, &in_buf);
		if (zstd_is_error(zerr) || (!zerr && outsz)) {
			erofs_err(sb, "failed to decompress in[%u] out[%u]: %s",
				  rq->inputsize, rq->outputsize,
				  zerr ? zstd_get_error_name(zerr) : "unexpected end of stream");
			err = -EFSCORRUPTED;
			break;
		}
	} while (outsz || out_buf.pos < out_buf.size);
failed:
	if (kout)
		kunmap_local(kout);
failed_zinit:
	kunmap_local(kin);
	/* 4. push back ZSTD stream context to the global list */
	spin_lock(&z_erofs_zstd_lock);
	strm->next = z_erofs_zstd_head;
	z_erofs_zstd_head = strm;
	spin_unlock(&z_erofs_zstd_lock);
	wake_up(&z_erofs_zstd_wq);
	return err;
}
