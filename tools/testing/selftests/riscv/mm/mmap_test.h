/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _TESTCASES_MMAP_TEST_H
#define _TESTCASES_MMAP_TEST_H
#include <sys/mman.h>
#include <sys/resource.h>
#include <stddef.h>
#include <strings.h>
#include "../../kselftest_harness.h"

#define TOP_DOWN 0
#define BOTTOM_UP 1

#if __riscv_xlen == 64
uint64_t random_addresses[] = {
	0x19764f0d73b3a9f0, 0x016049584cecef59, 0x3580bdd3562f4acd,
	0x1164219f20b17da0, 0x07d97fcb40ff2373, 0x76ec528921272ee7,
	0x4dd48c38a3de3f70, 0x2e11415055f6997d, 0x14b43334ac476c02,
	0x375a60795aff19f6, 0x47f3051725b8ee1a, 0x4e697cf240494a9f,
	0x456b59b5c2f9e9d1, 0x101724379d63cb96, 0x7fe9ad31619528c1,
	0x2f417247c495c2ea, 0x329a5a5b82943a5e, 0x06d7a9d6adcd3827,
	0x327b0b9ee37f62d5, 0x17c7b1851dfd9b76, 0x006ebb6456ec2cd9,
	0x00836cd14146a134, 0x00e5c4dcde7126db, 0x004c29feadf75753,
	0x00d8b20149ed930c, 0x00d71574c269387a, 0x0006ebe4a82acb7a,
	0x0016135df51f471b, 0x00758bdb55455160, 0x00d0bdd949b13b32,
	0x00ecea01e7c5f54b, 0x00e37b071b9948b1, 0x0011fdd00ff57ab3,
	0x00e407294b52f5ea, 0x00567748c200ed20, 0x000d073084651046,
	0x00ac896f4365463c, 0x00eb0d49a0b26216, 0x0066a2564a982a31,
	0x002e0d20237784ae, 0x0000554ff8a77a76, 0x00006ce07a54c012,
	0x000009570516d799, 0x00000954ca15b84d, 0x0000684f0d453379,
	0x00002ae5816302b5, 0x0000042403fb54bf, 0x00004bad7392bf30,
	0x00003e73bfa4b5e3, 0x00005442c29978e0, 0x00002803f11286b6,
	0x000073875d745fc6, 0x00007cede9cb8240, 0x000027df84cc6a4f,
	0x00006d7e0e74242a, 0x00004afd0b836e02, 0x000047d0e837cd82,
	0x00003b42405efeda, 0x00001531bafa4c95, 0x00007172cae34ac4,
};
#else
uint32_t random_addresses[] = {
	0x8dc302e0, 0x929ab1e0, 0xb47683ba, 0xea519c73, 0xa19f1c90, 0xc49ba213,
	0x8f57c625, 0xadfe5137, 0x874d4d95, 0xaa20f09d, 0xcf21ebfc, 0xda7737f1,
	0xcedf392a, 0x83026c14, 0xccedca52, 0xc6ccf826, 0xe0cd9415, 0x997472ca,
	0xa21a44c1, 0xe82196f5, 0xa23fd66b, 0xc28d5590, 0xd009cdce, 0xcf0be646,
	0x8fc8c7ff, 0xe2a85984, 0xa3d3236b, 0x89a0619d, 0xc03db924, 0xb5d4cc1b,
	0xb96ee04c, 0xd191da48, 0xb432a000, 0xaa2bebbc, 0xa2fcb289, 0xb0cca89b,
	0xb0c18d6a, 0x88f58deb, 0xa4d42d1c, 0xe4d74e86, 0x99902b09, 0x8f786d31,
	0xbec5e381, 0x9a727e65, 0xa9a65040, 0xa880d789, 0x8f1b335e, 0xfc821c1e,
	0x97e34be4, 0xbbef84ed, 0xf447d197, 0xfd7ceee2, 0xe632348d, 0xee4590f4,
	0x958992a5, 0xd57e05d6, 0xfd240970, 0xc5b0dcff, 0xd96da2c2, 0xa7ae041d,
};
#endif

// Only works on 64 bit
#if __riscv_xlen == 64
#define PROT (PROT_READ | PROT_WRITE)
#define FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)

/* mmap must return a value that doesn't use more bits than the hint address. */
static inline unsigned long get_max_value(unsigned long input)
{
	unsigned long max_bit = (1UL << (((sizeof(unsigned long) * 8) - 1 -
					  __builtin_clzl(input))));

	return max_bit + (max_bit - 1);
}

#define TEST_MMAPS                                                            \
	({                                                                    \
		void *mmap_addr;                                              \
		for (int i = 0; i < ARRAY_SIZE(random_addresses); i++) {      \
			mmap_addr = mmap((void *)random_addresses[i],         \
					 5 * sizeof(int), PROT, FLAGS, 0, 0); \
			EXPECT_NE(MAP_FAILED, mmap_addr);                     \
			EXPECT_GE((void *)get_max_value(random_addresses[i]), \
				  mmap_addr);                                 \
			mmap_addr = mmap((void *)random_addresses[i],         \
					 5 * sizeof(int), PROT, FLAGS, 0, 0); \
			EXPECT_NE(MAP_FAILED, mmap_addr);                     \
			EXPECT_GE((void *)get_max_value(random_addresses[i]), \
				  mmap_addr);                                 \
		}                                                             \
	})
#endif /* __riscv_xlen == 64 */

static inline int memory_layout(void)
{
	void *value1 = mmap(NULL, sizeof(int), PROT, FLAGS, 0, 0);
	void *value2 = mmap(NULL, sizeof(int), PROT, FLAGS, 0, 0);

	return value2 > value1;
}
#endif /* _TESTCASES_MMAP_TEST_H */
