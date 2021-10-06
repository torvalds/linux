// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <lzma.h>
#include <stdio.h>
#include <linux/compiler.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "compress.h"
#include "debug.h"
#include <string.h>
#include <unistd.h>
#include <internal/lib.h>

#define BUFSIZE 8192

static const char *lzma_strerror(lzma_ret ret)
{
	switch ((int) ret) {
	case LZMA_MEM_ERROR:
		return "Memory allocation failed";
	case LZMA_OPTIONS_ERROR:
		return "Unsupported decompressor flags";
	case LZMA_FORMAT_ERROR:
		return "The input is not in the .xz format";
	case LZMA_DATA_ERROR:
		return "Compressed file is corrupt";
	case LZMA_BUF_ERROR:
		return "Compressed file is truncated or otherwise corrupt";
	default:
		return "Unknown error, possibly a bug";
	}
}

int lzma_decompress_to_file(const char *input, int output_fd)
{
	lzma_action action = LZMA_RUN;
	lzma_stream strm   = LZMA_STREAM_INIT;
	lzma_ret ret;
	int err = -1;

	u8 buf_in[BUFSIZE];
	u8 buf_out[BUFSIZE];
	FILE *infile;

	infile = fopen(input, "rb");
	if (!infile) {
		pr_err("lzma: fopen failed on %s: '%s'\n",
		       input, strerror(errno));
		return -1;
	}

	ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		pr_err("lzma: lzma_stream_decoder failed %s (%d)\n",
			lzma_strerror(ret), ret);
		goto err_fclose;
	}

	strm.next_in   = NULL;
	strm.avail_in  = 0;
	strm.next_out  = buf_out;
	strm.avail_out = sizeof(buf_out);

	while (1) {
		if (strm.avail_in == 0 && !feof(infile)) {
			strm.next_in  = buf_in;
			strm.avail_in = fread(buf_in, 1, sizeof(buf_in), infile);

			if (ferror(infile)) {
				pr_err("lzma: read error: %s\n", strerror(errno));
				goto err_lzma_end;
			}

			if (feof(infile))
				action = LZMA_FINISH;
		}

		ret = lzma_code(&strm, action);

		if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
			ssize_t write_size = sizeof(buf_out) - strm.avail_out;

			if (writen(output_fd, buf_out, write_size) != write_size) {
				pr_err("lzma: write error: %s\n", strerror(errno));
				goto err_lzma_end;
			}

			strm.next_out  = buf_out;
			strm.avail_out = sizeof(buf_out);
		}

		if (ret != LZMA_OK) {
			if (ret == LZMA_STREAM_END)
				break;

			pr_err("lzma: failed %s\n", lzma_strerror(ret));
			goto err_lzma_end;
		}
	}

	err = 0;
err_lzma_end:
	lzma_end(&strm);
err_fclose:
	fclose(infile);
	return err;
}

bool lzma_is_compressed(const char *input)
{
	int fd = open(input, O_RDONLY);
	const uint8_t magic[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
	char buf[6] = { 0 };
	ssize_t rc;

	if (fd < 0)
		return -1;

	rc = read(fd, buf, sizeof(buf));
	close(fd);
	return rc == sizeof(buf) ?
	       memcmp(buf, magic, sizeof(buf)) == 0 : false;
}
