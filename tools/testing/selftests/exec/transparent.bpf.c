// SPDX-License-Identifier: GPL-2.0
/*
 * binfmt_misc_ops handler for the transparent-mode case: match a synthetic
 * riscv ELF header and run the asserting interpreter transparently - the
 * argument vector untouched, the binary in AT_EXECFD and mm->exe_file
 * labeled with the binary.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define EI_CLASS	4
#define ELFCLASS64	2
#define EM_RISCV	243

extern int bpf_binprm_set_interp(struct linux_binprm *bprm, const char *path,
				 size_t path__sz) __ksym;
extern int bpf_binprm_set_flags(struct linux_binprm *bprm,
				enum bpf_binprm_flags flags) __ksym;

SEC("struct_ops.s/match")
bool BPF_PROG(transparent_match, struct linux_binprm *bprm)
{
	__u16 machine;

	if (bprm->buf[0] != 0x7f || bprm->buf[1] != 'E' ||
	    bprm->buf[2] != 'L' || bprm->buf[3] != 'F' ||
	    bprm->buf[EI_CLASS] != ELFCLASS64)
		return false;

	/* e_machine is a 16-bit little-endian field at offset 18. */
	machine = (__u8)bprm->buf[18] | ((__u16)(__u8)bprm->buf[19] << 8);
	return machine == EM_RISCV;
}

SEC("struct_ops.s/load")
int BPF_PROG(transparent_load, struct linux_binprm *bprm)
{
	char interp[] = "/tmp/binfmt_transparent_interp";
	int err;

	err = bpf_binprm_set_flags(bprm, BPF_BINPRM_TRANSPARENT);
	if (err)
		return err;

	/* @path__sz includes the terminating NUL; 0 commits the selection. */
	return bpf_binprm_set_interp(bprm, interp, sizeof(interp));
}

SEC(".struct_ops.link")
struct binfmt_misc_ops transparent = {
	.match = (void *)transparent_match,
	.load = (void *)transparent_load,
	.name = "transparent",
};
