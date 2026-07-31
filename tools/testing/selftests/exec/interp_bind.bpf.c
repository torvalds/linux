// SPDX-License-Identifier: GPL-2.0
/*
 * binfmt_misc_ops handler for the selftest's bound-interpreter case: one
 * handler, one entry, an interpreter per guest architecture - each bound to
 * a file when the entry was registered rather than to a path resolved at
 * exec time. The load program names the one it wants; a name the entry did
 * not bind fails the exec, which the harness checks too.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define EI_CLASS	4
#define ELFCLASS64	2
#define E_MACHINE_OFF	18
#define EM_ARM		40
#define EM_AARCH64	183
#define EM_RISCV	243

extern int bpf_binprm_select_interp(struct linux_binprm *bprm,
				    const char *name, size_t name__sz) __ksym;

/* The guest architecture of a 64-bit ELF, or zero if it is not one. */
static __u16 elf_machine(struct linux_binprm *bprm)
{
	if (bprm->buf[0] != 0x7f || bprm->buf[1] != 'E' ||
	    bprm->buf[2] != 'L' || bprm->buf[3] != 'F' ||
	    bprm->buf[EI_CLASS] != ELFCLASS64)
		return 0;

	/* Little-endian 16-bit field, read byte-wise for the verifier. */
	return (__u8)bprm->buf[E_MACHINE_OFF] |
	       ((__u16)(__u8)bprm->buf[E_MACHINE_OFF + 1] << 8);
}

SEC("struct_ops.s/match")
bool BPF_PROG(interp_bind_match, struct linux_binprm *bprm)
{
	__u16 machine = elf_machine(bprm);

	return machine == EM_AARCH64 || machine == EM_RISCV ||
	       machine == EM_ARM;
}

SEC("struct_ops.s/load")
int BPF_PROG(interp_bind_load, struct linux_binprm *bprm)
{
	/*
	 * Names, not paths: each one selects a file the entry pre-opened, so
	 * nothing is resolved here or later, in any namespace. The buffers
	 * are on the stack because the verifier rejects .rodata for a sized
	 * memory argument.
	 */
	char first[] = "first";
	char second[] = "second";
	char unbound[] = "unbound";

	switch (elf_machine(bprm)) {
	case EM_AARCH64:
		return bpf_binprm_select_interp(bprm, first, sizeof(first));
	case EM_RISCV:
		return bpf_binprm_select_interp(bprm, second, sizeof(second));
	}

	/* The entry bound nothing under this name: -ENOENT fails the exec. */
	return bpf_binprm_select_interp(bprm, unbound, sizeof(unbound));
}

SEC(".struct_ops.link")
struct binfmt_misc_ops interp_bind = {
	.match	= (void *)interp_bind_match,
	.load	= (void *)interp_bind_load,
	.name	= "interp_bind",
};
