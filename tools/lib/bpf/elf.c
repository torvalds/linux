// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <linux/kernel.h>

#include "libbpf_internal.h"
#include "str_error.h"

#define STRERR_BUFSIZE  128

int elf_open(const char *binary_path, struct elf_fd *elf_fd)
{
	char errmsg[STRERR_BUFSIZE];
	int fd, ret;
	Elf *elf;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn("elf: failed to init libelf for %s\n", binary_path);
		return -LIBBPF_ERRNO__LIBELF;
	}
	fd = open(binary_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		pr_warn("elf: failed to open %s: %s\n", binary_path,
			libbpf_strerror_r(ret, errmsg, sizeof(errmsg)));
		return ret;
	}
	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		pr_warn("elf: could not read elf from %s: %s\n", binary_path, elf_errmsg(-1));
		close(fd);
		return -LIBBPF_ERRNO__FORMAT;
	}
	elf_fd->fd = fd;
	elf_fd->elf = elf;
	return 0;
}

void elf_close(struct elf_fd *elf_fd)
{
	if (!elf_fd)
		return;
	elf_end(elf_fd->elf);
	close(elf_fd->fd);
}

/* Return next ELF section of sh_type after scn, or first of that type if scn is NULL. */
static Elf_Scn *elf_find_next_scn_by_type(Elf *elf, int sh_type, Elf_Scn *scn)
{
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr sh;

		if (!gelf_getshdr(scn, &sh))
			continue;
		if (sh.sh_type == sh_type)
			return scn;
	}
	return NULL;
}

struct elf_sym {
	const char *name;
	GElf_Sym sym;
	GElf_Shdr sh;
};

struct elf_sym_iter {
	Elf *elf;
	Elf_Data *syms;
	size_t nr_syms;
	size_t strtabidx;
	size_t next_sym_idx;
	struct elf_sym sym;
	int st_type;
};

static int elf_sym_iter_new(struct elf_sym_iter *iter,
			    Elf *elf, const char *binary_path,
			    int sh_type, int st_type)
{
	Elf_Scn *scn = NULL;
	GElf_Ehdr ehdr;
	GElf_Shdr sh;

	memset(iter, 0, sizeof(*iter));

	if (!gelf_getehdr(elf, &ehdr)) {
		pr_warn("elf: failed to get ehdr from %s: %s\n", binary_path, elf_errmsg(-1));
		return -EINVAL;
	}

	scn = elf_find_next_scn_by_type(elf, sh_type, NULL);
	if (!scn) {
		pr_debug("elf: failed to find symbol table ELF sections in '%s'\n",
			 binary_path);
		return -ENOENT;
	}

	if (!gelf_getshdr(scn, &sh))
		return -EINVAL;

	iter->strtabidx = sh.sh_link;
	iter->syms = elf_getdata(scn, 0);
	if (!iter->syms) {
		pr_warn("elf: failed to get symbols for symtab section in '%s': %s\n",
			binary_path, elf_errmsg(-1));
		return -EINVAL;
	}
	iter->nr_syms = iter->syms->d_size / sh.sh_entsize;
	iter->elf = elf;
	iter->st_type = st_type;
	return 0;
}

static struct elf_sym *elf_sym_iter_next(struct elf_sym_iter *iter)
{
	struct elf_sym *ret = &iter->sym;
	GElf_Sym *sym = &ret->sym;
	const char *name = NULL;
	Elf_Scn *sym_scn;
	size_t idx;

	for (idx = iter->next_sym_idx; idx < iter->nr_syms; idx++) {
		if (!gelf_getsym(iter->syms, idx, sym))
			continue;
		if (GELF_ST_TYPE(sym->st_info) != iter->st_type)
			continue;
		name = elf_strptr(iter->elf, iter->strtabidx, sym->st_name);
		if (!name)
			continue;
		sym_scn = elf_getscn(iter->elf, sym->st_shndx);
		if (!sym_scn)
			continue;
		if (!gelf_getshdr(sym_scn, &ret->sh))
			continue;

		iter->next_sym_idx = idx + 1;
		ret->name = name;
		return ret;
	}

	return NULL;
}


/* Transform symbol's virtual address (absolute for binaries and relative
 * for shared libs) into file offset, which is what kernel is expecting
 * for uprobe/uretprobe attachment.
 * See Documentation/trace/uprobetracer.rst for more details. This is done
 * by looking up symbol's containing section's header and using iter's virtual
 * address (sh_addr) and corresponding file offset (sh_offset) to transform
 * sym.st_value (virtual address) into desired final file offset.
 */
