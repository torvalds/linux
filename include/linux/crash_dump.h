#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#ifdef CONFIG_CRASH_DUMP
#include <linux/kexec.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

extern unsigned long long elfcorehdr_addr;
extern ssize_t copy_oldmem_page(unsigned long, char *, size_t,
						unsigned long, int);
#endif /* CONFIG_CRASH_DUMP */
#endif /* LINUX_CRASHDUMP_H */
