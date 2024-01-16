// SPDX-License-Identifier: GPL-2.0
#include <zlib.h>

int main(void)
{
	z_stream zs;

	inflateInit(&zs);
	return 0;
}
