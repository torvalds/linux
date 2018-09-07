/*
 * elf.c - ELF access library
 *
 * Adapted from kpatch (https://github.com/dynup/kpatch):
 * Copyright (C) 2013-2015 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "elf.h"
#include "warn.h"

struct section *find_section_by_name(struct elf *elf, const char *name)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (!strcmp(sec->name, name))
			return sec;

	return NULL;
}

static struct section *find_section_by_index(struct elf *elf,
					     unsigned int idx)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (sec->idx == idx)
			return sec;

	return NULL;
}

static struct symbol *find_symbol_by_index(struct elf *elf, unsigned int idx)
{
	struct section *sec;
	struct symbol *sym;

	list_for_each_entry(sec, &elf->sections, list)
		hash_for_each_possible(sec->symbol_hash, sym, hash, idx)
			if (sym->idx == idx)
				return sym;

	return NULL;
}

struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset)
{
	struct symbol *sym;

	list_for_each_entry(sym, &sec->symbol_list, list)
		if (sym->type != STT_SECTION &&
		    sym->offset == offset)
			return sym;

	return NULL;
}

struct symbol *find_symbol_by_name(struct elf *elf, const char *name)
{
	struct section *sec;
	struct symbol *sym;

	list_for_each_entry(sec, &elf->sections, list)
		list_for_each_entry(sym, &sec->symbol_list, list)
			if (!strcmp(sym->name, name))
				return sym;

	return NULL;
}

struct symbol *find_symbol_containing(struct section *sec, unsigned long offset)
{
	struct symbol *sym;

	list_for_each_entry(sym, &sec->symbol_list, list)
		if (sym->type != STT_SECTION &&
		    offset >= sym->offset && offset < sym->offset + sym->len)
			return sym;

	return NULL;
}

struct rela *find_rela_by_dest_range(struct section *sec, unsigned long offset,
				     unsigned int len)
{
	struct rela *rela;
	unsigned long o;

	if (!sec->rela)
		return NULL;

	for (o = offset; o < offset + len; o++)
		hash_for_each_possible(sec->rela->rela_hash, rela, hash, o)
			if (rela->offset == o)
				return rela;

	return NULL;
}

struct rela *find_rela_by_dest(struct section *sec, unsigned long offset)
{
	return find_rela_by_dest_range(sec, offset, 1);
}

struct symbol *find_containing_func(struct section *sec, unsigned long offset)
{
	struct symbol *func;

	list_for_each_entry(func, &sec->symbol_list, list)
		if (func->type == STT_FUNC && offset >= func->offset &&
		    offset < func->offset + func->len)
			return func;

	return NULL;
}

static int read_sections(struct elf *elf)
{
	Elf_Scn *s = NULL;
	struct section *sec;
	size_t shstrndx, sections_nr;
	int i;

	if (elf_getshdrnum(elf->elf, &sections_nr)) {
		WARN_ELF("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf->elf, &shstrndx)) {
		WARN_ELF("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < sections_nr; i++) {
		sec = malloc(sizeof(*sec));
		if (!sec) {
			perror("malloc");
			return -1;
		}
		memset(sec, 0, sizeof(*sec));

		INIT_LIST_HEAD(&sec->symbol_list);
		INIT_LIST_HEAD(&sec->rela_list);
		hash_init(sec->rela_hash);
		hash_init(sec->symbol_hash);

		list_add_tail(&sec->list, &elf->sections);

		s = elf_getscn(elf->elf, i);
		if (!s) {
			WARN_ELF("elf_getscn");
			return -1;
		}

		sec->idx = elf_ndxscn(s);

		if (!gelf_getshdr(s, &sec->sh)) {
			WARN_ELF("gelf_getshdr");
			return -1;
		}

		sec->name = elf_strptr(elf->elf, shstrndx, sec->sh.sh_name);
		if (!sec->name) {
			WARN_ELF("elf_strptr");
			return -1;
		}

		if (sec->sh.sh_size != 0) {
			sec->data = elf_getdata(s, NULL);
			if (!sec->data) {
				WARN_ELF("elf_getdata");
				return -1;
			}
			if (sec->data->d_off != 0 ||
			    sec->data->d_size != sec->sh.sh_size) {
				WARN("unexpected data attributes for %s",
				     sec->name);
				return -1;
			}
		}
		sec->len = sec->sh.sh_size;
	}

	/* sanity check, one more call to elf_nextscn() should return NULL */
	if (elf_nextscn(elf->elf, s)) {
		WARN("section entry mismatch");
		return -1;
	}

	return 0;
}

