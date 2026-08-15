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
 * __klp_relocs.<objname> are intermediate sections which are created by klp
 * diff and converted into KLP symbols/relas by "objtool klp post-link".  This
 * is needed to work around the linker, which doesn't preserve SHN_LIVEPATCH or
 * SHF_RELA_LIVEPATCH, nor does it support having two RELA sections for a
 * single PROGBITS section.
 *
 * "objname" is the object whose loading gates the relocation: "vmlinux" for
 * references to vmlinux symbols, otherwise the name of the module being
 * patched.  post-link uses it to name the resulting
 * .klp.rela.objname.section_name sections.
 */
#define KLP_RELOCS_SEC	"__klp_relocs"
#define KLP_STRINGS_SEC	".rodata.klp.str1.1"

#define KLP_TOMBSTONE_PREFIX	".klp.tombstone."

struct klp_reloc {
	void *offset;
	void *sym;
	u32 type;
};

/*
 * .klp.symid is used to correlate symbols between vmlinux.o and vmlinux, for
 * calculating sympos to disambiguate duplicately-named symbols.
 */
#define KLP_SYMID_SEC	".klp.symid"

struct klp_symid {
	u64 id;
	u64 addr;
};

struct objtool_file;
struct elf;
struct symbol;

int klp_create_symid_sections(struct objtool_file *file);

int klp_sympos_init(struct elf *orig);
unsigned long klp_find_sympos(struct elf *elf, struct symbol *sym);

int cmd_klp_checksum(int argc, const char **argv);
int cmd_klp_diff(int argc, const char **argv);
int cmd_klp_post_link(int argc, const char **argv);

#endif /* _OBJTOOL_KLP_H */
