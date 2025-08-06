/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_CORE_H
#define LINUX_CRASH_CORE_H

#include <linux/linkage.h>
#include <linux/elfcore.h>
#include <linux/elf.h>

struct kimage;

struct crash_mem {
	unsigned int max_nr_ranges;
	unsigned int nr_ranges;
	struct range ranges[] __counted_by(max_nr_ranges);
};

#ifdef CONFIG_CRASH_DUMP

int crash_shrink_memory(unsigned long new_size);
ssize_t crash_get_memory_size(void);

#ifndef arch_kexec_protect_crashkres
/*
 * Protection mechanism for crashkernel reserved memory after
 * the kdump kernel is loaded.
 *
 * Provide an empty default implementation here -- architecture
 * code may override this
 */
static inline void arch_kexec_protect_crashkres(void) { }
#endif

#ifndef arch_kexec_unprotect_crashkres
static inline void arch_kexec_unprotect_crashkres(void) { }
#endif

#ifdef CONFIG_CRASH_DM_CRYPT
int crash_load_dm_crypt_keys(struct kimage *image);
ssize_t dm_crypt_keys_read(char *buf, size_t count, u64 *ppos);
#else
static inline int crash_load_dm_crypt_keys(struct kimage *image) {return 0; }
#endif

#ifndef arch_crash_handle_hotplug_event
static inline void arch_crash_handle_hotplug_event(struct kimage *image, void *arg) { }
#endif

int crash_check_hotplug_support(void);

#ifndef arch_crash_hotplug_support
static inline int arch_crash_hotplug_support(struct kimage *image, unsigned long kexec_flags)
{
	return 0;
}
#endif

#ifndef crash_get_elfcorehdr_size
static inline unsigned int crash_get_elfcorehdr_size(void) { return 0; }
#endif

/* Alignment required for elf header segment */
#define ELF_CORE_HEADER_ALIGN   4096

extern int crash_exclude_mem_range(struct crash_mem *mem,
				   unsigned long long mstart,
				   unsigned long long mend);
extern int crash_prepare_elf64_headers(struct crash_mem *mem, int need_kernel_map,
				       void **addr, unsigned long *sz);

struct kimage;
struct kexec_segment;

#define KEXEC_CRASH_HP_NONE			0
#define KEXEC_CRASH_HP_ADD_CPU			1
#define KEXEC_CRASH_HP_REMOVE_CPU		2
#define KEXEC_CRASH_HP_ADD_MEMORY		3
#define KEXEC_CRASH_HP_REMOVE_MEMORY		4
#define KEXEC_CRASH_HP_INVALID_CPU		-1U

extern void __crash_kexec(struct pt_regs *regs);
extern void crash_kexec(struct pt_regs *regs);
int kexec_should_crash(struct task_struct *p);
int kexec_crash_loaded(void);
void crash_save_cpu(struct pt_regs *regs, int cpu);
extern int kimage_crash_copy_vmcoreinfo(struct kimage *image);

#else /* !CONFIG_CRASH_DUMP*/
struct pt_regs;
struct task_struct;
struct kimage;
static inline void __crash_kexec(struct pt_regs *regs) { }
static inline void crash_kexec(struct pt_regs *regs) { }
static inline int kexec_should_crash(struct task_struct *p) { return 0; }
static inline int kexec_crash_loaded(void) { return 0; }
static inline void crash_save_cpu(struct pt_regs *regs, int cpu) {};
static inline int kimage_crash_copy_vmcoreinfo(struct kimage *image) { return 0; };
#endif /* CONFIG_CRASH_DUMP*/

#endif /* LINUX_CRASH_CORE_H */
