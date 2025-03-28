// SPDX-License-Identifier: GPL-2.0
#include <stddef.h>
#include <libelf.h>

int main(void)
{
	elf_compress(NULL, ELFCOMPRESS_ZSTD, 0);
	return 0;
}
