// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * elf.c - ELF access library
 *
 * Adapted from kpatch (https://github.com/dynup/kpatch):
 * Copyright (C) 2013-2015 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <objtool/builtin.h>

#include <objtool/elf.h>
#include <objtool/warn.h>

#define MAX_NAME_LEN 128

static inline u32 str_hash(const char *str)
{
	return jhash(str, strlen(str), 0);
}

static inline int elf_hash_bits(void)
{
	return vmlinux ? ELF_HASH_BITS : 16;
}

#define elf_hash_add(hashtable, node, key) \
	hlist_add_head(node, &hashtable[hash_min(key, elf_hash_bits())])

static void elf_hash_init(struct hlist_head *table)
{
	__hash_init(table, 1U << elf_hash_bits());
}

#define elf_hash_for_each_possible(name, obj, member, key)			\
	hlist_for_each_entry(obj, &name[hash_min(key, elf_hash_bits())], member)

static bool symbol_to_offset(struct rb_node *a, const struct rb_node *b)
{
	struct symbol *sa = rb_entry(a, struct symbol, node);
	struct symbol *sb = rb_entry(b, struct symbol, node);

	if (sa->offset < sb->offset)
		return true;
	if (sa->offset > sb->offset)
		return false;

	if (sa->len < sb->len)
		return true;
	if (sa->len > sb->len)
		return false;

	sa->alias = sb;

	return false;
}

static int symbol_by_offset(const void *key, const struct rb_node *node)
{
	const struct symbol *s = rb_entry(node, struct symbol, node);
	const unsigned long *o = key;

	if (*o < s->offset)
		return -1;
	if (*o >= s->offset + s->len)
		return 1;

	return 0;
}

struct section *find_section_by_name(const struct elf *elf, const char *name)
{
	struct section *sec;

	elf_hash_for_each_possible(elf->section_name_hash, sec, name_hash, str_hash(name))
		if (!strcmp(sec->name, name))
			return sec;

	return NULL;
}

static struct section *find_section_by_index(struct elf *elf,
					     unsigned int idx)
{
	struct section *sec;

	elf_hash_for_each_possible(elf->section_hash, sec, hash, idx)
		if (sec->idx == idx)
			return sec;

	return NULL;
}

static struct symbol *find_symbol_by_index(struct elf *elf, unsigned int idx)
{
	struct symbol *sym;

	elf_hash_for_each_possible(elf->symbol_hash, sym, hash, idx)
		if (sym->idx == idx)
			return sym;

	return NULL;
}

struct symbol *find_symbol_by_offset(struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(node, &offset, &sec->symbol_tree, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->offset == offset && s->type != STT_SECTION)
			return s;
	}

	return NULL;
}

struct symbol *find_func_by_offset(struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(node, &offset, &sec->symbol_tree, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->offset == offset && s->type == STT_FUNC)
			return s;
	}

	return NULL;
}

struct symbol *find_symbol_containing(const struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(node, &offset, &sec->symbol_tree, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->type != STT_SECTION)
			return s;
	}

	return NULL;
}

struct symbol *find_func_containing(struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(node, &offset, &sec->symbol_tree, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->type == STT_FUNC)
			return s;
	}

	return NULL;
}

struct symbol *find_symbol_by_name(const struct elf *elf, const char *name)
{
	struct symbol *sym;

	elf_hash_for_each_possible(elf->symbol_name_hash, sym, name_hash, str_hash(name))
		if (!strcmp(sym->name, name))
			return sym;

	return NULL;
}

struct reloc *find_reloc_by_dest_range(const struct elf *elf, struct section *sec,
				     unsigned long offset, unsigned int len)
{
	struct reloc *reloc, *r = NULL;
	unsigned long o;

	if (!sec->reloc)
		return NULL;

	sec = sec->reloc;

	for_offset_range(o, offset, offset + len) {
		elf_hash_for_each_possible(elf->reloc_hash, reloc, hash,
				       sec_offset_hash(sec, o)) {
			if (reloc->sec != sec)
				continue;

			if (reloc->offset >= offset && reloc->offset < offset + len) {
				if (!r || reloc->offset < r->offset)
					r = reloc;
			}
		}
		if (r)
			return r;
	}

	return NULL;
}

