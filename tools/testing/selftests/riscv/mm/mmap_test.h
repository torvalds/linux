/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TESTCASES_MMAP_TEST_H
#define _TESTCASES_MMAP_TEST_H
#include <sys/mman.h>
#include <sys/resource.h>
#include <stddef.h>

#define TOP_DOWN 0
#define BOTTOM_UP 1

struct addresses {
	int *no_hint;
	int *on_37_addr;
	int *on_38_addr;
	int *on_46_addr;
	int *on_47_addr;
	int *on_55_addr;
	int *on_56_addr;
};

// Only works on 64 bit
#if __riscv_xlen == 64
static inline void do_mmaps(struct addresses *mmap_addresses)
{
	/*
	 * Place all of the hint addresses on the boundaries of mmap
	 * sv39, sv48, sv57
	 * User addresses end at 1<<38, 1<<47, 1<<56 respectively
	 */
	void *on_37_bits = (void *)(1UL << 37);
	void *on_38_bits = (void *)(1UL << 38);
	void *on_46_bits = (void *)(1UL << 46);
	void *on_47_bits = (void *)(1UL << 47);
	void *on_55_bits = (void *)(1UL << 55);
	void *on_56_bits = (void *)(1UL << 56);

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	mmap_addresses->no_hint =
		mmap(NULL, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_37_addr =
		mmap(on_37_bits, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_38_addr =
		mmap(on_38_bits, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_46_addr =
		mmap(on_46_bits, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_47_addr =
		mmap(on_47_bits, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_55_addr =
		mmap(on_55_bits, 5 * sizeof(int), prot, flags, 0, 0);
	mmap_addresses->on_56_addr =
		mmap(on_56_bits, 5 * sizeof(int), prot, flags, 0, 0);
}
#endif /* __riscv_xlen == 64 */

static inline int memory_layout(void)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void *value1 = mmap(NULL, sizeof(int), prot, flags, 0, 0);
	void *value2 = mmap(NULL, sizeof(int), prot, flags, 0, 0);

	return value2 > value1;
}
#endif /* _TESTCASES_MMAP_TEST_H */
