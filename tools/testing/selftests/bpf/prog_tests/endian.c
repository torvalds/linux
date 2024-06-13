// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include "test_endian.skel.h"

static int duration;

#define IN16 0x1234
#define IN32 0x12345678U
#define IN64 0x123456789abcdef0ULL

#define OUT16 0x3412
#define OUT32 0x78563412U
#define OUT64 0xf0debc9a78563412ULL

void test_endian(void)
{
	struct test_endian* skel;
	struct test_endian__bss *bss;
	int err;

	skel = test_endian__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;
	bss = skel->bss;

	bss->in16 = IN16;
	bss->in32 = IN32;
	bss->in64 = IN64;

	err = test_endian__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	usleep(1);

	CHECK(bss->out16 != OUT16, "out16", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->out16, (__u64)OUT16);
	CHECK(bss->out32 != OUT32, "out32", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->out32, (__u64)OUT32);
	CHECK(bss->out64 != OUT64, "out16", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->out64, (__u64)OUT64);

	CHECK(bss->const16 != OUT16, "const16", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->const16, (__u64)OUT16);
	CHECK(bss->const32 != OUT32, "const32", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->const32, (__u64)OUT32);
	CHECK(bss->const64 != OUT64, "const64", "got 0x%llx != exp 0x%llx\n",
	      (__u64)bss->const64, (__u64)OUT64);
cleanup:
	test_endian__destroy(skel);
}
