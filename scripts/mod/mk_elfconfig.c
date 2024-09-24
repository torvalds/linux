// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

int
main(int argc, char **argv)
{
	unsigned char ei[EI_NIDENT];

	if (fread(ei, 1, EI_NIDENT, stdin) != EI_NIDENT) {
		fprintf(stderr, "Error: input truncated\n");
		return 1;
	}
	if (memcmp(ei, ELFMAG, SELFMAG) != 0) {
		fprintf(stderr, "Error: not ELF\n");
		return 1;
	}
	switch (ei[EI_CLASS]) {
	case ELFCLASS32:
		printf("#define KERNEL_ELFCLASS ELFCLASS32\n");
		break;
	case ELFCLASS64:
		printf("#define KERNEL_ELFCLASS ELFCLASS64\n");
		break;
	default:
		exit(1);
	}

	return 0;
}
