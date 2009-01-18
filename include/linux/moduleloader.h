#ifndef _LINUX_MODULELOADER_H
#define _LINUX_MODULELOADER_H
/* The stuff needed for archs to support modules. */

#include <linux/module.h>
#include <linux/elf.h>

/* These must be implemented by the specific architecture */

/* Adjust arch-specific sections.  Return 0 on success.  */
int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod);

/* Additional bytes needed by arch in front of individual sections */
unsigned int arch_mod_section_prepend(struct module *mod, unsigned int section);

/* Allocator used for allocating struct module, core sections and init
   sections.  Returns NULL on failure. */
void *module_alloc(unsigned long size);

/* Free memory returned from module_alloc. */
void module_free(struct module *mod, void *module_region);

/* Apply the given relocation to the (simplified) ELF.  Return -error
   or 0. */
int apply_relocate(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *mod);

/* Apply the given add relocation to the (simplified) ELF.  Return
   -error or 0 */
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *mod);

/* Any final processing of module before access.  Return -error or 0. */
int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *mod);

/* Any cleanup needed when module leaves. */
void module_arch_cleanup(struct module *mod);

#endif
