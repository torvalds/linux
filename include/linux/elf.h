/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H

#include <linux/types.h>
#include <asm/elf.h>
#include <uapi/linux/elf.h>

#ifndef elf_read_implies_exec
  /* Executables for which elf_read_implies_exec() returns TRUE will
     have the READ_IMPLIES_EXEC personality flag set automatically.
     Override in asm/elf.h as needed.  */
# define elf_read_implies_exec(ex, have_pt_gnu_stack)	0
#endif
#ifndef SET_PERSONALITY
#define SET_PERSONALITY(ex) \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK)))
#endif

#ifndef SET_PERSONALITY2
#define SET_PERSONALITY2(ex, state) \
	SET_PERSONALITY(ex)
#endif

#ifndef START_THREAD
#define START_THREAD(elf_ex, regs, elf_entry, start_stack)	\
	start_thread(regs, elf_entry, start_stack)
#endif

#if defined(ARCH_HAS_SETUP_ADDITIONAL_PAGES) && !defined(ARCH_SETUP_ADDITIONAL_PAGES)
#define ARCH_SETUP_ADDITIONAL_PAGES(bprm, ex, interpreter) \
	arch_setup_additional_pages(bprm, interpreter)
#endif

#define ELF32_GNU_PROPERTY_ALIGN	4
#define ELF64_GNU_PROPERTY_ALIGN	8

#if ELF_CLASS == ELFCLASS32

extern Elf32_Dyn _DYNAMIC [];
#define elfhdr		elf32_hdr
#define elf_phdr	elf32_phdr
#define elf_shdr	elf32_shdr
#define elf_note	elf32_note
#define elf_addr_t	Elf32_Off
#define Elf_Half	Elf32_Half
#define Elf_Word	Elf32_Word
#define ELF_GNU_PROPERTY_ALIGN	ELF32_GNU_PROPERTY_ALIGN

#else

extern Elf64_Dyn _DYNAMIC [];
#define elfhdr		elf64_hdr
#define elf_phdr	elf64_phdr
#define elf_shdr	elf64_shdr
#define elf_note	elf64_note
#define elf_addr_t	Elf64_Off
#define Elf_Half	Elf64_Half
#define Elf_Word	Elf64_Word
#define ELF_GNU_PROPERTY_ALIGN	ELF64_GNU_PROPERTY_ALIGN

#endif

/* Optional callbacks to write extra ELF notes. */
struct file;
struct coredump_params;

#ifndef CONFIG_ARCH_HAVE_EXTRA_ELF_NOTES
static inline int elf_coredump_extra_notes_size(void) { return 0; }
static inline int elf_coredump_extra_notes_write(struct coredump_params *cprm) { return 0; }
#else
extern int elf_coredump_extra_notes_size(void);
extern int elf_coredump_extra_notes_write(struct coredump_params *cprm);
#endif

/*
 * NT_GNU_PROPERTY_TYPE_0 header:
 * Keep this internal until/unless there is an agreed UAPI definition.
 * pr_type values (GNU_PROPERTY_*) are public and defined in the UAPI header.
 */
struct gnu_property {
	u32 pr_type;
	u32 pr_datasz;
};

struct arch_elf_state;

#ifndef CONFIG_ARCH_USE_GNU_PROPERTY
static inline int arch_parse_elf_property(u32 type, const void *data,
					  size_t datasz, bool compat,
					  struct arch_elf_state *arch)
{
	return 0;
}
#else
extern int arch_parse_elf_property(u32 type, const void *data, size_t datasz,
				   bool compat, struct arch_elf_state *arch);
#endif

#ifdef CONFIG_ARCH_HAVE_ELF_PROT
int arch_elf_adjust_prot(int prot, const struct arch_elf_state *state,
			 bool has_interp, bool is_interp);
#else
static inline int arch_elf_adjust_prot(int prot,
				       const struct arch_elf_state *state,
				       bool has_interp, bool is_interp)
{
	return prot;
}
#endif

#endif /* _LINUX_ELF_H */
