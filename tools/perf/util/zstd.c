// SPDX-License-Identifier: GPL-2.0

#include <string.h>

#include "util/compress.h"
#include "util/debug.h"

int zstd_init(struct zstd_data *data, int level)
{
	size_t ret;

	data->dstream = ZSTD_createDStream();
	if (data->dstream == NULL) {
		pr_err("Couldn't create decompression stream.\n");
		return -1;
	}

	ret = ZSTD_initDStream(data->dstream);
	if (ZSTD_isError(ret)) {
		pr_err("Failed to initialize decompression stream: %s\n", ZSTD_getErrorName(ret));
		return -1;
	}

	if (!level)
		return 0;

	data->cstream = ZSTD_createCStream();
	if (data->cstream == NULL) {
		pr_err("Couldn't create compression stream.\n");
		return -1;
	}

	ret = ZSTD_initCStream(data->cstream, level);
	if (ZSTD_isError(ret)) {
		pr_err("Failed to initialize compression stream: %s\n", ZSTD_getErrorName(ret));
		return -1;
	}

	return 0;
}

int zstd_fini(struct zstd_data *data)
{
	if (data->dstream) {
		ZSTD_freeDStream(data->dstream);
		data->dstream = NULL;
	}

	if (data->cstream) {
		ZSTD_freeCStream(data->cstream);
		data->cstream = NULL;
	}

	return 0;
}

size_t zstd_compress_stream_to_records(struct zstd_data *data, void *dst, size_t dst_size,
				       void *src, size_t src_size, size_t max_record_size,
				       size_t process_header(void *record, size_t increment))
{
	size_t ret, size, compressed = 0;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output;
	void *record;

	while (input.pos < input.size) {
		record = dst;
		size = process_header(record, 0);
		compressed += size;
		dst += size;
		dst_size -= size;
		output = (ZSTD_outBuffer){ dst, (dst_size > max_record_size) ?
						max_record_size : dst_size, 0 };
		ret = ZSTD_compressStream(data->cstream, &output, &input);
		ZSTD_flushStream(data->cstream, &output);
		if (ZSTD_isError(ret)) {
			pr_err("failed to compress %ld bytes: %s\n",
				(long)src_size, ZSTD_getErrorName(ret));
			memcpy(dst, src, src_size);
			return src_size;
		}
		size = output.pos;
		size = process_header(record, size);
		compressed += size;
		dst += size;
		dst_size -= size;
	}

	return compressed;
}

size_t zstd_decompress_stream(struct zstd_data *data, void *src, size_t src_size,
			      void *dst, size_t dst_size)
{
	size_t ret;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output = { dst, dst_size, 0 };

	while (input.pos < input.size) {
		ret = ZSTD_decompressStream(data->dstream, &output, &input);
		if (ZSTD_isError(ret)) {
			pr_err("failed to decompress (B): %zd -> %zd, dst_size %zd : %s\n",
			       src_size, output.size, dst_size, ZSTD_getErrorName(ret));
			break;
		}
		output.dst  = dst + output.pos;
		output.size = dst_size - output.pos;
	}

	return output.pos;
}
