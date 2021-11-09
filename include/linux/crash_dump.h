/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#include <linux/kexec.h>
#include <linux/proc_fs.h>
#include <linux/elf.h>
#include <linux/pgtable.h>
#include <uapi/linux/vmcore.h>

#include <linux/pgtable.h> /* for pgprot_t */

/* For IS_ENABLED(CONFIG_CRASH_DUMP) */
#define ELFCORE_ADDR_MAX	(-1ULL)
#define ELFCORE_ADDR_ERR	(-2ULL)

extern unsigned long long elfcorehdr_addr;
extern unsigned long long elfcorehdr_size;

#ifdef CONFIG_CRASH_DUMP
extern int elfcorehdr_alloc(unsigned long long *addr, unsigned long long *size);
extern void elfcorehdr_free(unsigned long long addr);
extern ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos);
extern ssize_t elfcorehdr_read_notes(char *buf, size_t count, u64 *ppos);
extern int remap_oldmem_pfn_range(struct vm_area_struct *vma,
				  unsigned long from, unsigned long pfn,
				  unsigned long size, pgprot_t prot);

extern ssize_t copy_oldmem_page(unsigned long, char *, size_t,
						unsigned long, int);
extern ssize_t copy_oldmem_page_encrypted(unsigned long pfn, char *buf,
					  size_t csize, unsigned long offset,
					  int userbuf);

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
 * return true if CONFIG_CRASH_DUMP=y and if kernel is booting after a panic
 * of previous kernel.
 */

static inline bool is_kdump_kernel(void)
{
	return elfcorehdr_addr != ELFCORE_ADDR_MAX;
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

/**
 * struct vmcore_cb - driver callbacks for /proc/vmcore handling
 * @pfn_is_ram: check whether a PFN really is RAM and should be accessed when
 *              reading the vmcore. Will return "true" if it is RAM or if the
 *              callback cannot tell. If any callback returns "false", it's not
 *              RAM and the page must not be accessed; zeroes should be
 *              indicated in the vmcore instead. For example, a ballooned page
 *              contains no data and reading from such a page will cause high
 *              load in the hypervisor.
 * @next: List head to manage registered callbacks internally; initialized by
 *        register_vmcore_cb().
 *
 * vmcore callbacks allow drivers managing physical memory ranges to
 * coordinate with vmcore handling code, for example, to prevent accessing
 * physical memory ranges that should not be accessed when reading the vmcore,
 * although included in the vmcore header as memory ranges to dump.
 */
struct vmcore_cb {
	bool (*pfn_is_ram)(struct vmcore_cb *cb, unsigned long pfn);
	struct list_head next;
};
extern void register_vmcore_cb(struct vmcore_cb *cb);
extern void unregister_vmcore_cb(struct vmcore_cb *cb);

#else /* !CONFIG_CRASH_DUMP */
static inline bool is_kdump_kernel(void) { return 0; }
#endif /* CONFIG_CRASH_DUMP */

/* Device Dump information to be filled by drivers */
struct vmcoredd_data {
	char dump_name[VMCOREDD_MAX_NAME_BYTES]; /* Unique name of the dump */
	unsigned int size;                       /* Size of the dump */
	/* Driver's registered callback to be invoked to collect dump */
	int (*vmcoredd_callback)(struct vmcoredd_data *data, void *buf);
};

#ifdef CONFIG_PROC_VMCORE_DEVICE_DUMP
int vmcore_add_device_dump(struct vmcoredd_data *data);
#else
static inline int vmcore_add_device_dump(struct vmcoredd_data *data)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_PROC_VMCORE_DEVICE_DUMP */

#ifdef CONFIG_PROC_VMCORE
ssize_t read_from_oldmem(char *buf, size_t count,
			 u64 *ppos, int userbuf,
			 bool encrypted);
#else
static inline ssize_t read_from_oldmem(char *buf, size_t count,
				       u64 *ppos, int userbuf,
				       bool encrypted)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_PROC_VMCORE */

#endif /* LINUX_CRASHDUMP_H */
