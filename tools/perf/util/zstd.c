// SPDX-License-Identifier: GPL-2.0

#include <string.h>
#include <linux/perf_event.h>

#include "util/compress.h"
#include "util/debug.h"

int zstd_init(struct zstd_data *data, int level)
{
	data->comp_level = level;
	data->dstream = NULL;
	data->cstream = NULL;
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

ssize_t zstd_compress_stream_to_records(struct zstd_data *data, void *dst, size_t dst_size,
				       void *src, size_t src_size, size_t max_record_size,
				       size_t process_header(void *record, size_t increment))
{
	size_t ret, size, compressed = 0;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output;
	void *record;

	if (!data->cstream) {
		data->cstream = ZSTD_createCStream();
		if (data->cstream == NULL) {
			pr_err("Couldn't create compression stream.\n");
			return -1;
		}

		ret = ZSTD_initCStream(data->cstream, data->comp_level);
		if (ZSTD_isError(ret)) {
			pr_err("Failed to initialize compression stream: %s\n",
				ZSTD_getErrorName(ret));
			return -1;
		}
	}

	while (input.pos < input.size) {
		record = dst;
		/* process_header writes the event header into record */
		if (dst_size < sizeof(struct perf_event_header))
			goto reset;
		size = process_header(record, 0);
		/* Output buffer full — cannot fit even the record header */
		if (size > dst_size)
			goto reset;
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
			goto reset;
		}
		size = output.pos;
		/*
		 * No progress: ZSTD couldn't emit any bytes into the
		 * remaining output buffer.  Calling process_header
		 * with size=0 would re-trigger header initialization,
		 * double-subtracting the header size from dst_size and
		 * underflowing the unsigned counter.
		 */
		if (size == 0)
			goto reset;
		size = process_header(record, size);
		compressed += size;
		dst += size;
		dst_size -= size;
	}

	return compressed;

reset:
	/* Reset so the context is usable if the caller retries */
	ret = ZSTD_initCStream(data->cstream, data->comp_level);
	if (ZSTD_isError(ret))
		pr_err("failed to reset compression context: %s\n",
			ZSTD_getErrorName(ret));
	return -1;
}

size_t zstd_decompress_stream(struct zstd_data *data, void *src, size_t src_size,
			      void *dst, size_t dst_size)
{
	size_t ret;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output = { dst, dst_size, 0 };

	if (!data->dstream) {
		data->dstream = ZSTD_createDStream();
		if (data->dstream == NULL) {
			pr_err("Couldn't create decompression stream.\n");
			return 0;
		}

		ret = ZSTD_initDStream(data->dstream);
		if (ZSTD_isError(ret)) {
			pr_err("Failed to initialize decompression stream: %s\n",
				ZSTD_getErrorName(ret));
			return 0;
		}
	}
	while (input.pos < input.size) {
		size_t prev_in = input.pos;
		size_t prev_out = output.pos;

		ret = ZSTD_decompressStream(data->dstream, &output, &input);
		if (ZSTD_isError(ret)) {
			pr_err("failed to decompress (B): %zd -> %zd, dst_size %zd : %s\n",
			       src_size, output.pos, dst_size, ZSTD_getErrorName(ret));
			return 0;
		}
		/*
		 * Neither stream advanced — decompression is stuck.
		 * Return 0 (error) rather than partial output: perf
		 * uses ZSTD_flushStream (not ZSTD_endStream), so the
		 * stream is continuous across compressed events.
		 * Discarding unconsumed input would desynchronize the
		 * decompressor, causing the next call to produce
		 * garbage that could be misinterpreted as valid events.
		 */
		if (input.pos == prev_in && output.pos == prev_out)
			return 0;
	}

	return output.pos;
}