static int read_symbols(struct elf *elf)
{
	struct section *symtab, *sec;
	struct symbol *sym, *pfunc;
	struct list_head *entry, *tmp;
	int symbols_nr, i;
	char *coldstr;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		WARN("missing symbol table");
		return -1;
	}

	symbols_nr = symtab->sh.sh_size / symtab->sh.sh_entsize;

	for (i = 0; i < symbols_nr; i++) {
		sym = malloc(sizeof(*sym));
		if (!sym) {
			perror("malloc");
			return -1;
		}
		memset(sym, 0, sizeof(*sym));

		sym->idx = i;

		if (!gelf_getsym(symtab->data, i, &sym->sym)) {
			WARN_ELF("gelf_getsym");
			goto err;
		}

		sym->name = elf_strptr(elf->elf, symtab->sh.sh_link,
				       sym->sym.st_name);
		if (!sym->name) {
			WARN_ELF("elf_strptr");
			goto err;
		}

		sym->type = GELF_ST_TYPE(sym->sym.st_info);
		sym->bind = GELF_ST_BIND(sym->sym.st_info);

		if (sym->sym.st_shndx > SHN_UNDEF &&
		    sym->sym.st_shndx < SHN_LORESERVE) {
			sym->sec = find_section_by_index(elf,
							 sym->sym.st_shndx);
			if (!sym->sec) {
				WARN("couldn't find section for symbol %s",
				     sym->name);
				goto err;
			}
			if (sym->type == STT_SECTION) {
				sym->name = sym->sec->name;
				sym->sec->sym = sym;
			}
		} else
			sym->sec = find_section_by_index(elf, 0);

		sym->offset = sym->sym.st_value;
		sym->len = sym->sym.st_size;

		/* sorted insert into a per-section list */
		entry = &sym->sec->symbol_list;
		list_for_each_prev(tmp, &sym->sec->symbol_list) {
			struct symbol *s;

			s = list_entry(tmp, struct symbol, list);

			if (sym->offset > s->offset) {
				entry = tmp;
				break;
			}

			if (sym->offset == s->offset && sym->len >= s->len) {
				entry = tmp;
				break;
			}
		}
		list_add(&sym->list, entry);
		hash_add(sym->sec->symbol_hash, &sym->hash, sym->idx);
	}

	/* Create parent/child links for any cold subfunctions */
	list_for_each_entry(sec, &elf->sections, list) {
		list_for_each_entry(sym, &sec->symbol_list, list) {
			if (sym->type != STT_FUNC)
				continue;
			sym->pfunc = sym->cfunc = sym;
			coldstr = strstr(sym->name, ".cold.");
			if (!coldstr)
				continue;

			coldstr[0] = '\0';
			pfunc = find_symbol_by_name(elf, sym->name);
			coldstr[0] = '.';

			if (!pfunc) {
				WARN("%s(): can't find parent function",
				     sym->name);
				goto err;
			}

			sym->pfunc = pfunc;
			pfunc->cfunc = sym;

			/*
			 * Unfortunately, -fnoreorder-functions puts the child
			 * inside the parent.  Remove the overlap so we can
			 * have sane assumptions.
			 *
			 * Note that pfunc->len now no longer matches
			 * pfunc->sym.st_size.
			 */
			if (sym->sec == pfunc->sec &&
			    sym->offset >= pfunc->offset &&
			    sym->offset + sym->len == pfunc->offset + pfunc->len) {
				pfunc->len -= sym->len;
			}
		}
	}

	return 0;

err:
	free(sym);
	return -1;
}

