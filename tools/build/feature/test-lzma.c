// SPDX-License-Identifier: GPL-2.0
#include <lzma.h>

int main(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_ret ret;

	ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
	return ret ? -1 : 0;
}
