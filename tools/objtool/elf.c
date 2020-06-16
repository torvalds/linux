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
#include "builtin.h"

#include "elf.h"
#include "warn.h"

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

static void rb_add(struct rb_root *tree, struct rb_node *node,
		   int (*cmp)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **link = &tree->rb_node;
	struct rb_node *parent = NULL;

	while (*link) {
		parent = *link;
		if (cmp(node, parent) < 0)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node(node, parent, link);
	rb_insert_color(node, tree);
}

static struct rb_node *rb_find_first(const struct rb_root *tree, const void *key,
			       int (*cmp)(const void *key, const struct rb_node *))
{
	struct rb_node *node = tree->rb_node;
	struct rb_node *match = NULL;

	while (node) {
		int c = cmp(key, node);
		if (c <= 0) {
			if (!c)
				match = node;
			node = node->rb_left;
		} else if (c > 0) {
			node = node->rb_right;
		}
	}

	return match;
}

static struct rb_node *rb_next_match(struct rb_node *node, const void *key,
				    int (*cmp)(const void *key, const struct rb_node *))
{
	node = rb_next(node);
	if (node && cmp(key, node))
		node = NULL;
	return node;
}

#define rb_for_each(tree, node, key, cmp) \
	for ((node) = rb_find_first((tree), (key), (cmp)); \
	     (node); (node) = rb_next_match((node), (key), (cmp)))

static int symbol_to_offset(struct rb_node *a, const struct rb_node *b)
{
	struct symbol *sa = rb_entry(a, struct symbol, node);
	struct symbol *sb = rb_entry(b, struct symbol, node);

	if (sa->offset < sb->offset)
		return -1;
	if (sa->offset > sb->offset)
		return 1;

	if (sa->len < sb->len)
		return -1;
	if (sa->len > sb->len)
		return 1;

	sa->alias = sb;

	return 0;
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

	rb_for_each(&sec->symbol_tree, node, &offset, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->offset == offset && s->type != STT_SECTION)
			return s;
	}

	return NULL;
}

struct symbol *find_func_by_offset(struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(&sec->symbol_tree, node, &offset, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->offset == offset && s->type == STT_FUNC)
			return s;
	}

	return NULL;
}

struct symbol *find_symbol_containing(const struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(&sec->symbol_tree, node, &offset, symbol_by_offset) {
		struct symbol *s = rb_entry(node, struct symbol, node);

		if (s->type != STT_SECTION)
			return s;
	}

	return NULL;
}

struct symbol *find_func_containing(struct section *sec, unsigned long offset)
{
	struct rb_node *node;

	rb_for_each(&sec->symbol_tree, node, &offset, symbol_by_offset) {
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

struct rela *find_rela_by_dest_range(const struct elf *elf, struct section *sec,
				     unsigned long offset, unsigned int len)
{
	struct rela *rela, *r = NULL;
	unsigned long o;

	if (!sec->rela)
		return NULL;

	sec = sec->rela;

	for_offset_range(o, offset, offset + len) {
		elf_hash_for_each_possible(elf->rela_hash, rela, hash,
				       sec_offset_hash(sec, o)) {
			if (rela->sec != sec)
				continue;

			if (rela->offset >= offset && rela->offset < offset + len) {
				if (!r || rela->offset < r->offset)
					r = rela;
			}
		}
		if (r)
			return r;
	}

	return NULL;
}

struct rela *find_rela_by_dest(const struct elf *elf, struct section *sec, unsigned long offset)
{
	return find_rela_by_dest_range(elf, sec, offset, 1);
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
		WARN("missing symbol table");
		return -1;
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

		rb_add(&sym->sec->symbol_tree, &sym->node, symbol_to_offset);
		pnode = rb_prev(&sym->node);
		if (pnode)
			entry = &rb_entry(pnode, struct symbol, node)->list;
		else
			entry = &sym->sec->symbol_list;
		list_add(&sym->list, entry);
		elf_hash_add(elf->symbol_hash, &sym->hash, sym->idx);
		elf_hash_add(elf->symbol_name_hash, &sym->name_hash, str_hash(sym->name));
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
			sym->pfunc = sym->cfunc = sym;
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

void elf_add_rela(struct elf *elf, struct rela *rela)
{
	struct section *sec = rela->sec;

	list_add_tail(&rela->list, &sec->rela_list);
	elf_hash_add(elf->rela_hash, &rela->hash, rela_hash(rela));
}

static int read_relas(struct elf *elf)
{
	struct section *sec;
	struct rela *rela;
	int i;
	unsigned int symndx;
	unsigned long nr_rela, max_rela = 0, tot_rela = 0;

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

		nr_rela = 0;
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
			rela->sec = sec;
			if (!rela->sym) {
				WARN("can't find rela entry symbol %d for %s",
				     symndx, sec->name);
				return -1;
			}

			elf_add_rela(elf, rela);
			nr_rela++;
		}
		max_rela = max(max_rela, nr_rela);
		tot_rela += nr_rela;
	}

	if (stats) {
		printf("max_rela: %lu\n", max_rela);
		printf("tot_rela: %lu\n", tot_rela);
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
	elf_hash_init(elf->rela_hash);

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
	Elf_Scn *s;
	Elf_Data *data;

	sec = malloc(sizeof(*sec));
	if (!sec) {
		perror("malloc");
		return NULL;
	}
	memset(sec, 0, sizeof(*sec));

	INIT_LIST_HEAD(&sec->symbol_list);
	INIT_LIST_HEAD(&sec->rela_list);

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

	list_add_tail(&sec->list, &elf->sections);
	elf_hash_add(elf->section_hash, &sec->hash, sec->idx);
	elf_hash_add(elf->section_name_hash, &sec->name_hash, str_hash(sec->name));

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

int elf_write(const struct elf *elf)
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
