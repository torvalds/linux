#include <libelf.h>
#include <gelf.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "symbol.h"
#include "debug.h"

#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

/**
 * elf_symtab__for_each_symbol - iterate thru all the symbols
 *
 * @syms: struct elf_symtab instance to iterate
 * @idx: uint32_t idx
 * @sym: GElf_Sym iterator
 */
#define elf_symtab__for_each_symbol(syms, nr_syms, idx, sym) \
	for (idx = 0, gelf_getsym(syms, idx, &sym);\
	     idx < nr_syms; \
	     idx++, gelf_getsym(syms, idx, &sym))

static inline uint8_t elf_sym__type(const GElf_Sym *sym)
{
	return GELF_ST_TYPE(sym->st_info);
}

static inline int elf_sym__is_function(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_FUNC &&
	       sym->st_name != 0 &&
	       sym->st_shndx != SHN_UNDEF;
}

static inline bool elf_sym__is_object(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_OBJECT &&
		sym->st_name != 0 &&
		sym->st_shndx != SHN_UNDEF;
}

static inline int elf_sym__is_label(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_NOTYPE &&
		sym->st_name != 0 &&
		sym->st_shndx != SHN_UNDEF &&
		sym->st_shndx != SHN_ABS;
}

static bool elf_sym__is_a(GElf_Sym *sym, enum map_type type)
{
	switch (type) {
	case MAP__FUNCTION:
		return elf_sym__is_function(sym);
	case MAP__VARIABLE:
		return elf_sym__is_object(sym);
	default:
		return false;
	}
}

static inline const char *elf_sym__name(const GElf_Sym *sym,
					const Elf_Data *symstrs)
{
	return symstrs->d_buf + sym->st_name;
}

static inline const char *elf_sec__name(const GElf_Shdr *shdr,
					const Elf_Data *secstrs)
{
	return secstrs->d_buf + shdr->sh_name;
}

static inline int elf_sec__is_text(const GElf_Shdr *shdr,
					const Elf_Data *secstrs)
{
	return strstr(elf_sec__name(shdr, secstrs), "text") != NULL;
}

static inline bool elf_sec__is_data(const GElf_Shdr *shdr,
				    const Elf_Data *secstrs)
{
	return strstr(elf_sec__name(shdr, secstrs), "data") != NULL;
}

static bool elf_sec__is_a(GElf_Shdr *shdr, Elf_Data *secstrs,
			  enum map_type type)
{
	switch (type) {
	case MAP__FUNCTION:
		return elf_sec__is_text(shdr, secstrs);
	case MAP__VARIABLE:
		return elf_sec__is_data(shdr, secstrs);
	default:
		return false;
	}
}

static size_t elf_addr_to_index(Elf *elf, GElf_Addr addr)
{
	Elf_Scn *sec = NULL;
	GElf_Shdr shdr;
	size_t cnt = 1;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		gelf_getshdr(sec, &shdr);

		if ((addr >= shdr.sh_addr) &&
		    (addr < (shdr.sh_addr + shdr.sh_size)))
			return cnt;

		++cnt;
	}

	return -1;
}

static Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
				    GElf_Shdr *shp, const char *name,
				    size_t *idx)
{
	Elf_Scn *sec = NULL;
	size_t cnt = 1;

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL))
		return NULL;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, shp);
		str = elf_strptr(elf, ep->e_shstrndx, shp->sh_name);
		if (!strcmp(name, str)) {
			if (idx)
				*idx = cnt;
			break;
		}
		++cnt;
	}

	return sec;
}

#define elf_section__for_each_rel(reldata, pos, pos_mem, idx, nr_entries) \
	for (idx = 0, pos = gelf_getrel(reldata, 0, &pos_mem); \
	     idx < nr_entries; \
	     ++idx, pos = gelf_getrel(reldata, idx, &pos_mem))

#define elf_section__for_each_rela(reldata, pos, pos_mem, idx, nr_entries) \
	for (idx = 0, pos = gelf_getrela(reldata, 0, &pos_mem); \
	     idx < nr_entries; \
	     ++idx, pos = gelf_getrela(reldata, idx, &pos_mem))