static unsigned long elf_sym_offset(struct elf_sym *sym)
{
	return sym->sym.st_value - sym->sh.sh_addr + sym->sh.sh_offset;
}

/* Find offset of function name in the provided ELF object. "binary_path" is
 * the path to the ELF binary represented by "elf", and only used for error
 * reporting matters. "name" matches symbol name or name@@LIB for library
 * functions.
 */
long elf_find_func_offset(Elf *elf, const char *binary_path, const char *name)
{
	int i, sh_types[2] = { SHT_DYNSYM, SHT_SYMTAB };
	bool is_shared_lib, is_name_qualified;
	long ret = -ENOENT;
	size_t name_len;
	GElf_Ehdr ehdr;

	if (!gelf_getehdr(elf, &ehdr)) {
		pr_warn("elf: failed to get ehdr from %s: %s\n", binary_path, elf_errmsg(-1));
		ret = -LIBBPF_ERRNO__FORMAT;
		goto out;
	}
	/* for shared lib case, we do not need to calculate relative offset */
	is_shared_lib = ehdr.e_type == ET_DYN;

	name_len = strlen(name);
	/* Does name specify "@@LIB"? */
	is_name_qualified = strstr(name, "@@") != NULL;

	/* Search SHT_DYNSYM, SHT_SYMTAB for symbol. This search order is used because if
	 * a binary is stripped, it may only have SHT_DYNSYM, and a fully-statically
	 * linked binary may not have SHT_DYMSYM, so absence of a section should not be
	 * reported as a warning/error.
	 */
	for (i = 0; i < ARRAY_SIZE(sh_types); i++) {
		struct elf_sym_iter iter;
		struct elf_sym *sym;
		int last_bind = -1;
		int cur_bind;

		ret = elf_sym_iter_new(&iter, elf, binary_path, sh_types[i], STT_FUNC);
		if (ret == -ENOENT)
			continue;
		if (ret)
			goto out;

		while ((sym = elf_sym_iter_next(&iter))) {
			/* User can specify func, func@@LIB or func@@LIB_VERSION. */
			if (strncmp(sym->name, name, name_len) != 0)
				continue;
			/* ...but we don't want a search for "foo" to match 'foo2" also, so any
			 * additional characters in sname should be of the form "@@LIB".
			 */
			if (!is_name_qualified && sym->name[name_len] != '\0' && sym->name[name_len] != '@')
				continue;

			cur_bind = GELF_ST_BIND(sym->sym.st_info);

			if (ret > 0) {
				/* handle multiple matches */
				if (last_bind != STB_WEAK && cur_bind != STB_WEAK) {
					/* Only accept one non-weak bind. */
					pr_warn("elf: ambiguous match for '%s', '%s' in '%s'\n",
						sym->name, name, binary_path);
					ret = -LIBBPF_ERRNO__FORMAT;
					goto out;
				} else if (cur_bind == STB_WEAK) {
					/* already have a non-weak bind, and
					 * this is a weak bind, so ignore.
					 */
					continue;
				}
			}

			ret = elf_sym_offset(sym);
			last_bind = cur_bind;
		}
		if (ret > 0)
			break;
	}

	if (ret > 0) {
		pr_debug("elf: symbol address match for '%s' in '%s': 0x%lx\n", name, binary_path,
			 ret);
	} else {
		if (ret == 0) {
			pr_warn("elf: '%s' is 0 in symtab for '%s': %s\n", name, binary_path,
				is_shared_lib ? "should not be 0 in a shared library" :
						"try using shared library path instead");
			ret = -ENOENT;
		} else {
			pr_warn("elf: failed to find symbol '%s' in '%s'\n", name, binary_path);
		}
	}
out:
	return ret;
}

/* Find offset of function name in ELF object specified by path. "name" matches
 * symbol name or name@@LIB for library functions.
 */
long elf_find_func_offset_from_file(const char *binary_path, const char *name)
{
	struct elf_fd elf_fd;
	long ret = -ENOENT;

	ret = elf_open(binary_path, &elf_fd);
	if (ret)
		return ret;
	ret = elf_find_func_offset(elf_fd.elf, binary_path, name);
	elf_close(&elf_fd);
	return ret;
}

struct symbol {
	const char *name;
	int bind;
	int idx;
};

static int symbol_cmp(const void *a, const void *b)
{
	const struct symbol *sym_a = a;
	const struct symbol *sym_b = b;

	return strcmp(sym_a->name, sym_b->name);
}

