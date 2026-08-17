/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BINFMT_MISC_H
#define _LINUX_BINFMT_MISC_H

#include <linux/types.h>

struct bpf_prog;
struct file;
struct linux_binprm;
struct ucounts;
struct user_namespace;

#define BINFMT_MISC_OPS_NAME_MAX 16

/* Longest name a 'B' entry can bind an interpreter under. */
#define BINFMT_MISC_INTERP_NAME_MAX 32

/* Most interpreters one entry can bind. */
#define BINFMT_MISC_INTERP_MAX 100

/**
 * struct binfmt_misc_interp - an interpreter an entry was registered with
 * @list: link in the entry's list, in registration order
 * @file: the file, opened at registration and never resolved again
 * @ucounts: the UCOUNT_BINFMT_MISC_INTERPRETERS charge the binding took
 * @path: the path it was registered under, used as the name the interpreter
 *        runs under; stored after @name in the same allocation
 * @name: the name the load program selects it by; empty for the fixed
 *        interpreter of a static 'F' entry
 *
 * Owned by the entry and living exactly as long as it does. The list head
 * is handed to the handler's load program for the duration of one exec,
 * which picks one with bpf_binprm_select_interp().
 */
struct binfmt_misc_interp {
	struct list_head	list;
	struct file		*file;
	struct ucounts		*ucounts;
	const char		*path;
	char			name[];
};

const struct binfmt_misc_interp *
binfmt_misc_find_interp(const struct list_head *interps, const char *name);

/**
 * enum bpf_binprm_flags - per-exec invocation flags a load program can request
 * @BPF_BINPRM_PRESERVE_ARGV0: keep the caller's argv[0] (like the 'P' flag)
 * @BPF_BINPRM_CREDENTIALS: compute credentials from the binary; implies execfd
 *                          (like the 'C' flag)
 * @BPF_BINPRM_EXECFD: pass the binary via AT_EXECFD (like the 'O' flag)
 * @BPF_BINPRM_TRANSPARENT: leave argv untouched, the interpreter takes the
 *                          binary from AT_EXECFD (like the 'T' flag); implies
 *                          execfd, excludes preserve-argv0
 * @BPF_BINPRM_LOADER: substitute the interpreter for the binary's PT_INTERP
 *                     and run the binary as a native exec (like the 'L'
 *                     flag); excludes every other flag
 *
 * Set from a load program with bpf_binprm_set_flags(). Unlike a static entry,
 * a bpf handler chooses these per exec rather than once at registration.
 */
enum bpf_binprm_flags {
	BPF_BINPRM_PRESERVE_ARGV0	= (1ULL << 0),
	BPF_BINPRM_CREDENTIALS		= (1ULL << 1),
	BPF_BINPRM_EXECFD		= (1ULL << 2),
	BPF_BINPRM_TRANSPARENT		= (1ULL << 3),
	BPF_BINPRM_LOADER		= (1ULL << 4),
};

/**
 * struct binfmt_misc_ops - bpf-backed binary type handler
 * @match: decide whether the handler applies to @bprm; consulted from the
 *         entry lookup walk like static magic and extension matching, in
 *         registration order with first-match-wins semantics; sleepable,
 *         so it can read the binary to decide, but the verifier rejects
 *         the interpreter selection kfuncs in it
 * @load:  select an interpreter for the matched @bprm via
 *         bpf_binprm_set_interp(), or one the entry bound via
 *         bpf_binprm_select_interp(), and return zero; a match is
 *         committed, so a failure fails the exec instead of falling
 *         through to later entries; -ENOEXEC does not fail the exec but
 *         moves on to the remaining binary formats
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