/*
 * We need to check if we have a .dynsym, so that we can handle the
 * .plt, synthesizing its symbols, that aren't on the symtabs (be it
 * .dynsym or .symtab).
 * And always look at the original dso, not at debuginfo packages, that
 * have the PLT data stripped out (shdr_rel_plt.sh_type == SHT_NOBITS).
 */
int dso__synthesize_plt_symbols(struct dso *dso, struct symsrc *ss, struct map *map,
				symbol_filter_t filter)
{
	uint32_t nr_rel_entries, idx;
	GElf_Sym sym;
	u64 plt_offset;
	GElf_Shdr shdr_plt;
	struct symbol *f;
	GElf_Shdr shdr_rel_plt, shdr_dynsym;
	Elf_Data *reldata, *syms, *symstrs;
	Elf_Scn *scn_plt_rel, *scn_symstrs, *scn_dynsym;
	size_t dynsym_idx;
	GElf_Ehdr ehdr;
	char sympltname[1024];
	Elf *elf;
	int nr = 0, symidx, err = 0;

	if (!ss->dynsym)
		return 0;

	elf = ss->elf;
	ehdr = ss->ehdr;

	scn_dynsym = ss->dynsym;
	shdr_dynsym = ss->dynshdr;
	dynsym_idx = ss->dynsym_idx;

	if (scn_dynsym == NULL)
		goto out_elf_end;

	scn_plt_rel = elf_section_by_name(elf, &ehdr, &shdr_rel_plt,
					  ".rela.plt", NULL);
	if (scn_plt_rel == NULL) {
		scn_plt_rel = elf_section_by_name(elf, &ehdr, &shdr_rel_plt,
						  ".rel.plt", NULL);
		if (scn_plt_rel == NULL)
			goto out_elf_end;
	}

	err = -1;

	if (shdr_rel_plt.sh_link != dynsym_idx)
		goto out_elf_end;

	if (elf_section_by_name(elf, &ehdr, &shdr_plt, ".plt", NULL) == NULL)
		goto out_elf_end;

	/*
	 * Fetch the relocation section to find the idxes to the GOT
	 * and the symbols in the .dynsym they refer to.
	 */
	reldata = elf_getdata(scn_plt_rel, NULL);
	if (reldata == NULL)
		goto out_elf_end;

	syms = elf_getdata(scn_dynsym, NULL);
	if (syms == NULL)
		goto out_elf_end;

	scn_symstrs = elf_getscn(elf, shdr_dynsym.sh_link);
	if (scn_symstrs == NULL)
		goto out_elf_end;

	symstrs = elf_getdata(scn_symstrs, NULL);
	if (symstrs == NULL)
		goto out_elf_end;

	if (symstrs->d_size == 0)
		goto out_elf_end;

	nr_rel_entries = shdr_rel_plt.sh_size / shdr_rel_plt.sh_entsize;
	plt_offset = shdr_plt.sh_offset;

	if (shdr_rel_plt.sh_type == SHT_RELA) {
		GElf_Rela pos_mem, *pos;

		elf_section__for_each_rela(reldata, pos, pos_mem, idx,
					   nr_rel_entries) {
			symidx = GELF_R_SYM(pos->r_info);
			plt_offset += shdr_plt.sh_entsize;
			gelf_getsym(syms, symidx, &sym);
			snprintf(sympltname, sizeof(sympltname),
				 "%s@plt", elf_sym__name(&sym, symstrs));

			f = symbol__new(plt_offset, shdr_plt.sh_entsize,
					STB_GLOBAL, sympltname);
			if (!f)
				goto out_elf_end;

			if (filter && filter(map, f))
				symbol__delete(f);
			else {
				symbols__insert(&dso->symbols[map->type], f);
				++nr;
			}
		}
	} else if (shdr_rel_plt.sh_type == SHT_REL) {
		GElf_Rel pos_mem, *pos;
		elf_section__for_each_rel(reldata, pos, pos_mem, idx,
					  nr_rel_entries) {
			symidx = GELF_R_SYM(pos->r_info);
			plt_offset += shdr_plt.sh_entsize;
			gelf_getsym(syms, symidx, &sym);
			snprintf(sympltname, sizeof(sympltname),
				 "%s@plt", elf_sym__name(&sym, symstrs));

			f = symbol__new(plt_offset, shdr_plt.sh_entsize,
					STB_GLOBAL, sympltname);
			if (!f)
				goto out_elf_end;

			if (filter && filter(map, f))
				symbol__delete(f);
			else {
				symbols__insert(&dso->symbols[map->type], f);
				++nr;
			}
		}
	}

	err = 0;
out_elf_end:
	if (err == 0)
		return nr;
	pr_debug("%s: problems reading %s PLT info.\n",
		 __func__, dso->long_name);
	return 0;
}

