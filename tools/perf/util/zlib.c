// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <zlib.h>

#include "util/compress.h"
#include "util/util.h"
#include "util/debug.h"


#define CHUNK_SIZE  16384

int gzip_decompress_to_file(const char *input, int output_fd)
{
	int ret = Z_STREAM_ERROR;
	int input_fd;
	void *ptr;
	int len;
	struct stat stbuf;
	unsigned char buf[CHUNK_SIZE];
	z_stream zs = {
		.zalloc		= Z_NULL,
		.zfree		= Z_NULL,
		.opaque		= Z_NULL,
		.avail_in	= 0,
		.next_in	= Z_NULL,
	};

	input_fd = open(input, O_RDONLY);
	if (input_fd < 0)
		return -1;

	if (fstat(input_fd, &stbuf) < 0)
		goto out_close;

	ptr = mmap(NULL, stbuf.st_size, PROT_READ, MAP_PRIVATE, input_fd, 0);
	if (ptr == MAP_FAILED)
		goto out_close;

	if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
		goto out_unmap;

	zs.next_in = ptr;
	zs.avail_in = stbuf.st_size;

	do {
		zs.next_out = buf;
		zs.avail_out = CHUNK_SIZE;

		ret = inflate(&zs, Z_NO_FLUSH);
		switch (ret) {
		case Z_NEED_DICT:
			ret = Z_DATA_ERROR;
			/* fall through */
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			goto out;
		default:
			break;
		}

		len = CHUNK_SIZE - zs.avail_out;
		if (writen(output_fd, buf, len) != len) {
			ret = Z_DATA_ERROR;
			goto out;
		}

	} while (ret != Z_STREAM_END);

out:
	inflateEnd(&zs);
out_unmap:
	munmap(ptr, stbuf.st_size);
out_close:
	close(input_fd);

	return ret == Z_STREAM_END ? 0 : -1;
}
