// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/zlib.h>
#include "compress.h"

struct z_erofs_deflate {
	struct z_erofs_deflate *next;
	struct z_stream_s z;
	u8 bounce[PAGE_SIZE];
};

static DEFINE_SPINLOCK(z_erofs_deflate_lock);
static unsigned int z_erofs_deflate_nstrms, z_erofs_deflate_avail_strms;
static struct z_erofs_deflate *z_erofs_deflate_head;
static DECLARE_WAIT_QUEUE_HEAD(z_erofs_deflate_wq);

module_param_named(deflate_streams, z_erofs_deflate_nstrms, uint, 0444);

void z_erofs_deflate_exit(void)
{
	/* there should be no running fs instance */
	while (z_erofs_deflate_avail_strms) {
		struct z_erofs_deflate *strm;

		spin_lock(&z_erofs_deflate_lock);
		strm = z_erofs_deflate_head;
		if (!strm) {
			spin_unlock(&z_erofs_deflate_lock);
			continue;
		}
		z_erofs_deflate_head = NULL;
		spin_unlock(&z_erofs_deflate_lock);

		while (strm) {
			struct z_erofs_deflate *n = strm->next;

			vfree(strm->z.workspace);
			kfree(strm);
			--z_erofs_deflate_avail_strms;
			strm = n;
		}
	}
}

int __init z_erofs_deflate_init(void)
{
	/* by default, use # of possible CPUs instead */
	if (!z_erofs_deflate_nstrms)
		z_erofs_deflate_nstrms = num_possible_cpus();

	for (; z_erofs_deflate_avail_strms < z_erofs_deflate_nstrms;
	     ++z_erofs_deflate_avail_strms) {
		struct z_erofs_deflate *strm;

		strm = kzalloc(sizeof(*strm), GFP_KERNEL);
		if (!strm)
			goto out_failed;

		/* XXX: in-kernel zlib cannot shrink windowbits currently */
		strm->z.workspace = vmalloc(zlib_inflate_workspacesize());
		if (!strm->z.workspace) {
			kfree(strm);
			goto out_failed;
		}

		spin_lock(&z_erofs_deflate_lock);
		strm->next = z_erofs_deflate_head;
		z_erofs_deflate_head = strm;
		spin_unlock(&z_erofs_deflate_lock);
	}
	return 0;

out_failed:
	erofs_err(NULL, "failed to allocate zlib workspace");
	z_erofs_deflate_exit();
	return -ENOMEM;
}

int z_erofs_load_deflate_config(struct super_block *sb,
			struct erofs_super_block *dsb, void *data, int size)
{
	struct z_erofs_deflate_cfgs *dfl = data;

	if (!dfl || size < sizeof(struct z_erofs_deflate_cfgs)) {
		erofs_err(sb, "invalid deflate cfgs, size=%u", size);
		return -EINVAL;
	}

	if (dfl->windowbits > MAX_WBITS) {
		erofs_err(sb, "unsupported windowbits %u", dfl->windowbits);
		return -EOPNOTSUPP;
	}

	erofs_info(sb, "EXPERIMENTAL DEFLATE feature in use. Use at your own risk!");
	return 0;
}

int z_erofs_deflate_decompress(struct z_erofs_decompress_req *rq,
			       struct page **pgpl)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int nrpages_in =
		PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT;
	struct super_block *sb = rq->sb;
	unsigned int insz, outsz, pofs;
	struct z_erofs_deflate *strm;
	u8 *kin, *kout = NULL;
	bool bounced = false;
	int no = -1, ni = 0, j = 0, zerr, err;

	/* 1. get the exact DEFLATE compressed size */
	kin = kmap_local_page(*rq->in);
	err = z_erofs_fixup_insize(rq, kin + rq->pageofs_in,
			min_t(unsigned int, rq->inputsize,
			      sb->s_blocksize - rq->pageofs_in));
	if (err) {
		kunmap_local(kin);
		return err;
	}

	/* 2. get an available DEFLATE context */
