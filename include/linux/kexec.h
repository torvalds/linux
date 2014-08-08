#ifndef LINUX_KEXEC_H
#define LINUX_KEXEC_H

#include <uapi/linux/kexec.h>

#ifdef CONFIG_KEXEC
#include <linux/list.h>
#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/ioport.h>
#include <linux/elfcore.h>
#include <linux/elf.h>
#include <asm/kexec.h>

/* Verify architecture specific macros are defined */

#ifndef KEXEC_SOURCE_MEMORY_LIMIT
#error KEXEC_SOURCE_MEMORY_LIMIT not defined
#endif

#ifndef KEXEC_DESTINATION_MEMORY_LIMIT
#error KEXEC_DESTINATION_MEMORY_LIMIT not defined
#endif

#ifndef KEXEC_CONTROL_MEMORY_LIMIT
#error KEXEC_CONTROL_MEMORY_LIMIT not defined
#endif

#ifndef KEXEC_CONTROL_PAGE_SIZE
#error KEXEC_CONTROL_PAGE_SIZE not defined
#endif

#ifndef KEXEC_ARCH
#error KEXEC_ARCH not defined
#endif

#ifndef KEXEC_CRASH_CONTROL_MEMORY_LIMIT
#define KEXEC_CRASH_CONTROL_MEMORY_LIMIT KEXEC_CONTROL_MEMORY_LIMIT
#endif

#ifndef KEXEC_CRASH_MEM_ALIGN
#define KEXEC_CRASH_MEM_ALIGN PAGE_SIZE
#endif

#define KEXEC_NOTE_HEAD_BYTES ALIGN(sizeof(struct elf_note), 4)
#define KEXEC_CORE_NOTE_NAME "CORE"
#define KEXEC_CORE_NOTE_NAME_BYTES ALIGN(sizeof(KEXEC_CORE_NOTE_NAME), 4)
#define KEXEC_CORE_NOTE_DESC_BYTES ALIGN(sizeof(struct elf_prstatus), 4)
/*
 * The per-cpu notes area is a list of notes terminated by a "NULL"
 * note header.  For kdump, the code in vmcore.c runs in the context
 * of the second kernel to combine them into one note.
 */
#ifndef KEXEC_NOTE_BYTES
#define KEXEC_NOTE_BYTES ( (KEXEC_NOTE_HEAD_BYTES * 2) +		\
			    KEXEC_CORE_NOTE_NAME_BYTES +		\
			    KEXEC_CORE_NOTE_DESC_BYTES )
#endif

/*
 * This structure is used to hold the arguments that are used when loading
 * kernel binaries.
 */

typedef unsigned long kimage_entry_t;
#define IND_DESTINATION  0x1
#define IND_INDIRECTION  0x2
#define IND_DONE         0x4
#define IND_SOURCE       0x8

struct kexec_segment {
	void __user *buf;
	size_t bufsz;
	unsigned long mem;
	size_t memsz;
};

#ifdef CONFIG_COMPAT
struct compat_kexec_segment {
	compat_uptr_t buf;
	compat_size_t bufsz;
	compat_ulong_t mem;	/* User space sees this as a (void *) ... */
	compat_size_t memsz;
};
#endif

struct kimage {
	kimage_entry_t head;
	kimage_entry_t *entry;
	kimage_entry_t *last_entry;

	unsigned long destination;

	unsigned long start;
	struct page *control_code_page;
	struct page *swap_page;

	unsigned long nr_segments;
	struct kexec_segment segment[KEXEC_SEGMENT_MAX];

	struct list_head control_pages;
	struct list_head dest_pages;
	struct list_head unusable_pages;

	/* Address of next control page to allocate for crash kernels. */
	unsigned long control_page;

	/* Flags to indicate special processing */
	unsigned int type : 1;
#define KEXEC_TYPE_DEFAULT 0
#define KEXEC_TYPE_CRASH   1
	unsigned int preserve_context : 1;

#ifdef ARCH_HAS_KIMAGE_ARCH
	struct kimage_arch arch;
#endif
};



/* kexec interface functions */
extern void machine_kexec(struct kimage *image);
extern int machine_kexec_prepare(struct kimage *image);
extern void machine_kexec_cleanup(struct kimage *image);
extern asmlinkage long sys_kexec_load(unsigned long entry,
					unsigned long nr_segments,
					struct kexec_segment __user *segments,
					unsigned long flags);
extern int kernel_kexec(void);
extern struct page *kimage_alloc_control_pages(struct kimage *image,
						unsigned int order);
