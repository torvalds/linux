#ifndef LINUX_KEXEC_H
#define LINUX_KEXEC_H

#define IND_DESTINATION_BIT 0
#define IND_INDIRECTION_BIT 1
#define IND_DONE_BIT        2
#define IND_SOURCE_BIT      3

#define IND_DESTINATION  (1 << IND_DESTINATION_BIT)
#define IND_INDIRECTION  (1 << IND_INDIRECTION_BIT)
#define IND_DONE         (1 << IND_DONE_BIT)
#define IND_SOURCE       (1 << IND_SOURCE_BIT)
#define IND_FLAGS (IND_DESTINATION | IND_INDIRECTION | IND_DONE | IND_SOURCE)

#if !defined(__ASSEMBLY__)

#include <asm/io.h>

#include <uapi/linux/kexec.h>

#ifdef CONFIG_KEXEC_CORE
#include <linux/list.h>
#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/ioport.h>
#include <linux/elfcore.h>
#include <linux/elf.h>
#include <linux/module.h>
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

#ifndef KEXEC_CONTROL_MEMORY_GFP
#define KEXEC_CONTROL_MEMORY_GFP (GFP_KERNEL | __GFP_NORETRY)
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

struct kexec_segment {
	/*
	 * This pointer can point to user memory if kexec_load() system
	 * call is used or will point to kernel memory if
	 * kexec_file_load() system call is used.
	 *
	 * Use ->buf when expecting to deal with user memory and use ->kbuf
	 * when expecting to deal with kernel memory.
	 */
	union {
		void __user *buf;
		void *kbuf;
	};
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

#ifdef CONFIG_KEXEC_FILE
struct purgatory_info {
	/* Pointer to elf header of read only purgatory */
	Elf_Ehdr *ehdr;

	/* Pointer to purgatory sechdrs which are modifiable */
	Elf_Shdr *sechdrs;
	/*
	 * Temporary buffer location where purgatory is loaded and relocated
	 * This memory can be freed post image load
	 */
	void *purgatory_buf;

	/* Address where purgatory is finally loaded and is executed from */
	unsigned long purgatory_load_addr;
};

typedef int (kexec_probe_t)(const char *kernel_buf, unsigned long kernel_size);
typedef void *(kexec_load_t)(struct kimage *image, char *kernel_buf,
			     unsigned long kernel_len, char *initrd,
			     unsigned long initrd_len, char *cmdline,
			     unsigned long cmdline_len);
typedef int (kexec_cleanup_t)(void *loader_data);

#ifdef CONFIG_KEXEC_VERIFY_SIG
typedef int (kexec_verify_sig_t)(const char *kernel_buf,
				 unsigned long kernel_len);
#endif

struct kexec_file_ops {
	kexec_probe_t *probe;
	kexec_load_t *load;
	kexec_cleanup_t *cleanup;
#ifdef CONFIG_KEXEC_VERIFY_SIG
	kexec_verify_sig_t *verify_sig;
#endif
};
#endif

struct kimage {
	kimage_entry_t head;
	kimage_entry_t *entry;
	kimage_entry_t *last_entry;

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
	/* If set, we are using file mode kexec syscall */
	unsigned int file_mode:1;

#ifdef ARCH_HAS_KIMAGE_ARCH
	struct kimage_arch arch;
#endif

#ifdef CONFIG_KEXEC_FILE
	/* Additional fields for file based kexec syscall */
	void *kernel_buf;
	unsigned long kernel_buf_len;

	void *initrd_buf;
	unsigned long initrd_buf_len;

	char *cmdline_buf;
	unsigned long cmdline_buf_len;

	/* File operations provided by image loader */
	struct kexec_file_ops *fops;

	/* Image loader handling the kernel can store a pointer here */
	void *image_loader_data;

