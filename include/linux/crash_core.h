/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_CORE_H
#define LINUX_CRASH_CORE_H

#include <linux/linkage.h>
#include <linux/elfcore.h>
#include <linux/elf.h>
#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
#include <asm/crash_core.h>
#endif

/* Location of a reserved region to hold the crash kernel.
 */
extern struct resource crashk_res;
extern struct resource crashk_low_res;

#define CRASH_CORE_NOTE_NAME	   "CORE"
#define CRASH_CORE_NOTE_HEAD_BYTES ALIGN(sizeof(struct elf_note), 4)
#define CRASH_CORE_NOTE_NAME_BYTES ALIGN(sizeof(CRASH_CORE_NOTE_NAME), 4)
#define CRASH_CORE_NOTE_DESC_BYTES ALIGN(sizeof(struct elf_prstatus), 4)

/*
 * The per-cpu notes area is a list of notes terminated by a "NULL"
 * note header.  For kdump, the code in vmcore.c runs in the context
 * of the second kernel to combine them into one note.
 */
#define CRASH_CORE_NOTE_BYTES	   ((CRASH_CORE_NOTE_HEAD_BYTES * 2) +	\
				     CRASH_CORE_NOTE_NAME_BYTES +	\
				     CRASH_CORE_NOTE_DESC_BYTES)

#define VMCOREINFO_BYTES	   PAGE_SIZE
#define VMCOREINFO_NOTE_NAME	   "VMCOREINFO"
#define VMCOREINFO_NOTE_NAME_BYTES ALIGN(sizeof(VMCOREINFO_NOTE_NAME), 4)
#define VMCOREINFO_NOTE_SIZE	   ((CRASH_CORE_NOTE_HEAD_BYTES * 2) +	\
				     VMCOREINFO_NOTE_NAME_BYTES +	\
				     VMCOREINFO_BYTES)

typedef u32 note_buf_t[CRASH_CORE_NOTE_BYTES/4];
/* Per cpu memory for storing cpu states in case of system crash. */
extern note_buf_t __percpu *crash_notes;

void crash_update_vmcoreinfo_safecopy(void *ptr);
void crash_save_vmcoreinfo(void);
void arch_crash_save_vmcoreinfo(void);
__printf(1, 2)
void vmcoreinfo_append_str(const char *fmt, ...);
phys_addr_t paddr_vmcoreinfo_note(void);

#define VMCOREINFO_OSRELEASE(value) \
	vmcoreinfo_append_str("OSRELEASE=%s\n", value)
#define VMCOREINFO_BUILD_ID()						\
	({								\
		static_assert(sizeof(vmlinux_build_id) == 20);		\
		vmcoreinfo_append_str("BUILD-ID=%20phN\n", vmlinux_build_id); \
	})

#define VMCOREINFO_PAGESIZE(value) \
	vmcoreinfo_append_str("PAGESIZE=%ld\n", value)
#define VMCOREINFO_SYMBOL(name) \
	vmcoreinfo_append_str("SYMBOL(%s)=%lx\n", #name, (unsigned long)&name)
#define VMCOREINFO_SYMBOL_ARRAY(name) \
	vmcoreinfo_append_str("SYMBOL(%s)=%lx\n", #name, (unsigned long)name)
#define VMCOREINFO_SIZE(name) \
	vmcoreinfo_append_str("SIZE(%s)=%lu\n", #name, \
			      (unsigned long)sizeof(name))
#define VMCOREINFO_STRUCT_SIZE(name) \
	vmcoreinfo_append_str("SIZE(%s)=%lu\n", #name, \
			      (unsigned long)sizeof(struct name))
#define VMCOREINFO_OFFSET(name, field) \
	vmcoreinfo_append_str("OFFSET(%s.%s)=%lu\n", #name, #field, \
			      (unsigned long)offsetof(struct name, field))
#define VMCOREINFO_TYPE_OFFSET(name, field) \
	vmcoreinfo_append_str("OFFSET(%s.%s)=%lu\n", #name, #field, \
			      (unsigned long)offsetof(name, field))
#define VMCOREINFO_LENGTH(name, value) \
	vmcoreinfo_append_str("LENGTH(%s)=%lu\n", #name, (unsigned long)value)
#define VMCOREINFO_NUMBER(name) \
	vmcoreinfo_append_str("NUMBER(%s)=%ld\n", #name, (long)name)
#define VMCOREINFO_CONFIG(name) \
	vmcoreinfo_append_str("CONFIG_%s=y\n", #name)

extern unsigned char *vmcoreinfo_data;
extern size_t vmcoreinfo_size;
extern u32 *vmcoreinfo_note;

Elf_Word *append_elf_note(Elf_Word *buf, char *name, unsigned int type,
			  void *data, size_t data_len);
void final_note(Elf_Word *buf);

int __init parse_crashkernel(char *cmdline, unsigned long long system_ram,
		unsigned long long *crash_size, unsigned long long *crash_base,
		unsigned long long *low_size, bool *high);

#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
#ifndef DEFAULT_CRASH_KERNEL_LOW_SIZE
#define DEFAULT_CRASH_KERNEL_LOW_SIZE	(128UL << 20)
#endif
#ifndef CRASH_ALIGN
#define CRASH_ALIGN			SZ_2M
#endif
#ifndef CRASH_ADDR_LOW_MAX
#define CRASH_ADDR_LOW_MAX		SZ_4G
#endif
#ifndef CRASH_ADDR_HIGH_MAX
#define CRASH_ADDR_HIGH_MAX		memblock_end_of_DRAM()
#endif

void __init reserve_crashkernel_generic(char *cmdline,
		unsigned long long crash_size,
		unsigned long long crash_base,
		unsigned long long crash_low_size,
		bool high);
#else
static inline void __init reserve_crashkernel_generic(char *cmdline,
		unsigned long long crash_size,
		unsigned long long crash_base,
		unsigned long long crash_low_size,
		bool high)
{}
#endif

/* Alignment required for elf header segment */
#define ELF_CORE_HEADER_ALIGN   4096

struct crash_mem {
	unsigned int max_nr_ranges;
	unsigned int nr_ranges;
	struct range ranges[] __counted_by(max_nr_ranges);
};

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

#endif /* LINUX_CRASH_CORE_H */
