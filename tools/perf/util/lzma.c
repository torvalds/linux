#include <lzma.h>
#include <stdio.h>
#include <linux/compiler.h>
#include "util.h"
#include "debug.h"

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
		return -1;
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
				return -1;
			}

			if (feof(infile))
				action = LZMA_FINISH;
		}

		ret = lzma_code(&strm, action);

		if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
			ssize_t write_size = sizeof(buf_out) - strm.avail_out;

			if (writen(output_fd, buf_out, write_size) != write_size) {
				pr_err("lzma: write error: %s\n", strerror(errno));
				return -1;
			}

			strm.next_out  = buf_out;
			strm.avail_out = sizeof(buf_out);
		}

		if (ret != LZMA_OK) {
			if (ret == LZMA_STREAM_END)
				return 0;

			pr_err("lzma: failed %s\n", lzma_strerror(ret));
			return -1;
		}
	}

	fclose(infile);
	return 0;
}