	/* Information for loading purgatory */
	struct purgatory_info purgatory_info;
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
extern int kexec_add_buffer(struct kimage *image, char *buffer,
			    unsigned long bufsz, unsigned long memsz,
			    unsigned long buf_align, unsigned long buf_min,
			    unsigned long buf_max, bool top_down,
			    unsigned long *load_addr);
extern struct page *kimage_alloc_control_pages(struct kimage *image,
						unsigned int order);
extern int kexec_load_purgatory(struct kimage *image, unsigned long min,
				unsigned long max, int top_down,
				unsigned long *load_addr);
extern int kexec_purgatory_get_set_symbol(struct kimage *image,
					  const char *name, void *buf,
					  unsigned int size, bool get_value);
extern void *kexec_purgatory_get_symbol_addr(struct kimage *image,
					     const char *name);
extern void __crash_kexec(struct pt_regs *);
extern void crash_kexec(struct pt_regs *);
int kexec_should_crash(struct task_struct *);
int kexec_crash_loaded(void);
void crash_save_cpu(struct pt_regs *regs, int cpu);
void crash_save_vmcoreinfo(void);
void arch_crash_save_vmcoreinfo(void);
__printf(1, 2)
void vmcoreinfo_append_str(const char *fmt, ...);
phys_addr_t paddr_vmcoreinfo_note(void);

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
#define VMCOREINFO_PAGE_OFFSET(value) \
	vmcoreinfo_append_str("PAGE_OFFSET=%lx\n", (unsigned long)value)
#define VMCOREINFO_VMALLOC_START(value) \
	vmcoreinfo_append_str("VMALLOC_START=%lx\n", (unsigned long)value)
#define VMCOREINFO_VMEMMAP_START(value) \
	vmcoreinfo_append_str("VMEMMAP_START=%lx\n", (unsigned long)value)

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

/* List of defined/legal kexec file flags */
#define KEXEC_FILE_FLAGS	(KEXEC_FILE_UNLOAD | KEXEC_FILE_ON_CRASH | \
				 KEXEC_FILE_NO_INITRAMFS)

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

int __weak arch_kexec_kernel_image_probe(struct kimage *image, void *buf,
					 unsigned long buf_len);
void * __weak arch_kexec_kernel_image_load(struct kimage *image);
int __weak arch_kimage_file_post_load_cleanup(struct kimage *image);
int __weak arch_kexec_kernel_verify_sig(struct kimage *image, void *buf,
					unsigned long buf_len);
int __weak arch_kexec_apply_relocations_add(const Elf_Ehdr *ehdr,
					Elf_Shdr *sechdrs, unsigned int relsec);
int __weak arch_kexec_apply_relocations(const Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
					unsigned int relsec);
void arch_kexec_protect_crashkres(void);
void arch_kexec_unprotect_crashkres(void);

#ifndef page_to_boot_pfn
static inline unsigned long page_to_boot_pfn(struct page *page)
{
	return page_to_pfn(page);
}
#endif

#ifndef boot_pfn_to_page
static inline struct page *boot_pfn_to_page(unsigned long boot_pfn)
{
	return pfn_to_page(boot_pfn);
}
#endif

#ifndef phys_to_boot_phys
static inline unsigned long phys_to_boot_phys(phys_addr_t phys)
{
	return phys;
}
#endif

#ifndef boot_phys_to_phys
static inline phys_addr_t boot_phys_to_phys(unsigned long boot_phys)
{
	return boot_phys;
}
#endif

static inline unsigned long virt_to_boot_phys(void *addr)
{
	return phys_to_boot_phys(__pa((unsigned long)addr));
}

static inline void *boot_phys_to_virt(unsigned long entry)
{
	return phys_to_virt(boot_phys_to_phys(entry));
}

#else /* !CONFIG_KEXEC_CORE */
struct pt_regs;
struct task_struct;
static inline void __crash_kexec(struct pt_regs *regs) { }
static inline void crash_kexec(struct pt_regs *regs) { }
static inline int kexec_should_crash(struct task_struct *p) { return 0; }
static inline int kexec_crash_loaded(void) { return 0; }
#define kexec_in_progress false
#endif /* CONFIG_KEXEC_CORE */

#endif /* !defined(__ASSEBMLY__) */

#endif /* LINUX_KEXEC_H */
