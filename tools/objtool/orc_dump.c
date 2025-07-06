// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <unistd.h>
#include <asm/orc_types.h>
#include <objtool/objtool.h>
#include <objtool/orc.h>
#include <objtool/warn.h>
#include <objtool/endianness.h>

int orc_dump(const char *filename)
{
	int fd, nr_entries, i, *orc_ip = NULL, orc_size = 0;
	struct orc_entry *orc = NULL;
	char *name;
	size_t nr_sections;
	Elf64_Addr orc_ip_addr = 0;
	size_t shstrtab_idx, strtab_idx = 0;
	Elf *elf;
	Elf_Scn *scn;
	GElf_Shdr sh;
	GElf_Rela rela;
	GElf_Sym sym;
	Elf_Data *data, *symtab = NULL, *rela_orc_ip = NULL;
	struct elf dummy_elf = {};

	elf_version(EV_CURRENT);

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		ERROR_ELF("elf_begin");
		return -1;
	}

	if (!elf64_getehdr(elf)) {
		ERROR_ELF("elf64_getehdr");
		return -1;
	}
	memcpy(&dummy_elf.ehdr, elf64_getehdr(elf), sizeof(dummy_elf.ehdr));

	if (elf_getshdrnum(elf, &nr_sections)) {
		ERROR_ELF("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf, &shstrtab_idx)) {
		ERROR_ELF("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < nr_sections; i++) {
		scn = elf_getscn(elf, i);
		if (!scn) {
			ERROR_ELF("elf_getscn");
			return -1;
		}

		if (!gelf_getshdr(scn, &sh)) {
			ERROR_ELF("gelf_getshdr");
			return -1;
		}

		name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
		if (!name) {
			ERROR_ELF("elf_strptr");
			return -1;
		}

		data = elf_getdata(scn, NULL);
		if (!data) {
			ERROR_ELF("elf_getdata");
			return -1;
		}

		if (!strcmp(name, ".symtab")) {
			symtab = data;
		} else if (!strcmp(name, ".strtab")) {
			strtab_idx = i;
		} else if (!strcmp(name, ".orc_unwind")) {
			orc = data->d_buf;
			orc_size = sh.sh_size;
		} else if (!strcmp(name, ".orc_unwind_ip")) {
			orc_ip = data->d_buf;
			orc_ip_addr = sh.sh_addr;
		} else if (!strcmp(name, ".rela.orc_unwind_ip")) {
			rela_orc_ip = data;
		}
	}

	if (!symtab || !strtab_idx || !orc || !orc_ip)
		return 0;

	if (orc_size % sizeof(*orc) != 0) {
		ERROR("bad .orc_unwind section size");
		return -1;
	}

	nr_entries = orc_size / sizeof(*orc);
	for (i = 0; i < nr_entries; i++) {
		if (rela_orc_ip) {
			if (!gelf_getrela(rela_orc_ip, i, &rela)) {
				ERROR_ELF("gelf_getrela");
				return -1;
			}

			if (!gelf_getsym(symtab, GELF_R_SYM(rela.r_info), &sym)) {
				ERROR_ELF("gelf_getsym");
				return -1;
			}

			if (GELF_ST_TYPE(sym.st_info) == STT_SECTION) {
				scn = elf_getscn(elf, sym.st_shndx);
				if (!scn) {
					ERROR_ELF("elf_getscn");
					return -1;
				}

				if (!gelf_getshdr(scn, &sh)) {
					ERROR_ELF("gelf_getshdr");
					return -1;
				}

				name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
				if (!name) {
					ERROR_ELF("elf_strptr");
					return -1;
				}
			} else {
				name = elf_strptr(elf, strtab_idx, sym.st_name);
				if (!name) {
					ERROR_ELF("elf_strptr");
					return -1;
				}
			}

			printf("%s+%llx:", name, (unsigned long long)rela.r_addend);

		} else {
			printf("%llx:", (unsigned long long)(orc_ip_addr + (i * sizeof(int)) + orc_ip[i]));
		}

		orc_print_dump(&dummy_elf, orc, i);
	}

	elf_end(elf);
	close(fd);

	return 0;
}