/*
 * Align offset to 4 bytes as needed for note name and descriptor data.
 */
#define NOTE_ALIGN(n) (((n) + 3) & -4U)

static int elf_read_build_id(Elf *elf, void *bf, size_t size)
{
	int err = -1;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *data;
	Elf_Scn *sec;
	Elf_Kind ek;
	void *ptr;

	if (size < BUILD_ID_SIZE)
		goto out;

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		goto out;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		pr_err("%s: cannot get elf header.\n", __func__);
		goto out;
	}

	/*
	 * Check following sections for notes:
	 *   '.note.gnu.build-id'
	 *   '.notes'
	 *   '.note' (VDSO specific)
	 */
	do {
		sec = elf_section_by_name(elf, &ehdr, &shdr,
					  ".note.gnu.build-id", NULL);
		if (sec)
			break;

		sec = elf_section_by_name(elf, &ehdr, &shdr,
					  ".notes", NULL);
		if (sec)
			break;

		sec = elf_section_by_name(elf, &ehdr, &shdr,
					  ".note", NULL);
		if (sec)
			break;

		return err;

	} while (0);

	data = elf_getdata(sec, NULL);
	if (data == NULL)
		goto out;

	ptr = data->d_buf;
	while (ptr < (data->d_buf + data->d_size)) {
		GElf_Nhdr *nhdr = ptr;
		size_t namesz = NOTE_ALIGN(nhdr->n_namesz),
		       descsz = NOTE_ALIGN(nhdr->n_descsz);
		const char *name;

		ptr += sizeof(*nhdr);
		name = ptr;
		ptr += namesz;
		if (nhdr->n_type == NT_GNU_BUILD_ID &&
		    nhdr->n_namesz == sizeof("GNU")) {
			if (memcmp(name, "GNU", sizeof("GNU")) == 0) {
				size_t sz = min(size, descsz);
				memcpy(bf, ptr, sz);
				memset(bf + sz, 0, size - sz);
				err = descsz;
				break;
			}
		}
		ptr += descsz;
	}

out:
	return err;
}

int filename__read_build_id(const char *filename, void *bf, size_t size)
{
	int fd, err = -1;
	Elf *elf;

	if (size < BUILD_ID_SIZE)
		goto out;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		goto out;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		pr_debug2("%s: cannot read %s ELF file.\n", __func__, filename);
		goto out_close;
	}

	err = elf_read_build_id(elf, bf, size);

	elf_end(elf);
out_close:
	close(fd);
out:
	return err;
}

int sysfs__read_build_id(const char *filename, void *build_id, size_t size)
{
	int fd, err = -1;

	if (size < BUILD_ID_SIZE)
		goto out;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		goto out;

	while (1) {
		char bf[BUFSIZ];
		GElf_Nhdr nhdr;
		size_t namesz, descsz;

		if (read(fd, &nhdr, sizeof(nhdr)) != sizeof(nhdr))
			break;

		namesz = NOTE_ALIGN(nhdr.n_namesz);
		descsz = NOTE_ALIGN(nhdr.n_descsz);
		if (nhdr.n_type == NT_GNU_BUILD_ID &&
		    nhdr.n_namesz == sizeof("GNU")) {
			if (read(fd, bf, namesz) != (ssize_t)namesz)
				break;
			if (memcmp(bf, "GNU", sizeof("GNU")) == 0) {
				size_t sz = min(descsz, size);
				if (read(fd, build_id, sz) == (ssize_t)sz) {
					memset(build_id + sz, 0, size - sz);
					err = 0;
					break;
				}
			} else if (read(fd, bf, descsz) != (ssize_t)descsz)
				break;
		} else {
			int n = namesz + descsz;
			if (read(fd, bf, n) != n)
				break;
		}
	}
	close(fd);
out:
	return err;
}

