/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include "orc.h"
#include "warn.h"

static const char *reg_name(unsigned int reg)
{
	switch (reg) {
	case ORC_REG_PREV_SP:
		return "prevsp";
	case ORC_REG_DX:
		return "dx";
	case ORC_REG_DI:
		return "di";
	case ORC_REG_BP:
		return "bp";
	case ORC_REG_SP:
		return "sp";
	case ORC_REG_R10:
		return "r10";
	case ORC_REG_R13:
		return "r13";
	case ORC_REG_BP_INDIRECT:
		return "bp(ind)";
	case ORC_REG_SP_INDIRECT:
		return "sp(ind)";
	default:
		return "?";
	}
}

static const char *orc_type_name(unsigned int type)
{
	switch (type) {
	case ORC_TYPE_CALL:
		return "call";
	case ORC_TYPE_REGS:
		return "regs";
	case ORC_TYPE_REGS_IRET:
		return "iret";
	default:
		return "?";
	}
}

static void print_reg(unsigned int reg, int offset)
{
	if (reg == ORC_REG_BP_INDIRECT)
		printf("(bp%+d)", offset);
	else if (reg == ORC_REG_SP_INDIRECT)
		printf("(sp%+d)", offset);
	else if (reg == ORC_REG_UNDEFINED)
		printf("(und)");
	else
		printf("%s%+d", reg_name(reg), offset);
}

int orc_dump(const char *_objname)
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


	objname = _objname;

	elf_version(EV_CURRENT);

	fd = open(objname, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -1;
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		WARN_ELF("elf_begin");
		return -1;
	}

	if (elf_getshdrnum(elf, &nr_sections)) {
		WARN_ELF("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf, &shstrtab_idx)) {
		WARN_ELF("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < nr_sections; i++) {
		scn = elf_getscn(elf, i);
		if (!scn) {
			WARN_ELF("elf_getscn");
			return -1;
		}

		if (!gelf_getshdr(scn, &sh)) {
			WARN_ELF("gelf_getshdr");
			return -1;
		}

		name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
		if (!name) {
			WARN_ELF("elf_strptr");
			return -1;
		}

		data = elf_getdata(scn, NULL);
		if (!data) {
			WARN_ELF("elf_getdata");
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
		WARN("bad .orc_unwind section size");
		return -1;
	}

	nr_entries = orc_size / sizeof(*orc);
	for (i = 0; i < nr_entries; i++) {
		if (rela_orc_ip) {
			if (!gelf_getrela(rela_orc_ip, i, &rela)) {
				WARN_ELF("gelf_getrela");
				return -1;
			}

			if (!gelf_getsym(symtab, GELF_R_SYM(rela.r_info), &sym)) {
				WARN_ELF("gelf_getsym");
				return -1;
			}

			if (GELF_ST_TYPE(sym.st_info) == STT_SECTION) {
				scn = elf_getscn(elf, sym.st_shndx);
				if (!scn) {
					WARN_ELF("elf_getscn");
					return -1;
				}

				if (!gelf_getshdr(scn, &sh)) {
					WARN_ELF("gelf_getshdr");
					return -1;
				}

				name = elf_strptr(elf, shstrtab_idx, sh.sh_name);
				if (!name) {
					WARN_ELF("elf_strptr");
					return -1;
				}
			} else {
				name = elf_strptr(elf, strtab_idx, sym.st_name);
				if (!name) {
					WARN_ELF("elf_strptr");
					return -1;
				}
			}

			printf("%s+%llx:", name, (unsigned long long)rela.r_addend);

		} else {
			printf("%llx:", (unsigned long long)(orc_ip_addr + (i * sizeof(int)) + orc_ip[i]));
		}


		printf(" sp:");

		print_reg(orc[i].sp_reg, orc[i].sp_offset);

		printf(" bp:");

		print_reg(orc[i].bp_reg, orc[i].bp_offset);

		printf(" type:%s end:%d\n",
		       orc_type_name(orc[i].type), orc[i].end);
	}

	elf_end(elf);
	close(fd);

	return 0;
}
