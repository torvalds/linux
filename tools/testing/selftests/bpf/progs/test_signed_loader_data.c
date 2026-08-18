// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/*
 * A single initialized global, so the generated loader has one internal
 * (.data) map that it seeds with an initial value while loading.
 * prog_tests/signed_loader.c uses this to check that a signed loader
 * keeps the attested contents and ignores a ctx-supplied initial_value:
 * the host cannot re-seed a signed program's maps through the loader ctx.
 */
__u64 magic = 0x5eed1234abad1deaULL;

SEC("socket")
int probe(void *ctx)
{
	return (int)magic;
}

char _license[] SEC("license") = "GPL";