static int read_relas(struct elf *elf)
{
	struct section *sec;
	struct rela *rela;
	int i;
	unsigned int symndx;

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_type != SHT_RELA)
			continue;

		sec->base = find_section_by_name(elf, sec->name + 5);
		if (!sec->base) {
			WARN("can't find base section for rela section %s",
			     sec->name);
			return -1;
		}

		sec->base->rela = sec;

		for (i = 0; i < sec->sh.sh_size / sec->sh.sh_entsize; i++) {
			rela = malloc(sizeof(*rela));
			if (!rela) {
				perror("malloc");
				return -1;
			}
			memset(rela, 0, sizeof(*rela));

			if (!gelf_getrela(sec->data, i, &rela->rela)) {
				WARN_ELF("gelf_getrela");
				return -1;
			}

			rela->type = GELF_R_TYPE(rela->rela.r_info);
			rela->addend = rela->rela.r_addend;
			rela->offset = rela->rela.r_offset;
			symndx = GELF_R_SYM(rela->rela.r_info);
			rela->sym = find_symbol_by_index(elf, symndx);
			rela->rela_sec = sec;
			if (!rela->sym) {
				WARN("can't find rela entry symbol %d for %s",
				     symndx, sec->name);
				return -1;
			}

			list_add_tail(&rela->list, &sec->rela_list);
			hash_add(sec->rela_hash, &rela->hash, rela->offset);

		}
	}

	return 0;
}

struct elf *elf_open(const char *name, int flags)
{
	struct elf *elf;
	Elf_Cmd cmd;

	elf_version(EV_CURRENT);

	elf = malloc(sizeof(*elf));
	if (!elf) {
		perror("malloc");
		return NULL;
	}
	memset(elf, 0, sizeof(*elf));

	INIT_LIST_HEAD(&elf->sections);

	elf->fd = open(name, flags);
	if (elf->fd == -1) {
		fprintf(stderr, "objtool: Can't open '%s': %s\n",
			name, strerror(errno));
		goto err;
	}

	if ((flags & O_ACCMODE) == O_RDONLY)
		cmd = ELF_C_READ_MMAP;
	else if ((flags & O_ACCMODE) == O_RDWR)
		cmd = ELF_C_RDWR;
	else /* O_WRONLY */
		cmd = ELF_C_WRITE;

	elf->elf = elf_begin(elf->fd, cmd, NULL);
	if (!elf->elf) {
		WARN_ELF("elf_begin");
		goto err;
	}

	if (!gelf_getehdr(elf->elf, &elf->ehdr)) {
		WARN_ELF("gelf_getehdr");
		goto err;
	}

	if (read_sections(elf))
		goto err;

	if (read_symbols(elf))
		goto err;

	if (read_relas(elf))
		goto err;

	return elf;

err:
	elf_close(elf);
	return NULL;
}

struct section *elf_create_section(struct elf *elf, const char *name,
				   size_t entsize, int nr)
{
	struct section *sec, *shstrtab;
	size_t size = entsize * nr;
	struct Elf_Scn *s;
	Elf_Data *data;

	sec = malloc(sizeof(*sec));
	if (!sec) {
		perror("malloc");
		return NULL;
	}
	memset(sec, 0, sizeof(*sec));

	INIT_LIST_HEAD(&sec->symbol_list);
	INIT_LIST_HEAD(&sec->rela_list);
	hash_init(sec->rela_hash);
	hash_init(sec->symbol_hash);

	list_add_tail(&sec->list, &elf->sections);

	s = elf_newscn(elf->elf);
	if (!s) {
		WARN_ELF("elf_newscn");
		return NULL;
	}

	sec->name = strdup(name);
	if (!sec->name) {
		perror("strdup");
		return NULL;
	}

	sec->idx = elf_ndxscn(s);
	sec->len = size;
	sec->changed = true;

	sec->data = elf_newdata(s);
	if (!sec->data) {
		WARN_ELF("elf_newdata");
		return NULL;
	}

	sec->data->d_size = size;
	sec->data->d_align = 1;

	if (size) {
		sec->data->d_buf = malloc(size);
		if (!sec->data->d_buf) {
			perror("malloc");
			return NULL;
		}
		memset(sec->data->d_buf, 0, size);
	}

	if (!gelf_getshdr(s, &sec->sh)) {
		WARN_ELF("gelf_getshdr");
		return NULL;
	}

	sec->sh.sh_size = size;
	sec->sh.sh_entsize = entsize;
	sec->sh.sh_type = SHT_PROGBITS;
	sec->sh.sh_addralign = 1;
	sec->sh.sh_flags = SHF_ALLOC;


	/* Add section name to .shstrtab (or .strtab for Clang) */
	shstrtab = find_section_by_name(elf, ".shstrtab");
	if (!shstrtab)
		shstrtab = find_section_by_name(elf, ".strtab");
	if (!shstrtab) {
		WARN("can't find .shstrtab or .strtab section");
		return NULL;
	}

	s = elf_getscn(elf->elf, shstrtab->idx);
	if (!s) {
		WARN_ELF("elf_getscn");
		return NULL;
	}

	data = elf_newdata(s);
	if (!data) {
		WARN_ELF("elf_newdata");
		return NULL;
	}

	data->d_buf = sec->name;
	data->d_size = strlen(name) + 1;
	data->d_align = 1;

	sec->sh.sh_name = shstrtab->len;

	shstrtab->len += strlen(name) + 1;
	shstrtab->changed = true;

	return sec;
}

