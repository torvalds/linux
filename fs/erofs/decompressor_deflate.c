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

static void z_erofs_deflate_exit(void)
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

static int __init z_erofs_deflate_init(void)
{
	/* by default, use # of possible CPUs instead */
	if (!z_erofs_deflate_nstrms)
		z_erofs_deflate_nstrms = num_possible_cpus();
	return 0;
}

static int z_erofs_load_deflate_config(struct super_block *sb,
			struct erofs_super_block *dsb, void *data, int size)
{
	struct z_erofs_deflate_cfgs *dfl = data;
	static DEFINE_MUTEX(deflate_resize_mutex);
	static bool inited;

	if (!dfl || size < sizeof(struct z_erofs_deflate_cfgs)) {
		erofs_err(sb, "invalid deflate cfgs, size=%u", size);
		return -EINVAL;
	}

	if (dfl->windowbits > MAX_WBITS) {
		erofs_err(sb, "unsupported windowbits %u", dfl->windowbits);
		return -EOPNOTSUPP;
	}
	mutex_lock(&deflate_resize_mutex);
	if (!inited) {
		for (; z_erofs_deflate_avail_strms < z_erofs_deflate_nstrms;
		     ++z_erofs_deflate_avail_strms) {
			struct z_erofs_deflate *strm;

			strm = kzalloc(sizeof(*strm), GFP_KERNEL);
			if (!strm)
				goto failed;
			/* XXX: in-kernel zlib cannot customize windowbits */
			strm->z.workspace = vmalloc(zlib_inflate_workspacesize());
			if (!strm->z.workspace) {
				kfree(strm);
				goto failed;
			}

			spin_lock(&z_erofs_deflate_lock);
			strm->next = z_erofs_deflate_head;
			z_erofs_deflate_head = strm;
			spin_unlock(&z_erofs_deflate_lock);
		}
		inited = true;
	}
	mutex_unlock(&deflate_resize_mutex);
	erofs_info(sb, "EXPERIMENTAL DEFLATE feature in use. Use at your own risk!");
	return 0;
failed:
	mutex_unlock(&deflate_resize_mutex);
	z_erofs_deflate_exit();
	return -ENOMEM;
}

static int __z_erofs_deflate_decompress(struct z_erofs_decompress_req *rq,
					struct page **pgpl)
{
	struct super_block *sb = rq->sb;
	struct z_erofs_stream_dctx dctx = { .rq = rq, .no = -1, .ni = 0 };
	struct z_erofs_deflate *strm;
	int zerr, err;

	/* 1. get the exact DEFLATE compressed size */
	dctx.kin = kmap_local_page(*rq->in);
	err = z_erofs_fixup_insize(rq, dctx.kin + rq->pageofs_in,
			min(rq->inputsize, sb->s_blocksize - rq->pageofs_in));
	if (err) {
		kunmap_local(dctx.kin);
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
	zerr = zlib_inflateInit2(&strm->z, -MAX_WBITS);
	if (zerr != Z_OK) {
		err = -EIO;
		goto failed_zinit;
	}

	rq->fillgaps = true;	/* DEFLATE doesn't support NULL output buffer */
	strm->z.avail_in = min(rq->inputsize, PAGE_SIZE - rq->pageofs_in);
	rq->inputsize -= strm->z.avail_in;
	strm->z.next_in = dctx.kin + rq->pageofs_in;
	strm->z.avail_out = 0;
	dctx.bounce = strm->bounce;

	while (1) {
		dctx.avail_out = strm->z.avail_out;
		dctx.inbuf_sz = strm->z.avail_in;
		err = z_erofs_stream_switch_bufs(&dctx,
					(void **)&strm->z.next_out,
					(void **)&strm->z.next_in, pgpl);
		if (err)
			break;
		strm->z.avail_out = dctx.avail_out;
		strm->z.avail_in = dctx.inbuf_sz;

		zerr = zlib_inflate(&strm->z, Z_SYNC_FLUSH);
		if (zerr != Z_OK || !(rq->outputsize + strm->z.avail_out)) {
			if (zerr == Z_OK && rq->partial_decoding)
				break;
			if (zerr == Z_STREAM_END && !rq->outputsize)
				break;
			erofs_err(sb, "failed to decompress %d in[%u] out[%u]",
				  zerr, rq->inputsize, rq->outputsize);
			err = -EFSCORRUPTED;
			break;
		}
	}
	if (zlib_inflateEnd(&strm->z) != Z_OK && !err)
		err = -EIO;
	if (dctx.kout)
		kunmap_local(dctx.kout);
failed_zinit:
	kunmap_local(dctx.kin);
	/* 4. push back DEFLATE stream context to the global list */
	spin_lock(&z_erofs_deflate_lock);
	strm->next = z_erofs_deflate_head;
	z_erofs_deflate_head = strm;
	spin_unlock(&z_erofs_deflate_lock);
	wake_up(&z_erofs_deflate_wq);
	return err;
}

static int z_erofs_deflate_decompress(struct z_erofs_decompress_req *rq,
				      struct page **pgpl)
{
#ifdef CONFIG_EROFS_FS_ZIP_ACCEL
	int err;

	if (!rq->partial_decoding) {
		err = z_erofs_crypto_decompress(rq, pgpl);
		if (err != -EOPNOTSUPP)
			return err;

	}
#endif
	return __z_erofs_deflate_decompress(rq, pgpl);
}

const struct z_erofs_decompressor z_erofs_deflate_decomp = {
	.config = z_erofs_load_deflate_config,
	.decompress = z_erofs_deflate_decompress,
	.init = z_erofs_deflate_init,
	.exit = z_erofs_deflate_exit,
	.name = "deflate",
};
