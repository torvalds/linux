// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Read the intermediate KLP reloc/symbol representations created by klp diff
 * and convert them to the proper format required by livepatch.  This needs to
 * run last to avoid linker wreckage.  Linkers don't tend to handle the "two
 * rela sections for a single base section" case very well, nor do they like
 * SHN_LIVEPATCH.
 *
 * This is the final tool in the livepatch module generation pipeline:
 *
 *   kernel builds -> objtool klp diff -> module link -> objtool klp post-link
 */

#include <fcntl.h>
#include <gelf.h>
#include <objtool/objtool.h>
#include <objtool/warn.h>
#include <objtool/klp.h>
#include <objtool/util.h>
#include <linux/livepatch_external.h>

static int fix_klp_relocs(struct elf *elf)
{
	struct section *symtab, *klp_relocs;

	klp_relocs = find_section_by_name(elf, KLP_RELOCS_SEC);
	if (!klp_relocs)
		return 0;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		ERROR("missing .symtab");
		return -1;
	}

	for (int i = 0; i < sec_size(klp_relocs) / sizeof(struct klp_reloc); i++) {
		struct klp_reloc *klp_reloc;
		unsigned long klp_reloc_off;
		struct section *sec, *tmp, *klp_rsec;
		unsigned long offset;
		struct reloc *reloc;
		char sym_modname[64];
		char rsec_name[SEC_NAME_LEN];
		u64 addend;
		struct symbol *sym, *klp_sym;

		klp_reloc_off = i * sizeof(*klp_reloc);
		klp_reloc = klp_relocs->data->d_buf + klp_reloc_off;

		/*
		 * Read __klp_relocs[i]:
		 */

		/* klp_reloc.sec_offset */
		reloc = find_reloc_by_dest(elf, klp_relocs,
					   klp_reloc_off + offsetof(struct klp_reloc, offset));
		if (!reloc) {
			ERROR("malformed " KLP_RELOCS_SEC " section");
			return -1;
		}

		sec = reloc->sym->sec;
		offset = reloc_addend(reloc);

		/* klp_reloc.sym */
		reloc = find_reloc_by_dest(elf, klp_relocs,
					   klp_reloc_off + offsetof(struct klp_reloc, sym));
		if (!reloc) {
			ERROR("malformed " KLP_RELOCS_SEC " section");
			return -1;
		}

		klp_sym = reloc->sym;
		addend = reloc_addend(reloc);

		/* symbol format: .klp.sym.modname.sym_name,sympos */
		if (sscanf(klp_sym->name + strlen(KLP_SYM_PREFIX), "%55[^.]", sym_modname) != 1)
			ERROR("can't find modname in klp symbol '%s'", klp_sym->name);

		/*
		 * Create the KLP rela:
		 */

		/* section format: .klp.rela.sec_objname.section_name */
		if (snprintf_check(rsec_name, SEC_NAME_LEN,
				   KLP_RELOC_SEC_PREFIX "%s.%s",
				   sym_modname, sec->name))
			return -1;

		klp_rsec = find_section_by_name(elf, rsec_name);
		if (!klp_rsec) {
			klp_rsec = elf_create_section(elf, rsec_name, 0,
						      elf_rela_size(elf),
						      SHT_RELA, elf_addr_size(elf),
						      SHF_ALLOC | SHF_INFO_LINK | SHF_RELA_LIVEPATCH);
			if (!klp_rsec)
				return -1;

			klp_rsec->sh.sh_link = symtab->idx;
			klp_rsec->sh.sh_info = sec->idx;
			klp_rsec->base = sec;
		}

		tmp = sec->rsec;
		sec->rsec = klp_rsec;
		if (!elf_create_reloc(elf, sec, offset, klp_sym, addend, klp_reloc->type))
			return -1;
		sec->rsec = tmp;

		/*
		 * Fix up the corresponding KLP symbol:
		 */

		klp_sym->sym.st_shndx = SHN_LIVEPATCH;
		if (!gelf_update_sym(symtab->data, klp_sym->idx, &klp_sym->sym)) {
			ERROR_ELF("gelf_update_sym");
			return -1;
		}

		/*
		 * Disable the original non-KLP reloc by converting it to R_*_NONE:
		 */

		reloc = find_reloc_by_dest(elf, sec, offset);
		sym = reloc->sym;
		sym->sym.st_shndx = SHN_LIVEPATCH;
		set_reloc_type(elf, reloc, 0);
		if (!gelf_update_sym(symtab->data, sym->idx, &sym->sym)) {
			ERROR_ELF("gelf_update_sym");
			return -1;
		}
	}

	return 0;
}

/*
 * This runs on the livepatch module after all other linking has been done.  It
 * converts the intermediate __klp_relocs section into proper KLP relocs to be
 * processed by livepatch.  This needs to run last to avoid linker wreckage.
 * Linkers don't tend to handle the "two rela sections for a single base
 * section" case very well, nor do they appreciate SHN_LIVEPATCH.
 */
int cmd_klp_post_link(int argc, const char **argv)
{
	struct elf *elf;

	argc--;
	argv++;

	if (argc != 1) {
		fprintf(stderr, "%d\n", argc);
		fprintf(stderr, "usage: objtool link <file.ko>\n");
		return -1;
	}

	elf = elf_open_read(argv[0], O_RDWR);
	if (!elf)
		return -1;

	if (fix_klp_relocs(elf))
		return -1;

	if (elf_write(elf))
		return -1;

	return elf_close(elf);
}
