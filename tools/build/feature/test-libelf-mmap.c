// SPDX-License-Identifier: GPL-2.0
#include <libelf.h>

int main(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ_MMAP, 0);

	return (long)elf;
}
