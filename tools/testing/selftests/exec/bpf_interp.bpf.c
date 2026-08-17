// SPDX-License-Identifier: GPL-2.0
/*
 * binfmt_misc_ops handler for the selftest's fixed-interpreter case: match a
 * 64-bit aarch64 ELF header from the prefetched buffer and route it to a fixed
 * interpreter chosen by the program. This is the portable, self-contained
 * equivalent of routing a foreign binary to an emulator: it matches
 * programmatically and computes the interpreter, but points at a test binary
 * the harness installs rather than a system emulator.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define EI_CLASS	4
#define ELFCLASS64	2
#define EM_AARCH64	183

extern int bpf_binprm_set_interp(struct linux_binprm *bprm, const char *path,
				 size_t path__sz) __ksym;

/*
 * A magic-style decision needs nothing beyond the prefetched bprm->buf,
 * even though the match program could read the file.
 */
SEC("struct_ops.s/match")
bool BPF_PROG(bpf_interp_match, struct linux_binprm *bprm)
{
	__u16 machine;

	if (bprm->buf[0] != 0x7f || bprm->buf[1] != 'E' ||
	    bprm->buf[2] != 'L' || bprm->buf[3] != 'F' ||
	    bprm->buf[EI_CLASS] != ELFCLASS64)
		return false;

	/* e_machine is a 16-bit little-endian field at offset 18. */
	machine = (__u8)bprm->buf[18] | ((__u16)(__u8)bprm->buf[19] << 8);
	return machine == EM_AARCH64;
}

SEC("struct_ops.s/load")
int BPF_PROG(bpf_interp_load, struct linux_binprm *bprm)
{
	/*
	 * Keep the path on the (writable) stack: bpf_binprm_set_interp() takes
	 * a sized memory arg and the verifier rejects a read-only .rodata
	 * buffer for it. The harness installs the interpreter at this path.
	 */
	char interp[] = "/tmp/binfmt_bpf_interp";

	/* @path__sz includes the terminating NUL; 0 commits the selection. */
	return bpf_binprm_set_interp(bprm, interp, sizeof(interp));
}

SEC(".struct_ops.link")
struct binfmt_misc_ops bpf_interp = {
	.match = (void *)bpf_interp_match,
	.load = (void *)bpf_interp_load,
	.name = "bpf_interp",
};
