#ifdef STATIC
/* Pre-boot environment: included */

/* prevent inclusion of _LINUX_KERNEL_H in pre-boot environment: lots
 * errors about console_printk etc... on ARM */
#define _LINUX_KERNEL_H

#include "zlib_inflate/inftrees.c"
#include "zlib_inflate/inffast.c"
#include "zlib_inflate/inflate.c"

#else /* STATIC */
/* initramfs et al: linked */

#include <linux/zutil.h>

#include "zlib_inflate/inftrees.h"
#include "zlib_inflate/inffast.h"
#include "zlib_inflate/inflate.h"

#include "zlib_inflate/infutil.h"
#include <linux/decompress/inflate.h>

#endif /* STATIC */

#include <linux/decompress/mm.h>

#define GZIP_IOBUF_SIZE (16*1024)

static long INIT nofill(void *buffer, unsigned long len)
{
	return -1;
}

/* Included from initramfs et al code */
STATIC int INIT gunzip(unsigned char *buf, long len,
		       long (*fill)(void*, unsigned long),
		       long (*flush)(void*, unsigned long),
		       unsigned char *out_buf,
		       long *pos,
		       void(*error)(char *x)) {
	u8 *zbuf;
	struct z_stream_s *strm;
	int rc;
	size_t out_len;

	rc = -1;
	if (flush) {
		out_len = 0x8000; /* 32 K */
		out_buf = malloc(out_len);
	} else {
		out_len = ((size_t)~0) - (size_t)out_buf; /* no limit */
	}
	if (!out_buf) {
		error("Out of memory while allocating output buffer");
		goto gunzip_nomem1;
	}

	if (buf)
		zbuf = buf;
	else {
		zbuf = malloc(GZIP_IOBUF_SIZE);
		len = 0;
	}
	if (!zbuf) {
		error("Out of memory while allocating input buffer");
		goto gunzip_nomem2;
	}

	strm = malloc(sizeof(*strm));
	if (strm == NULL) {
		error("Out of memory while allocating z_stream");
		goto gunzip_nomem3;
	}

	strm->workspace = malloc(flush ? zlib_inflate_workspacesize() :
				 sizeof(struct inflate_state));
	if (strm->workspace == NULL) {
		error("Out of memory while allocating workspace");
		goto gunzip_nomem4;
	}

	if (!fill)
		fill = nofill;

	if (len == 0)
		len = fill(zbuf, GZIP_IOBUF_SIZE);

	/* verify the gzip header */
	if (len < 10 ||
	   zbuf[0] != 0x1f || zbuf[1] != 0x8b || zbuf[2] != 0x08) {
		if (pos)
			*pos = 0;
		error("Not a gzip file");
		goto gunzip_5;
	}

	/* skip over gzip header (1f,8b,08... 10 bytes total +
	 * possible asciz filename)
	 */
	strm->next_in = zbuf + 10;
	strm->avail_in = len - 10;
	/* skip over asciz filename */
	if (zbuf[3] & 0x8) {
		do {
			/*
			 * If the filename doesn't fit into the buffer,
			 * the file is very probably corrupt. Don't try
			 * to read more data.
			 */
			if (strm->avail_in == 0) {
				error("header error");
				goto gunzip_5;
			}
			--strm->avail_in;
		} while (*strm->next_in++);
	}

	strm->next_out = out_buf;
	strm->avail_out = out_len;

	rc = zlib_inflateInit2(strm, -MAX_WBITS);

	if (!flush) {
		WS(strm)->inflate_state.wsize = 0;
		WS(strm)->inflate_state.window = NULL;
	}

	while (rc == Z_OK) {
		if (strm->avail_in == 0) {
			/* TODO: handle case where both pos and fill are set */
			len = fill(zbuf, GZIP_IOBUF_SIZE);
			if (len < 0) {
				rc = -1;
				error("read error");
				break;
			}
			strm->next_in = zbuf;
			strm->avail_in = len;
		}
		rc = zlib_inflate(strm, 0);

		/* Write any data generated */
		if (flush && strm->next_out > out_buf) {
			long l = strm->next_out - out_buf;
			if (l != flush(out_buf, l)) {
				rc = -1;
				error("write error");
				break;
			}
			strm->next_out = out_buf;
			strm->avail_out = out_len;
		}

		/* after Z_FINISH, only Z_STREAM_END is "we unpacked it all" */
		if (rc == Z_STREAM_END) {
			rc = 0;
			break;
		} else if (rc != Z_OK) {
			error("uncompression error");
			rc = -1;
		}
	}

	zlib_inflateEnd(strm);
	if (pos)
		/* add + 8 to skip over trailer */
		*pos = strm->next_in - zbuf+8;

gunzip_5:
	free(strm->workspace);
gunzip_nomem4:
	free(strm);
gunzip_nomem3:
	if (!buf)
		free(zbuf);
gunzip_nomem2:
	if (flush)
		free(out_buf);
gunzip_nomem1:
	return rc; /* returns Z_OK (0) if successful */
}

#define decompress gunzip
