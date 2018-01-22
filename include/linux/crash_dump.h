/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#ifdef CONFIG_CRASH_DUMP
#include <linux/kexec.h>
#include <linux/proc_fs.h>
#include <linux/elf.h>

#include <asm/pgtable.h> /* for pgprot_t */

#define ELFCORE_ADDR_MAX	(-1ULL)
#define ELFCORE_ADDR_ERR	(-2ULL)

extern unsigned long long elfcorehdr_addr;
extern unsigned long long elfcorehdr_size;

extern int elfcorehdr_alloc(unsigned long long *addr, unsigned long long *size);
extern void elfcorehdr_free(unsigned long long addr);
extern ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos);
extern ssize_t elfcorehdr_read_notes(char *buf, size_t count, u64 *ppos);
extern int remap_oldmem_pfn_range(struct vm_area_struct *vma,
				  unsigned long from, unsigned long pfn,
				  unsigned long size, pgprot_t prot);

extern ssize_t copy_oldmem_page(unsigned long, char *, size_t,
						unsigned long, int);
void vmcore_cleanup(void);

/* Architecture code defines this if there are other possible ELF
 * machine types, e.g. on bi-arch capable hardware. */
#ifndef vmcore_elf_check_arch_cross
#define vmcore_elf_check_arch_cross(x) 0
#endif

/*
 * Architecture code can redefine this if there are any special checks
 * needed for 32-bit ELF or 64-bit ELF vmcores.  In case of 32-bit
 * only architecture, vmcore_elf64_check_arch can be set to zero.
 */
#ifndef vmcore_elf32_check_arch
#define vmcore_elf32_check_arch(x) elf_check_arch(x)
#endif

#ifndef vmcore_elf64_check_arch
#define vmcore_elf64_check_arch(x) (elf_check_arch(x) || vmcore_elf_check_arch_cross(x))
#endif

/*
 * is_kdump_kernel() checks whether this kernel is booting after a panic of
 * previous kernel or not. This is determined by checking if previous kernel
 * has passed the elf core header address on command line.
 *
 * This is not just a test if CONFIG_CRASH_DUMP is enabled or not. It will
 * return 1 if CONFIG_CRASH_DUMP=y and if kernel is booting after a panic of
 * previous kernel.
 */

static inline int is_kdump_kernel(void)
{
	return (elfcorehdr_addr != ELFCORE_ADDR_MAX) ? 1 : 0;
}

/* is_vmcore_usable() checks if the kernel is booting after a panic and
 * the vmcore region is usable.
 *
 * This makes use of the fact that due to alignment -2ULL is not
 * a valid pointer, much in the vain of IS_ERR(), except
 * dealing directly with an unsigned long long rather than a pointer.
 */

static inline int is_vmcore_usable(void)
{
	return is_kdump_kernel() && elfcorehdr_addr != ELFCORE_ADDR_ERR ? 1 : 0;
}

/* vmcore_unusable() marks the vmcore as unusable,
 * without disturbing the logic of is_kdump_kernel()
 */

static inline void vmcore_unusable(void)
{
	if (is_kdump_kernel())
		elfcorehdr_addr = ELFCORE_ADDR_ERR;
}

#define HAVE_OLDMEM_PFN_IS_RAM 1
extern int register_oldmem_pfn_is_ram(int (*fn)(unsigned long pfn));
extern void unregister_oldmem_pfn_is_ram(void);

#else /* !CONFIG_CRASH_DUMP */
static inline int is_kdump_kernel(void) { return 0; }
#endif /* CONFIG_CRASH_DUMP */

extern unsigned long saved_max_pfn;
#endif /* LINUX_CRASHDUMP_H */