/*
 * Return offsets in @poffsets for symbols specified in @syms array argument.
 * On success returns 0 and offsets are returned in allocated array with @cnt
 * size, that needs to be released by the caller.
 */
int elf_resolve_syms_offsets(const char *binary_path, int cnt,
			     const char **syms, unsigned long **poffsets)
{
	int sh_types[2] = { SHT_DYNSYM, SHT_SYMTAB };
	int err = 0, i, cnt_done = 0;
	unsigned long *offsets;
	struct symbol *symbols;
	struct elf_fd elf_fd;

	err = elf_open(binary_path, &elf_fd);
	if (err)
		return err;

	offsets = calloc(cnt, sizeof(*offsets));
	symbols = calloc(cnt, sizeof(*symbols));

	if (!offsets || !symbols) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < cnt; i++) {
		symbols[i].name = syms[i];
		symbols[i].idx = i;
	}

	qsort(symbols, cnt, sizeof(*symbols), symbol_cmp);

	for (i = 0; i < ARRAY_SIZE(sh_types); i++) {
		struct elf_sym_iter iter;
		struct elf_sym *sym;

		err = elf_sym_iter_new(&iter, elf_fd.elf, binary_path, sh_types[i], STT_FUNC);
		if (err == -ENOENT)
			continue;
		if (err)
			goto out;

		while ((sym = elf_sym_iter_next(&iter))) {
			unsigned long sym_offset = elf_sym_offset(sym);
			int bind = GELF_ST_BIND(sym->sym.st_info);
			struct symbol *found, tmp = {
				.name = sym->name,
			};
			unsigned long *offset;

			found = bsearch(&tmp, symbols, cnt, sizeof(*symbols), symbol_cmp);
			if (!found)
				continue;

			offset = &offsets[found->idx];
			if (*offset > 0) {
				/* same offset, no problem */
				if (*offset == sym_offset)
					continue;
				/* handle multiple matches */
				if (found->bind != STB_WEAK && bind != STB_WEAK) {
					/* Only accept one non-weak bind. */
					pr_warn("elf: ambiguous match found '%s@%lu' in '%s' previous offset %lu\n",
						sym->name, sym_offset, binary_path, *offset);
					err = -ESRCH;
					goto out;
				} else if (bind == STB_WEAK) {
					/* already have a non-weak bind, and
					 * this is a weak bind, so ignore.
					 */
					continue;
				}
			} else {
				cnt_done++;
			}
			*offset = sym_offset;
			found->bind = bind;
		}
	}

	if (cnt != cnt_done) {
		err = -ENOENT;
		goto out;
	}

	*poffsets = offsets;

out:
	free(symbols);
	if (err)
		free(offsets);
	elf_close(&elf_fd);
	return err;
}

/*
 * Return offsets in @poffsets for symbols specified by @pattern argument.
 * On success returns 0 and offsets are returned in allocated @poffsets
 * array with the @pctn size, that needs to be released by the caller.
 */
int elf_resolve_pattern_offsets(const char *binary_path, const char *pattern,
				unsigned long **poffsets, size_t *pcnt)
{
	int sh_types[2] = { SHT_SYMTAB, SHT_DYNSYM };
	unsigned long *offsets = NULL;
	size_t cap = 0, cnt = 0;
	struct elf_fd elf_fd;
	int err = 0, i;

	err = elf_open(binary_path, &elf_fd);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(sh_types); i++) {
		struct elf_sym_iter iter;
		struct elf_sym *sym;

		err = elf_sym_iter_new(&iter, elf_fd.elf, binary_path, sh_types[i], STT_FUNC);
		if (err == -ENOENT)
			continue;
		if (err)
			goto out;

		while ((sym = elf_sym_iter_next(&iter))) {
			if (!glob_match(sym->name, pattern))
				continue;

			err = libbpf_ensure_mem((void **) &offsets, &cap, sizeof(*offsets),
						cnt + 1);
			if (err)
				goto out;

			offsets[cnt++] = elf_sym_offset(sym);
		}

		/* If we found anything in the first symbol section,
		 * do not search others to avoid duplicates.
		 */
		if (cnt)
			break;
	}

	if (cnt) {
		*poffsets = offsets;
		*pcnt = cnt;
	} else {
		err = -ENOENT;
	}

out:
	if (err)
		free(offsets);
	elf_close(&elf_fd);
	return err;
}
