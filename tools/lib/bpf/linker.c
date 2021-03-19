// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/*
 * BPF static linker
 *
 * Copyright (c) 2021 Facebook
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/err.h>
#include <linux/btf.h>
#include <elf.h>
#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include "libbpf.h"
#include "btf.h"
#include "libbpf_internal.h"
#include "strset.h"

struct src_sec {
	const char *sec_name;
	/* positional (not necessarily ELF) index in an array of sections */
	int id;
	/* positional (not necessarily ELF) index of a matching section in a final object file */
	int dst_id;
	/* section data offset in a matching output section */
	int dst_off;
	/* whether section is omitted from the final ELF file */
	bool skipped;
	/* whether section is an ephemeral section, not mapped to an ELF section */
	bool ephemeral;

	/* ELF info */
	size_t sec_idx;
	Elf_Scn *scn;
	Elf64_Shdr *shdr;
	Elf_Data *data;

	/* corresponding BTF DATASEC type ID */
	int sec_type_id;
};

struct src_obj {
	const char *filename;
	int fd;
	Elf *elf;
	/* Section header strings section index */
	size_t shstrs_sec_idx;
	/* SYMTAB section index */
	size_t symtab_sec_idx;

	struct btf *btf;
	struct btf_ext *btf_ext;

	/* List of sections (including ephemeral). Slot zero is unused. */
	struct src_sec *secs;
	int sec_cnt;

	/* mapping of symbol indices from src to dst ELF */
	int *sym_map;
	/* mapping from the src BTF type IDs to dst ones */
	int *btf_type_map;
};

/* single .BTF.ext data section */
struct btf_ext_sec_data {
	size_t rec_cnt;
	__u32 rec_sz;
	void *recs;
};

struct dst_sec {
	char *sec_name;
	/* positional (not necessarily ELF) index in an array of sections */
	int id;

	/* ELF info */
	size_t sec_idx;
	Elf_Scn *scn;
	Elf64_Shdr *shdr;
	Elf_Data *data;

	/* final output section size */
	int sec_sz;
	/* final output contents of the section */
	void *raw_data;

	/* corresponding STT_SECTION symbol index in SYMTAB */
	int sec_sym_idx;

	/* section's DATASEC variable info, emitted on BTF finalization */
	int sec_var_cnt;
	struct btf_var_secinfo *sec_vars;

	/* section's .BTF.ext data */
	struct btf_ext_sec_data func_info;
	struct btf_ext_sec_data line_info;
	struct btf_ext_sec_data core_relo_info;
};

struct bpf_linker {
	char *filename;
	int fd;
	Elf *elf;
	Elf64_Ehdr *elf_hdr;

	/* Output sections metadata */
	struct dst_sec *secs;
	int sec_cnt;

	struct strset *strtab_strs; /* STRTAB unique strings */
	size_t strtab_sec_idx; /* STRTAB section index */
	size_t symtab_sec_idx; /* SYMTAB section index */

	struct btf *btf;
	struct btf_ext *btf_ext;
};

