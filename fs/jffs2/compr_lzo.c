/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2007 Nokia Corporation. All rights reserved.
 *
 * Created by Richard Purdie <rpurdie@openedhand.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/lzo.h>
#include "compr.h"

static void *lzo_mem;
static void *lzo_compress_buf;
static DEFINE_MUTEX(deflate_mutex);	/* for lzo_mem and lzo_compress_buf */

static void free_workspace(void)
{
	vfree(lzo_mem);
	vfree(lzo_compress_buf);
}

static int __init alloc_workspace(void)
{
	lzo_mem = vmalloc(LZO1X_MEM_COMPRESS);
	lzo_compress_buf = vmalloc(lzo1x_worst_compress(PAGE_SIZE));

	if (!lzo_mem || !lzo_compress_buf) {
		printk(KERN_WARNING "Failed to allocate lzo deflate workspace\n");
		free_workspace();
		return -ENOMEM;
	}

	return 0;
}

static int jffs2_lzo_compress(unsigned char *data_in, unsigned char *cpage_out,
			      uint32_t *sourcelen, uint32_t *dstlen, void *model)
{
	size_t compress_size;
	int ret;

	mutex_lock(&deflate_mutex);
	ret = lzo1x_1_compress(data_in, *sourcelen, lzo_compress_buf, &compress_size, lzo_mem);
	if (ret != LZO_E_OK)
		goto fail;

	if (compress_size > *dstlen)
		goto fail;

	memcpy(cpage_out, lzo_compress_buf, compress_size);
	mutex_unlock(&deflate_mutex);

	*dstlen = compress_size;
	return 0;

 fail:
	mutex_unlock(&deflate_mutex);
	return -1;
}

static int jffs2_lzo_decompress(unsigned char *data_in, unsigned char *cpage_out,
				 uint32_t srclen, uint32_t destlen, void *model)
{
	size_t dl = destlen;
	int ret;

	ret = lzo1x_decompress_safe(data_in, srclen, cpage_out, &dl);

	if (ret != LZO_E_OK || dl != destlen)
		return -1;

	return 0;
}

static struct jffs2_compressor jffs2_lzo_comp = {
	.priority = JFFS2_LZO_PRIORITY,
	.name = "lzo",
	.compr = JFFS2_COMPR_LZO,
	.compress = &jffs2_lzo_compress,
	.decompress = &jffs2_lzo_decompress,
	.disabled = 0,
};

int __init jffs2_lzo_init(void)
{
	int ret;

	ret = alloc_workspace();
	if (ret < 0)
		return ret;

	ret = jffs2_register_compressor(&jffs2_lzo_comp);
	if (ret)
		free_workspace();

	return ret;
}

void jffs2_lzo_exit(void)
{
	jffs2_unregister_compressor(&jffs2_lzo_comp);
	free_workspace();
}
