// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <libarena/common.h>
#include <libarena/asan.h>
#include <libarena/buddy.h>

/* Required for parsing the ASAN call stacks. */
#include "test_progs_compat.h"

extern struct buddy __arena buddy;

#ifdef BPF_ARENA_ASAN

#include "st_asan_common.h"

static __always_inline int asan_test_buddy_oob_single(size_t alloc_size)
{
	u8 __arena *mem;
	int ret, i;

	ret = asan_validate();
	if (ret < 0)
		return ret;

	mem = buddy_alloc(&buddy, alloc_size);
	if (!mem) {
		arena_stdout("buddy_alloc failed for size %lu", alloc_size);
		return -ENOMEM;
	}

	ret = asan_validate();
	if (ret < 0)
		return ret;

	for (i = zero; i < alloc_size && can_loop; i++) {
		mem[i] = 0xba;
		ret = asan_validate_addr(false, &mem[i]);
		if (ret < 0)
			return ret;
	}

	mem[alloc_size] = 0xba;
	ret = asan_validate_addr(true, &mem[alloc_size]);
	if (ret < 0)
		return ret;

	buddy_free(&buddy, mem);

	return 0;
}

/*
 * Factored out because asan_validate_addr is complex enough to cause
 * verification failures if verified with the rest of asan_test_buddy_uaf_single.
 */
__weak int asan_test_buddy_byte(u8 __arena *mem, int i, bool freed)
{
	int ret;

	/* The header in freed blocks doesn't get poisoned. */
	if (freed && BUDDY_HEADER_OFF <= i &&
		i < BUDDY_HEADER_OFF + sizeof(struct buddy_header))
		return 0;

	mem[i] = 0xba;
	ret = asan_validate_addr(freed, &mem[i]);
	if (ret < 0)
		return ret;

	return 0;
}

__weak int asan_test_buddy_uaf_single(size_t alloc_size)
{
	u8 __arena *mem;
	int ret;
	int i;

	mem = buddy_alloc(&buddy, alloc_size);
	if (!mem) {
		arena_stdout("buddy_alloc failed for size %lu", alloc_size);
		return -ENOMEM;
	}

	ret = asan_validate();
	if (ret < 0)
		return ret;

	for (i = zero; i < alloc_size && can_loop; i++) {
		ret = asan_test_buddy_byte(mem, i, false);
		if (ret)
			return ret;
	}

	ret = asan_validate();
	if (ret < 0)
		return ret;

	buddy_free(&buddy, mem);

	for (i = zero; i < alloc_size && can_loop; i++) {
		ret = asan_test_buddy_byte(mem, i, true);
		if (ret)
			return ret;
	}

	return 0;
}

struct buddy_blob {
	volatile u8 mem[48];
	u8 oob;
};

static __always_inline int asan_test_buddy_blob_single(void)
{
	volatile struct buddy_blob __arena *blob;
	const size_t alloc_size = sizeof(struct buddy_blob) - 1;
	int ret;

	blob = buddy_alloc(&buddy, alloc_size);
	if (!blob)
		return -ENOMEM;

	blob->mem[0] = 0xba;
	ret = asan_validate_addr(false, &blob->mem[0]);
	if (ret < 0)
		return ret;

	blob->mem[47] = 0xba;
	ret = asan_validate_addr(false, &blob->mem[47]);
	if (ret < 0)
		return ret;

	blob->oob = 0;
	ret = asan_validate_addr(true, &blob->oob);
	if (ret < 0)
		return ret;

	buddy_free(&buddy, (void __arena *)blob);

	return 0;
}

SEC("syscall")
__stderr("Memory violation for address {{.*}} for write of size 1")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
__weak int asan_test_buddy_oob(void)
{
	size_t sizes[] = {
		7, 8, 17, 18, 64, 256, 317, 512, 1024,
	};
	int ret, i;

	ret = buddy_init(&buddy);
	if (ret) {
		arena_stdout("buddy_init failed with %d", ret);
		return ret;
	}

	for (i = zero; i < sizeof(sizes) / sizeof(sizes[0]) && can_loop; i++) {
		ret = asan_test_buddy_oob_single(sizes[i]);
		if (ret) {
			arena_stdout("%s:%d Failed for size %lu", __func__,
				   __LINE__, sizes[i]);
			buddy_destroy(&buddy);
			return ret;
		}
	}

	buddy_destroy(&buddy);

	ret = asan_validate();
	if (ret < 0)
		return ret;

	return 0;
}

SEC("syscall")
__stderr("Memory violation for address {{.*}} for write of size 1")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
__weak int asan_test_buddy_uaf(void)
{
	size_t sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 16384 };
	int ret, i;

	ret = buddy_init(&buddy);
	if (ret) {
		arena_stdout("buddy_init failed with %d", ret);
		return ret;
	}

	for (i = zero; i < sizeof(sizes) / sizeof(sizes[0]) && can_loop; i++) {
		ret = asan_test_buddy_uaf_single(sizes[i]);
		if (ret) {
			arena_stdout("%s:%d Failed for size %lu", __func__,
				   __LINE__, sizes[i]);
			buddy_destroy(&buddy);
			return ret;
		}
	}

	buddy_destroy(&buddy);

	ret = asan_validate();
	if (ret < 0)
		return ret;

	return 0;
}

SEC("syscall")
__stderr("Memory violation for address {{.*}} for write of size 1")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
__weak int asan_test_buddy_blob(void)
{
	const int iters = 10;
	int ret, i;

	ret = buddy_init(&buddy);
	if (ret) {
		arena_stdout("buddy_init failed with %d", ret);
		return ret;
	}

	for (i = zero; i < iters && can_loop; i++) {
		ret = asan_test_buddy_blob_single();
		if (ret) {
			arena_stdout("%s:%d Failed on iteration %d", __func__,
				   __LINE__, i);
			buddy_destroy(&buddy);
			return ret;
		}
	}

	buddy_destroy(&buddy);

	ret = asan_validate();
	if (ret < 0)
		return ret;

	return 0;
}

#endif

__weak char _license[] SEC("license") = "GPL";
