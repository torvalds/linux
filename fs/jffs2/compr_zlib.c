/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#if !defined(__KERNEL__) && !defined(__ECOS)
#error "The userspace support got too messy and was removed. Update your mkfs.jffs2"
#endif

#include <linux/kernel.h>
#include <linux/zlib.h>
#include <linux/zutil.h>
#include "nodelist.h"
#include "compr.h"

	/* Plan: call deflate() with avail_in == *sourcelen,
		avail_out = *dstlen - 12 and flush == Z_FINISH.
		If it doesn't manage to finish,	call it again with
		avail_in == 0 and avail_out set to the remaining 12
		bytes for it to clean up.
	   Q: Is 12 bytes sufficient?
	*/
#define STREAM_END_SPACE 12

static DEFINE_MUTEX(deflate_mutex);
static DEFINE_MUTEX(inflate_mutex);
static z_stream inf_strm, def_strm;

#ifdef __KERNEL__ /* Linux-only */
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/mutex.h>

static int __init alloc_workspaces(void)
{
	def_strm.workspace = vmalloc(zlib_deflate_workspacesize());
	if (!def_strm.workspace) {
		printk(KERN_WARNING "Failed to allocate %d bytes for deflate workspace\n", zlib_deflate_workspacesize());
		return -ENOMEM;
	}
	D1(printk(KERN_DEBUG "Allocated %d bytes for deflate workspace\n", zlib_deflate_workspacesize()));
	inf_strm.workspace = vmalloc(zlib_inflate_workspacesize());
	if (!inf_strm.workspace) {
		printk(KERN_WARNING "Failed to allocate %d bytes for inflate workspace\n", zlib_inflate_workspacesize());
		vfree(def_strm.workspace);
		return -ENOMEM;
	}
	D1(printk(KERN_DEBUG "Allocated %d bytes for inflate workspace\n", zlib_inflate_workspacesize()));
	return 0;
}

static void free_workspaces(void)
{
	vfree(def_strm.workspace);
	vfree(inf_strm.workspace);
}
#else
#define alloc_workspaces() (0)
#define free_workspaces() do { } while(0)
#endif /* __KERNEL__ */

static int jffs2_zlib_compress(unsigned char *data_in,
			       unsigned char *cpage_out,
			       uint32_t *sourcelen, uint32_t *dstlen,
			       void *model)
{
	int ret;

	if (*dstlen <= STREAM_END_SPACE)
		return -1;

	mutex_lock(&deflate_mutex);

	if (Z_OK != zlib_deflateInit(&def_strm, 3)) {
		printk(KERN_WARNING "deflateInit failed\n");
		mutex_unlock(&deflate_mutex);
		return -1;
	}

	def_strm.next_in = data_in;
	def_strm.total_in = 0;

	def_strm.next_out = cpage_out;
	def_strm.total_out = 0;

	while (def_strm.total_out < *dstlen - STREAM_END_SPACE && def_strm.total_in < *sourcelen) {
		def_strm.avail_out = *dstlen - (def_strm.total_out + STREAM_END_SPACE);
		def_strm.avail_in = min((unsigned)(*sourcelen-def_strm.total_in), def_strm.avail_out);
		D1(printk(KERN_DEBUG "calling deflate with avail_in %d, avail_out %d\n",
			  def_strm.avail_in, def_strm.avail_out));
		ret = zlib_deflate(&def_strm, Z_PARTIAL_FLUSH);
		D1(printk(KERN_DEBUG "deflate returned with avail_in %d, avail_out %d, total_in %ld, total_out %ld\n",
			  def_strm.avail_in, def_strm.avail_out, def_strm.total_in, def_strm.total_out));
		if (ret != Z_OK) {
			D1(printk(KERN_DEBUG "deflate in loop returned %d\n", ret));
			zlib_deflateEnd(&def_strm);
			mutex_unlock(&deflate_mutex);
			return -1;
		}
	}
	def_strm.avail_out += STREAM_END_SPACE;
	def_strm.avail_in = 0;
	ret = zlib_deflate(&def_strm, Z_FINISH);
	zlib_deflateEnd(&def_strm);

	if (ret != Z_STREAM_END) {
		D1(printk(KERN_DEBUG "final deflate returned %d\n", ret));
		ret = -1;
		goto out;
	}

	if (def_strm.total_out >= def_strm.total_in) {
		D1(printk(KERN_DEBUG "zlib compressed %ld bytes into %ld; failing\n",
			  def_strm.total_in, def_strm.total_out));
		ret = -1;
		goto out;
	}

	D1(printk(KERN_DEBUG "zlib compressed %ld bytes into %ld\n",
		  def_strm.total_in, def_strm.total_out));

	*dstlen = def_strm.total_out;
	*sourcelen = def_strm.total_in;
	ret = 0;
 out:
	mutex_unlock(&deflate_mutex);
	return ret;
}

static int jffs2_zlib_decompress(unsigned char *data_in,
				 unsigned char *cpage_out,
				 uint32_t srclen, uint32_t destlen,
				 void *model)
{
	int ret;
	int wbits = MAX_WBITS;

	mutex_lock(&inflate_mutex);

	inf_strm.next_in = data_in;
	inf_strm.avail_in = srclen;
	inf_strm.total_in = 0;

	inf_strm.next_out = cpage_out;
	inf_strm.avail_out = destlen;
	inf_strm.total_out = 0;

	/* If it's deflate, and it's got no preset dictionary, then
	   we can tell zlib to skip the adler32 check. */
	if (srclen > 2 && !(data_in[1] & PRESET_DICT) &&
	    ((data_in[0] & 0x0f) == Z_DEFLATED) &&
	    !(((data_in[0]<<8) + data_in[1]) % 31)) {

		D2(printk(KERN_DEBUG "inflate skipping adler32\n"));
		wbits = -((data_in[0] >> 4) + 8);
		inf_strm.next_in += 2;
		inf_strm.avail_in -= 2;
	} else {
		/* Let this remain D1 for now -- it should never happen */
		D1(printk(KERN_DEBUG "inflate not skipping adler32\n"));
	}


	if (Z_OK != zlib_inflateInit2(&inf_strm, wbits)) {
		printk(KERN_WARNING "inflateInit failed\n");
		mutex_unlock(&inflate_mutex);
		return 1;
	}

	while((ret = zlib_inflate(&inf_strm, Z_FINISH)) == Z_OK)
		;
	if (ret != Z_STREAM_END) {
		printk(KERN_NOTICE "inflate returned %d\n", ret);
	}
	zlib_inflateEnd(&inf_strm);
	mutex_unlock(&inflate_mutex);
	return 0;
}

static struct jffs2_compressor jffs2_zlib_comp = {
    .priority = JFFS2_ZLIB_PRIORITY,
    .name = "zlib",
    .compr = JFFS2_COMPR_ZLIB,
    .compress = &jffs2_zlib_compress,
    .decompress = &jffs2_zlib_decompress,
#ifdef JFFS2_ZLIB_DISABLED
    .disabled = 1,
#else
    .disabled = 0,
#endif
};

int __init jffs2_zlib_init(void)
{
    int ret;

    ret = alloc_workspaces();
    if (ret)
	    return ret;

    ret = jffs2_register_compressor(&jffs2_zlib_comp);
    if (ret)
	    free_workspaces();

    return ret;
}

void jffs2_zlib_exit(void)
{
    jffs2_unregister_compressor(&jffs2_zlib_comp);
    free_workspaces();
}