struct section *elf_create_rela_section(struct elf *elf, struct section *base)
{
	char *relaname;
	struct section *sec;

	relaname = malloc(strlen(base->name) + strlen(".rela") + 1);
	if (!relaname) {
		perror("malloc");
		return NULL;
	}
	strcpy(relaname, ".rela");
	strcat(relaname, base->name);

	sec = elf_create_section(elf, relaname, sizeof(GElf_Rela), 0);
	free(relaname);
	if (!sec)
		return NULL;

	base->rela = sec;
	sec->base = base;

	sec->sh.sh_type = SHT_RELA;
	sec->sh.sh_addralign = 8;
	sec->sh.sh_link = find_section_by_name(elf, ".symtab")->idx;
	sec->sh.sh_info = base->idx;
	sec->sh.sh_flags = SHF_INFO_LINK;

	return sec;
}

int elf_rebuild_rela_section(struct section *sec)
{
	struct rela *rela;
	int nr, idx = 0, size;
	GElf_Rela *relas;

	nr = 0;
	list_for_each_entry(rela, &sec->rela_list, list)
		nr++;

	size = nr * sizeof(*relas);
	relas = malloc(size);
	if (!relas) {
		perror("malloc");
		return -1;
	}

	sec->data->d_buf = relas;
	sec->data->d_size = size;

	sec->sh.sh_size = size;

	idx = 0;
	list_for_each_entry(rela, &sec->rela_list, list) {
		relas[idx].r_offset = rela->offset;
		relas[idx].r_addend = rela->addend;
		relas[idx].r_info = GELF_R_INFO(rela->sym->idx, rela->type);
		idx++;
	}

	return 0;
}

int elf_write(struct elf *elf)
{
	struct section *sec;
	Elf_Scn *s;

	/* Update section headers for changed sections: */
	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->changed) {
			s = elf_getscn(elf->elf, sec->idx);
			if (!s) {
				WARN_ELF("elf_getscn");
				return -1;
			}
			if (!gelf_update_shdr(s, &sec->sh)) {
				WARN_ELF("gelf_update_shdr");
				return -1;
			}
		}
	}

	/* Make sure the new section header entries get updated properly. */
	elf_flagelf(elf->elf, ELF_C_SET, ELF_F_DIRTY);

	/* Write all changes to the file. */
	if (elf_update(elf->elf, ELF_C_WRITE) < 0) {
		WARN_ELF("elf_update");
		return -1;
	}

	return 0;
}

void elf_close(struct elf *elf)
{
	struct section *sec, *tmpsec;
	struct symbol *sym, *tmpsym;
	struct rela *rela, *tmprela;

	if (elf->elf)
		elf_end(elf->elf);

	if (elf->fd > 0)
		close(elf->fd);

	list_for_each_entry_safe(sec, tmpsec, &elf->sections, list) {
		list_for_each_entry_safe(sym, tmpsym, &sec->symbol_list, list) {
			list_del(&sym->list);
			hash_del(&sym->hash);
			free(sym);
		}
		list_for_each_entry_safe(rela, tmprela, &sec->rela_list, list) {
			list_del(&rela->list);
			hash_del(&rela->hash);
			free(rela);
		}
		list_del(&sec->list);
		free(sec);
	}

	free(elf);
}
