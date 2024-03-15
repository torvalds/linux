/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_VMCORE_INFO_H
#define LINUX_VMCORE_INFO_H

#include <linux/linkage.h>
#include <linux/elfcore.h>
#include <linux/elf.h>

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
#endif /* LINUX_VMCORE_INFO_H */
