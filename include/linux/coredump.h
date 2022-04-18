/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/siginfo.h>

struct core_vma_metadata {
	unsigned long start, end;
	unsigned long flags;
	unsigned long dump_size;
	unsigned long pgoff;
	struct file   *file;
};

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
struct coredump_params;
extern int dump_skip(struct coredump_params *cprm, size_t nr);
extern int dump_emit(struct coredump_params *cprm, const void *addr, int nr);
extern int dump_align(struct coredump_params *cprm, int align);
extern void dump_truncate(struct coredump_params *cprm);
int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len);
#ifdef CONFIG_COREDUMP
extern void do_coredump(const kernel_siginfo_t *siginfo);
#else
static inline void do_coredump(const kernel_siginfo_t *siginfo) {}
#endif

extern int core_uses_pid;
extern char core_pattern[];
extern unsigned int core_pipe_limit;

#endif /* _LINUX_COREDUMP_H */
