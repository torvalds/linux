/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/siginfo.h>

#ifdef CONFIG_COREDUMP
struct core_vma_metadata {
	unsigned long start, end;
	unsigned long flags;
	unsigned long dump_size;
};

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
struct coredump_params;
extern void dump_skip_to(struct coredump_params *cprm, unsigned long to);
extern void dump_skip(struct coredump_params *cprm, size_t nr);
extern int dump_emit(struct coredump_params *cprm, const void *addr, int nr);
extern int dump_align(struct coredump_params *cprm, int align);
int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len);
int dump_vma_snapshot(struct coredump_params *cprm, int *vma_count,
		      struct core_vma_metadata **vma_meta,
		      size_t *vma_data_size_ptr);
extern void do_coredump(const kernel_siginfo_t *siginfo);
#else
static inline void do_coredump(const kernel_siginfo_t *siginfo) {}
#endif

#if defined(CONFIG_COREDUMP) && defined(CONFIG_SYSCTL)
extern void validate_coredump_safety(void);
#else
static inline void validate_coredump_safety(void) {}
#endif

#endif /* _LINUX_COREDUMP_H */
