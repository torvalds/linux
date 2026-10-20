/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/sched/coredump.h>
#include <uapi/linux/coredump.h>
#include <asm/siginfo.h>

#ifdef CONFIG_COREDUMP
/**
 * enum coredump_state - what happened while the coredump was written
 * @COREDUMP_STATE_STARTED: the dumper committed to writing a coredump
 * @COREDUMP_STATE_TRUNCATED: the dumper stopped before it had written all of it
 */
enum coredump_state {
	COREDUMP_STATE_STARTED		= (1U << 0),
	COREDUMP_STATE_TRUNCATED	= (1U << 1),
};

struct core_vma_metadata {
	unsigned long start, end;
	vm_flags_t flags;
	unsigned long dump_size;
	unsigned long pgoff;
	struct file   *file;
};

struct coredump_params {
	const kernel_siginfo_t *siginfo;
	struct file *file;
	unsigned long limit;
	/* COREDUMP_MEMORY_* types to dump, the task's or the server's. */
	u64 memory_types;
	/* Snapshot of dumpable at dump start. */
	enum task_dumpable dumpable;
	int cpu;
	/* COREDUMP_* options negotiated with the coredump server. */
	u64 mask;
	/* COREDUMP_STATE_* raised while the coredump is written. */
	enum coredump_state state;
	/* Record header scratch, NULL unless the coredump is a record stream. */
	struct coredump_record_header *record_hdr;
	/* Bytes handed to the file, record headers included. */
	loff_t written;
	/* Offset in the coredump, record headers excluded. */
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
void dump_skip_to(struct coredump_params *cprm, unsigned long to);
void dump_skip(struct coredump_params *cprm, size_t nr);
bool dump_emit(struct coredump_params *cprm, const void *addr, int nr);
bool dump_align(struct coredump_params *cprm, int align);
bool dump_user_range(struct coredump_params *cprm, unsigned long start,
		     unsigned long len);
void vfs_coredump(const kernel_siginfo_t *siginfo);

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
static inline void vfs_coredump(const kernel_siginfo_t *siginfo) {}

#define coredump_report(...)
#define coredump_report_failure(...)

#endif

#if defined(CONFIG_COREDUMP) && defined(CONFIG_SYSCTL)
extern void validate_coredump_safety(void);
#else
static inline void validate_coredump_safety(void) {}
#endif

#endif /* _LINUX_COREDUMP_H */
