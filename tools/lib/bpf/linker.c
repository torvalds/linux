// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/*
 * BPF static linker
 *
 * Copyright (c) 2021 Facebook
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include <fcntl.h>
#include <sys/mman.h>
#include "libbpf.h"
#include "btf.h"
#include "libbpf_internal.h"
#include "strset.h"
#include "str_error.h"

#define BTF_EXTERN_SEC ".extern"

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

struct glob_sym {
	/* ELF symbol index */
	int sym_idx;
	/* associated section id for .ksyms, .kconfig, etc, but not .extern */
	int sec_id;
	/* extern name offset in STRTAB */
	int name_off;
	/* optional associated BTF type ID */
	int btf_id;
	/* BTF type ID to which VAR/FUNC type is pointing to; used for
	 * rewriting types when extern VAR/FUNC is resolved to a concrete
	 * definition
	 */
	int underlying_btf_id;
	/* sec_var index in the corresponding dst_sec, if exists */
	int var_idx;

	/* extern or resolved/global symbol */
	bool is_extern;
	/* weak or strong symbol, never goes back from strong to weak */
	bool is_weak;
};

struct dst_sec {
	char *sec_name;
	/* positional (not necessarily ELF) index in an array of sections */
	int id;

	bool ephemeral;

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
	bool has_btf;
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
	bool swapped_endian;

	/* Output sections metadata */
	struct dst_sec *secs;
	int sec_cnt;

	struct strset *strtab_strs; /* STRTAB unique strings */
	size_t strtab_sec_idx; /* STRTAB section index */
	size_t symtab_sec_idx; /* SYMTAB section index */

	struct btf *btf;
	struct btf_ext *btf_ext;

	/* global (including extern) ELF symbols */
	int glob_sym_cnt;
	struct glob_sym *glob_syms;

	bool fd_is_owned;
};

#define pr_warn_elf(fmt, ...)									\
	libbpf_print(LIBBPF_WARN, "libbpf: " fmt ": %s\n", ##__VA_ARGS__, elf_errmsg(-1))

static int init_output_elf(struct bpf_linker *linker);

static int bpf_linker_add_file(struct bpf_linker *linker, int fd,
			       const char *filename);

static int linker_load_obj_file(struct bpf_linker *linker,
				struct src_obj *obj);
static int linker_sanity_check_elf(struct src_obj *obj);
static int linker_sanity_check_elf_symtab(struct src_obj *obj, struct src_sec *sec);
static int linker_sanity_check_elf_relos(struct src_obj *obj, struct src_sec *sec);
static int linker_sanity_check_btf(struct src_obj *obj);
static int linker_sanity_check_btf_ext(struct src_obj *obj);
static int linker_fixup_btf(struct src_obj *obj);
static int linker_append_sec_data(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_elf_syms(struct bpf_linker *linker, struct src_obj *obj);
static int linker_append_elf_sym(struct bpf_linker *linker, struct src_obj *obj,
				 Elf64_Sym *sym, const char *sym_name, int src_sym_idx);
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

	if (linker->fd >= 0 && linker->fd_is_owned)
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

	free(linker->glob_syms);
	free(linker);
}

struct bpf_linker *bpf_linker__new(const char *filename, struct bpf_linker_opts *opts)
{
	struct bpf_linker *linker;
	int err;

	if (!OPTS_VALID(opts, bpf_linker_opts))
		return errno = EINVAL, NULL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn_elf("libelf initialization failed");
		return errno = EINVAL, NULL;
	}

	linker = calloc(1, sizeof(*linker));
	if (!linker)
		return errno = ENOMEM, NULL;

	linker->filename = strdup(filename);
	if (!linker->filename) {
		err = -ENOMEM;
		goto err_out;
	}

	linker->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (linker->fd < 0) {
		err = -errno;
		pr_warn("failed to create '%s': %d\n", filename, err);
		goto err_out;
	}
	linker->fd_is_owned = true;

	err = init_output_elf(linker);
	if (err)
		goto err_out;

	return linker;

err_out:
	bpf_linker__free(linker);
	return errno = -err, NULL;
}

struct bpf_linker *bpf_linker__new_fd(int fd, struct bpf_linker_opts *opts)
{
	struct bpf_linker *linker;
	char filename[32];
	int err;

	if (fd < 0)
		return errno = EINVAL, NULL;

	if (!OPTS_VALID(opts, bpf_linker_opts))
		return errno = EINVAL, NULL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn_elf("libelf initialization failed");
		return errno = EINVAL, NULL;
	}

	linker = calloc(1, sizeof(*linker));
	if (!linker)
		return errno = ENOMEM, NULL;

	snprintf(filename, sizeof(filename), "fd:%d", fd);
	linker->filename = strdup(filename);
	if (!linker->filename) {
		err = -ENOMEM;
		goto err_out;
	}

	linker->fd = fd;
	linker->fd_is_owned = false;

	err = init_output_elf(linker);
	if (err)
		goto err_out;

	return linker;

err_out:
	bpf_linker__free(linker);
	return errno = -err, NULL;
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

