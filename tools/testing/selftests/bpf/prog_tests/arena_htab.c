// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <sys/mman.h>
#include <network_helpers.h>
#include <sys/user.h>
#ifndef PAGE_SIZE /* on some archs it comes in sys/user.h */
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#endif
#include "arena_htab_asm.skel.h"
#include "arena_htab.skel.h"

#include "bpf_arena_htab.h"

static void test_arena_htab_common(struct htab *htab)
{
	int i;

	printf("htab %p buckets %p n_buckets %d\n", htab, htab->buckets, htab->n_buckets);
	ASSERT_OK_PTR(htab->buckets, "htab->buckets shouldn't be NULL");
	for (i = 0; htab->buckets && i < 16; i += 4) {
		/*
		 * Walk htab buckets and link lists since all pointers are correct,
		 * though they were written by bpf program.
		 */
		int val = htab_lookup_elem(htab, i);

		ASSERT_EQ(i, val, "key == value");
	}
}

static void test_arena_htab_llvm(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct arena_htab *skel;
	struct htab *htab;
	size_t arena_sz;
	void *area;
	int ret;

	skel = arena_htab__open_and_load();
	if (!ASSERT_OK_PTR(skel, "arena_htab__open_and_load"))
		return;

	area = bpf_map__initial_value(skel->maps.arena, &arena_sz);
	/* fault-in a page with pgoff == 0 as sanity check */
	*(volatile int *)area = 0x55aa;

	/* bpf prog will allocate more pages */
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arena_htab_llvm), &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");
	if (skel->bss->skip) {
		printf("%s:SKIP:compiler doesn't support arena_cast\n", __func__);
		test__skip();
		goto out;
	}
	htab = skel->bss->htab_for_user;
	test_arena_htab_common(htab);
out:
	arena_htab__destroy(skel);
}

static void test_arena_htab_asm(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct arena_htab_asm *skel;
	struct htab *htab;
	int ret;

	skel = arena_htab_asm__open_and_load();
	if (!ASSERT_OK_PTR(skel, "arena_htab_asm__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arena_htab_asm), &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");
	htab = skel->bss->htab_for_user;
	test_arena_htab_common(htab);
	arena_htab_asm__destroy(skel);
}

void test_arena_htab(void)
{
	if (test__start_subtest("arena_htab_llvm"))
		test_arena_htab_llvm();
	if (test__start_subtest("arena_htab_asm"))
		test_arena_htab_asm();
}
