// SPDX-License-Identifier: GPL-2.0
/*
 * binfmt_misc_ops handler for the loader-substitution case: match the
 * marker the harness poked into the payload's e_ident padding and ask for
 * the selected interpreter to be substituted for the binary's PT_INTERP,
 * so the binary itself runs as a fully native exec.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define EI_CLASS	4
#define EI_PAD		9
#define ELFCLASS64	2

extern int bpf_binprm_set_interp(struct linux_binprm *bprm, const char *path,
				 size_t path__sz) __ksym;
extern int bpf_binprm_set_flags(struct linux_binprm *bprm,
				enum bpf_binprm_flags flags) __ksym;

SEC("struct_ops.s/match")
bool BPF_PROG(loader_match, struct linux_binprm *bprm)
{
	if (bprm->buf[0] != 0x7f || bprm->buf[1] != 'E' ||
	    bprm->buf[2] != 'L' || bprm->buf[3] != 'F' ||
	    bprm->buf[EI_CLASS] != ELFCLASS64)
		return false;

	/* The harness marks the payload with "LDRTST" at EI_PAD. */
	return bprm->buf[EI_PAD + 0] == 'L' && bprm->buf[EI_PAD + 1] == 'D' &&
	       bprm->buf[EI_PAD + 2] == 'R' && bprm->buf[EI_PAD + 3] == 'T' &&
	       bprm->buf[EI_PAD + 4] == 'S' && bprm->buf[EI_PAD + 5] == 'T';
}

SEC("struct_ops.s/load")
int BPF_PROG(loader_load, struct linux_binprm *bprm)
{
	char interp[] = "/tmp/binfmt_loader_interp";
	int err;

	err = bpf_binprm_set_flags(bprm, BPF_BINPRM_LOADER);
	if (err)
		return err;

	/* @path__sz includes the terminating NUL; 0 commits the selection. */
	return bpf_binprm_set_interp(bprm, interp, sizeof(interp));
}

SEC(".struct_ops.link")
struct binfmt_misc_ops loader = {
	.match = (void *)loader_match,
	.load = (void *)loader_load,
	.name = "loader",
};