static int init_output_elf(struct bpf_linker *linker)
{
	int err, str_off;
	Elf64_Sym *init_sym;
	struct dst_sec *sec;

	linker->elf = elf_begin(linker->fd, ELF_C_WRITE, NULL);
	if (!linker->elf) {
		pr_warn_elf("failed to create ELF object");
		return -EINVAL;
	}

	/* ELF header */
	linker->elf_hdr = elf64_newehdr(linker->elf);
	if (!linker->elf_hdr) {
		pr_warn_elf("failed to create ELF header");
		return -EINVAL;
	}

	linker->elf_hdr->e_machine = EM_BPF;
	linker->elf_hdr->e_type = ET_REL;
	/* Set unknown ELF endianness, assign later from input files */
	linker->elf_hdr->e_ident[EI_DATA] = ELFDATANONE;

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
	/* Ensure libelf translates byte-order of symbol records */
	sec->data->d_type = ELF_T_SYM;

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

static int bpf_linker_add_file(struct bpf_linker *linker, int fd,
			       const char *filename)
{
	struct src_obj obj = {};
	int err = 0;

	obj.filename = filename;
	obj.fd = fd;

	err = err ?: linker_load_obj_file(linker, &obj);
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

	return err;
}

int bpf_linker__add_file(struct bpf_linker *linker, const char *filename,
			 const struct bpf_linker_file_opts *opts)
{
	int fd, err;

	if (!OPTS_VALID(opts, bpf_linker_file_opts))
		return libbpf_err(-EINVAL);

	if (!linker->elf)
		return libbpf_err(-EINVAL);

	fd = open(filename, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = -errno;
		pr_warn("failed to open file '%s': %s\n", filename, errstr(err));
		return libbpf_err(err);
	}

	err = bpf_linker_add_file(linker, fd, filename);
	close(fd);
	return libbpf_err(err);
}

int bpf_linker__add_fd(struct bpf_linker *linker, int fd,
		       const struct bpf_linker_file_opts *opts)
{
	char filename[32];
	int err;

	if (!OPTS_VALID(opts, bpf_linker_file_opts))
		return libbpf_err(-EINVAL);

	if (!linker->elf)
		return libbpf_err(-EINVAL);

	if (fd < 0)
		return libbpf_err(-EINVAL);

	snprintf(filename, sizeof(filename), "fd:%d", fd);
	err = bpf_linker_add_file(linker, fd, filename);
	return libbpf_err(err);
}

int bpf_linker__add_buf(struct bpf_linker *linker, void *buf, size_t buf_sz,
			const struct bpf_linker_file_opts *opts)
{
	char filename[32];
	int fd, written, ret;

	if (!OPTS_VALID(opts, bpf_linker_file_opts))
		return libbpf_err(-EINVAL);

	if (!linker->elf)
		return libbpf_err(-EINVAL);

	snprintf(filename, sizeof(filename), "mem:%p+%zu", buf, buf_sz);

	fd = sys_memfd_create(filename, 0);
	if (fd < 0) {
		ret = -errno;
		pr_warn("failed to create memfd '%s': %s\n", filename, errstr(ret));
		return libbpf_err(ret);
	}

	written = 0;
	while (written < buf_sz) {
		ret = write(fd, buf, buf_sz);
		if (ret < 0) {
			ret = -errno;
			pr_warn("failed to write '%s': %s\n", filename, errstr(ret));
			goto err_out;
		}
		written += ret;
	}

	ret = bpf_linker_add_file(linker, fd, filename);
err_out:
	close(fd);
	return libbpf_err(ret);
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

static int linker_load_obj_file(struct bpf_linker *linker,
				struct src_obj *obj)
{
	int err = 0;
	Elf_Scn *scn;
	Elf_Data *data;
	Elf64_Ehdr *ehdr;
	Elf64_Shdr *shdr;
	struct src_sec *sec;
	unsigned char obj_byteorder;
	unsigned char link_byteorder = linker->elf_hdr->e_ident[EI_DATA];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	const unsigned char host_byteorder = ELFDATA2LSB;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	const unsigned char host_byteorder = ELFDATA2MSB;
#else
#error "Unknown __BYTE_ORDER__"
#endif

	pr_debug("linker: adding object file '%s'...\n", obj->filename);

	obj->elf = elf_begin(obj->fd, ELF_C_READ_MMAP, NULL);
	if (!obj->elf) {
		pr_warn_elf("failed to parse ELF file '%s'", obj->filename);
		return -EINVAL;
	}

	/* Sanity check ELF file high-level properties */
	ehdr = elf64_getehdr(obj->elf);
	if (!ehdr) {
		pr_warn_elf("failed to get ELF header for %s", obj->filename);
		return -EINVAL;
	}

	/* Linker output endianness set by first input object */
	obj_byteorder = ehdr->e_ident[EI_DATA];
	if (obj_byteorder != ELFDATA2LSB && obj_byteorder != ELFDATA2MSB) {
		err = -EOPNOTSUPP;
		pr_warn("unknown byte order of ELF file %s\n", obj->filename);
		return err;
	}
	if (link_byteorder == ELFDATANONE) {
		linker->elf_hdr->e_ident[EI_DATA] = obj_byteorder;
		linker->swapped_endian = obj_byteorder != host_byteorder;
		pr_debug("linker: set %s-endian output byte order\n",
			 obj_byteorder == ELFDATA2MSB ? "big" : "little");
	} else if (link_byteorder != obj_byteorder) {
		err = -EOPNOTSUPP;
		pr_warn("byte order mismatch with ELF file %s\n", obj->filename);
		return err;
	}

	if (ehdr->e_type != ET_REL
	    || ehdr->e_machine != EM_BPF
	    || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
		err = -EOPNOTSUPP;
		pr_warn_elf("unsupported kind of ELF file %s", obj->filename);
		return err;
	}

	if (elf_getshdrstrndx(obj->elf, &obj->shstrs_sec_idx)) {
		pr_warn_elf("failed to get SHSTRTAB section index for %s", obj->filename);
		return -EINVAL;
	}

	scn = NULL;
	while ((scn = elf_nextscn(obj->elf, scn)) != NULL) {
		size_t sec_idx = elf_ndxscn(scn);
		const char *sec_name;

		shdr = elf64_getshdr(scn);
		if (!shdr) {
			pr_warn_elf("failed to get section #%zu header for %s",
				    sec_idx, obj->filename);
			return -EINVAL;
		}

		sec_name = elf_strptr(obj->elf, obj->shstrs_sec_idx, shdr->sh_name);
		if (!sec_name) {
			pr_warn_elf("failed to get section #%zu name for %s",
				    sec_idx, obj->filename);
			return -EINVAL;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warn_elf("failed to get section #%zu (%s) data from %s",
				    sec_idx, sec_name, obj->filename);
			return -EINVAL;
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
					pr_warn("failed to parse .BTF from %s: %s\n",
						obj->filename, errstr(err));
					return err;
				}
				sec->skipped = true;
				continue;
			}
			if (strcmp(sec_name, BTF_EXT_ELF_SEC) == 0) {
				obj->btf_ext = btf_ext__new(data->d_buf, shdr->sh_size);
				err = libbpf_get_error(obj->btf_ext);
				if (err) {
					pr_warn("failed to parse .BTF.ext from '%s': %s\n",
						obj->filename, errstr(err));
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
				sec_idx, sec_name, obj->filename);
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

static int linker_sanity_check_elf(struct src_obj *obj)
{
	struct src_sec *sec;
	int i, err;

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

		if (is_dwarf_sec_name(sec->sec_name))
			continue;

		if (sec->shdr->sh_addralign && !is_pow_of_2(sec->shdr->sh_addralign)) {
			pr_warn("ELF section #%zu alignment %llu is non pow-of-2 alignment in %s\n",
				sec->sec_idx, (long long unsigned)sec->shdr->sh_addralign,
				obj->filename);
			return -EINVAL;
		}
		if (sec->shdr->sh_addralign != sec->data->d_align) {
			pr_warn("ELF section #%zu has inconsistent alignment addr=%llu != d=%llu in %s\n",
				sec->sec_idx, (long long unsigned)sec->shdr->sh_addralign,
				(long long unsigned)sec->data->d_align, obj->filename);
			return -EINVAL;
		}

		if (sec->shdr->sh_size != sec->data->d_size) {
			pr_warn("ELF section #%zu has inconsistent section size sh=%llu != d=%llu in %s\n",
				sec->sec_idx, (long long unsigned)sec->shdr->sh_size,
				(long long unsigned)sec->data->d_size, obj->filename);
			return -EINVAL;
		}

		switch (sec->shdr->sh_type) {
		case SHT_SYMTAB:
			err = linker_sanity_check_elf_symtab(obj, sec);
			if (err)
				return err;
			break;
		case SHT_STRTAB:
			break;
		case SHT_PROGBITS:
			if (sec->shdr->sh_flags & SHF_EXECINSTR) {
				if (sec->shdr->sh_size % sizeof(struct bpf_insn) != 0) {
					pr_warn("ELF section #%zu has unexpected size alignment %llu in %s\n",
						sec->sec_idx, (long long unsigned)sec->shdr->sh_size,
						obj->filename);
					return -EINVAL;
				}
			}
			break;
		case SHT_NOBITS:
			break;
		case SHT_REL:
			err = linker_sanity_check_elf_relos(obj, sec);
			if (err)
				return err;
			break;
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

static int linker_sanity_check_elf_symtab(struct src_obj *obj, struct src_sec *sec)
{
	struct src_sec *link_sec;
	Elf64_Sym *sym;
	int i, n;

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
	for (i = 0; i < n; i++, sym++) {
		int sym_type = ELF64_ST_TYPE(sym->st_info);
		int sym_bind = ELF64_ST_BIND(sym->st_info);
		int sym_vis = ELF64_ST_VISIBILITY(sym->st_other);

		if (i == 0) {
			if (sym->st_name != 0 || sym->st_info != 0
			    || sym->st_other != 0 || sym->st_shndx != 0
			    || sym->st_value != 0 || sym->st_size != 0) {
				pr_warn("ELF sym #0 is invalid in %s\n", obj->filename);
				return -EINVAL;
			}
			continue;
		}
		if (sym_bind != STB_LOCAL && sym_bind != STB_GLOBAL && sym_bind != STB_WEAK) {
			pr_warn("ELF sym #%d in section #%zu has unsupported symbol binding %d\n",
				i, sec->sec_idx, sym_bind);
			return -EINVAL;
		}
		if (sym_vis != STV_DEFAULT && sym_vis != STV_HIDDEN) {
			pr_warn("ELF sym #%d in section #%zu has unsupported symbol visibility %d\n",
				i, sec->sec_idx, sym_vis);
			return -EINVAL;
		}
		if (sym->st_shndx == 0) {
			if (sym_type != STT_NOTYPE || sym_bind == STB_LOCAL
			    || sym->st_value != 0 || sym->st_size != 0) {
				pr_warn("ELF sym #%d is invalid extern symbol in %s\n",
					i, obj->filename);

				return -EINVAL;
			}
			continue;
		}
		if (sym->st_shndx < SHN_LORESERVE && sym->st_shndx >= obj->sec_cnt) {
			pr_warn("ELF sym #%d in section #%zu points to missing section #%zu in %s\n",
				i, sec->sec_idx, (size_t)sym->st_shndx, obj->filename);
			return -EINVAL;
		}
		if (sym_type == STT_SECTION) {
			if (sym->st_value != 0)
				return -EINVAL;
			continue;
		}
	}

	return 0;
}

static int linker_sanity_check_elf_relos(struct src_obj *obj, struct src_sec *sec)
{
	struct src_sec *link_sec, *sym_sec;
	Elf64_Rel *relo;
	int i, n;

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
		return 0;

	/* relocatable section is data or instructions */
	if (link_sec->shdr->sh_type != SHT_PROGBITS && link_sec->shdr->sh_type != SHT_NOBITS) {
		pr_warn("ELF relo section #%zu points to invalid section #%zu in %s\n",
			sec->sec_idx, (size_t)sec->shdr->sh_info, obj->filename);
		return -EINVAL;
	}

	/* check sanity of each relocation */
	n = sec->shdr->sh_size / sec->shdr->sh_entsize;
	relo = sec->data->d_buf;
	sym_sec = &obj->secs[obj->symtab_sec_idx];
	for (i = 0; i < n; i++, relo++) {
		size_t sym_idx = ELF64_R_SYM(relo->r_info);
		size_t sym_type = ELF64_R_TYPE(relo->r_info);

		if (sym_type != R_BPF_64_64 && sym_type != R_BPF_64_32 &&
		    sym_type != R_BPF_64_ABS64 && sym_type != R_BPF_64_ABS32) {
			pr_warn("ELF relo #%d in section #%zu has unexpected type %zu in %s\n",
				i, sec->sec_idx, sym_type, obj->filename);
			return -EINVAL;
		}

		if (!sym_idx || sym_idx * sizeof(Elf64_Sym) >= sym_sec->shdr->sh_size) {
			pr_warn("ELF relo #%d in section #%zu points to invalid symbol #%zu in %s\n",
				i, sec->sec_idx, sym_idx, obj->filename);
			return -EINVAL;
		}

		if (link_sec->shdr->sh_flags & SHF_EXECINSTR) {
			if (relo->r_offset % sizeof(struct bpf_insn) != 0) {
				pr_warn("ELF relo #%d in section #%zu points to missing symbol #%zu in %s\n",
					i, sec->sec_idx, sym_idx, obj->filename);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int check_btf_type_id(__u32 *type_id, void *ctx)
{
	struct btf *btf = ctx;

	if (*type_id >= btf__type_cnt(btf))
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
	int i, n, err;

	if (!obj->btf)
		return 0;

	n = btf__type_cnt(obj->btf);
	for (i = 1; i < n; i++) {
		struct btf_field_iter it;
		__u32 *type_id, *str_off;

		t = btf_type_by_id(obj->btf, i);

		err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
		if (err)
			return err;
		while ((type_id = btf_field_iter_next(&it))) {
			if (*type_id >= n)
				return -EINVAL;
		}

		err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_STRS);
		if (err)
			return err;
		while ((str_off = btf_field_iter_next(&it))) {
			if (!btf__str_by_offset(obj->btf, *str_off))
				return -EINVAL;
		}
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
	dst_sec->ephemeral = src_sec->ephemeral;

	/* ephemeral sections are just thin section shells lacking most parts */
	if (src_sec->ephemeral)
		return 0;

	scn = elf_newscn(linker->elf);
	if (!scn)
		return -ENOMEM;
	data = elf_newdata(scn);
	if (!data)
		return -ENOMEM;
	shdr = elf64_getshdr(scn);
	if (!shdr)
		return -ENOMEM;

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
	if (dst->ephemeral || src->ephemeral)
		return true;

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

static bool is_exec_sec(struct dst_sec *sec)
{
	if (!sec || sec->ephemeral)
		return false;
	return (sec->shdr->sh_type == SHT_PROGBITS) &&
	       (sec->shdr->sh_flags & SHF_EXECINSTR);
}

static void exec_sec_bswap(void *raw_data, int size)
{
	const int insn_cnt = size / sizeof(struct bpf_insn);
	struct bpf_insn *insn = raw_data;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++)
		bpf_insn_bswap(insn);
}

static int extend_sec(struct bpf_linker *linker, struct dst_sec *dst, struct src_sec *src)
{
	void *tmp;
	size_t dst_align, src_align;
	size_t dst_align_sz, dst_final_sz;
	int err;

	/* Ephemeral source section doesn't contribute anything to ELF
	 * section data.
	 */
	if (src->ephemeral)
		return 0;

	/* Some sections (like .maps) can contain both externs (and thus be
	 * ephemeral) and non-externs (map definitions). So it's possible that
	 * it has to be "upgraded" from ephemeral to non-ephemeral when the
	 * first non-ephemeral entity appears. In such case, we add ELF
	 * section, data, etc.
	 */
	if (dst->ephemeral) {
		err = init_sec(linker, dst, src);
		if (err)
			return err;
	}

	dst_align = dst->shdr->sh_addralign;
	src_align = src->shdr->sh_addralign;
	if (dst_align == 0)
		dst_align = 1;
	if (dst_align < src_align)
		dst_align = src_align;

	dst_align_sz = (dst->sec_sz + dst_align - 1) / dst_align * dst_align;

	/* no need to re-align final size */
	dst_final_sz = dst_align_sz + src->shdr->sh_size;

	if (src->shdr->sh_type != SHT_NOBITS) {
		tmp = realloc(dst->raw_data, dst_final_sz);
		/* If dst_align_sz == 0, realloc() behaves in a special way:
		 * 1. When dst->raw_data is NULL it returns:
		 *    "either NULL or a pointer suitable to be passed to free()" [1].
		 * 2. When dst->raw_data is not-NULL it frees dst->raw_data and returns NULL,
		 *    thus invalidating any "pointer suitable to be passed to free()" obtained
		 *    at step (1).
		 *
		 * The dst_align_sz > 0 check avoids error exit after (2), otherwise
		 * dst->raw_data would be freed again in bpf_linker__free().
		 *
		 * [1] man 3 realloc
		 */
		if (!tmp && dst_align_sz > 0)
			return -ENOMEM;
		dst->raw_data = tmp;

		/* pad dst section, if it's alignment forced size increase */
		memset(dst->raw_data + dst->sec_sz, 0, dst_align_sz - dst->sec_sz);
		/* now copy src data at a properly aligned offset */
		memcpy(dst->raw_data + dst_align_sz, src->data->d_buf, src->shdr->sh_size);

		/* convert added bpf insns to native byte-order */
		if (linker->swapped_endian && is_exec_sec(dst))
			exec_sec_bswap(dst->raw_data + dst_align_sz, src->shdr->sh_size);
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
				return -EINVAL;
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

		err = extend_sec(linker, dst_sec, src_sec);
		if (err)
			return err;
	}

	return 0;
}

static int linker_append_elf_syms(struct bpf_linker *linker, struct src_obj *obj)
{
	struct src_sec *symtab = &obj->secs[obj->symtab_sec_idx];
	Elf64_Sym *sym = symtab->data->d_buf;
	int i, n = symtab->shdr->sh_size / symtab->shdr->sh_entsize, err;
	int str_sec_idx = symtab->shdr->sh_link;
	const char *sym_name;

	obj->sym_map = calloc(n + 1, sizeof(*obj->sym_map));
	if (!obj->sym_map)
		return -ENOMEM;

	for (i = 0; i < n; i++, sym++) {
		/* We already validated all-zero symbol #0 and we already
		 * appended it preventively to the final SYMTAB, so skip it.
		 */
		if (i == 0)
			continue;

		sym_name = elf_strptr(obj->elf, str_sec_idx, sym->st_name);
		if (!sym_name) {
			pr_warn("can't fetch symbol name for symbol #%d in '%s'\n", i, obj->filename);
			return -EINVAL;
		}

		err = linker_append_elf_sym(linker, obj, sym, sym_name, i);
		if (err)
			return err;
	}

	return 0;
}

static Elf64_Sym *get_sym_by_idx(struct bpf_linker *linker, size_t sym_idx)
{
	struct dst_sec *symtab = &linker->secs[linker->symtab_sec_idx];
	Elf64_Sym *syms = symtab->raw_data;

	return &syms[sym_idx];
}

static struct glob_sym *find_glob_sym(struct bpf_linker *linker, const char *sym_name)
{
	struct glob_sym *glob_sym;
	const char *name;
	int i;

	for (i = 0; i < linker->glob_sym_cnt; i++) {
		glob_sym = &linker->glob_syms[i];
		name = strset__data(linker->strtab_strs) + glob_sym->name_off;

		if (strcmp(name, sym_name) == 0)
			return glob_sym;
	}

	return NULL;
}

static struct glob_sym *add_glob_sym(struct bpf_linker *linker)
{
	struct glob_sym *syms, *sym;

	syms = libbpf_reallocarray(linker->glob_syms, linker->glob_sym_cnt + 1,
				   sizeof(*linker->glob_syms));
	if (!syms)
		return NULL;

	sym = &syms[linker->glob_sym_cnt];
	memset(sym, 0, sizeof(*sym));
	sym->var_idx = -1;

	linker->glob_syms = syms;
	linker->glob_sym_cnt++;

	return sym;
}

static bool glob_sym_btf_matches(const char *sym_name, bool exact,
				 const struct btf *btf1, __u32 id1,
				 const struct btf *btf2, __u32 id2)
{
	const struct btf_type *t1, *t2;
	bool is_static1, is_static2;
	const char *n1, *n2;
	int i, n;

recur:
	n1 = n2 = NULL;
	t1 = skip_mods_and_typedefs(btf1, id1, &id1);
	t2 = skip_mods_and_typedefs(btf2, id2, &id2);

	/* check if only one side is FWD, otherwise handle with common logic */
	if (!exact && btf_is_fwd(t1) != btf_is_fwd(t2)) {
		n1 = btf__str_by_offset(btf1, t1->name_off);
		n2 = btf__str_by_offset(btf2, t2->name_off);
		if (strcmp(n1, n2) != 0) {
			pr_warn("global '%s': incompatible forward declaration names '%s' and '%s'\n",
				sym_name, n1, n2);
			return false;
		}
		/* validate if FWD kind matches concrete kind */
		if (btf_is_fwd(t1)) {
			if (btf_kflag(t1) && btf_is_union(t2))
				return true;
			if (!btf_kflag(t1) && btf_is_struct(t2))
				return true;
			pr_warn("global '%s': incompatible %s forward declaration and concrete kind %s\n",
				sym_name, btf_kflag(t1) ? "union" : "struct", btf_kind_str(t2));
		} else {
			if (btf_kflag(t2) && btf_is_union(t1))
				return true;
			if (!btf_kflag(t2) && btf_is_struct(t1))
				return true;
			pr_warn("global '%s': incompatible %s forward declaration and concrete kind %s\n",
				sym_name, btf_kflag(t2) ? "union" : "struct", btf_kind_str(t1));
		}
		return false;
	}

	if (btf_kind(t1) != btf_kind(t2)) {
		pr_warn("global '%s': incompatible BTF kinds %s and %s\n",
			sym_name, btf_kind_str(t1), btf_kind_str(t2));
		return false;
	}

	switch (btf_kind(t1)) {
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
	case BTF_KIND_FWD:
	case BTF_KIND_FUNC:
	case BTF_KIND_VAR:
		n1 = btf__str_by_offset(btf1, t1->name_off);
		n2 = btf__str_by_offset(btf2, t2->name_off);
		if (strcmp(n1, n2) != 0) {
			pr_warn("global '%s': incompatible %s names '%s' and '%s'\n",
				sym_name, btf_kind_str(t1), n1, n2);
			return false;
		}
		break;
	default:
		break;
	}

	switch (btf_kind(t1)) {
	case BTF_KIND_UNKN: /* void */
	case BTF_KIND_FWD:
		return true;
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		/* ignore encoding for int and enum values for enum */
		if (t1->size != t2->size) {
			pr_warn("global '%s': incompatible %s '%s' size %u and %u\n",
				sym_name, btf_kind_str(t1), n1, t1->size, t2->size);
			return false;
		}
		return true;
	case BTF_KIND_PTR:
		/* just validate overall shape of the referenced type, so no
		 * contents comparison for struct/union, and allowed fwd vs
		 * struct/union
		 */
		exact = false;
		id1 = t1->type;
		id2 = t2->type;
		goto recur;
	case BTF_KIND_ARRAY:
		/* ignore index type and array size */
		id1 = btf_array(t1)->type;
		id2 = btf_array(t2)->type;
		goto recur;
	case BTF_KIND_FUNC:
		/* extern and global linkages are compatible */
		is_static1 = btf_func_linkage(t1) == BTF_FUNC_STATIC;
		is_static2 = btf_func_linkage(t2) == BTF_FUNC_STATIC;
		if (is_static1 != is_static2) {
			pr_warn("global '%s': incompatible func '%s' linkage\n", sym_name, n1);
			return false;
		}

		id1 = t1->type;
		id2 = t2->type;
		goto recur;
	case BTF_KIND_VAR:
		/* extern and global linkages are compatible */
		is_static1 = btf_var(t1)->linkage == BTF_VAR_STATIC;
		is_static2 = btf_var(t2)->linkage == BTF_VAR_STATIC;
		if (is_static1 != is_static2) {
			pr_warn("global '%s': incompatible var '%s' linkage\n", sym_name, n1);
			return false;
		}

		id1 = t1->type;
		id2 = t2->type;
		goto recur;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m1, *m2;

		if (!exact)
			return true;

		if (btf_vlen(t1) != btf_vlen(t2)) {
			pr_warn("global '%s': incompatible number of %s fields %u and %u\n",
				sym_name, btf_kind_str(t1), btf_vlen(t1), btf_vlen(t2));
			return false;
		}

		n = btf_vlen(t1);
		m1 = btf_members(t1);
		m2 = btf_members(t2);
		for (i = 0; i < n; i++, m1++, m2++) {
			n1 = btf__str_by_offset(btf1, m1->name_off);
			n2 = btf__str_by_offset(btf2, m2->name_off);
			if (strcmp(n1, n2) != 0) {
				pr_warn("global '%s': incompatible field #%d names '%s' and '%s'\n",
					sym_name, i, n1, n2);
				return false;
			}
			if (m1->offset != m2->offset) {
				pr_warn("global '%s': incompatible field #%d ('%s') offsets\n",
					sym_name, i, n1);
				return false;
			}
			if (!glob_sym_btf_matches(sym_name, exact, btf1, m1->type, btf2, m2->type))
				return false;
		}

		return true;
	}
	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *m1, *m2;

		if (btf_vlen(t1) != btf_vlen(t2)) {
			pr_warn("global '%s': incompatible number of %s params %u and %u\n",
				sym_name, btf_kind_str(t1), btf_vlen(t1), btf_vlen(t2));
			return false;
		}

		n = btf_vlen(t1);
		m1 = btf_params(t1);
		m2 = btf_params(t2);
		for (i = 0; i < n; i++, m1++, m2++) {
			/* ignore func arg names */
			if (!glob_sym_btf_matches(sym_name, exact, btf1, m1->type, btf2, m2->type))
				return false;
		}

		/* now check return type as well */
		id1 = t1->type;
		id2 = t2->type;
		goto recur;
	}

	/* skip_mods_and_typedefs() make this impossible */
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	/* DATASECs are never compared with each other */
	case BTF_KIND_DATASEC:
	default:
		pr_warn("global '%s': unsupported BTF kind %s\n",
			sym_name, btf_kind_str(t1));
		return false;
	}
}

static bool map_defs_match(const char *sym_name,
			   const struct btf *main_btf,
			   const struct btf_map_def *main_def,
			   const struct btf_map_def *main_inner_def,
			   const struct btf *extra_btf,
			   const struct btf_map_def *extra_def,
			   const struct btf_map_def *extra_inner_def)
{
	const char *reason;

	if (main_def->map_type != extra_def->map_type) {
		reason = "type";
		goto mismatch;
	}

	/* check key type/size match */
	if (main_def->key_size != extra_def->key_size) {
		reason = "key_size";
		goto mismatch;
	}
	if (!!main_def->key_type_id != !!extra_def->key_type_id) {
		reason = "key type";
		goto mismatch;
	}
	if ((main_def->parts & MAP_DEF_KEY_TYPE)
	     && !glob_sym_btf_matches(sym_name, true /*exact*/,
				      main_btf, main_def->key_type_id,
				      extra_btf, extra_def->key_type_id)) {
		reason = "key type";
		goto mismatch;
	}

	/* validate value type/size match */
	if (main_def->value_size != extra_def->value_size) {
		reason = "value_size";
		goto mismatch;
	}
	if (!!main_def->value_type_id != !!extra_def->value_type_id) {
		reason = "value type";
		goto mismatch;
	}
	if ((main_def->parts & MAP_DEF_VALUE_TYPE)
	     && !glob_sym_btf_matches(sym_name, true /*exact*/,
				      main_btf, main_def->value_type_id,
				      extra_btf, extra_def->value_type_id)) {
		reason = "key type";
		goto mismatch;
	}

	if (main_def->max_entries != extra_def->max_entries) {
		reason = "max_entries";
		goto mismatch;
	}
	if (main_def->map_flags != extra_def->map_flags) {
		reason = "map_flags";
		goto mismatch;
	}
	if (main_def->numa_node != extra_def->numa_node) {
		reason = "numa_node";
		goto mismatch;
	}
	if (main_def->pinning != extra_def->pinning) {
		reason = "pinning";
		goto mismatch;
	}

	if ((main_def->parts & MAP_DEF_INNER_MAP) != (extra_def->parts & MAP_DEF_INNER_MAP)) {
		reason = "inner map";
		goto mismatch;
	}

	if (main_def->parts & MAP_DEF_INNER_MAP) {
		char inner_map_name[128];

		snprintf(inner_map_name, sizeof(inner_map_name), "%s.inner", sym_name);

		return map_defs_match(inner_map_name,
				      main_btf, main_inner_def, NULL,
				      extra_btf, extra_inner_def, NULL);
	}

	return true;

mismatch:
	pr_warn("global '%s': map %s mismatch\n", sym_name, reason);
	return false;
}

static bool glob_map_defs_match(const char *sym_name,
				struct bpf_linker *linker, struct glob_sym *glob_sym,
				struct src_obj *obj, Elf64_Sym *sym, int btf_id)
{
	struct btf_map_def dst_def = {}, dst_inner_def = {};
	struct btf_map_def src_def = {}, src_inner_def = {};
	const struct btf_type *t;
	int err;

	t = btf__type_by_id(obj->btf, btf_id);
	if (!btf_is_var(t)) {
		pr_warn("global '%s': invalid map definition type [%d]\n", sym_name, btf_id);
		return false;
	}
	t = skip_mods_and_typedefs(obj->btf, t->type, NULL);

	err = parse_btf_map_def(sym_name, obj->btf, t, true /*strict*/, &src_def, &src_inner_def);
	if (err) {
		pr_warn("global '%s': invalid map definition\n", sym_name);
		return false;
	}

	/* re-parse existing map definition */
	t = btf__type_by_id(linker->btf, glob_sym->btf_id);
	t = skip_mods_and_typedefs(linker->btf, t->type, NULL);
	err = parse_btf_map_def(sym_name, linker->btf, t, true /*strict*/, &dst_def, &dst_inner_def);
	if (err) {
		/* this should not happen, because we already validated it */
		pr_warn("global '%s': invalid dst map definition\n", sym_name);
		return false;
	}

	/* Currently extern map definition has to be complete and match
	 * concrete map definition exactly. This restriction might be lifted
	 * in the future.
	 */
	return map_defs_match(sym_name, linker->btf, &dst_def, &dst_inner_def,
			      obj->btf, &src_def, &src_inner_def);
}

static bool glob_syms_match(const char *sym_name,
			    struct bpf_linker *linker, struct glob_sym *glob_sym,
			    struct src_obj *obj, Elf64_Sym *sym, size_t sym_idx, int btf_id)
{
	const struct btf_type *src_t;

	/* if we are dealing with externs, BTF types describing both global
	 * and extern VARs/FUNCs should be completely present in all files
	 */
	if (!glob_sym->btf_id || !btf_id) {
		pr_warn("BTF info is missing for global symbol '%s'\n", sym_name);
		return false;
	}

	src_t = btf__type_by_id(obj->btf, btf_id);
	if (!btf_is_var(src_t) && !btf_is_func(src_t)) {
		pr_warn("only extern variables and functions are supported, but got '%s' for '%s'\n",
			btf_kind_str(src_t), sym_name);
		return false;
	}

	/* deal with .maps definitions specially */
	if (glob_sym->sec_id && strcmp(linker->secs[glob_sym->sec_id].sec_name, MAPS_ELF_SEC) == 0)
		return glob_map_defs_match(sym_name, linker, glob_sym, obj, sym, btf_id);

	if (!glob_sym_btf_matches(sym_name, true /*exact*/,
				  linker->btf, glob_sym->btf_id, obj->btf, btf_id))
		return false;

	return true;
}

static bool btf_is_non_static(const struct btf_type *t)
{
	return (btf_is_var(t) && btf_var(t)->linkage != BTF_VAR_STATIC)
	       || (btf_is_func(t) && btf_func_linkage(t) != BTF_FUNC_STATIC);
}

static int find_glob_sym_btf(struct src_obj *obj, Elf64_Sym *sym, const char *sym_name,
			     int *out_btf_sec_id, int *out_btf_id)
{
	int i, j, n, m, btf_id = 0;
	const struct btf_type *t;
	const struct btf_var_secinfo *vi;
	const char *name;

	if (!obj->btf) {
		pr_warn("failed to find BTF info for object '%s'\n", obj->filename);
		return -EINVAL;
	}

	n = btf__type_cnt(obj->btf);
	for (i = 1; i < n; i++) {
		t = btf__type_by_id(obj->btf, i);

		/* some global and extern FUNCs and VARs might not be associated with any
		 * DATASEC, so try to detect them in the same pass
		 */
		if (btf_is_non_static(t)) {
			name = btf__str_by_offset(obj->btf, t->name_off);
			if (strcmp(name, sym_name) != 0)
				continue;

			/* remember and still try to find DATASEC */
			btf_id = i;
			continue;
		}

		if (!btf_is_datasec(t))
			continue;

		vi = btf_var_secinfos(t);
		for (j = 0, m = btf_vlen(t); j < m; j++, vi++) {
			t = btf__type_by_id(obj->btf, vi->type);
			name = btf__str_by_offset(obj->btf, t->name_off);

			if (strcmp(name, sym_name) != 0)
				continue;
			if (btf_is_var(t) && btf_var(t)->linkage == BTF_VAR_STATIC)
				continue;
			if (btf_is_func(t) && btf_func_linkage(t) == BTF_FUNC_STATIC)
				continue;

			if (btf_id && btf_id != vi->type) {
				pr_warn("global/extern '%s' BTF is ambiguous: both types #%d and #%u match\n",
					sym_name, btf_id, vi->type);
				return -EINVAL;
			}

			*out_btf_sec_id = i;
			*out_btf_id = vi->type;

			return 0;
		}
	}

	/* free-floating extern or global FUNC */
	if (btf_id) {
		*out_btf_sec_id = 0;
		*out_btf_id = btf_id;
		return 0;
	}

	pr_warn("failed to find BTF info for global/extern symbol '%s'\n", sym_name);
	return -ENOENT;
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

static int complete_extern_btf_info(struct btf *dst_btf, int dst_id,
				    struct btf *src_btf, int src_id)
{
	struct btf_type *dst_t = btf_type_by_id(dst_btf, dst_id);
	struct btf_type *src_t = btf_type_by_id(src_btf, src_id);
	struct btf_param *src_p, *dst_p;
	const char *s;
	int i, n, off;

	/* We already made sure that source and destination types (FUNC or
	 * VAR) match in terms of types and argument names.
	 */
	if (btf_is_var(dst_t)) {
		btf_var(dst_t)->linkage = BTF_VAR_GLOBAL_ALLOCATED;
		return 0;
	}

	dst_t->info = btf_type_info(BTF_KIND_FUNC, BTF_FUNC_GLOBAL, 0);

	/* now onto FUNC_PROTO types */
	src_t = btf_type_by_id(src_btf, src_t->type);
	dst_t = btf_type_by_id(dst_btf, dst_t->type);

	/* Fill in all the argument names, which for extern FUNCs are missing.
	 * We'll end up with two copies of FUNCs/VARs for externs, but that
	 * will be taken care of by BTF dedup at the very end.
	 * It might be that BTF types for extern in one file has less/more BTF
	 * information (e.g., FWD instead of full STRUCT/UNION information),
	 * but that should be (in most cases, subject to BTF dedup rules)
	 * handled and resolved by BTF dedup algorithm as well, so we won't
	 * worry about it. Our only job is to make sure that argument names
	 * are populated on both sides, otherwise BTF dedup will pedantically
	 * consider them different.
	 */
	src_p = btf_params(src_t);
	dst_p = btf_params(dst_t);
	for (i = 0, n = btf_vlen(dst_t); i < n; i++, src_p++, dst_p++) {
		if (!src_p->name_off)
			continue;

		/* src_btf has more complete info, so add name to dst_btf */
		s = btf__str_by_offset(src_btf, src_p->name_off);
		off = btf__add_str(dst_btf, s);
		if (off < 0)
			return off;
		dst_p->name_off = off;
	}
	return 0;
}

static void sym_update_bind(Elf64_Sym *sym, int sym_bind)
{
	sym->st_info = ELF64_ST_INFO(sym_bind, ELF64_ST_TYPE(sym->st_info));
}

static void sym_update_type(Elf64_Sym *sym, int sym_type)
{
	sym->st_info = ELF64_ST_INFO(ELF64_ST_BIND(sym->st_info), sym_type);
}

static void sym_update_visibility(Elf64_Sym *sym, int sym_vis)
{
	/* libelf doesn't provide setters for ST_VISIBILITY,
	 * but it is stored in the lower 2 bits of st_other
	 */
	sym->st_other &= ~0x03;
	sym->st_other |= sym_vis;
}

static int linker_append_elf_sym(struct bpf_linker *linker, struct src_obj *obj,
				 Elf64_Sym *sym, const char *sym_name, int src_sym_idx)
{
	struct src_sec *src_sec = NULL;
	struct dst_sec *dst_sec = NULL;
	struct glob_sym *glob_sym = NULL;
	int name_off, sym_type, sym_bind, sym_vis, err;
	int btf_sec_id = 0, btf_id = 0;
	size_t dst_sym_idx;
	Elf64_Sym *dst_sym;
	bool sym_is_extern;

	sym_type = ELF64_ST_TYPE(sym->st_info);
	sym_bind = ELF64_ST_BIND(sym->st_info);
	sym_vis = ELF64_ST_VISIBILITY(sym->st_other);
	sym_is_extern = sym->st_shndx == SHN_UNDEF;

	if (sym_is_extern) {
		if (!obj->btf) {
			pr_warn("externs without BTF info are not supported\n");
			return -ENOTSUP;
		}
	} else if (sym->st_shndx < SHN_LORESERVE) {
		src_sec = &obj->secs[sym->st_shndx];
		if (src_sec->skipped)
			return 0;
		dst_sec = &linker->secs[src_sec->dst_id];

		/* allow only one STT_SECTION symbol per section */
		if (sym_type == STT_SECTION && dst_sec->sec_sym_idx) {
			obj->sym_map[src_sym_idx] = dst_sec->sec_sym_idx;
			return 0;
		}
	}

	if (sym_bind == STB_LOCAL)
		goto add_sym;

	/* find matching BTF info */
	err = find_glob_sym_btf(obj, sym, sym_name, &btf_sec_id, &btf_id);
	if (err)
		return err;

	if (sym_is_extern && btf_sec_id) {
		const char *sec_name = NULL;
		const struct btf_type *t;

		t = btf__type_by_id(obj->btf, btf_sec_id);
		sec_name = btf__str_by_offset(obj->btf, t->name_off);

		/* Clang puts unannotated extern vars into
		 * '.extern' BTF DATASEC. Treat them the same
		 * as unannotated extern funcs (which are
		 * currently not put into any DATASECs).
		 * Those don't have associated src_sec/dst_sec.
		 */
		if (strcmp(sec_name, BTF_EXTERN_SEC) != 0) {
			src_sec = find_src_sec_by_name(obj, sec_name);
			if (!src_sec) {
				pr_warn("failed to find matching ELF sec '%s'\n", sec_name);
				return -ENOENT;
			}
			dst_sec = &linker->secs[src_sec->dst_id];
		}
	}

	glob_sym = find_glob_sym(linker, sym_name);
	if (glob_sym) {
		/* Preventively resolve to existing symbol. This is
		 * needed for further relocation symbol remapping in
		 * the next step of linking.
		 */
		obj->sym_map[src_sym_idx] = glob_sym->sym_idx;

		/* If both symbols are non-externs, at least one of
		 * them has to be STB_WEAK, otherwise they are in
		 * a conflict with each other.
		 */
		if (!sym_is_extern && !glob_sym->is_extern
		    && !glob_sym->is_weak && sym_bind != STB_WEAK) {
			pr_warn("conflicting non-weak symbol #%d (%s) definition in '%s'\n",
				src_sym_idx, sym_name, obj->filename);
			return -EINVAL;
		}

		if (!glob_syms_match(sym_name, linker, glob_sym, obj, sym, src_sym_idx, btf_id))
			return -EINVAL;

		dst_sym = get_sym_by_idx(linker, glob_sym->sym_idx);

		/* If new symbol is strong, then force dst_sym to be strong as
		 * well; this way a mix of weak and non-weak extern
		 * definitions will end up being strong.
		 */
		if (sym_bind == STB_GLOBAL) {
			/* We still need to preserve type (NOTYPE or
			 * OBJECT/FUNC, depending on whether the symbol is
			 * extern or not)
			 */
			sym_update_bind(dst_sym, STB_GLOBAL);
			glob_sym->is_weak = false;
		}

		/* Non-default visibility is "contaminating", with stricter
		 * visibility overwriting more permissive ones, even if more
		 * permissive visibility comes from just an extern definition.
		 * Currently only STV_DEFAULT and STV_HIDDEN are allowed and
		 * ensured by ELF symbol sanity checks above.
		 */
		if (sym_vis > ELF64_ST_VISIBILITY(dst_sym->st_other))
			sym_update_visibility(dst_sym, sym_vis);

		/* If the new symbol is extern, then regardless if
		 * existing symbol is extern or resolved global, just
		 * keep the existing one untouched.
		 */
		if (sym_is_extern)
			return 0;

		/* If existing symbol is a strong resolved symbol, bail out,
		 * because we lost resolution battle have nothing to
		 * contribute. We already checked above that there is no
		 * strong-strong conflict. We also already tightened binding
		 * and visibility, so nothing else to contribute at that point.
		 */
		if (!glob_sym->is_extern && sym_bind == STB_WEAK)
			return 0;

		/* At this point, new symbol is strong non-extern,
		 * so overwrite glob_sym with new symbol information.
		 * Preserve binding and visibility.
		 */
		sym_update_type(dst_sym, sym_type);
		dst_sym->st_shndx = dst_sec->sec_idx;
		dst_sym->st_value = src_sec->dst_off + sym->st_value;
		dst_sym->st_size = sym->st_size;

		/* see comment below about dst_sec->id vs dst_sec->sec_idx */
		glob_sym->sec_id = dst_sec->id;
		glob_sym->is_extern = false;

		if (complete_extern_btf_info(linker->btf, glob_sym->btf_id,
					     obj->btf, btf_id))
			return -EINVAL;

		/* request updating VAR's/FUNC's underlying BTF type when appending BTF type */
		glob_sym->underlying_btf_id = 0;

		obj->sym_map[src_sym_idx] = glob_sym->sym_idx;
		return 0;
	}

add_sym:
	name_off = strset__add_str(linker->strtab_strs, sym_name);
	if (name_off < 0)
		return name_off;

	dst_sym = add_new_sym(linker, &dst_sym_idx);
	if (!dst_sym)
		return -ENOMEM;

	dst_sym->st_name = name_off;
	dst_sym->st_info = sym->st_info;
	dst_sym->st_other = sym->st_other;
	dst_sym->st_shndx = dst_sec ? dst_sec->sec_idx : sym->st_shndx;
	dst_sym->st_value = (src_sec ? src_sec->dst_off : 0) + sym->st_value;
	dst_sym->st_size = sym->st_size;

	obj->sym_map[src_sym_idx] = dst_sym_idx;

	if (sym_type == STT_SECTION && dst_sec) {
		dst_sec->sec_sym_idx = dst_sym_idx;
		dst_sym->st_value = 0;
	}

	if (sym_bind != STB_LOCAL) {
		glob_sym = add_glob_sym(linker);
		if (!glob_sym)
			return -ENOMEM;

		glob_sym->sym_idx = dst_sym_idx;
		/* we use dst_sec->id (and not dst_sec->sec_idx), because
		 * ephemeral sections (.kconfig, .ksyms, etc) don't have
		 * sec_idx (as they don't have corresponding ELF section), but
		 * still have id. .extern doesn't have even ephemeral section
		 * associated with it, so dst_sec->id == dst_sec->sec_idx == 0.
		 */
		glob_sym->sec_id = dst_sec ? dst_sec->id : 0;
		glob_sym->name_off = name_off;
		/* we will fill btf_id in during BTF merging step */
		glob_sym->btf_id = 0;
		glob_sym->is_extern = sym_is_extern;
		glob_sym->is_weak = sym_bind == STB_WEAK;
	}

	return 0;
}

static int linker_append_elf_relos(struct bpf_linker *linker, struct src_obj *obj)
{
	struct src_sec *src_symtab = &obj->secs[obj->symtab_sec_idx];
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
			pr_warn("sections %s are not compatible\n", src_sec->sec_name);
			return -EINVAL;
		}

		/* shdr->sh_link points to SYMTAB */
		dst_sec->shdr->sh_link = linker->symtab_sec_idx;

		/* shdr->sh_info points to relocated section */
		dst_linked_sec = &linker->secs[src_linked_sec->dst_id];
		dst_sec->shdr->sh_info = dst_linked_sec->sec_idx;

		src_sec->dst_id = dst_sec->id;
		err = extend_sec(linker, dst_sec, src_sec);
		if (err)
			return err;

		src_rel = src_sec->data->d_buf;
		dst_rel = dst_sec->raw_data + src_sec->dst_off;
		n = src_sec->shdr->sh_size / src_sec->shdr->sh_entsize;
		for (j = 0; j < n; j++, src_rel++, dst_rel++) {
			size_t src_sym_idx, dst_sym_idx, sym_type;
			Elf64_Sym *src_sym;

			src_sym_idx = ELF64_R_SYM(src_rel->r_info);
			src_sym = src_symtab->data->d_buf + sizeof(*src_sym) * src_sym_idx;

			dst_sym_idx = obj->sym_map[src_sym_idx];
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

	n = btf__type_cnt(obj->btf);
	for (i = 1; i < n; i++) {
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
			 * in ELF, e.g., .kconfig, .ksyms, .extern, which are used
			 * for special extern variables.
			 *
			 * For all but one such special (ephemeral)
			 * sections, we pre-create "section shells" to be able
			 * to keep track of extra per-section metadata later
			 * (e.g., those BTF extern variables).
			 *
			 * .extern is even more special, though, because it
			 * contains extern variables that need to be resolved
			 * by static linker, not libbpf and kernel. When such
			 * externs are resolved, we are going to remove them
			 * from .extern BTF section and might end up not
			 * needing it at all. Each resolved extern should have
			 * matching non-extern VAR/FUNC in other sections.
			 *
			 * We do support leaving some of the externs
			 * unresolved, though, to support cases of building
			 * libraries, which will later be linked against final
			 * BPF applications. So if at finalization we still
			 * see unresolved externs, we'll create .extern
			 * section on our own.
			 */
			if (strcmp(sec_name, BTF_EXTERN_SEC) == 0)
				continue;

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
			const char *var_name;
			int var_linkage;
			Elf64_Sym *sym;

			/* could be a variable or function */
			if (!btf_is_var(vt))
				continue;

			var_name = btf__str_by_offset(obj->btf, vt->name_off);
			var_linkage = btf_var(vt)->linkage;

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

static int linker_append_btf(struct bpf_linker *linker, struct src_obj *obj)
{
	const struct btf_type *t;
	int i, j, n, start_id, id, err;
	const char *name;

	if (!obj->btf)
		return 0;

	start_id = btf__type_cnt(linker->btf);
	n = btf__type_cnt(obj->btf);

	obj->btf_type_map = calloc(n + 1, sizeof(int));
	if (!obj->btf_type_map)
		return -ENOMEM;

	for (i = 1; i < n; i++) {
		struct glob_sym *glob_sym = NULL;

		t = btf__type_by_id(obj->btf, i);

		/* DATASECs are handled specially below */
		if (btf_kind(t) == BTF_KIND_DATASEC)
			continue;

		if (btf_is_non_static(t)) {
			/* there should be glob_sym already */
			name = btf__str_by_offset(obj->btf, t->name_off);
			glob_sym = find_glob_sym(linker, name);

			/* VARs without corresponding glob_sym are those that
			 * belong to skipped/deduplicated sections (i.e.,
			 * license and version), so just skip them
			 */
			if (!glob_sym)
				continue;

			/* linker_append_elf_sym() might have requested
			 * updating underlying type ID, if extern was resolved
			 * to strong symbol or weak got upgraded to non-weak
			 */
			if (glob_sym->underlying_btf_id == 0)
				glob_sym->underlying_btf_id = -t->type;

			/* globals from previous object files that match our
			 * VAR/FUNC already have a corresponding associated
			 * BTF type, so just make sure to use it
			 */
			if (glob_sym->btf_id) {
				/* reuse existing BTF type for global var/func */
				obj->btf_type_map[i] = glob_sym->btf_id;
				continue;
			}
		}

		id = btf__add_type(linker->btf, obj->btf, t);
		if (id < 0) {
			pr_warn("failed to append BTF type #%d from file '%s'\n", i, obj->filename);
			return id;
		}

		obj->btf_type_map[i] = id;

		/* record just appended BTF type for var/func */
		if (glob_sym) {
			glob_sym->btf_id = id;
			glob_sym->underlying_btf_id = -t->type;
		}
	}

	/* remap all the types except DATASECs */
	n = btf__type_cnt(linker->btf);
	for (i = start_id; i < n; i++) {
		struct btf_type *dst_t = btf_type_by_id(linker->btf, i);
		struct btf_field_iter it;
		__u32 *type_id;

		err = btf_field_iter_init(&it, dst_t, BTF_FIELD_ITER_IDS);
		if (err)
			return err;

		while ((type_id = btf_field_iter_next(&it))) {
			int new_id = obj->btf_type_map[*type_id];

			/* Error out if the type wasn't remapped. Ignore VOID which stays VOID. */
			if (new_id == 0 && *type_id != 0) {
				pr_warn("failed to find new ID mapping for original BTF type ID %u\n",
					*type_id);
				return -EINVAL;
			}

			*type_id = obj->btf_type_map[*type_id];
		}
	}

	/* Rewrite VAR/FUNC underlying types (i.e., FUNC's FUNC_PROTO and VAR's
	 * actual type), if necessary
	 */
	for (i = 0; i < linker->glob_sym_cnt; i++) {
		struct glob_sym *glob_sym = &linker->glob_syms[i];
		struct btf_type *glob_t;

		if (glob_sym->underlying_btf_id >= 0)
			continue;

		glob_sym->underlying_btf_id = obj->btf_type_map[-glob_sym->underlying_btf_id];

		glob_t = btf_type_by_id(linker->btf, glob_sym->btf_id);
		glob_t->type = glob_sym->underlying_btf_id;
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

		/* Mark section as having BTF regardless of the presence of
		 * variables. In some cases compiler might generate empty BTF
		 * with no variables information. E.g., when promoting local
		 * array/structure variable initial values and BPF object
		 * file otherwise has no read-only static variables in
		 * .rodata. We need to preserve such empty BTF and just set
		 * correct section size.
		 */
		dst_sec->has_btf = true;

		t = btf__type_by_id(obj->btf, src_sec->sec_type_id);
		src_var = btf_var_secinfos(t);
		n = btf_vlen(t);
		for (j = 0; j < n; j++, src_var++) {
			void *sec_vars = dst_sec->sec_vars;
			int new_id = obj->btf_type_map[src_var->type];
			struct glob_sym *glob_sym = NULL;

			t = btf_type_by_id(linker->btf, new_id);
			if (btf_is_non_static(t)) {
				name = btf__str_by_offset(linker->btf, t->name_off);
				glob_sym = find_glob_sym(linker, name);
				if (glob_sym->sec_id != dst_sec->id) {
					pr_warn("global '%s': section mismatch %d vs %d\n",
						name, glob_sym->sec_id, dst_sec->id);
					return -EINVAL;
				}
			}

			/* If there is already a member (VAR or FUNC) mapped
			 * to the same type, don't add a duplicate entry.
			 * This will happen when multiple object files define
			 * the same extern VARs/FUNCs.
			 */
			if (glob_sym && glob_sym->var_idx >= 0) {
				__s64 sz;

				/* FUNCs don't have size, nothing to update */
				if (btf_is_func(t))
					continue;

				dst_var = &dst_sec->sec_vars[glob_sym->var_idx];
				/* Because underlying BTF type might have
				 * changed, so might its size have changed, so
				 * re-calculate and update it in sec_var.
				 */
				sz = btf__resolve_size(linker->btf, glob_sym->underlying_btf_id);
				if (sz < 0) {
					pr_warn("global '%s': failed to resolve size of underlying type: %d\n",
						name, (int)sz);
					return -EINVAL;
				}
				dst_var->size = sz;
				continue;
			}

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

			if (glob_sym)
				glob_sym->var_idx = dst_sec->sec_var_cnt - 1;
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
		return libbpf_err(-EINVAL);

	err = finalize_btf(linker);
	if (err)
		return libbpf_err(err);

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

		/* restore sections with bpf insns to target byte-order */
		if (linker->swapped_endian && is_exec_sec(sec))
			exec_sec_bswap(sec->raw_data, sec->sec_sz);

		sec->data->d_buf = sec->raw_data;
	}

	/* Finalize ELF layout */
	if (elf_update(linker->elf, ELF_C_NULL) < 0) {
		err = -EINVAL;
		pr_warn_elf("failed to finalize ELF layout");
		return libbpf_err(err);
	}

	/* Write out final ELF contents */
	if (elf_update(linker->elf, ELF_C_WRITE) < 0) {
		err = -EINVAL;
		pr_warn_elf("failed to write ELF contents");
		return libbpf_err(err);
	}

	elf_end(linker->elf);
	linker->elf = NULL;

	if (linker->fd_is_owned)
		close(linker->fd);
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
	enum btf_endianness link_endianness;
	LIBBPF_OPTS(btf_dedup_opts, opts);
	struct btf *btf = linker->btf;
	const void *raw_data;
	int i, j, id, err;
	__u32 raw_sz;

	/* bail out if no BTF data was produced */
	if (btf__type_cnt(linker->btf) == 1)
		return 0;

	for (i = 1; i < linker->sec_cnt; i++) {
		struct dst_sec *sec = &linker->secs[i];

		if (!sec->has_btf)
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
		pr_warn(".BTF.ext generation failed: %s\n", errstr(err));
		return err;
	}

	opts.btf_ext = linker->btf_ext;
	err = btf__dedup(linker->btf, &opts);
	if (err) {
		pr_warn("BTF dedup failed: %s\n", errstr(err));
		return err;
	}

	/* Set .BTF and .BTF.ext output byte order */
	link_endianness = linker->elf_hdr->e_ident[EI_DATA] == ELFDATA2MSB ?
			  BTF_BIG_ENDIAN : BTF_LITTLE_ENDIAN;
	btf__set_endianness(linker->btf, link_endianness);
	if (linker->btf_ext)
		btf_ext__set_endianness(linker->btf_ext, link_endianness);

	/* Emit .BTF section */
	raw_data = btf__raw_data(linker->btf, &raw_sz);
	if (!raw_data)
		return -ENOMEM;

	err = emit_elf_data_sec(linker, BTF_ELF_SEC, 8, raw_data, raw_sz);
	if (err) {
		pr_warn("failed to write out .BTF ELF section: %s\n", errstr(err));
		return err;
	}

	/* Emit .BTF.ext section */
	if (linker->btf_ext) {
		raw_data = btf_ext__raw_data(linker->btf_ext, &raw_sz);
		if (!raw_data)
			return -ENOMEM;

		err = emit_elf_data_sec(linker, BTF_EXT_ELF_SEC, 8, raw_data, raw_sz);
		if (err) {
			pr_warn("failed to write out .BTF.ext ELF section: %s\n", errstr(err));
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
	hdr->core_relo_off = funcs_sz + lines_sz;
	hdr->core_relo_len = core_relos_sz;

	if (funcs_sz) {
		*(__u32 *)cur = func_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->func_info);
			if (sz < 0) {
				err = sz;
				goto out;
			}

			cur += sz;
		}
	}

	if (lines_sz) {
		*(__u32 *)cur = line_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->line_info);
			if (sz < 0) {
				err = sz;
				goto out;
			}

			cur += sz;
		}
	}

	if (core_relos_sz) {
		*(__u32 *)cur = core_relo_rec_sz;
		cur += sizeof(__u32);

		for (i = 1; i < linker->sec_cnt; i++) {
			struct dst_sec *sec = &linker->secs[i];

			sz = emit_btf_ext_data(linker, cur, sec->sec_name, &sec->core_relo_info);
			if (sz < 0) {
				err = sz;
				goto out;
			}

			cur += sz;
		}
	}

	linker->btf_ext = btf_ext__new(data, total_sz);
	err = libbpf_get_error(linker->btf_ext);
	if (err) {
		linker->btf_ext = NULL;
		pr_warn("failed to parse final .BTF.ext data: %s\n", errstr(err));
		goto out;
	}

out:
	free(data);
	return err;
}
