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
	unsigned long pgoff;
	struct file   *file;
};

struct coredump_params {
	const kernel_siginfo_t *siginfo;
	struct file *file;
	unsigned long limit;
	unsigned long mm_flags;
	int cpu;
	loff_t written;
	loff_t pos;
	loff_t to_skip;
	int vma_count;
	size_t vma_data_size;
	struct core_vma_metadata *vma_meta;
	struct pid *pid;
};

extern unsigned int core_file_note_size_limit;

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
extern void dump_skip_to(struct coredump_params *cprm, unsigned long to);
extern void dump_skip(struct coredump_params *cprm, size_t nr);
extern int dump_emit(struct coredump_params *cprm, const void *addr, int nr);
extern int dump_align(struct coredump_params *cprm, int align);
int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len);
extern void do_coredump(const kernel_siginfo_t *siginfo);

/*
 * Logging for the coredump code, ratelimited.
 * The TGID and comm fields are added to the message.
 */

#define __COREDUMP_PRINTK(Level, Format, ...) \
	do {	\
		char comm[TASK_COMM_LEN];	\
		/* This will always be NUL terminated. */ \
		memcpy(comm, current->comm, sizeof(comm)); \
		printk_ratelimited(Level "coredump: %d(%*pE): " Format "\n",	\
			task_tgid_vnr(current), (int)strlen(comm), comm, ##__VA_ARGS__);	\
	} while (0)	\

#define coredump_report(fmt, ...) __COREDUMP_PRINTK(KERN_INFO, fmt, ##__VA_ARGS__)
#define coredump_report_failure(fmt, ...) __COREDUMP_PRINTK(KERN_WARNING, fmt, ##__VA_ARGS__)

#else
static inline void do_coredump(const kernel_siginfo_t *siginfo) {}

#define coredump_report(...)
#define coredump_report_failure(...)

#endif

#if defined(CONFIG_COREDUMP) && defined(CONFIG_SYSCTL)
extern void validate_coredump_safety(void);
#else
static inline void validate_coredump_safety(void) {}
#endif

#endif /* _LINUX_COREDUMP_H */
