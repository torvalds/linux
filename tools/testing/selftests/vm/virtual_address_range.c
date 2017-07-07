/*
 * Copyright 2017, Anshuman Khandual, IBM Corp.
 * Licensed under GPLv2.
 *
 * Works on architectures which support 128TB virtual
 * address range and beyond.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <numaif.h>
#include <sys/mman.h>
#include <sys/time.h>

/*
 * Maximum address range mapped with a single mmap()
 * call is little bit more than 16GB. Hence 16GB is
 * chosen as the single chunk size for address space
 * mapping.
 */
#define MAP_CHUNK_SIZE   17179869184UL /* 16GB */

/*
 * Address space till 128TB is mapped without any hint
 * and is enabled by default. Address space beyond 128TB
 * till 512TB is obtained by passing hint address as the
 * first argument into mmap() system call.
 *
 * The process heap address space is divided into two
 * different areas one below 128TB and one above 128TB
 * till it reaches 512TB. One with size 128TB and the
 * other being 384TB.
 */
#define NR_CHUNKS_128TB   8192UL /* Number of 16GB chunks for 128TB */
#define NR_CHUNKS_384TB  24576UL /* Number of 16GB chunks for 384TB */

#define ADDR_MARK_128TB  (1UL << 47) /* First address beyond 128TB */

static char *hind_addr(void)
{
	int bits = 48 + rand() % 15;

	return (char *) (1UL << bits);
}

static int validate_addr(char *ptr, int high_addr)
{
	unsigned long addr = (unsigned long) ptr;

	if (high_addr) {
		if (addr < ADDR_MARK_128TB) {
			printf("Bad address %lx\n", addr);
			return 1;
		}
		return 0;
	}

	if (addr > ADDR_MARK_128TB) {
		printf("Bad address %lx\n", addr);
		return 1;
	}
	return 0;
}

static int validate_lower_address_hint(void)
{
	char *ptr;

	ptr = mmap((void *) (1UL << 45), MAP_CHUNK_SIZE, PROT_READ |
			PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr == MAP_FAILED)
		return 0;

	return 1;
}

int main(int argc, char *argv[])
{
	char *ptr[NR_CHUNKS_128TB];
	char *hptr[NR_CHUNKS_384TB];
	char *hint;
	unsigned long i, lchunks, hchunks;

	for (i = 0; i < NR_CHUNKS_128TB; i++) {
		ptr[i] = mmap(NULL, MAP_CHUNK_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (ptr[i] == MAP_FAILED) {
			if (validate_lower_address_hint())
				return 1;
			break;
		}

		if (validate_addr(ptr[i], 0))
			return 1;
	}
	lchunks = i;

	for (i = 0; i < NR_CHUNKS_384TB; i++) {
		hint = hind_addr();
		hptr[i] = mmap(hint, MAP_CHUNK_SIZE, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (hptr[i] == MAP_FAILED)
			break;

		if (validate_addr(hptr[i], 1))
			return 1;
	}
	hchunks = i;

	for (i = 0; i < lchunks; i++)
		munmap(ptr[i], MAP_CHUNK_SIZE);

	for (i = 0; i < hchunks; i++)
		munmap(hptr[i], MAP_CHUNK_SIZE);

	return 0;
}