extern void crash_kexec(struct pt_regs *);
int kexec_should_crash(struct task_struct *);
void crash_save_cpu(struct pt_regs *regs, int cpu);
void crash_save_vmcoreinfo(void);
void crash_map_reserved_pages(void);
void crash_unmap_reserved_pages(void);
void arch_crash_save_vmcoreinfo(void);
__printf(1, 2)
void vmcoreinfo_append_str(const char *fmt, ...);
unsigned long paddr_vmcoreinfo_note(void);

#define VMCOREINFO_OSRELEASE(value) \
	vmcoreinfo_append_str("OSRELEASE=%s\n", value)
#define VMCOREINFO_PAGESIZE(value) \
	vmcoreinfo_append_str("PAGESIZE=%ld\n", value)
#define VMCOREINFO_SYMBOL(name) \
	vmcoreinfo_append_str("SYMBOL(%s)=%lx\n", #name, (unsigned long)&name)
#define VMCOREINFO_SIZE(name) \
	vmcoreinfo_append_str("SIZE(%s)=%lu\n", #name, \
			      (unsigned long)sizeof(name))
#define VMCOREINFO_STRUCT_SIZE(name) \
	vmcoreinfo_append_str("SIZE(%s)=%lu\n", #name, \
			      (unsigned long)sizeof(struct name))
#define VMCOREINFO_OFFSET(name, field) \
	vmcoreinfo_append_str("OFFSET(%s.%s)=%lu\n", #name, #field, \
			      (unsigned long)offsetof(struct name, field))
#define VMCOREINFO_LENGTH(name, value) \
	vmcoreinfo_append_str("LENGTH(%s)=%lu\n", #name, (unsigned long)value)
#define VMCOREINFO_NUMBER(name) \
	vmcoreinfo_append_str("NUMBER(%s)=%ld\n", #name, (long)name)
#define VMCOREINFO_CONFIG(name) \
	vmcoreinfo_append_str("CONFIG_%s=y\n", #name)

extern struct kimage *kexec_image;
extern struct kimage *kexec_crash_image;
extern int kexec_load_disabled;

#ifndef kexec_flush_icache_page
#define kexec_flush_icache_page(page)
#endif

/* List of defined/legal kexec flags */
#ifndef CONFIG_KEXEC_JUMP
#define KEXEC_FLAGS    KEXEC_ON_CRASH
#else
#define KEXEC_FLAGS    (KEXEC_ON_CRASH | KEXEC_PRESERVE_CONTEXT)
#endif

#define VMCOREINFO_BYTES           (4096)
#define VMCOREINFO_NOTE_NAME       "VMCOREINFO"
#define VMCOREINFO_NOTE_NAME_BYTES ALIGN(sizeof(VMCOREINFO_NOTE_NAME), 4)
#define VMCOREINFO_NOTE_SIZE       (KEXEC_NOTE_HEAD_BYTES*2 + VMCOREINFO_BYTES \
				    + VMCOREINFO_NOTE_NAME_BYTES)

/* Location of a reserved region to hold the crash kernel.
 */
extern struct resource crashk_res;
extern struct resource crashk_low_res;
typedef u32 note_buf_t[KEXEC_NOTE_BYTES/4];
extern note_buf_t __percpu *crash_notes;
extern u32 vmcoreinfo_note[VMCOREINFO_NOTE_SIZE/4];
extern size_t vmcoreinfo_size;
extern size_t vmcoreinfo_max_size;

/* flag to track if kexec reboot is in progress */
extern bool kexec_in_progress;

int __init parse_crashkernel(char *cmdline, unsigned long long system_ram,
		unsigned long long *crash_size, unsigned long long *crash_base);
int parse_crashkernel_high(char *cmdline, unsigned long long system_ram,
		unsigned long long *crash_size, unsigned long long *crash_base);
int parse_crashkernel_low(char *cmdline, unsigned long long system_ram,
		unsigned long long *crash_size, unsigned long long *crash_base);
int crash_shrink_memory(unsigned long new_size);
size_t crash_get_memory_size(void);
void crash_free_reserved_phys_range(unsigned long begin, unsigned long end);

#else /* !CONFIG_KEXEC */
struct pt_regs;
struct task_struct;
static inline void crash_kexec(struct pt_regs *regs) { }
static inline int kexec_should_crash(struct task_struct *p) { return 0; }
#endif /* CONFIG_KEXEC */
#endif /* LINUX_KEXEC_H */