again:
	spin_lock(&z_erofs_deflate_lock);
	strm = z_erofs_deflate_head;
	if (!strm) {
		spin_unlock(&z_erofs_deflate_lock);
		wait_event(z_erofs_deflate_wq, READ_ONCE(z_erofs_deflate_head));
		goto again;
	}
	z_erofs_deflate_head = strm->next;
	spin_unlock(&z_erofs_deflate_lock);

	/* 3. multi-call decompress */
	insz = rq->inputsize;
	outsz = rq->outputsize;
	zerr = zlib_inflateInit2(&strm->z, -MAX_WBITS);
	if (zerr != Z_OK) {
		err = -EIO;
		goto failed_zinit;
	}

	pofs = rq->pageofs_out;
	strm->z.avail_in = min_t(u32, insz, PAGE_SIZE - rq->pageofs_in);
	insz -= strm->z.avail_in;
	strm->z.next_in = kin + rq->pageofs_in;
	strm->z.avail_out = 0;

	while (1) {
		if (!strm->z.avail_out) {
			if (++no >= nrpages_out || !outsz) {
				erofs_err(sb, "insufficient space for decompressed data");
				err = -EFSCORRUPTED;
				break;
			}

			if (kout)
				kunmap_local(kout);
			strm->z.avail_out = min_t(u32, outsz, PAGE_SIZE - pofs);
			outsz -= strm->z.avail_out;
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
			strm->z.next_out = kout + pofs;
			pofs = 0;
		}

		if (!strm->z.avail_in && insz) {
			if (++ni >= nrpages_in) {
				erofs_err(sb, "invalid compressed data");
				err = -EFSCORRUPTED;
				break;
			}

			if (kout) { /* unlike kmap(), take care of the orders */
				j = strm->z.next_out - kout;
				kunmap_local(kout);
			}
			kunmap_local(kin);
			strm->z.avail_in = min_t(u32, insz, PAGE_SIZE);
			insz -= strm->z.avail_in;
			kin = kmap_local_page(rq->in[ni]);
			strm->z.next_in = kin;
			bounced = false;
			if (kout) {
				kout = kmap_local_page(rq->out[no]);
				strm->z.next_out = kout + j;
			}
		}

		/*
		 * Handle overlapping: Use bounced buffer if the compressed
		 * data is under processing; Or use short-lived pages from the
		 * on-stack pagepool where pages share among the same request
		 * and not _all_ inplace I/O pages are needed to be doubled.
		 */
		if (!bounced && rq->out[no] == rq->in[ni]) {
			memcpy(strm->bounce, strm->z.next_in, strm->z.avail_in);
			strm->z.next_in = strm->bounce;
			bounced = true;
		}

		for (j = ni + 1; j < nrpages_in; ++j) {
			struct page *tmppage;

			if (rq->out[no] != rq->in[j])
				continue;

			DBG_BUGON(erofs_page_is_managed(EROFS_SB(sb),
							rq->in[j]));
			tmppage = erofs_allocpage(pgpl, rq->gfp);
			if (!tmppage) {
				err = -ENOMEM;
				goto failed;
			}
			set_page_private(tmppage, Z_EROFS_SHORTLIVED_PAGE);
			copy_highpage(tmppage, rq->in[j]);
			rq->in[j] = tmppage;
		}

		zerr = zlib_inflate(&strm->z, Z_SYNC_FLUSH);
		if (zerr != Z_OK || !(outsz + strm->z.avail_out)) {
			if (zerr == Z_OK && rq->partial_decoding)
				break;
			if (zerr == Z_STREAM_END && !outsz)
				break;
			erofs_err(sb, "failed to decompress %d in[%u] out[%u]",
				  zerr, rq->inputsize, rq->outputsize);
			err = -EFSCORRUPTED;
			break;
		}
	}
failed:
	if (zlib_inflateEnd(&strm->z) != Z_OK && !err)
		err = -EIO;
	if (kout)
		kunmap_local(kout);
failed_zinit:
	kunmap_local(kin);
	/* 4. push back DEFLATE stream context to the global list */
	spin_lock(&z_erofs_deflate_lock);
	strm->next = z_erofs_deflate_head;
	z_erofs_deflate_head = strm;
	spin_unlock(&z_erofs_deflate_lock);
	wake_up(&z_erofs_deflate_wq);
	return err;
}
