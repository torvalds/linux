/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_KLP_H
#define _OBJTOOL_KLP_H

#define SHF_RELA_LIVEPATCH	0x00100000
#define SHN_LIVEPATCH		0xff20

/*
 * .init.klp_objects and .init.klp_funcs are created by klp diff and used by the
 * patch module init code to build the klp_patch, klp_object and klp_func
 * structs needed by the livepatch API.
 */
#define KLP_OBJECTS_SEC	".init.klp_objects"
#define KLP_FUNCS_SEC	".init.klp_funcs"

/*
 * __klp_relocs is an intermediate section which are created by klp diff and
 * converted into KLP symbols/relas by "objtool klp post-link".  This is needed
 * to work around the linker, which doesn't preserve SHN_LIVEPATCH or
 * SHF_RELA_LIVEPATCH, nor does it support having two RELA sections for a
 * single PROGBITS section.
 */
#define KLP_RELOCS_SEC	"__klp_relocs"
#define KLP_STRINGS_SEC	".rodata.klp.str1.1"

struct klp_reloc {
	void *offset;
	void *sym;
	u32 type;
};

int cmd_klp_diff(int argc, const char **argv);
int cmd_klp_post_link(int argc, const char **argv);

#endif /* _OBJTOOL_KLP_H */