int filename__read_debuglink(const char *filename, char *debuglink,
			     size_t size)
{
	int fd, err = -1;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *data;
	Elf_Scn *sec;
	Elf_Kind ek;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		goto out;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		pr_debug2("%s: cannot read %s ELF file.\n", __func__, filename);
		goto out_close;
	}

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		goto out_close;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		pr_err("%s: cannot get elf header.\n", __func__);
		goto out_close;
	}

	sec = elf_section_by_name(elf, &ehdr, &shdr,
				  ".gnu_debuglink", NULL);
	if (sec == NULL)
		goto out_close;

	data = elf_getdata(sec, NULL);
	if (data == NULL)
		goto out_close;

	/* the start of this section is a zero-terminated string */
	strncpy(debuglink, data->d_buf, size);

	elf_end(elf);

out_close:
	close(fd);
out:
	return err;
}

static int dso__swap_init(struct dso *dso, unsigned char eidata)
{
	static unsigned int const endian = 1;

	dso->needs_swap = DSO_SWAP__NO;

	switch (eidata) {
	case ELFDATA2LSB:
		/* We are big endian, DSO is little endian. */
		if (*(unsigned char const *)&endian != 1)
			dso->needs_swap = DSO_SWAP__YES;
		break;

	case ELFDATA2MSB:
		/* We are little endian, DSO is big endian. */
		if (*(unsigned char const *)&endian != 0)
			dso->needs_swap = DSO_SWAP__YES;
		break;

	default:
		pr_err("unrecognized DSO data encoding %d\n", eidata);
		return -EINVAL;
	}

	return 0;
}

bool symsrc__possibly_runtime(struct symsrc *ss)
{
	return ss->dynsym || ss->opdsec;
}

bool symsrc__has_symtab(struct symsrc *ss)
{
	return ss->symtab != NULL;
}

void symsrc__destroy(struct symsrc *ss)
{
	free(ss->name);
	elf_end(ss->elf);
	close(ss->fd);
}

int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name,
		 enum dso_binary_type type)
{
	int err = -1;
	GElf_Ehdr ehdr;
	Elf *elf;
	int fd;

