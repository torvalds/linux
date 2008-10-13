#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#ifdef CONFIG_CRASH_DUMP
#include <linux/kexec.h>
#include <linux/smp_lock.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

#define ELFCORE_ADDR_MAX	(-1ULL)

#ifdef CONFIG_PROC_VMCORE
extern unsigned long long elfcorehdr_addr;
#else
static const unsigned long long elfcorehdr_addr = ELFCORE_ADDR_MAX;
#endif

extern ssize_t copy_oldmem_page(unsigned long, char *, size_t,
						unsigned long, int);
extern const struct file_operations proc_vmcore_operations;
extern struct proc_dir_entry *proc_vmcore;

/* Architecture code defines this if there are other possible ELF
 * machine types, e.g. on bi-arch capable hardware. */
#ifndef vmcore_elf_check_arch_cross
#define vmcore_elf_check_arch_cross(x) 0
#endif

#define vmcore_elf_check_arch(x) (elf_check_arch(x) || vmcore_elf_check_arch_cross(x))

static inline int is_kdump_kernel(void)
{
	return (elfcorehdr_addr != ELFCORE_ADDR_MAX) ? 1 : 0;
}
#else /* !CONFIG_CRASH_DUMP */
static inline int is_kdump_kernel(void) { return 0; }
#endif /* CONFIG_CRASH_DUMP */

extern unsigned long saved_max_pfn;
#endif /* LINUX_CRASHDUMP_H */
