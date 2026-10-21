// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/*
 * Minimal, map-less program. Driven through libbpf's gen_loader (gen_hash)
 * by prog_tests/signed_loader.c so the generated light-skeleton loader can be
 * exercised against good and tampered metadata, which the kernel now verifies
 * at load time via the insns||metadata signature. A socket filter needs no
 * load-time attach resolution, and having no maps keeps the generated loader's
 * ctx trivial (0 maps, 1 prog).
 */
SEC("socket")
int probe(void *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