	fd = open(name, O_RDONLY);
	if (fd < 0)
		return -1;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		pr_debug("%s: cannot read %s ELF file.\n", __func__, name);
		goto out_close;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		pr_debug("%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	if (dso__swap_init(dso, ehdr.e_ident[EI_DATA]))
		goto out_elf_end;

	/* Always reject images with a mismatched build-id: */
	if (dso->has_build_id) {
		u8 build_id[BUILD_ID_SIZE];

		if (elf_read_build_id(elf, build_id, BUILD_ID_SIZE) < 0)
			goto out_elf_end;

		if (!dso__build_id_equal(dso, build_id))
			goto out_elf_end;
	}

	ss->symtab = elf_section_by_name(elf, &ehdr, &ss->symshdr, ".symtab",
			NULL);
	if (ss->symshdr.sh_type != SHT_SYMTAB)
		ss->symtab = NULL;

	ss->dynsym_idx = 0;
	ss->dynsym = elf_section_by_name(elf, &ehdr, &ss->dynshdr, ".dynsym",
			&ss->dynsym_idx);
	if (ss->dynshdr.sh_type != SHT_DYNSYM)
		ss->dynsym = NULL;

	ss->opdidx = 0;
	ss->opdsec = elf_section_by_name(elf, &ehdr, &ss->opdshdr, ".opd",
			&ss->opdidx);
	if (ss->opdshdr.sh_type != SHT_PROGBITS)
		ss->opdsec = NULL;

	if (dso->kernel == DSO_TYPE_USER) {
		GElf_Shdr shdr;
		ss->adjust_symbols = (ehdr.e_type == ET_EXEC ||
				elf_section_by_name(elf, &ehdr, &shdr,
						     ".gnu.prelink_undo",
						     NULL) != NULL);
	} else {
		ss->adjust_symbols = 0;
	}

	ss->name   = strdup(name);
	if (!ss->name)
		goto out_elf_end;

	ss->elf    = elf;
	ss->fd     = fd;
	ss->ehdr   = ehdr;
	ss->type   = type;

	return 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
	return err;
}

int dso__load_sym(struct dso *dso, struct map *map,
		  struct symsrc *syms_ss, struct symsrc *runtime_ss,
		  symbol_filter_t filter, int kmodule)
{
	struct kmap *kmap = dso->kernel ? map__kmap(map) : NULL;
	struct map *curr_map = map;
	struct dso *curr_dso = dso;
	Elf_Data *symstrs, *secstrs;
	uint32_t nr_syms;
	int err = -1;
	uint32_t idx;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *syms, *opddata = NULL;
	GElf_Sym sym;
	Elf_Scn *sec, *sec_strndx;
	Elf *elf;
	int nr = 0;

	dso->symtab_type = syms_ss->type;

	if (!syms_ss->symtab) {
		syms_ss->symtab  = syms_ss->dynsym;
		syms_ss->symshdr = syms_ss->dynshdr;
	}

	elf = syms_ss->elf;
	ehdr = syms_ss->ehdr;
	sec = syms_ss->symtab;
	shdr = syms_ss->symshdr;

	if (runtime_ss->opdsec)
		opddata = elf_rawdata(runtime_ss->opdsec, NULL);

	syms = elf_getdata(sec, NULL);
	if (syms == NULL)
		goto out_elf_end;

	sec = elf_getscn(elf, shdr.sh_link);
	if (sec == NULL)
		goto out_elf_end;

	symstrs = elf_getdata(sec, NULL);
	if (symstrs == NULL)
		goto out_elf_end;

	sec_strndx = elf_getscn(elf, ehdr.e_shstrndx);
	if (sec_strndx == NULL)
		goto out_elf_end;

	secstrs = elf_getdata(sec_strndx, NULL);
	if (secstrs == NULL)
		goto out_elf_end;

	nr_syms = shdr.sh_size / shdr.sh_entsize;

	memset(&sym, 0, sizeof(sym));
	dso->adjust_symbols = runtime_ss->adjust_symbols;
	elf_symtab__for_each_symbol(syms, nr_syms, idx, sym) {
		struct symbol *f;
		const char *elf_name = elf_sym__name(&sym, symstrs);
		char *demangled = NULL;
		int is_label = elf_sym__is_label(&sym);
		const char *section_name;
		bool used_opd = false;

		if (kmap && kmap->ref_reloc_sym && kmap->ref_reloc_sym->name &&
		    strcmp(elf_name, kmap->ref_reloc_sym->name) == 0)
			kmap->ref_reloc_sym->unrelocated_addr = sym.st_value;

		if (!is_label && !elf_sym__is_a(&sym, map->type))
			continue;

		/* Reject ARM ELF "mapping symbols": these aren't unique and
		 * don't identify functions, so will confuse the profile
		 * output: */
		if (ehdr.e_machine == EM_ARM) {
			if (!strcmp(elf_name, "$a") ||
			    !strcmp(elf_name, "$d") ||
			    !strcmp(elf_name, "$t"))
				continue;
		}

		if (runtime_ss->opdsec && sym.st_shndx == runtime_ss->opdidx) {
			u32 offset = sym.st_value - syms_ss->opdshdr.sh_addr;
			u64 *opd = opddata->d_buf + offset;
			sym.st_value = DSO__SWAP(dso, u64, *opd);
			sym.st_shndx = elf_addr_to_index(runtime_ss->elf,
					sym.st_value);
			used_opd = true;
		}

		sec = elf_getscn(runtime_ss->elf, sym.st_shndx);
		if (!sec)
			goto out_elf_end;

		gelf_getshdr(sec, &shdr);

		if (is_label && !elf_sec__is_a(&shdr, secstrs, map->type))
			continue;

		section_name = elf_sec__name(&shdr, secstrs);

		/* On ARM, symbols for thumb functions have 1 added to
		 * the symbol address as a flag - remove it */
		if ((ehdr.e_machine == EM_ARM) &&
		    (map->type == MAP__FUNCTION) &&
		    (sym.st_value & 1))
			--sym.st_value;

		if (dso->kernel != DSO_TYPE_USER || kmodule) {
			char dso_name[PATH_MAX];

			if (strcmp(section_name,
				   (curr_dso->short_name +
				    dso->short_name_len)) == 0)
				goto new_symbol;

			if (strcmp(section_name, ".text") == 0) {
				curr_map = map;
				curr_dso = dso;
				goto new_symbol;
			}

			snprintf(dso_name, sizeof(dso_name),
				 "%s%s", dso->short_name, section_name);

			curr_map = map_groups__find_by_name(kmap->kmaps, map->type, dso_name);
			if (curr_map == NULL) {
				u64 start = sym.st_value;

				if (kmodule)
					start += map->start + shdr.sh_offset;

				curr_dso = dso__new(dso_name);
				if (curr_dso == NULL)
					goto out_elf_end;
				curr_dso->kernel = dso->kernel;
				curr_dso->long_name = dso->long_name;
				curr_dso->long_name_len = dso->long_name_len;
				curr_map = map__new2(start, curr_dso,
						     map->type);
				if (curr_map == NULL) {
					dso__delete(curr_dso);
					goto out_elf_end;
				}
				curr_map->map_ip = identity__map_ip;
				curr_map->unmap_ip = identity__map_ip;
				curr_dso->symtab_type = dso->symtab_type;
				map_groups__insert(kmap->kmaps, curr_map);
				dsos__add(&dso->node, curr_dso);
				dso__set_loaded(curr_dso, map->type);
			} else
				curr_dso = curr_map->dso;

			goto new_symbol;
		}

		if ((used_opd && runtime_ss->adjust_symbols)
				|| (!used_opd && syms_ss->adjust_symbols)) {
			pr_debug4("%s: adjusting symbol: st_value: %#" PRIx64 " "
				  "sh_addr: %#" PRIx64 " sh_offset: %#" PRIx64 "\n", __func__,
				  (u64)sym.st_value, (u64)shdr.sh_addr,
				  (u64)shdr.sh_offset);
			sym.st_value -= shdr.sh_addr - shdr.sh_offset;
		}
		/*
		 * We need to figure out if the object was created from C++ sources
		 * DWARF DW_compile_unit has this, but we don't always have access
		 * to it...
		 */
		demangled = bfd_demangle(NULL, elf_name, DMGL_PARAMS | DMGL_ANSI);
		if (demangled != NULL)
			elf_name = demangled;
new_symbol:
		f = symbol__new(sym.st_value, sym.st_size,
				GELF_ST_BIND(sym.st_info), elf_name);
		free(demangled);
		if (!f)
			goto out_elf_end;

		if (filter && filter(curr_map, f))
			symbol__delete(f);
		else {
			symbols__insert(&curr_dso->symbols[curr_map->type], f);
			nr++;
		}
	}

	/*
	 * For misannotated, zeroed, ASM function sizes.
	 */
	if (nr > 0) {
		symbols__fixup_duplicate(&dso->symbols[map->type]);
		symbols__fixup_end(&dso->symbols[map->type]);
		if (kmap) {
			/*
			 * We need to fixup this here too because we create new
			 * maps here, for things like vsyscall sections.
			 */
			__map_groups__fixup_end(kmap->kmaps, map->type);
		}
	}
	err = nr;
out_elf_end:
	return err;
}

void symbol__elf_init(void)
{
	elf_version(EV_CURRENT);
}
