#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#ifdef CONFIG_CRASH_DUMP
#include <linux/kexec.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

#define ELFCORE_ADDR_MAX	(-1ULL)
extern unsigned long long elfcorehdr_addr;
extern ssize_t copy_oldmem_page(unsigned long, char *, size_t,
						unsigned long, int);
extern const struct file_operations proc_vmcore_operations;
extern struct proc_dir_entry *proc_vmcore;

#endif /* CONFIG_CRASH_DUMP */
#endif /* LINUX_CRASHDUMP_H */