#define pr_warn_elf(fmt, ...)									\
do {												\
	libbpf_print(LIBBPF_WARN, "libbpf: " fmt ": %s\n", ##__VA_ARGS__, elf_errmsg(-1));	\
} while (0)

static int init_output_elf(struct bpf_linker *linker, const char *file);

static int linker_load_obj_file(struct bpf_linker *linker, const char *filename, struct src_obj *obj);
static int linker_sanity_check_elf(struct src_obj *obj);
static int linker_sanity_check_btf(struct src_obj *obj);
static int linker_sanity_check_btf_ext(struct src_obj *obj);
static int linker_fixup_btf(struct src_obj *obj);
static int linker_append_sec_data(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_elf_syms(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_elf_relos(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_btf(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_btf_ext(struct bpf_linker *linker, struct src_obj *obj);

static int finalize_btf(struct bpf_linker *linker);
static int finalize_btf_ext(struct bpf_linker *linker);

void bpf_linker__free(struct bpf_linker *linker)
{
	int i;

	if (!linker)
		return;

	free(linker->filename);

	if (linker->elf)
		elf_end(linker->elf);

	if (linker->fd >= 0)
		close(linker->fd);

	strset__free(linker->strtab_strs);

	btf__free(linker->btf);
	btf_ext__free(linker->btf_ext);

	for (i = 1; i < linker->sec_cnt; i++) {
		struct dst_sec *sec = &linker->secs[i];

		free(sec->sec_name);
		free(sec->raw_data);
		free(sec->sec_vars);

		free(sec->func_info.recs);
		free(sec->line_info.recs);
		free(sec->core_relo_info.recs);
	}
	free(linker->secs);

	free(linker);
}

struct bpf_linker *bpf_linker__new(const char *filename, struct bpf_linker_opts *opts)
{
	struct bpf_linker *linker;
	int err;

	if (!OPTS_VALID(opts, bpf_linker_opts))
		return NULL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn_elf("libelf initialization failed");
		return NULL;
	}

	linker = calloc(1, sizeof(*linker));
	if (!linker)
		return NULL;

	linker->fd = -1;

	err = init_output_elf(linker, filename);
	if (err)
		goto err_out;

	return linker;

err_out:
	bpf_linker__free(linker);
	return NULL;
}

static struct dst_sec *add_dst_sec(struct bpf_linker *linker, const char *sec_name)
{
	struct dst_sec *secs = linker->secs, *sec;
	size_t new_cnt = linker->sec_cnt ? linker->sec_cnt + 1 : 2;

	secs = libbpf_reallocarray(secs, new_cnt, sizeof(*secs));
	if (!secs)
		return NULL;

	/* zero out newly allocated memory */
	memset(secs + linker->sec_cnt, 0, (new_cnt - linker->sec_cnt) * sizeof(*secs));

	linker->secs = secs;
	linker->sec_cnt = new_cnt;

	sec = &linker->secs[new_cnt - 1];
	sec->id = new_cnt - 1;
	sec->sec_name = strdup(sec_name);
	if (!sec->sec_name)
		return NULL;

	return sec;
}

static Elf64_Sym *add_new_sym(struct bpf_linker *linker, size_t *sym_idx)
{
	struct dst_sec *symtab = &linker->secs[linker->symtab_sec_idx];
	Elf64_Sym *syms, *sym;
	size_t sym_cnt = symtab->sec_sz / sizeof(*sym);

	syms = libbpf_reallocarray(symtab->raw_data, sym_cnt + 1, sizeof(*sym));
	if (!syms)
		return NULL;

	sym = &syms[sym_cnt];
	memset(sym, 0, sizeof(*sym));

	symtab->raw_data = syms;
	symtab->sec_sz += sizeof(*sym);
	symtab->shdr->sh_size += sizeof(*sym);
	symtab->data->d_size += sizeof(*sym);

	if (sym_idx)
		*sym_idx = sym_cnt;

	return sym;
}

static int init_output_elf(struct bpf_linker *linker, const char *file)
{
	int err, str_off;
	Elf64_Sym *init_sym;
	struct dst_sec *sec;

	linker->filename = strdup(file);
	if (!linker->filename)
		return -ENOMEM;

	linker->fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (linker->fd < 0) {
		err = -errno;
		pr_warn("failed to create '%s': %d\n", file, err);
		return err;
	}

	linker->elf = elf_begin(linker->fd, ELF_C_WRITE, NULL);
	if (!linker->elf) {
		pr_warn_elf("failed to create ELF object");
		return -EINVAL;
	}

	/* ELF header */
	linker->elf_hdr = elf64_newehdr(linker->elf);
	if (!linker->elf_hdr){
		pr_warn_elf("failed to create ELF header");
		return -EINVAL;
	}

	linker->elf_hdr->e_machine = EM_BPF;
	linker->elf_hdr->e_type = ET_REL;
#if __BYTE_ORDER == __LITTLE_ENDIAN
	linker->elf_hdr->e_ident[EI_DATA] = ELFDATA2LSB;
#elif __BYTE_ORDER == __BIG_ENDIAN
	linker->elf_hdr->e_ident[EI_DATA] = ELFDATA2MSB;
#else
#error "Unknown __BYTE_ORDER"
#endif

	/* STRTAB */
	/* initialize strset with an empty string to conform to ELF */
	linker->strtab_strs = strset__new(INT_MAX, "", sizeof(""));
	if (libbpf_get_error(linker->strtab_strs))
		return libbpf_get_error(linker->strtab_strs);

	sec = add_dst_sec(linker, ".strtab");
	if (!sec)
		return -ENOMEM;

	sec->scn = elf_newscn(linker->elf);
	if (!sec->scn) {
		pr_warn_elf("failed to create STRTAB section");
		return -EINVAL;
	}

	sec->shdr = elf64_getshdr(sec->scn);
	if (!sec->shdr)
		return -EINVAL;

	sec->data = elf_newdata(sec->scn);
	if (!sec->data) {
		pr_warn_elf("failed to create STRTAB data");
		return -EINVAL;
	}

	str_off = strset__add_str(linker->strtab_strs, sec->sec_name);
	if (str_off < 0)
		return str_off;

	sec->sec_idx = elf_ndxscn(sec->scn);
	linker->elf_hdr->e_shstrndx = sec->sec_idx;
	linker->strtab_sec_idx = sec->sec_idx;

	sec->shdr->sh_name = str_off;
	sec->shdr->sh_type = SHT_STRTAB;
	sec->shdr->sh_flags = SHF_STRINGS;
	sec->shdr->sh_offset = 0;
	sec->shdr->sh_link = 0;
	sec->shdr->sh_info = 0;
	sec->shdr->sh_addralign = 1;
	sec->shdr->sh_size = sec->sec_sz = 0;
	sec->shdr->sh_entsize = 0;

	/* SYMTAB */
	sec = add_dst_sec(linker, ".symtab");
	if (!sec)
		return -ENOMEM;

	sec->scn = elf_newscn(linker->elf);
	if (!sec->scn) {
		pr_warn_elf("failed to create SYMTAB section");
		return -EINVAL;
	}

	sec->shdr = elf64_getshdr(sec->scn);
	if (!sec->shdr)
		return -EINVAL;

	sec->data = elf_newdata(sec->scn);
	if (!sec->data) {
		pr_warn_elf("failed to create SYMTAB data");
		return -EINVAL;
	}

	str_off = strset__add_str(linker->strtab_strs, sec->sec_name);
	if (str_off < 0)
		return str_off;

	sec->sec_idx = elf_ndxscn(sec->scn);
	linker->symtab_sec_idx = sec->sec_idx;

	sec->shdr->sh_name = str_off;
	sec->shdr->sh_type = SHT_SYMTAB;
	sec->shdr->sh_flags = 0;
	sec->shdr->sh_offset = 0;
	sec->shdr->sh_link = linker->strtab_sec_idx;
	/* sh_info should be one greater than the index of the last local
	 * symbol (i.e., binding is STB_LOCAL). But why and who cares?
	 */
	sec->shdr->sh_info = 0;
	sec->shdr->sh_addralign = 8;
	sec->shdr->sh_entsize = sizeof(Elf64_Sym);

	/* .BTF */
	linker->btf = btf__new_empty();
	err = libbpf_get_error(linker->btf);
	if (err)
		return err;

	/* add the special all-zero symbol */
	init_sym = add_new_sym(linker, NULL);
	if (!init_sym)
		return -EINVAL;

	init_sym->st_name = 0;
	init_sym->st_info = 0;
	init_sym->st_other = 0;
	init_sym->st_shndx = SHN_UNDEF;
	init_sym->st_value = 0;
	init_sym->st_size = 0;

	return 0;
}

int bpf_linker__add_file(struct bpf_linker *linker, const char *filename)
{
	struct src_obj obj = {};
	int err = 0;

	if (!linker->elf)
		return -EINVAL;

	err = err ?: linker_load_obj_file(linker, filename, &obj);
	err = err ?: linker_append_sec_data(linker, &obj);
	err = err ?: linker_append_elf_syms(linker, &obj);
	err = err ?: linker_append_elf_relos(linker, &obj);
	err = err ?: linker_append_btf(linker, &obj);
	err = err ?: linker_append_btf_ext(linker, &obj);

	/* free up src_obj resources */
	free(obj.btf_type_map);
	btf__free(obj.btf);
	btf_ext__free(obj.btf_ext);
	free(obj.secs);
	free(obj.sym_map);
	if (obj.elf)
		elf_end(obj.elf);
	if (obj.fd >= 0)
		close(obj.fd);

	return err;
}

static bool is_dwarf_sec_name(const char *name)
{
	/* approximation, but the actual list is too long */
	return strncmp(name, ".debug_", sizeof(".debug_") - 1) == 0;
}

static bool is_ignored_sec(struct src_sec *sec)
{
	Elf64_Shdr *shdr = sec->shdr;
	const char *name = sec->sec_name;

	/* no special handling of .strtab */
	if (shdr->sh_type == SHT_STRTAB)
		return true;

	/* ignore .llvm_addrsig section as well */
	if (shdr->sh_type == SHT_LLVM_ADDRSIG)
		return true;

	/* no subprograms will lead to an empty .text section, ignore it */
	if (shdr->sh_type == SHT_PROGBITS && shdr->sh_size == 0 &&
	    strcmp(sec->sec_name, ".text") == 0)
		return true;

	/* DWARF sections */
	if (is_dwarf_sec_name(sec->sec_name))
		return true;

	if (strncmp(name, ".rel", sizeof(".rel") - 1) == 0) {
		name += sizeof(".rel") - 1;
		/* DWARF section relocations */
		if (is_dwarf_sec_name(name))
			return true;

		/* .BTF and .BTF.ext don't need relocations */
		if (strcmp(name, BTF_ELF_SEC) == 0 ||
		    strcmp(name, BTF_EXT_ELF_SEC) == 0)
			return true;
	}

	return false;
}

static struct src_sec *add_src_sec(struct src_obj *obj, const char *sec_name)
{
	struct src_sec *secs = obj->secs, *sec;
	size_t new_cnt = obj->sec_cnt ? obj->sec_cnt + 1 : 2;

	secs = libbpf_reallocarray(secs, new_cnt, sizeof(*secs));
	if (!secs)
		return NULL;

	/* zero out newly allocated memory */
	memset(secs + obj->sec_cnt, 0, (new_cnt - obj->sec_cnt) * sizeof(*secs));

	obj->secs = secs;
	obj->sec_cnt = new_cnt;

	sec = &obj->secs[new_cnt - 1];
	sec->id = new_cnt - 1;
	sec->sec_name = sec_name;

	return sec;
}

static int linker_load_obj_file(struct bpf_linker *linker, const char *filename, struct src_obj *obj)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	const int host_endianness = ELFDATA2LSB;
#elif __BYTE_ORDER == __BIG_ENDIAN
	const int host_endianness = ELFDATA2MSB;
#else
#error "Unknown __BYTE_ORDER"
#endif
	int err = 0;
	Elf_Scn *scn;
	Elf_Data *data;
	Elf64_Ehdr *ehdr;
	Elf64_Shdr *shdr;
	struct src_sec *sec;

	pr_debug("linker: adding object file '%s'...\n", filename);

	obj->filename = filename;

	obj->fd = open(filename, O_RDONLY);
	if (obj->fd < 0) {
		err = -errno;
		pr_warn("failed to open file '%s': %d\n", filename, err);
		return err;
	}
	obj->elf = elf_begin(obj->fd, ELF_C_READ_MMAP, NULL);
	if (!obj->elf) {
		err = -errno;
		pr_warn_elf("failed to parse ELF file '%s'", filename);
		return err;
	}

	/* Sanity check ELF file high-level properties */
	ehdr = elf64_getehdr(obj->elf);
	if (!ehdr) {
		err = -errno;
		pr_warn_elf("failed to get ELF header for %s", filename);
		return err;
	}
	if (ehdr->e_ident[EI_DATA] != host_endianness) {
		err = -EOPNOTSUPP;
		pr_warn_elf("unsupported byte order of ELF file %s", filename);
		return err;
	}
	if (ehdr->e_type != ET_REL
	    || ehdr->e_machine != EM_BPF
	    || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
		err = -EOPNOTSUPP;
		pr_warn_elf("unsupported kind of ELF file %s", filename);
		return err;
	}

	if (elf_getshdrstrndx(obj->elf, &obj->shstrs_sec_idx)) {
		err = -errno;
		pr_warn_elf("failed to get SHSTRTAB section index for %s", filename);
		return err;
	}

	scn = NULL;
	while ((scn = elf_nextscn(obj->elf, scn)) != NULL) {
		size_t sec_idx = elf_ndxscn(scn);
		const char *sec_name;

		shdr = elf64_getshdr(scn);
		if (!shdr) {
			err = -errno;
			pr_warn_elf("failed to get section #%zu header for %s",
				    sec_idx, filename);
			return err;
		}

		sec_name = elf_strptr(obj->elf, obj->shstrs_sec_idx, shdr->sh_name);
		if (!sec_name) {
			err = -errno;
			pr_warn_elf("failed to get section #%zu name for %s",
				    sec_idx, filename);
			return err;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			err = -errno;
			pr_warn_elf("failed to get section #%zu (%s) data from %s",
				    sec_idx, sec_name, filename);
			return err;
		}

		sec = add_src_sec(obj, sec_name);
		if (!sec)
			return -ENOMEM;

		sec->scn = scn;
		sec->shdr = shdr;
		sec->data = data;
		sec->sec_idx = elf_ndxscn(scn);

		if (is_ignored_sec(sec)) {
			sec->skipped = true;
			continue;
		}

		switch (shdr->sh_type) {
		case SHT_SYMTAB:
			if (obj->symtab_sec_idx) {
				err = -EOPNOTSUPP;
				pr_warn("multiple SYMTAB sections found, not supported\n");
				return err;
			}
			obj->symtab_sec_idx = sec_idx;
			break;
		case SHT_STRTAB:
			/* we'll construct our own string table */
			break;
		case SHT_PROGBITS:
			if (strcmp(sec_name, BTF_ELF_SEC) == 0) {
				obj->btf = btf__new(data->d_buf, shdr->sh_size);
				err = libbpf_get_error(obj->btf);
				if (err) {
					pr_warn("failed to parse .BTF from %s: %d\n", filename, err);
					return err;
				}
				sec->skipped = true;
				continue;
			}
			if (strcmp(sec_name, BTF_EXT_ELF_SEC) == 0) {
				obj->btf_ext = btf_ext__new(data->d_buf, shdr->sh_size);
				err = libbpf_get_error(obj->btf_ext);
				if (err) {
					pr_warn("failed to parse .BTF.ext from '%s': %d\n", filename, err);
					return err;
				}
				sec->skipped = true;
				continue;
			}

			/* data & code */
			break;
		case SHT_NOBITS:
			/* BSS */
			break;
		case SHT_REL:
			/* relocations */
			break;
		default:
			pr_warn("unrecognized section #%zu (%s) in %s\n",
				sec_idx, sec_name, filename);
			err = -EINVAL;
			return err;
		}
	}

	err = err ?: linker_sanity_check_elf(obj);
	err = err ?: linker_sanity_check_btf(obj);
	err = err ?: linker_sanity_check_btf_ext(obj);
	err = err ?: linker_fixup_btf(obj);

	return err;
}

static bool is_pow_of_2(size_t x)
{
	return x && (x & (x - 1)) == 0;
}

static int linker_sanity_check_elf(struct src_obj *obj)
{
	struct src_sec *sec, *link_sec;
	int i, j, n;

	if (!obj->symtab_sec_idx) {
		pr_warn("ELF is missing SYMTAB section in %s\n", obj->filename);
		return -EINVAL;
	}
	if (!obj->shstrs_sec_idx) {
		pr_warn("ELF is missing section headers STRTAB section in %s\n", obj->filename);
		return -EINVAL;
	}

	for (i = 1; i < obj->sec_cnt; i++) {
		sec = &obj->secs[i];

		if (sec->sec_name[0] == '\0') {
			pr_warn("ELF section #%zu has empty name in %s\n", sec->sec_idx, obj->filename);
			return -EINVAL;
		}

		if (sec->shdr->sh_addralign && !is_pow_of_2(sec->shdr->sh_addralign))
			return -EINVAL;
		if (sec->shdr->sh_addralign != sec->data->d_align)
			return -EINVAL;

		if (sec->shdr->sh_size != sec->data->d_size)
			return -EINVAL;

		switch (sec->shdr->sh_type) {
		case SHT_SYMTAB: {
			Elf64_Sym *sym;

			if (sec->shdr->sh_entsize != sizeof(Elf64_Sym))
				return -EINVAL;
			if (sec->shdr->sh_size % sec->shdr->sh_entsize != 0)
				return -EINVAL;

			if (!sec->shdr->sh_link || sec->shdr->sh_link >= obj->sec_cnt) {
				pr_warn("ELF SYMTAB section #%zu points to missing STRTAB section #%zu in %s\n",
					sec->sec_idx, (size_t)sec->shdr->sh_link, obj->filename);
				return -EINVAL;
			}
			link_sec = &obj->secs[sec->shdr->sh_link];
			if (link_sec->shdr->sh_type != SHT_STRTAB) {
				pr_warn("ELF SYMTAB section #%zu points to invalid STRTAB section #%zu in %s\n",
					sec->sec_idx, (size_t)sec->shdr->sh_link, obj->filename);
				return -EINVAL;
			}

			n = sec->shdr->sh_size / sec->shdr->sh_entsize;
			sym = sec->data->d_buf;
			for (j = 0; j < n; j++, sym++) {
				if (sym->st_shndx
				    && sym->st_shndx < SHN_LORESERVE
				    && sym->st_shndx >= obj->sec_cnt) {
					pr_warn("ELF sym #%d in section #%zu points to missing section #%zu in %s\n",
						j, sec->sec_idx, (size_t)sym->st_shndx, obj->filename);
					return -EINVAL;
				}
				if (ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
					if (sym->st_value != 0)
						return -EINVAL;
				}
			}
			break;
		}
		case SHT_STRTAB:
			break;
		case SHT_PROGBITS:
			if (sec->shdr->sh_flags & SHF_EXECINSTR) {
				if (sec->shdr->sh_size % sizeof(struct bpf_insn) != 0)
					return -EINVAL;
			}
			break;
		case SHT_NOBITS:
			break;
		case SHT_REL: {
			Elf64_Rel *relo;
			struct src_sec *sym_sec;

			if (sec->shdr->sh_entsize != sizeof(Elf64_Rel))
				return -EINVAL;
			if (sec->shdr->sh_size % sec->shdr->sh_entsize != 0)
				return -EINVAL;

			/* SHT_REL's sh_link should point to SYMTAB */
			if (sec->shdr->sh_link != obj->symtab_sec_idx) {
				pr_warn("ELF relo section #%zu points to invalid SYMTAB section #%zu in %s\n",
					sec->sec_idx, (size_t)sec->shdr->sh_link, obj->filename);
				return -EINVAL;
			}

			/* SHT_REL's sh_info points to relocated section */
			if (!sec->shdr->sh_info || sec->shdr->sh_info >= obj->sec_cnt) {
				pr_warn("ELF relo section #%zu points to missing section #%zu in %s\n",
					sec->sec_idx, (size_t)sec->shdr->sh_info, obj->filename);
				return -EINVAL;
			}
			link_sec = &obj->secs[sec->shdr->sh_info];

			/* .rel<secname> -> <secname> pattern is followed */
			if (strncmp(sec->sec_name, ".rel", sizeof(".rel") - 1) != 0
			    || strcmp(sec->sec_name + sizeof(".rel") - 1, link_sec->sec_name) != 0) {
				pr_warn("ELF relo section #%zu name has invalid name in %s\n",
					sec->sec_idx, obj->filename);
				return -EINVAL;
			}

			/* don't further validate relocations for ignored sections */
			if (link_sec->skipped)
				break;

			/* relocatable section is data or instructions */
			if (link_sec->shdr->sh_type != SHT_PROGBITS
			    && link_sec->shdr->sh_type != SHT_NOBITS) {
				pr_warn("ELF relo section #%zu points to invalid section #%zu in %s\n",
					sec->sec_idx, (size_t)sec->shdr->sh_info, obj->filename);
				return -EINVAL;
			}

			/* check sanity of each relocation */
			n = sec->shdr->sh_size / sec->shdr->sh_entsize;
			relo = sec->data->d_buf;
			sym_sec = &obj->secs[obj->symtab_sec_idx];
			for (j = 0; j < n; j++, relo++) {
				size_t sym_idx = ELF64_R_SYM(relo->r_info);
				size_t sym_type = ELF64_R_TYPE(relo->r_info);

				if (sym_type != R_BPF_64_64 && sym_type != R_BPF_64_32) {
					pr_warn("ELF relo #%d in section #%zu has unexpected type %zu in %s\n",
						j, sec->sec_idx, sym_type, obj->filename);
					return -EINVAL;
				}

				if (!sym_idx || sym_idx * sizeof(Elf64_Sym) >= sym_sec->shdr->sh_size) {
					pr_warn("ELF relo #%d in section #%zu points to invalid symbol #%zu in %s\n",
						j, sec->sec_idx, sym_idx, obj->filename);
					return -EINVAL;
				}

				if (link_sec->shdr->sh_flags & SHF_EXECINSTR) {
					if (relo->r_offset % sizeof(struct bpf_insn) != 0) {
						pr_warn("ELF relo #%d in section #%zu points to missing symbol #%zu in %s\n",
							j, sec->sec_idx, sym_idx, obj->filename);
						return -EINVAL;
					}
				}
			}
			break;
		}
		case SHT_LLVM_ADDRSIG:
			break;
		default:
			pr_warn("ELF section #%zu (%s) has unrecognized type %zu in %s\n",
				sec->sec_idx, sec->sec_name, (size_t)sec->shdr->sh_type, obj->filename);
			return -EINVAL;
		}
	}

	return 0;
}

static int check_btf_type_id(__u32 *type_id, void *ctx)
{
	struct btf *btf = ctx;

	if (*type_id > btf__get_nr_types(btf))
		return -EINVAL;

	return 0;
}

static int check_btf_str_off(__u32 *str_off, void *ctx)
{
	struct btf *btf = ctx;
	const char *s;

	s = btf__str_by_offset(btf, *str_off);

	if (!s)
		return -EINVAL;

	return 0;
}

static int linker_sanity_check_btf(struct src_obj *obj)
{
	struct btf_type *t;
	int i, n, err = 0;

	if (!obj->btf)
		return 0;

	n = btf__get_nr_types(obj->btf);
	for (i = 1; i <= n; i++) {
		t = btf_type_by_id(obj->btf, i);

		err = err ?: btf_type_visit_type_ids(t, check_btf_type_id, obj->btf);
		err = err ?: btf_type_visit_str_offs(t, check_btf_str_off, obj->btf);
		if (err)
			return err;
	}

	return 0;
}

static int linker_sanity_check_btf_ext(struct src_obj *obj)
{
	int err = 0;

	if (!obj->btf_ext)
		return 0;

	/* can't use .BTF.ext without .BTF */
	if (!obj->btf)
		return -EINVAL;

	err = err ?: btf_ext_visit_type_ids(obj->btf_ext, check_btf_type_id, obj->btf);
	err = err ?: btf_ext_visit_str_offs(obj->btf_ext, check_btf_str_off, obj->btf);
	if (err)
		return err;

	return 0;
}

static int init_sec(struct bpf_linker *linker, struct dst_sec *dst_sec, struct src_sec *src_sec)
{
	Elf_Scn *scn;
	Elf_Data *data;
	Elf64_Shdr *shdr;
	int name_off;

	dst_sec->sec_sz = 0;
	dst_sec->sec_idx = 0;

	/* ephemeral sections are just thin section shells lacking most parts */
	if (src_sec->ephemeral)
		return 0;

	scn = elf_newscn(linker->elf);
	if (!scn)
		return -1;
	data = elf_newdata(scn);
	if (!data)
		return -1;
	shdr = elf64_getshdr(scn);
	if (!shdr)
		return -1;

	dst_sec->scn = scn;
	dst_sec->shdr = shdr;
	dst_sec->data = data;
	dst_sec->sec_idx = elf_ndxscn(scn);

	name_off = strset__add_str(linker->strtab_strs, src_sec->sec_name);
	if (name_off < 0)
		return name_off;

	shdr->sh_name = name_off;
	shdr->sh_type = src_sec->shdr->sh_type;
	shdr->sh_flags = src_sec->shdr->sh_flags;
	shdr->sh_size = 0;
	/* sh_link and sh_info have different meaning for different types of
	 * sections, so we leave it up to the caller code to fill them in, if
	 * necessary
	 */
	shdr->sh_link = 0;
	shdr->sh_info = 0;
	shdr->sh_addralign = src_sec->shdr->sh_addralign;
	shdr->sh_entsize = src_sec->shdr->sh_entsize;

	data->d_type = src_sec->data->d_type;
	data->d_size = 0;
	data->d_buf = NULL;
	data->d_align = src_sec->data->d_align;
	data->d_off = 0;

	return 0;
}

static struct dst_sec *find_dst_sec_by_name(struct bpf_linker *linker, const char *sec_name)
{
	struct dst_sec *sec;
	int i;

	for (i = 1; i < linker->sec_cnt; i++) {
		sec = &linker->secs[i];

		if (strcmp(sec->sec_name, sec_name) == 0)
			return sec;
	}

	return NULL;
}

static bool secs_match(struct dst_sec *dst, struct src_sec *src)
{
	if (dst->shdr->sh_type != src->shdr->sh_type) {
		pr_warn("sec %s types mismatch\n", dst->sec_name);
		return false;
	}
	if (dst->shdr->sh_flags != src->shdr->sh_flags) {
		pr_warn("sec %s flags mismatch\n", dst->sec_name);
		return false;
	}
	if (dst->shdr->sh_entsize != src->shdr->sh_entsize) {
		pr_warn("sec %s entsize mismatch\n", dst->sec_name);
		return false;
	}

	return true;
}

static bool sec_content_is_same(struct dst_sec *dst_sec, struct src_sec *src_sec)
{
	if (dst_sec->sec_sz != src_sec->shdr->sh_size)
		return false;
	if (memcmp(dst_sec->raw_data, src_sec->data->d_buf, dst_sec->sec_sz) != 0)
		return false;
	return true;
}

static int extend_sec(struct dst_sec *dst, struct src_sec *src)
{
	void *tmp;
	size_t dst_align = dst->shdr->sh_addralign;
	size_t src_align = src->shdr->sh_addralign;
	size_t dst_align_sz, dst_final_sz;

	if (dst_align == 0)
		dst_align = 1;
	if (dst_align < src_align)
		dst_align = src_align;

	dst_align_sz = (dst->sec_sz + dst_align - 1) / dst_align * dst_align;

	/* no need to re-align final size */
	dst_final_sz = dst_align_sz + src->shdr->sh_size;

	if (src->shdr->sh_type != SHT_NOBITS) {
		tmp = realloc(dst->raw_data, dst_final_sz);
		if (!tmp)
			return -ENOMEM;
		dst->raw_data = tmp;

		/* pad dst section, if it's alignment forced size increase */
		memset(dst->raw_data + dst->sec_sz, 0, dst_align_sz - dst->sec_sz);
		/* now copy src data at a properly aligned offset */
		memcpy(dst->raw_data + dst_align_sz, src->data->d_buf, src->shdr->sh_size);
	}

	dst->sec_sz = dst_final_sz;
	dst->shdr->sh_size = dst_final_sz;
	dst->data->d_size = dst_final_sz;

	dst->shdr->sh_addralign = dst_align;
	dst->data->d_align = dst_align;

	src->dst_off = dst_align_sz;

	return 0;
}

static bool is_data_sec(struct src_sec *sec)
{
	if (!sec || sec->skipped)
		return false;
	/* ephemeral sections are data sections, e.g., .kconfig, .ksyms */
	if (sec->ephemeral)
		return true;
	return sec->shdr->sh_type == SHT_PROGBITS || sec->shdr->sh_type == SHT_NOBITS;
}

static bool is_relo_sec(struct src_sec *sec)
{
	if (!sec || sec->skipped || sec->ephemeral)
		return false;
	return sec->shdr->sh_type == SHT_REL;
}

static int linker_append_sec_data(struct bpf_linker *linker, struct src_obj *obj)
{
	int i, err;

	for (i = 1; i < obj->sec_cnt; i++) {
		struct src_sec *src_sec;
		struct dst_sec *dst_sec;

		src_sec = &obj->secs[i];
		if (!is_data_sec(src_sec))
			continue;

		dst_sec = find_dst_sec_by_name(linker, src_sec->sec_name);
		if (!dst_sec) {
			dst_sec = add_dst_sec(linker, src_sec->sec_name);
			if (!dst_sec)
				return -ENOMEM;
			err = init_sec(linker, dst_sec, src_sec);
			if (err) {
				pr_warn("failed to init section '%s'\n", src_sec->sec_name);
				return err;
			}
		} else {
			if (!secs_match(dst_sec, src_sec)) {
				pr_warn("ELF sections %s are incompatible\n", src_sec->sec_name);
				return -1;
			}

			/* "license" and "version" sections are deduped */
			if (strcmp(src_sec->sec_name, "license") == 0
			    || strcmp(src_sec->sec_name, "version") == 0) {
				if (!sec_content_is_same(dst_sec, src_sec)) {
					pr_warn("non-identical contents of section '%s' are not supported\n", src_sec->sec_name);
					return -EINVAL;
				}
				src_sec->skipped = true;
				src_sec->dst_id = dst_sec->id;
				continue;
			}
		}

		/* record mapped section index */
		src_sec->dst_id = dst_sec->id;

		if (src_sec->ephemeral)
			continue;

		err = extend_sec(dst_sec, src_sec);
		if (err)
			return err;
	}

	return 0;
}

static int linker_append_elf_syms(struct bpf_linker *linker, struct src_obj *obj)
{
	struct src_sec *symtab = &obj->secs[obj->symtab_sec_idx];
	Elf64_Sym *sym = symtab->data->d_buf, *dst_sym;
	int i, n = symtab->shdr->sh_size / symtab->shdr->sh_entsize;
	int str_sec_idx = symtab->shdr->sh_link;

	obj->sym_map = calloc(n + 1, sizeof(*obj->sym_map));
	if (!obj->sym_map)
		return -ENOMEM;

	for (i = 0; i < n; i++, sym++) {
		struct src_sec *src_sec = NULL;
		struct dst_sec *dst_sec = NULL;
		const char *sym_name;
		size_t dst_sym_idx;
		int name_off;

		/* we already have all-zero initial symbol */
		if (sym->st_name == 0 && sym->st_info == 0 &&
		    sym->st_other == 0 && sym->st_shndx == SHN_UNDEF &&
		    sym->st_value == 0 && sym->st_size ==0)
			continue;

		sym_name = elf_strptr(obj->elf, str_sec_idx, sym->st_name);
		if (!sym_name) {
			pr_warn("can't fetch symbol name for symbol #%d in '%s'\n", i, obj->filename);
			return -1;
		}

		if (sym->st_shndx && sym->st_shndx < SHN_LORESERVE) {
			src_sec = &obj->secs[sym->st_shndx];
			if (src_sec->skipped)
				continue;
			dst_sec = &linker->secs[src_sec->dst_id];

			/* allow only one STT_SECTION symbol per section */
			if (ELF64_ST_TYPE(sym->st_info) == STT_SECTION && dst_sec->sec_sym_idx) {
				obj->sym_map[i] = dst_sec->sec_sym_idx;
				continue;
			}
		}

		name_off = strset__add_str(linker->strtab_strs, sym_name);
		if (name_off < 0)
			return name_off;

		dst_sym = add_new_sym(linker, &dst_sym_idx);
		if (!dst_sym)
			return -ENOMEM;

		dst_sym->st_name = name_off;
		dst_sym->st_info = sym->st_info;
		dst_sym->st_other = sym->st_other;
		dst_sym->st_shndx = src_sec ? dst_sec->sec_idx : sym->st_shndx;
		dst_sym->st_value = (src_sec ? src_sec->dst_off : 0) + sym->st_value;
		dst_sym->st_size = sym->st_size;

		obj->sym_map[i] = dst_sym_idx;

		if (ELF64_ST_TYPE(sym->st_info) == STT_SECTION && dst_sym) {
			dst_sec->sec_sym_idx = dst_sym_idx;
			dst_sym->st_value = 0;
		}

	}

	return 0;
}

static int linker_append_elf_relos(struct bpf_linker *linker, struct src_obj *obj)
{
	struct src_sec *src_symtab = &obj->secs[obj->symtab_sec_idx];
	struct dst_sec *dst_symtab = &linker->secs[linker->symtab_sec_idx];
	int i, err;

	for (i = 1; i < obj->sec_cnt; i++) {
		struct src_sec *src_sec, *src_linked_sec;
		struct dst_sec *dst_sec, *dst_linked_sec;
		Elf64_Rel *src_rel, *dst_rel;
		int j, n;

		src_sec = &obj->secs[i];
		if (!is_relo_sec(src_sec))
			continue;

		/* shdr->sh_info points to relocatable section */
		src_linked_sec = &obj->secs[src_sec->shdr->sh_info];
		if (src_linked_sec->skipped)
			continue;

		dst_sec = find_dst_sec_by_name(linker, src_sec->sec_name);
		if (!dst_sec) {
			dst_sec = add_dst_sec(linker, src_sec->sec_name);
			if (!dst_sec)
				return -ENOMEM;
			err = init_sec(linker, dst_sec, src_sec);
			if (err) {
				pr_warn("failed to init section '%s'\n", src_sec->sec_name);
				return err;
			}
		} else if (!secs_match(dst_sec, src_sec)) {
			pr_warn("Secs %s are not compatible\n", src_sec->sec_name);
			return -1;
		}

		/* shdr->sh_link points to SYMTAB */
		dst_sec->shdr->sh_link = linker->symtab_sec_idx;

		/* shdr->sh_info points to relocated section */
		dst_linked_sec = &linker->secs[src_linked_sec->dst_id];
		dst_sec->shdr->sh_info = dst_linked_sec->sec_idx;

		src_sec->dst_id = dst_sec->id;
		err = extend_sec(dst_sec, src_sec);
		if (err)
			return err;

		src_rel = src_sec->data->d_buf;
		dst_rel = dst_sec->raw_data + src_sec->dst_off;
		n = src_sec->shdr->sh_size / src_sec->shdr->sh_entsize;
		for (j = 0; j < n; j++, src_rel++, dst_rel++) {
			size_t src_sym_idx = ELF64_R_SYM(src_rel->r_info);
			size_t sym_type = ELF64_R_TYPE(src_rel->r_info);
			Elf64_Sym *src_sym, *dst_sym;
			size_t dst_sym_idx;

			src_sym_idx = ELF64_R_SYM(src_rel->r_info);
			src_sym = src_symtab->data->d_buf + sizeof(*src_sym) * src_sym_idx;

			dst_sym_idx = obj->sym_map[src_sym_idx];
			dst_sym = dst_symtab->raw_data + sizeof(*dst_sym) * dst_sym_idx;
			dst_rel->r_offset += src_linked_sec->dst_off;
			sym_type = ELF64_R_TYPE(src_rel->r_info);
			dst_rel->r_info = ELF64_R_INFO(dst_sym_idx, sym_type);

			if (ELF64_ST_TYPE(src_sym->st_info) == STT_SECTION) {
				struct src_sec *sec = &obj->secs[src_sym->st_shndx];
				struct bpf_insn *insn;

				if (src_linked_sec->shdr->sh_flags & SHF_EXECINSTR) {
					/* calls to the very first static function inside
					 * .text section at offset 0 will
					 * reference section symbol, not the
					 * function symbol. Fix that up,
					 * otherwise it won't be possible to
					 * relocate calls to two different
					 * static functions with the same name
					 * (rom two different object files)
					 */
					insn = dst_linked_sec->raw_data + dst_rel->r_offset;
					if (insn->code == (BPF_JMP | BPF_CALL))
						insn->imm += sec->dst_off / sizeof(struct bpf_insn);
					else
						insn->imm += sec->dst_off;
				} else {
					pr_warn("relocation against STT_SECTION in non-exec section is not supported!\n");
					return -EINVAL;
				}
			}

		}
	}

	return 0;
}

static struct src_sec *find_src_sec_by_name(struct src_obj *obj, const char *sec_name)
{
	struct src_sec *sec;
	int i;

	for (i = 1; i < obj->sec_cnt; i++) {
		sec = &obj->secs[i];

		if (strcmp(sec->sec_name, sec_name) == 0)
			return sec;
	}

	return NULL;
}

static Elf64_Sym *find_sym_by_name(struct src_obj *obj, size_t sec_idx,
				   int sym_type, const char *sym_name)
{
	struct src_sec *symtab = &obj->secs[obj->symtab_sec_idx];
	Elf64_Sym *sym = symtab->data->d_buf;
	int i, n = symtab->shdr->sh_size / symtab->shdr->sh_entsize;
	int str_sec_idx = symtab->shdr->sh_link;
	const char *name;

	for (i = 0; i < n; i++, sym++) {
		if (sym->st_shndx != sec_idx)
			continue;
		if (ELF64_ST_TYPE(sym->st_info) != sym_type)
			continue;

		name = elf_strptr(obj->elf, str_sec_idx, sym->st_name);
		if (!name)
			return NULL;

		if (strcmp(sym_name, name) != 0)
			continue;

		return sym;
	}

	return NULL;
}

static int linker_fixup_btf(struct src_obj *obj)
{
	const char *sec_name;
	struct src_sec *sec;
	int i, j, n, m;

	if (!obj->btf)
		return 0;

	n = btf__get_nr_types(obj->btf);
	for (i = 1; i <= n; i++) {
		struct btf_var_secinfo *vi;
		struct btf_type *t;

		t = btf_type_by_id(obj->btf, i);
		if (btf_kind(t) != BTF_KIND_DATASEC)
			continue;

		sec_name = btf__str_by_offset(obj->btf, t->name_off);
		sec = find_src_sec_by_name(obj, sec_name);
		if (sec) {
			/* record actual section size, unless ephemeral */
			if (sec->shdr)
				t->size = sec->shdr->sh_size;
		} else {
			/* BTF can have some sections that are not represented
			 * in ELF, e.g., .kconfig and .ksyms, which are used
			 * for special extern variables.  Here we'll
			 * pre-create "section shells" for them to be able to
			 * keep track of extra per-section metadata later
			 * (e.g., BTF variables).
			 */
			sec = add_src_sec(obj, sec_name);
			if (!sec)
				return -ENOMEM;

			sec->ephemeral = true;
			sec->sec_idx = 0; /* will match UNDEF shndx in ELF */
		}

		/* remember ELF section and its BTF type ID match */
		sec->sec_type_id = i;

		/* fix up variable offsets */
		vi = btf_var_secinfos(t);
		for (j = 0, m = btf_vlen(t); j < m; j++, vi++) {
			const struct btf_type *vt = btf__type_by_id(obj->btf, vi->type);
			const char *var_name = btf__str_by_offset(obj->btf, vt->name_off);
			int var_linkage = btf_var(vt)->linkage;
			Elf64_Sym *sym;

			/* no need to patch up static or extern vars */
			if (var_linkage != BTF_VAR_GLOBAL_ALLOCATED)
				continue;

			sym = find_sym_by_name(obj, sec->sec_idx, STT_OBJECT, var_name);
			if (!sym) {
				pr_warn("failed to find symbol for variable '%s' in section '%s'\n", var_name, sec_name);
				return -ENOENT;
			}

			vi->offset = sym->st_value;
		}
	}

	return 0;
}

static int remap_type_id(__u32 *type_id, void *ctx)
{
	int *id_map = ctx;

	*type_id = id_map[*type_id];

	return 0;
}

static int linker_append_btf(struct bpf_linker *linker, struct src_obj *obj)
{
	const struct btf_type *t;
	int i, j, n, start_id, id;

	if (!obj->btf)
		return 0;

	start_id = btf__get_nr_types(linker->btf) + 1;
	n = btf__get_nr_types(obj->btf);

	obj->btf_type_map = calloc(n + 1, sizeof(int));
	if (!obj->btf_type_map)
		return -ENOMEM;

	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(obj->btf, i);

		/* DATASECs are handled specially below */
		if (btf_kind(t) == BTF_KIND_DATASEC)
			continue;

		id = btf__add_type(linker->btf, obj->btf, t);
		if (id < 0) {
			pr_warn("failed to append BTF type #%d from file '%s'\n", i, obj->filename);
			return id;
		}

		obj->btf_type_map[i] = id;
	}

	/* remap all the types except DATASECs */
	n = btf__get_nr_types(linker->btf);
	for (i = start_id; i <= n; i++) {
		struct btf_type *dst_t = btf_type_by_id(linker->btf, i);

		if (btf_type_visit_type_ids(dst_t, remap_type_id, obj->btf_type_map))
			return -EINVAL;
	}

	/* append DATASEC info */
	for (i = 1; i < obj->sec_cnt; i++) {
		struct src_sec *src_sec;
		struct dst_sec *dst_sec;
		const struct btf_var_secinfo *src_var;
		struct btf_var_secinfo *dst_var;

		src_sec = &obj->secs[i];
		if (!src_sec->sec_type_id || src_sec->skipped)
			continue;
		dst_sec = &linker->secs[src_sec->dst_id];

		t = btf__type_by_id(obj->btf, src_sec->sec_type_id);
		src_var = btf_var_secinfos(t);
		n = btf_vlen(t);
		for (j = 0; j < n; j++, src_var++) {
			void *sec_vars = dst_sec->sec_vars;

			sec_vars = libbpf_reallocarray(sec_vars,
						       dst_sec->sec_var_cnt + 1,
						       sizeof(*dst_sec->sec_vars));
			if (!sec_vars)
				return -ENOMEM;

			dst_sec->sec_vars = sec_vars;
			dst_sec->sec_var_cnt++;

			dst_var = &dst_sec->sec_vars[dst_sec->sec_var_cnt - 1];
			dst_var->type = obj->btf_type_map[src_var->type];
			dst_var->size = src_var->size;
			dst_var->offset = src_sec->dst_off + src_var->offset;
		}
	}

	return 0;
}

static void *add_btf_ext_rec(struct btf_ext_sec_data *ext_data, const void *src_rec)
{
	void *tmp;

	tmp = libbpf_reallocarray(ext_data->recs, ext_data->rec_cnt + 1, ext_data->rec_sz);
	if (!tmp)
		return NULL;
	ext_data->recs = tmp;

	tmp += ext_data->rec_cnt * ext_data->rec_sz;
	memcpy(tmp, src_rec, ext_data->rec_sz);

	ext_data->rec_cnt++;

	return tmp;
}

static int linker_append_btf_ext(struct bpf_linker *linker, struct src_obj *obj)
{
	const struct btf_ext_info_sec *ext_sec;
	const char *sec_name, *s;
	struct src_sec *src_sec;
	struct dst_sec *dst_sec;
	int rec_sz, str_off, i;

	if (!obj->btf_ext)
		return 0;

	rec_sz = obj->btf_ext->func_info.rec_size;
	for_each_btf_ext_sec(&obj->btf_ext->func_info, ext_sec) {
		struct bpf_func_info_min *src_rec, *dst_rec;

		sec_name = btf__name_by_offset(obj->btf, ext_sec->sec_name_off);
		src_sec = find_src_sec_by_name(obj, sec_name);
		if (!src_sec) {
			pr_warn("can't find section '%s' referenced from .BTF.ext\n", sec_name);
			return -EINVAL;
		}
		dst_sec = &linker->secs[src_sec->dst_id];

		if (dst_sec->func_info.rec_sz == 0)
			dst_sec->func_info.rec_sz = rec_sz;
		if (dst_sec->func_info.rec_sz != rec_sz) {
			pr_warn("incompatible .BTF.ext record sizes for section '%s'\n", sec_name);
			return -EINVAL;
		}

		for_each_btf_ext_rec(&obj->btf_ext->func_info, ext_sec, i, src_rec) {
			dst_rec = add_btf_ext_rec(&dst_sec->func_info, src_rec);
			if (!dst_rec)
				return -ENOMEM;

			dst_rec->insn_off += src_sec->dst_off;
			dst_rec->type_id = obj->btf_type_map[dst_rec->type_id];
		}
	}

	rec_sz = obj->btf_ext->line_info.rec_size;
	for_each_btf_ext_sec(&obj->btf_ext->line_info, ext_sec) {
		struct bpf_line_info_min *src_rec, *dst_rec;

		sec_name = btf__name_by_offset(obj->btf, ext_sec->sec_name_off);
		src_sec = find_src_sec_by_name(obj, sec_name);
		if (!src_sec) {
			pr_warn("can't find section '%s' referenced from .BTF.ext\n", sec_name);
			return -EINVAL;
		}
		dst_sec = &linker->secs[src_sec->dst_id];

		if (dst_sec->line_info.rec_sz == 0)
			dst_sec->line_info.rec_sz = rec_sz;
		if (dst_sec->line_info.rec_sz != rec_sz) {
			pr_warn("incompatible .BTF.ext record sizes for section '%s'\n", sec_name);
			return -EINVAL;
		}

		for_each_btf_ext_rec(&obj->btf_ext->line_info, ext_sec, i, src_rec) {
			dst_rec = add_btf_ext_rec(&dst_sec->line_info, src_rec);
			if (!dst_rec)
				return -ENOMEM;

			dst_rec->insn_off += src_sec->dst_off;

			s = btf__str_by_offset(obj->btf, src_rec->file_name_off);
			str_off = btf__add_str(linker->btf, s);
			if (str_off < 0)
				return -ENOMEM;
			dst_rec->file_name_off = str_off;

			s = btf__str_by_offset(obj->btf, src_rec->line_off);
			str_off = btf__add_str(linker->btf, s);
			if (str_off < 0)
				return -ENOMEM;
			dst_rec->line_off = str_off;

			/* dst_rec->line_col is fine */
		}
	}

	rec_sz = obj->btf_ext->core_relo_info.rec_size;
	for_each_btf_ext_sec(&obj->btf_ext->core_relo_info, ext_sec) {
		struct bpf_core_relo *src_rec, *dst_rec;

		sec_name = btf__name_by_offset(obj->btf, ext_sec->sec_name_off);
		src_sec = find_src_sec_by_name(obj, sec_name);
		if (!src_sec) {
			pr_warn("can't find section '%s' referenced from .BTF.ext\n", sec_name);
			return -EINVAL;
		}
		dst_sec = &linker->secs[src_sec->dst_id];

		if (dst_sec->core_relo_info.rec_sz == 0)
			dst_sec->core_relo_info.rec_sz = rec_sz;
		if (dst_sec->core_relo_info.rec_sz != rec_sz) {
			pr_warn("incompatible .BTF.ext record sizes for section '%s'\n", sec_name);
			return -EINVAL;
		}

		for_each_btf_ext_rec(&obj->btf_ext->core_relo_info, ext_sec, i, src_rec) {
			dst_rec = add_btf_ext_rec(&dst_sec->core_relo_info, src_rec);
			if (!dst_rec)
				return -ENOMEM;

			dst_rec->insn_off += src_sec->dst_off;
			dst_rec->type_id = obj->btf_type_map[dst_rec->type_id];

			s = btf__str_by_offset(obj->btf, src_rec->access_str_off);
			str_off = btf__add_str(linker->btf, s);
			if (str_off < 0)
				return -ENOMEM;
			dst_rec->access_str_off = str_off;

			/* dst_rec->kind is fine */
		}
	}

	return 0;
}

int bpf_linker__finalize(struct bpf_linker *linker)
{
	struct dst_sec *sec;
	size_t strs_sz;
	const void *strs;
	int err, i;

	if (!linker->elf)
		return -EINVAL;

	err = finalize_btf(linker);
	if (err)
		return err;

	/* Finalize strings */
	strs_sz = strset__data_size(linker->strtab_strs);
	strs = strset__data(linker->strtab_strs);

	sec = &linker->secs[linker->strtab_sec_idx];
	sec->data->d_align = 1;
	sec->data->d_off = 0LL;
	sec->data->d_buf = (void *)strs;
	sec->data->d_type = ELF_T_BYTE;
	sec->data->d_size = strs_sz;
	sec->shdr->sh_size = strs_sz;

	for (i = 1; i < linker->sec_cnt; i++) {
		sec = &linker->secs[i];

		/* STRTAB is handled specially above */
		if (sec->sec_idx == linker->strtab_sec_idx)
			continue;

		/* special ephemeral sections (.ksyms, .kconfig, etc) */
		if (!sec->scn)
			continue;

		sec->data->d_buf = sec->raw_data;
	}

	/* Finalize ELF layout */
	if (elf_update(linker->elf, ELF_C_NULL) < 0) {
		err = -errno;
		pr_warn_elf("failed to finalize ELF layout");
		return err;
	}

	/* Write out final ELF contents */
	if (elf_update(linker->elf, ELF_C_WRITE) < 0) {
		err = -errno;
		pr_warn_elf("failed to write ELF contents");
		return err;
	}

	elf_end(linker->elf);
	close(linker->fd);

	linker->elf = NULL;
	linker->fd = -1;

	return 0;
}

static int emit_elf_data_sec(struct bpf_linker *linker, const char *sec_name,
			     size_t align, const void *raw_data, size_t raw_sz)
{
	Elf_Scn *scn;
	Elf_Data *data;
	Elf64_Shdr *shdr;
	int name_off;

	name_off = strset__add_str(linker->strtab_strs, sec_name);
	if (name_off < 0)
		return name_off;

	scn = elf_newscn(linker->elf);
	if (!scn)
		return -ENOMEM;
	data = elf_newdata(scn);
	if (!data)
		return -ENOMEM;
	shdr = elf64_getshdr(scn);
	if (!shdr)
		return -EINVAL;

	shdr->sh_name = name_off;
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_flags = 0;
	shdr->sh_size = raw_sz;
	shdr->sh_link = 0;
	shdr->sh_info = 0;
	shdr->sh_addralign = align;
	shdr->sh_entsize = 0;

	data->d_type = ELF_T_BYTE;
	data->d_size = raw_sz;
	data->d_buf = (void *)raw_data;
	data->d_align = align;
	data->d_off = 0;

	return 0;
}

static int finalize_btf(struct bpf_linker *linker)
{
	struct btf *btf = linker->btf;
	const void *raw_data;
	int i, j, id, err;
	__u32 raw_sz;

	/* bail out if no BTF data was produced */
	if (btf__get_nr_types(linker->btf) == 0)
		return 0;

	for (i = 1; i < linker->sec_cnt; i++) {
		struct dst_sec *sec = &linker->secs[i];

		if (!sec->sec_var_cnt)
			continue;

		id = btf__add_datasec(btf, sec->sec_name, sec->sec_sz);
		if (id < 0) {
			pr_warn("failed to add consolidated BTF type for datasec '%s': %d\n",
				sec->sec_name, id);
			return id;
		}

		for (j = 0; j < sec->sec_var_cnt; j++) {
			struct btf_var_secinfo *vi = &sec->sec_vars[j];

			if (btf__add_datasec_var_info(btf, vi->type, vi->offset, vi->size))
				return -EINVAL;
		}
	}

	err = finalize_btf_ext(linker);
	if (err) {
		pr_warn(".BTF.ext generation failed: %d\n", err);
		return err;
	}

	err = btf__dedup(linker->btf, linker->btf_ext, NULL);
	if (err) {
		pr_warn("BTF dedup failed: %d\n", err);
		return err;
	}

	/* Emit .BTF section */
	raw_data = btf__get_raw_data(linker->btf, &raw_sz);
	if (!raw_data)
		return -ENOMEM;

	err = emit_elf_data_sec(linker, BTF_ELF_SEC, 8, raw_data, raw_sz);
	if (err) {
		pr_warn("failed to write out .BTF ELF section: %d\n", err);
		return err;
	}

	/* Emit .BTF.ext section */
	if (linker->btf_ext) {
		raw_data = btf_ext__get_raw_data(linker->btf_ext, &raw_sz);
		if (!raw_data)
			return -ENOMEM;

		err = emit_elf_data_sec(linker, BTF_EXT_ELF_SEC, 8, raw_data, raw_sz);
		if (err) {
			pr_warn("failed to write out .BTF.ext ELF section: %d\n", err);
			return err;
		}
	}

	return 0;
}

static int emit_btf_ext_data(struct bpf_linker *linker, void *output,
			     const char *sec_name, struct btf_ext_sec_data *sec_data)
{
	struct btf_ext_info_sec *sec_info;
	void *cur = output;
	int str_off;
	size_t sz;

	if (!sec_data->rec_cnt)
		return 0;

	str_off = btf__add_str(linker->btf, sec_name);
	if (str_off < 0)
		return -ENOMEM;

	sec_info = cur;
	sec_info->sec_name_off = str_off;
	sec_info->num_info = sec_data->rec_cnt;
	cur += sizeof(struct btf_ext_info_sec);

	sz = sec_data->rec_cnt * sec_data->rec_sz;
	memcpy(cur, sec_data->recs, sz);
	cur += sz;

	return cur - output;
}

static int finalize_btf_ext(struct bpf_linker *linker)
{
	size_t funcs_sz = 0, lines_sz = 0, core_relos_sz = 0, total_sz = 0;
	size_t func_rec_sz = 0, line_rec_sz = 0, core_relo_rec_sz = 0;
	struct btf_ext_header *hdr;
	void *data, *cur;
	int i, err, sz;

	/* validate that all sections have the same .BTF.ext record sizes
	 * and calculate total data size for each type of data (func info,
	 * line info, core relos)
	 */
	for (i = 1; i < linker->sec_cnt; i++) {
		struct dst_sec *sec = &linker->secs[i];

		if (sec->func_info.rec_cnt) {
			if (func_rec_sz == 0)
				func_rec_sz = sec->func_info.rec_sz;
			if (func_rec_sz != sec->func_info.rec_sz) {
				pr_warn("mismatch in func_info record size %zu != %u\n",
					func_rec_sz, sec->func_info.rec_sz);
				return -EINVAL;
			}

			funcs_sz += sizeof(struct btf_ext_info_sec) + func_rec_sz * sec->func_info.rec_cnt;
		}
		if (sec->line_info.rec_cnt) {
			if (line_rec_sz == 0)
				line_rec_sz = sec->line_info.rec_sz;
			if (line_rec_sz != sec->line_info.rec_sz) {
				pr_warn("mismatch in line_info record size %zu != %u\n",
					line_rec_sz, sec->line_info.rec_sz);
				return -EINVAL;
			}

			lines_sz += sizeof(struct btf_ext_info_sec) + line_rec_sz * sec->line_info.rec_cnt;
		}
		if (sec->core_relo_info.rec_cnt) {
			if (core_relo_rec_sz == 0)
				core_relo_rec_sz = sec->core_relo_info.rec_sz;
			if (core_relo_rec_sz != sec->core_relo_info.rec_sz) {
				pr_warn("mismatch in core_relo_info record size %zu != %u\n",
					core_relo_rec_sz, sec->core_relo_info.rec_sz);
				return -EINVAL;
			}

			core_relos_sz += sizeof(struct btf_ext_info_sec) + core_relo_rec_sz * sec->core_relo_info.rec_cnt;
		}
	}

	if (!funcs_sz && !lines_sz && !core_relos_sz)
		return 0;

	total_sz += sizeof(struct btf_ext_header);
	if (funcs_sz) {
		funcs_sz += sizeof(__u32); /* record size prefix */
		total_sz += funcs_sz;
	}
	if (lines_sz) {
		lines_sz += sizeof(__u32); /* record size prefix */
		total_sz += lines_sz;
	}
	if (core_relos_sz) {
		core_relos_sz += sizeof(__u32); /* record size prefix */
		total_sz += core_relos_sz;
	}

	cur = data = calloc(1, total_sz);
	if (!data)
		return -ENOMEM;

	hdr = cur;
	hdr->magic = BTF_MAGIC;
	hdr->version = BTF_VERSION;
	hdr->flags = 0;
	hdr->hdr_len = sizeof(struct btf_ext_header);
	cur += sizeof(struct btf_ext_header);

	/* All offsets are in bytes relative to the end of this header */
	hdr->func_info_off = 0;
	hdr->func_info_len = funcs_sz;
	hdr->line_info_off = funcs_sz;
	hdr->line_info_len = lines_sz;
	hdr->core_relo_off = funcs_sz + lines_sz;;
	hdr->core_relo_len = core_relos_sz;

	if (funcs_sz) {
		*(__u32 *)cur = func_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->func_info);
			if (sz < 0)
				return sz;

			cur += sz;
		}
	}

	if (lines_sz) {
		*(__u32 *)cur = line_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->line_info);
			if (sz < 0)
				return sz;

			cur += sz;
		}
	}

	if (core_relos_sz) {
		*(__u32 *)cur = core_relo_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->core_relo_info);
			if (sz < 0)
				return sz;

			cur += sz;
		}
	}

	linker->btf_ext = btf_ext__new(data, total_sz);
	err = libbpf_get_error(linker->btf_ext);
	if (err) {
		linker->btf_ext = NULL;
		pr_warn("failed to parse final .BTF.ext data: %d\n", err);
		return err;
	}

	return 0;
}
