/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BINFMT_MISC_H
#define _LINUX_BINFMT_MISC_H

#include <linux/types.h>

struct bpf_prog;
struct linux_binprm;
struct user_namespace;

#define BINFMT_MISC_OPS_NAME_MAX 16

/**
 * enum bpf_binprm_flags - per-exec invocation flags a load program can request
 * @BPF_BINPRM_PRESERVE_ARGV0: keep the caller's argv[0] (like the 'P' flag)
 * @BPF_BINPRM_CREDENTIALS: compute credentials from the binary; implies execfd
 *                          (like the 'C' flag)
 * @BPF_BINPRM_EXECFD: pass the binary via AT_EXECFD (like the 'O' flag)
 *
 * Set from a load program with bpf_binprm_set_flags(). Unlike a static entry,
 * a bpf handler chooses these per exec rather than once at registration.
 */
enum bpf_binprm_flags {
	BPF_BINPRM_PRESERVE_ARGV0	= (1ULL << 0),
	BPF_BINPRM_CREDENTIALS		= (1ULL << 1),
	BPF_BINPRM_EXECFD		= (1ULL << 2),
};

/**
 * struct binfmt_misc_ops - bpf-backed binary type handler
 * @match: decide whether the handler applies to @bprm; consulted from the
 *         entry lookup walk like static magic and extension matching, in
 *         registration order with first-match-wins semantics; sleepable,
 *         so it can read the binary to decide, but the verifier rejects
 *         the interpreter selection kfuncs in it
 * @load:  select an interpreter for the matched @bprm via
 *         bpf_binprm_set_interp() and return zero; a match is committed, so
 *         a failure fails the exec instead of falling through to later
 *         entries; -ENOEXEC does not fail the exec but moves on to the
 *         remaining binary formats
 * @name: name that 'B' entries reference the handler by
 */
struct binfmt_misc_ops {
	bool (*match)(struct linux_binprm *bprm);
	int (*load)(struct linux_binprm *bprm);
	char name[BINFMT_MISC_OPS_NAME_MAX];
};

#ifdef CONFIG_BINFMT_MISC_BPF
const struct binfmt_misc_ops *binfmt_misc_get_ops(struct user_namespace *user_ns,
						  const char *name);
void binfmt_misc_put_ops(const struct binfmt_misc_ops *ops);
bool bpf_prog_is_binfmt_misc_ops(const struct bpf_prog *prog);
#else
static inline const struct binfmt_misc_ops *
binfmt_misc_get_ops(struct user_namespace *user_ns, const char *name)
{
	return NULL;
}

static inline void binfmt_misc_put_ops(const struct binfmt_misc_ops *ops)
{
}

static inline bool bpf_prog_is_binfmt_misc_ops(const struct bpf_prog *prog)
{
	return false;
}
#endif /* CONFIG_BINFMT_MISC_BPF */

#endif /* _LINUX_BINFMT_MISC_H */