struct reloc *find_reloc_by_dest(const struct elf *elf, struct section *sec, unsigned long offset)
{
	return find_reloc_by_dest_range(elf, sec, offset, 1);
}

void insn_to_reloc_sym_addend(struct section *sec, unsigned long offset,
			      struct reloc *reloc)
{
	if (sec->sym) {
		reloc->sym = sec->sym;
		reloc->addend = offset;
		return;
	}

	/*
	 * The Clang assembler strips section symbols, so we have to reference
	 * the function symbol instead:
	 */
	reloc->sym = find_symbol_containing(sec, offset);
	if (!reloc->sym) {
		/*
		 * Hack alert.  This happens when we need to reference the NOP
		 * pad insn immediately after the function.
		 */
		reloc->sym = find_symbol_containing(sec, offset - 1);
	}

	if (reloc->sym)
		reloc->addend = offset - reloc->sym->offset;
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
		INIT_LIST_HEAD(&sec->reloc_list);

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

		list_add_tail(&sec->list, &elf->sections);
		elf_hash_add(elf->section_hash, &sec->hash, sec->idx);
		elf_hash_add(elf->section_name_hash, &sec->name_hash, str_hash(sec->name));
	}

	if (stats)
		printf("nr_sections: %lu\n", (unsigned long)sections_nr);

	/* sanity check, one more call to elf_nextscn() should return NULL */
	if (elf_nextscn(elf->elf, s)) {
		WARN("section entry mismatch");
		return -1;
	}

	return 0;
}

