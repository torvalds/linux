#include <linux/zutil.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/* Utility function: initialize zlib, unpack binary blob, clean up zlib,
 * return len or negative error code.
 */
int zlib_inflate_blob(void *gunzip_buf, unsigned int sz,
		      const void *buf, unsigned int len)
{
	const u8 *zbuf = buf;
	struct z_stream_s *strm;
	int rc;

	rc = -ENOMEM;
	strm = kmalloc(sizeof(*strm), GFP_KERNEL);
	if (strm == NULL)
		goto gunzip_nomem1;
	strm->workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (strm->workspace == NULL)
		goto gunzip_nomem2;

	/* gzip header (1f,8b,08... 10 bytes total + possible asciz filename)
	 * expected to be stripped from input
	 */
	strm->next_in = zbuf;
	strm->avail_in = len;
	strm->next_out = gunzip_buf;
	strm->avail_out = sz;

	rc = zlib_inflateInit2(strm, -MAX_WBITS);
	if (rc == Z_OK) {
		rc = zlib_inflate(strm, Z_FINISH);
		/* after Z_FINISH, only Z_STREAM_END is "we unpacked it all" */
		if (rc == Z_STREAM_END)
			rc = sz - strm->avail_out;
		else
			rc = -EINVAL;
		zlib_inflateEnd(strm);
	} else
		rc = -EINVAL;

	kfree(strm->workspace);
gunzip_nomem2:
	kfree(strm);
gunzip_nomem1:
	return rc; /* returns Z_OK (0) if successful */
}
