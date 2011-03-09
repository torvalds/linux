/*
 * fs/logfs/compr.c	- compression routines
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/vmalloc.h>
#include <linux/zlib.h>

#define COMPR_LEVEL 3

static DEFINE_MUTEX(compr_mutex);
static struct z_stream_s stream;

int logfs_compress(void *in, void *out, size_t inlen, size_t outlen)
{
	int err, ret;

	ret = -EIO;
	mutex_lock(&compr_mutex);
	err = zlib_deflateInit(&stream, COMPR_LEVEL);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_deflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	if (stream.total_out >= stream.total_in)
		goto error;

	ret = stream.total_out;
error:
	mutex_unlock(&compr_mutex);
	return ret;
}

int logfs_uncompress(void *in, void *out, size_t inlen, size_t outlen)
{
	int err, ret;

	ret = -EIO;
	mutex_lock(&compr_mutex);
	err = zlib_inflateInit(&stream);
	if (err != Z_OK)
		goto error;

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	err = zlib_inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END)
		goto error;

	err = zlib_inflateEnd(&stream);
	if (err != Z_OK)
		goto error;

	ret = 0;
error:
	mutex_unlock(&compr_mutex);
	return ret;
}

int __init logfs_compr_init(void)
{
	size_t size = max(zlib_deflate_workspacesize(),
			zlib_inflate_workspacesize());
	stream.workspace = vmalloc(size);
	if (!stream.workspace)
		return -ENOMEM;
	return 0;
}

void logfs_compr_exit(void)
{
	vfree(stream.workspace);
}