static int read_symbols(struct elf *elf)
{
	struct section *symtab, *symtab_shndx, *sec;
	struct symbol *sym, *pfunc;
	struct list_head *entry;
	struct rb_node *pnode;
	int symbols_nr, i;
	char *coldstr;
	Elf_Data *shndx_data = NULL;
	Elf32_Word shndx;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		/*
		 * A missing symbol table is actually possible if it's an empty
		 * .o file.  This can happen for thunk_64.o.
		 */
		return 0;
	}

	symtab_shndx = find_section_by_name(elf, ".symtab_shndx");
	if (symtab_shndx)
		shndx_data = symtab_shndx->data;

	symbols_nr = symtab->sh.sh_size / symtab->sh.sh_entsize;

	for (i = 0; i < symbols_nr; i++) {
		sym = malloc(sizeof(*sym));
		if (!sym) {
			perror("malloc");
			return -1;
		}
		memset(sym, 0, sizeof(*sym));
		sym->alias = sym;

		sym->idx = i;

		if (!gelf_getsymshndx(symtab->data, shndx_data, i, &sym->sym,
				      &shndx)) {
			WARN_ELF("gelf_getsymshndx");
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

		if ((sym->sym.st_shndx > SHN_UNDEF &&
		     sym->sym.st_shndx < SHN_LORESERVE) ||
		    (shndx_data && sym->sym.st_shndx == SHN_XINDEX)) {
			if (sym->sym.st_shndx != SHN_XINDEX)
				shndx = sym->sym.st_shndx;

			sym->sec = find_section_by_index(elf, shndx);
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

		rb_add(&sym->node, &sym->sec->symbol_tree, symbol_to_offset);
		pnode = rb_prev(&sym->node);
		if (pnode)
			entry = &rb_entry(pnode, struct symbol, node)->list;
		else
			entry = &sym->sec->symbol_list;
		list_add(&sym->list, entry);
		elf_hash_add(elf->symbol_hash, &sym->hash, sym->idx);
		elf_hash_add(elf->symbol_name_hash, &sym->name_hash, str_hash(sym->name));

		/*
		 * Don't store empty STT_NOTYPE symbols in the rbtree.  They
		 * can exist within a function, confusing the sorting.
		 */
		if (!sym->len)
			rb_erase(&sym->node, &sym->sec->symbol_tree);
	}

	if (stats)
		printf("nr_symbols: %lu\n", (unsigned long)symbols_nr);

	/* Create parent/child links for any cold subfunctions */
	list_for_each_entry(sec, &elf->sections, list) {
		list_for_each_entry(sym, &sec->symbol_list, list) {
			char pname[MAX_NAME_LEN + 1];
			size_t pnamelen;
			if (sym->type != STT_FUNC)
				continue;

			if (sym->pfunc == NULL)
				sym->pfunc = sym;

			if (sym->cfunc == NULL)
				sym->cfunc = sym;

			coldstr = strstr(sym->name, ".cold");
			if (!coldstr)
				continue;

			pnamelen = coldstr - sym->name;
			if (pnamelen > MAX_NAME_LEN) {
				WARN("%s(): parent function name exceeds maximum length of %d characters",
				     sym->name, MAX_NAME_LEN);
				return -1;
			}

			strncpy(pname, sym->name, pnamelen);
			pname[pnamelen] = '\0';
			pfunc = find_symbol_by_name(elf, pname);

			if (!pfunc) {
				WARN("%s(): can't find parent function",
				     sym->name);
				return -1;
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

void elf_add_reloc(struct elf *elf, struct reloc *reloc)
{
	struct section *sec = reloc->sec;

	list_add_tail(&reloc->list, &sec->reloc_list);
	elf_hash_add(elf->reloc_hash, &reloc->hash, reloc_hash(reloc));
}

static int read_rel_reloc(struct section *sec, int i, struct reloc *reloc, unsigned int *symndx)
{
	if (!gelf_getrel(sec->data, i, &reloc->rel)) {
		WARN_ELF("gelf_getrel");
		return -1;
	}
	reloc->type = GELF_R_TYPE(reloc->rel.r_info);
	reloc->addend = 0;
	reloc->offset = reloc->rel.r_offset;
	*symndx = GELF_R_SYM(reloc->rel.r_info);
	return 0;
}

static int read_rela_reloc(struct section *sec, int i, struct reloc *reloc, unsigned int *symndx)
{
	if (!gelf_getrela(sec->data, i, &reloc->rela)) {
		WARN_ELF("gelf_getrela");
		return -1;
	}
	reloc->type = GELF_R_TYPE(reloc->rela.r_info);
	reloc->addend = reloc->rela.r_addend;
	reloc->offset = reloc->rela.r_offset;
	*symndx = GELF_R_SYM(reloc->rela.r_info);
	return 0;
}

static int read_relocs(struct elf *elf)
{
	struct section *sec;
	struct reloc *reloc;
	int i;
	unsigned int symndx;
	unsigned long nr_reloc, max_reloc = 0, tot_reloc = 0;

	list_for_each_entry(sec, &elf->sections, list) {
		if ((sec->sh.sh_type != SHT_RELA) &&
		    (sec->sh.sh_type != SHT_REL))
			continue;

		sec->base = find_section_by_index(elf, sec->sh.sh_info);
		if (!sec->base) {
			WARN("can't find base section for reloc section %s",
			     sec->name);
			return -1;
		}

		sec->base->reloc = sec;

		nr_reloc = 0;
		for (i = 0; i < sec->sh.sh_size / sec->sh.sh_entsize; i++) {
			reloc = malloc(sizeof(*reloc));
			if (!reloc) {
				perror("malloc");
				return -1;
			}
			memset(reloc, 0, sizeof(*reloc));
			switch (sec->sh.sh_type) {
			case SHT_REL:
				if (read_rel_reloc(sec, i, reloc, &symndx))
					return -1;
				break;
			case SHT_RELA:
				if (read_rela_reloc(sec, i, reloc, &symndx))
					return -1;
				break;
			default: return -1;
			}

			reloc->sec = sec;
			reloc->idx = i;
			reloc->sym = find_symbol_by_index(elf, symndx);
			if (!reloc->sym) {
				WARN("can't find reloc entry symbol %d for %s",
				     symndx, sec->name);
				return -1;
			}

			elf_add_reloc(elf, reloc);
			nr_reloc++;
		}
		max_reloc = max(max_reloc, nr_reloc);
		tot_reloc += nr_reloc;
	}

	if (stats) {
		printf("max_reloc: %lu\n", max_reloc);
		printf("tot_reloc: %lu\n", tot_reloc);
	}

	return 0;
}

struct elf *elf_open_read(const char *name, int flags)
{
	struct elf *elf;
	Elf_Cmd cmd;

	elf_version(EV_CURRENT);

	elf = malloc(sizeof(*elf));
	if (!elf) {
		perror("malloc");
		return NULL;
	}
	memset(elf, 0, offsetof(struct elf, sections));

	INIT_LIST_HEAD(&elf->sections);

	elf_hash_init(elf->symbol_hash);
	elf_hash_init(elf->symbol_name_hash);
	elf_hash_init(elf->section_hash);
	elf_hash_init(elf->section_name_hash);
	elf_hash_init(elf->reloc_hash);

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

	if (read_relocs(elf))
		goto err;

	return elf;

err:
	elf_close(elf);
	return NULL;
}

struct section *elf_create_section(struct elf *elf, const char *name,
				   unsigned int sh_flags, size_t entsize, int nr)
{
	struct section *sec, *shstrtab;
	size_t size = entsize * nr;
	Elf_Scn *s;
	Elf_Data *data;

	sec = malloc(sizeof(*sec));
	if (!sec) {
		perror("malloc");
		return NULL;
	}
	memset(sec, 0, sizeof(*sec));

	INIT_LIST_HEAD(&sec->symbol_list);
	INIT_LIST_HEAD(&sec->reloc_list);

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
	sec->sh.sh_flags = SHF_ALLOC | sh_flags;


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

	list_add_tail(&sec->list, &elf->sections);
	elf_hash_add(elf->section_hash, &sec->hash, sec->idx);
	elf_hash_add(elf->section_name_hash, &sec->name_hash, str_hash(sec->name));

	elf->changed = true;

	return sec;
}

static struct section *elf_create_rel_reloc_section(struct elf *elf, struct section *base)
{
	char *relocname;
	struct section *sec;

	relocname = malloc(strlen(base->name) + strlen(".rel") + 1);
	if (!relocname) {
		perror("malloc");
		return NULL;
	}
	strcpy(relocname, ".rel");
	strcat(relocname, base->name);

	sec = elf_create_section(elf, relocname, 0, sizeof(GElf_Rel), 0);
	free(relocname);
	if (!sec)
		return NULL;

	base->reloc = sec;
	sec->base = base;

	sec->sh.sh_type = SHT_REL;
	sec->sh.sh_addralign = 8;
	sec->sh.sh_link = find_section_by_name(elf, ".symtab")->idx;
	sec->sh.sh_info = base->idx;
	sec->sh.sh_flags = SHF_INFO_LINK;

	return sec;
}

static struct section *elf_create_rela_reloc_section(struct elf *elf, struct section *base)
{
	char *relocname;
	struct section *sec;

	relocname = malloc(strlen(base->name) + strlen(".rela") + 1);
	if (!relocname) {
		perror("malloc");
		return NULL;
	}
	strcpy(relocname, ".rela");
	strcat(relocname, base->name);

	sec = elf_create_section(elf, relocname, 0, sizeof(GElf_Rela), 0);
	free(relocname);
	if (!sec)
		return NULL;

	base->reloc = sec;
	sec->base = base;

	sec->sh.sh_type = SHT_RELA;
	sec->sh.sh_addralign = 8;
	sec->sh.sh_link = find_section_by_name(elf, ".symtab")->idx;
	sec->sh.sh_info = base->idx;
	sec->sh.sh_flags = SHF_INFO_LINK;

	return sec;
}

struct section *elf_create_reloc_section(struct elf *elf,
					 struct section *base,
					 int reltype)
{
	switch (reltype) {
	case SHT_REL:  return elf_create_rel_reloc_section(elf, base);
	case SHT_RELA: return elf_create_rela_reloc_section(elf, base);
	default:       return NULL;
	}
}

static int elf_rebuild_rel_reloc_section(struct section *sec, int nr)
{
	struct reloc *reloc;
	int idx = 0, size;
	void *buf;

	/* Allocate a buffer for relocations */
	size = nr * sizeof(GElf_Rel);
	buf = malloc(size);
	if (!buf) {
		perror("malloc");
		return -1;
	}

	sec->data->d_buf = buf;
	sec->data->d_size = size;
	sec->data->d_type = ELF_T_REL;

	sec->sh.sh_size = size;

	idx = 0;
	list_for_each_entry(reloc, &sec->reloc_list, list) {
		reloc->rel.r_offset = reloc->offset;
		reloc->rel.r_info = GELF_R_INFO(reloc->sym->idx, reloc->type);
		gelf_update_rel(sec->data, idx, &reloc->rel);
		idx++;
	}

	return 0;
}

static int elf_rebuild_rela_reloc_section(struct section *sec, int nr)
{
	struct reloc *reloc;
	int idx = 0, size;
	void *buf;

	/* Allocate a buffer for relocations with addends */
	size = nr * sizeof(GElf_Rela);
	buf = malloc(size);
	if (!buf) {
		perror("malloc");
		return -1;
	}

	sec->data->d_buf = buf;
	sec->data->d_size = size;
	sec->data->d_type = ELF_T_RELA;

	sec->sh.sh_size = size;

	idx = 0;
	list_for_each_entry(reloc, &sec->reloc_list, list) {
		reloc->rela.r_offset = reloc->offset;
		reloc->rela.r_addend = reloc->addend;
		reloc->rela.r_info = GELF_R_INFO(reloc->sym->idx, reloc->type);
		gelf_update_rela(sec->data, idx, &reloc->rela);
		idx++;
	}

	return 0;
}

int elf_rebuild_reloc_section(struct elf *elf, struct section *sec)
{
	struct reloc *reloc;
	int nr;

	sec->changed = true;
	elf->changed = true;

	nr = 0;
	list_for_each_entry(reloc, &sec->reloc_list, list)
		nr++;

	switch (sec->sh.sh_type) {
	case SHT_REL:  return elf_rebuild_rel_reloc_section(sec, nr);
	case SHT_RELA: return elf_rebuild_rela_reloc_section(sec, nr);
	default:       return -1;
	}
}

int elf_write_insn(struct elf *elf, struct section *sec,
		   unsigned long offset, unsigned int len,
		   const char *insn)
{
	Elf_Data *data = sec->data;

	if (data->d_type != ELF_T_BYTE || data->d_off) {
		WARN("write to unexpected data for section: %s", sec->name);
		return -1;
	}

	memcpy(data->d_buf + offset, insn, len);
	elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY);

	elf->changed = true;

	return 0;
}

int elf_write_reloc(struct elf *elf, struct reloc *reloc)
{
	struct section *sec = reloc->sec;

	if (sec->sh.sh_type == SHT_REL) {
		reloc->rel.r_info = GELF_R_INFO(reloc->sym->idx, reloc->type);
		reloc->rel.r_offset = reloc->offset;

		if (!gelf_update_rel(sec->data, reloc->idx, &reloc->rel)) {
			WARN_ELF("gelf_update_rel");
			return -1;
		}
	} else {
		reloc->rela.r_info = GELF_R_INFO(reloc->sym->idx, reloc->type);
		reloc->rela.r_addend = reloc->addend;
		reloc->rela.r_offset = reloc->offset;

		if (!gelf_update_rela(sec->data, reloc->idx, &reloc->rela)) {
			WARN_ELF("gelf_update_rela");
			return -1;
		}
	}

	elf->changed = true;

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

			sec->changed = false;
		}
	}

	/* Make sure the new section header entries get updated properly. */
	elf_flagelf(elf->elf, ELF_C_SET, ELF_F_DIRTY);

	/* Write all changes to the file. */
	if (elf_update(elf->elf, ELF_C_WRITE) < 0) {
		WARN_ELF("elf_update");
		return -1;
	}

	elf->changed = false;

	return 0;
}

void elf_close(struct elf *elf)
{
	struct section *sec, *tmpsec;
	struct symbol *sym, *tmpsym;
	struct reloc *reloc, *tmpreloc;

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
		list_for_each_entry_safe(reloc, tmpreloc, &sec->reloc_list, list) {
			list_del(&reloc->list);
			hash_del(&reloc->hash);
			free(reloc);
		}
		list_del(&sec->list);
		free(sec);
	}

	free(elf);
}
