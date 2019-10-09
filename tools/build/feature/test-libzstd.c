// SPDX-License-Identifier: GPL-2.0-only
#include <zstd.h>

int main(void)
{
	ZSTD_CStream	*cstream;

	cstream = ZSTD_createCStream();
	ZSTD_freeCStream(cstream);

	return 0;
}
