/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#include <iostream>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include "test_core_extern.skel.h"

/* do nothing, just make sure we can link successfully */

static void dump_printf(void *ctx, const char *fmt, va_list args)
{
}

int main(int argc, char *argv[])
{
	struct btf_dump_opts opts = { };
	struct test_core_extern *skel;
	struct btf *btf;

	/* libbpf.h */
	libbpf_set_print(NULL);

	/* bpf.h */
	bpf_prog_get_fd_by_id(0);

	/* btf.h */
	btf = btf__new(NULL, 0);
	btf_dump__new(btf, dump_printf, nullptr, &opts);

	/* BPF skeleton */
	skel = test_core_extern__open_and_load();
	test_core_extern__destroy(skel);

	std::cout << "DONE!" << std::endl;

	return 0;
}
