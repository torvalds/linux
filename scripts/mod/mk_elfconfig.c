#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

int
main(int argc, char **argv)
{
	unsigned char ei[EI_NIDENT];
	union { short s; char c[2]; } endian_test;

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
	switch (ei[EI_DATA]) {
	case ELFDATA2LSB:
		printf("#define KERNEL_ELFDATA ELFDATA2LSB\n");
		break;
	case ELFDATA2MSB:
		printf("#define KERNEL_ELFDATA ELFDATA2MSB\n");
		break;
	default:
		exit(1);
	}

	if (sizeof(unsigned long) == 4) {
		printf("#define HOST_ELFCLASS ELFCLASS32\n");
	} else if (sizeof(unsigned long) == 8) {
		printf("#define HOST_ELFCLASS ELFCLASS64\n");
	}

	endian_test.s = 0x0102;
	if (memcmp(endian_test.c, "\x01\x02", 2) == 0)
		printf("#define HOST_ELFDATA ELFDATA2MSB\n");
	else if (memcmp(endian_test.c, "\x02\x01", 2) == 0)
		printf("#define HOST_ELFDATA ELFDATA2LSB\n");
	else
		exit(1);

	return 0;
}
