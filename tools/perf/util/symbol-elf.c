// SPDX-License-Identifier: GPL-2.0
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "dso.h"
#include "map.h"
#include "maps.h"
#include "symbol.h"
#include "symsrc.h"
#include "demangle-cxx.h"
#include "demangle-ocaml.h"
#include "demangle-java.h"
#include "demangle-rust.h"
#include "machine.h"
#include "vdso.h"
#include "debug.h"
#include "util/copyfile.h"
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <symbol/kallsyms.h>
#include <internal/lib.h>

#ifdef HAVE_LIBBFD_SUPPORT
#define PACKAGE 'perf'
#include <bfd.h>
#endif

#if defined(HAVE_LIBBFD_SUPPORT) || defined(HAVE_CPLUS_DEMANGLE_SUPPORT)
#ifndef DMGL_PARAMS
#define DMGL_PARAMS     (1 << 0)  /* Include function args */
#define DMGL_ANSI       (1 << 1)  /* Include const, volatile, etc */
#endif
#endif

#ifndef EM_AARCH64
#define EM_AARCH64	183  /* ARM 64 bit */
#endif

#ifndef EM_LOONGARCH
#define EM_LOONGARCH	258
#endif

#ifndef ELF32_ST_VISIBILITY
#define ELF32_ST_VISIBILITY(o)	((o) & 0x03)
#endif

/* For ELF64 the definitions are the same.  */
#ifndef ELF64_ST_VISIBILITY
#define ELF64_ST_VISIBILITY(o)	ELF32_ST_VISIBILITY (o)
#endif

/* How to extract information held in the st_other field.  */
#ifndef GELF_ST_VISIBILITY
#define GELF_ST_VISIBILITY(val)	ELF64_ST_VISIBILITY (val)
#endif

typedef Elf64_Nhdr GElf_Nhdr;


#ifndef HAVE_ELF_GETPHDRNUM_SUPPORT
static int elf_getphdrnum(Elf *elf, size_t *dst)
{
	GElf_Ehdr gehdr;
	GElf_Ehdr *ehdr;

	ehdr = gelf_getehdr(elf, &gehdr);
	if (!ehdr)
		return -1;

	*dst = ehdr->e_phnum;

	return 0;
}
#endif

#ifndef HAVE_ELF_GETSHDRSTRNDX_SUPPORT
static int elf_getshdrstrndx(Elf *elf __maybe_unused, size_t *dst __maybe_unused)
{
	pr_err("%s: update your libelf to > 0.140, this one lacks elf_getshdrstrndx().\n", __func__);
	return -1;
}
#endif

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

static inline uint8_t elf_sym__visibility(const GElf_Sym *sym)
{
	return GELF_ST_VISIBILITY(sym->st_other);
}

#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif

static inline int elf_sym__is_function(const GElf_Sym *sym)
{
	return (elf_sym__type(sym) == STT_FUNC ||
		elf_sym__type(sym) == STT_GNU_IFUNC) &&
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
		sym->st_shndx != SHN_ABS &&
		elf_sym__visibility(sym) != STV_HIDDEN &&
		elf_sym__visibility(sym) != STV_INTERNAL;
}

static bool elf_sym__filter(GElf_Sym *sym)
{
	return elf_sym__is_function(sym) || elf_sym__is_object(sym);
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

static bool elf_sec__filter(GElf_Shdr *shdr, Elf_Data *secstrs)
{
	return elf_sec__is_text(shdr, secstrs) || 
	       elf_sec__is_data(shdr, secstrs);
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

Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
			     GElf_Shdr *shp, const char *name, size_t *idx)
{
	Elf_Scn *sec = NULL;
	size_t cnt = 1;

	/* ELF is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL))
		return NULL;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, shp);
		str = elf_strptr(elf, ep->e_shstrndx, shp->sh_name);
		if (str && !strcmp(name, str)) {
			if (idx)
				*idx = cnt;
			return sec;
		}
		++cnt;
	}

	return NULL;
}

bool filename__has_section(const char *filename, const char *sec)
{
	int fd;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	bool found = false;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return false;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		goto out;

	if (gelf_getehdr(elf, &ehdr) == NULL)
		goto elf_out;

	found = !!elf_section_by_name(elf, &ehdr, &shdr, sec, NULL);

elf_out:
	elf_end(elf);
out:
	close(fd);
	return found;
}

static int elf_read_program_header(Elf *elf, u64 vaddr, GElf_Phdr *phdr)
{
	size_t i, phdrnum;
	u64 sz;

	if (elf_getphdrnum(elf, &phdrnum))
		return -1;

	for (i = 0; i < phdrnum; i++) {
		if (gelf_getphdr(elf, i, phdr) == NULL)
			return -1;

		if (phdr->p_type != PT_LOAD)
			continue;

		sz = max(phdr->p_memsz, phdr->p_filesz);
		if (!sz)
			continue;

		if (vaddr >= phdr->p_vaddr && (vaddr < phdr->p_vaddr + sz))
			return 0;
	}

	/* Not found any valid program header */
	return -1;
}

static bool want_demangle(bool is_kernel_sym)
{
	return is_kernel_sym ? symbol_conf.demangle_kernel : symbol_conf.demangle;
}

/*
 * Demangle C++ function signature, typically replaced by demangle-cxx.cpp
 * version.
 */
__weak char *cxx_demangle_sym(const char *str __maybe_unused, bool params __maybe_unused,
			      bool modifiers __maybe_unused)
{
#ifdef HAVE_LIBBFD_SUPPORT
	int flags = (params ? DMGL_PARAMS : 0) | (modifiers ? DMGL_ANSI : 0);

	return bfd_demangle(NULL, str, flags);
#elif defined(HAVE_CPLUS_DEMANGLE_SUPPORT)
	int flags = (params ? DMGL_PARAMS : 0) | (modifiers ? DMGL_ANSI : 0);

	return cplus_demangle(str, flags);
#else
	return NULL;
#endif
}

static char *demangle_sym(struct dso *dso, int kmodule, const char *elf_name)
{
	char *demangled = NULL;

	/*
	 * We need to figure out if the object was created from C++ sources
	 * DWARF DW_compile_unit has this, but we don't always have access
	 * to it...
	 */
	if (!want_demangle(dso->kernel || kmodule))
	    return demangled;

	demangled = cxx_demangle_sym(elf_name, verbose > 0, verbose > 0);
	if (demangled == NULL) {
		demangled = ocaml_demangle_sym(elf_name);
		if (demangled == NULL) {
			demangled = java_demangle_sym(elf_name, JAVA_DEMANGLE_NORET);
		}
	}
	else if (rust_is_mangled(demangled))
		/*
		    * Input to Rust demangling is the BFD-demangled
		    * name which it Rust-demangles in place.
		    */
		rust_demangle_sym(demangled);

	return demangled;
}

struct rel_info {
	u32		nr_entries;
	u32		*sorted;
	bool		is_rela;
	Elf_Data	*reldata;
	GElf_Rela	rela;
	GElf_Rel	rel;
};

static u32 get_rel_symidx(struct rel_info *ri, u32 idx)
{
	idx = ri->sorted ? ri->sorted[idx] : idx;
	if (ri->is_rela) {
		gelf_getrela(ri->reldata, idx, &ri->rela);
		return GELF_R_SYM(ri->rela.r_info);
	}
	gelf_getrel(ri->reldata, idx, &ri->rel);
	return GELF_R_SYM(ri->rel.r_info);
}

static u64 get_rel_offset(struct rel_info *ri, u32 x)
{
	if (ri->is_rela) {
		GElf_Rela rela;

		gelf_getrela(ri->reldata, x, &rela);
		return rela.r_offset;
	} else {
		GElf_Rel rel;

		gelf_getrel(ri->reldata, x, &rel);
		return rel.r_offset;
	}
}

static int rel_cmp(const void *a, const void *b, void *r)
{
	struct rel_info *ri = r;
	u64 a_offset = get_rel_offset(ri, *(const u32 *)a);
	u64 b_offset = get_rel_offset(ri, *(const u32 *)b);

	return a_offset < b_offset ? -1 : (a_offset > b_offset ? 1 : 0);
}

static int sort_rel(struct rel_info *ri)
{
	size_t sz = sizeof(ri->sorted[0]);
	u32 i;

	ri->sorted = calloc(ri->nr_entries, sz);
	if (!ri->sorted)
		return -1;
	for (i = 0; i < ri->nr_entries; i++)
		ri->sorted[i] = i;
	qsort_r(ri->sorted, ri->nr_entries, sz, rel_cmp, ri);
	return 0;
}

/*
 * For x86_64, the GNU linker is putting IFUNC information in the relocation
 * addend.
 */
static bool addend_may_be_ifunc(GElf_Ehdr *ehdr, struct rel_info *ri)
{
	return ehdr->e_machine == EM_X86_64 && ri->is_rela &&
	       GELF_R_TYPE(ri->rela.r_info) == R_X86_64_IRELATIVE;
}

static bool get_ifunc_name(Elf *elf, struct dso *dso, GElf_Ehdr *ehdr,
			   struct rel_info *ri, char *buf, size_t buf_sz)
{
	u64 addr = ri->rela.r_addend;
	struct symbol *sym;
	GElf_Phdr phdr;

	if (!addend_may_be_ifunc(ehdr, ri))
		return false;

	if (elf_read_program_header(elf, addr, &phdr))
		return false;

	addr -= phdr.p_vaddr - phdr.p_offset;

	sym = dso__find_symbol_nocache(dso, addr);

	/* Expecting the address to be an IFUNC or IFUNC alias */
	if (!sym || sym->start != addr || (sym->type != STT_GNU_IFUNC && !sym->ifunc_alias))
		return false;

	snprintf(buf, buf_sz, "%s@plt", sym->name);

	return true;
}

static void exit_rel(struct rel_info *ri)
{
	zfree(&ri->sorted);
}

static bool get_plt_sizes(struct dso *dso, GElf_Ehdr *ehdr, GElf_Shdr *shdr_plt,
			  u64 *plt_header_size, u64 *plt_entry_size)
{
	switch (ehdr->e_machine) {
	case EM_ARM:
		*plt_header_size = 20;
		*plt_entry_size = 12;
		return true;
	case EM_AARCH64:
		*plt_header_size = 32;
		*plt_entry_size = 16;
		return true;
	case EM_LOONGARCH:
		*plt_header_size = 32;
		*plt_entry_size = 16;
		return true;
	case EM_SPARC:
		*plt_header_size = 48;
		*plt_entry_size = 12;
		return true;
	case EM_SPARCV9:
		*plt_header_size = 128;
		*plt_entry_size = 32;
		return true;
	case EM_386:
	case EM_X86_64:
		*plt_entry_size = shdr_plt->sh_entsize;
		/* Size is 8 or 16, if not, assume alignment indicates size */
		if (*plt_entry_size != 8 && *plt_entry_size != 16)
			*plt_entry_size = shdr_plt->sh_addralign == 8 ? 8 : 16;
		*plt_header_size = *plt_entry_size;
		break;
	default: /* FIXME: s390/alpha/mips/parisc/poperpc/sh/xtensa need to be checked */
		*plt_header_size = shdr_plt->sh_entsize;
		*plt_entry_size = shdr_plt->sh_entsize;
		break;
	}
	if (*plt_entry_size)
		return true;
	pr_debug("Missing PLT entry size for %s\n", dso->long_name);
	return false;
}

static bool machine_is_x86(GElf_Half e_machine)
{
	return e_machine == EM_386 || e_machine == EM_X86_64;
}

struct rela_dyn {
	GElf_Addr	offset;
	u32		sym_idx;
};

struct rela_dyn_info {
	struct dso	*dso;
	Elf_Data	*plt_got_data;
	u32		nr_entries;
	struct rela_dyn	*sorted;
	Elf_Data	*dynsym_data;
	Elf_Data	*dynstr_data;
	Elf_Data	*rela_dyn_data;
};

static void exit_rela_dyn(struct rela_dyn_info *di)
{
	zfree(&di->sorted);
}

static int cmp_offset(const void *a, const void *b)
{
	const struct rela_dyn *va = a;
	const struct rela_dyn *vb = b;

	return va->offset < vb->offset ? -1 : (va->offset > vb->offset ? 1 : 0);
}

static int sort_rela_dyn(struct rela_dyn_info *di)
{
	u32 i, n;

	di->sorted = calloc(di->nr_entries, sizeof(di->sorted[0]));
	if (!di->sorted)
		return -1;

	/* Get data for sorting: the offset and symbol index */
	for (i = 0, n = 0; i < di->nr_entries; i++) {
		GElf_Rela rela;
		u32 sym_idx;

		gelf_getrela(di->rela_dyn_data, i, &rela);
		sym_idx = GELF_R_SYM(rela.r_info);
		if (sym_idx) {
			di->sorted[n].sym_idx = sym_idx;
			di->sorted[n].offset = rela.r_offset;
			n += 1;
		}
	}

	/* Sort by offset */
	di->nr_entries = n;
	qsort(di->sorted, n, sizeof(di->sorted[0]), cmp_offset);

	return 0;
}

static void get_rela_dyn_info(Elf *elf, GElf_Ehdr *ehdr, struct rela_dyn_info *di, Elf_Scn *scn)
{
	GElf_Shdr rela_dyn_shdr;
	GElf_Shdr shdr;

	di->plt_got_data = elf_getdata(scn, NULL);

	scn = elf_section_by_name(elf, ehdr, &rela_dyn_shdr, ".rela.dyn", NULL);
	if (!scn || !rela_dyn_shdr.sh_link || !rela_dyn_shdr.sh_entsize)
		return;

	di->nr_entries = rela_dyn_shdr.sh_size / rela_dyn_shdr.sh_entsize;
	di->rela_dyn_data = elf_getdata(scn, NULL);

	scn = elf_getscn(elf, rela_dyn_shdr.sh_link);
	if (!scn || !gelf_getshdr(scn, &shdr) || !shdr.sh_link)
		return;

	di->dynsym_data = elf_getdata(scn, NULL);
	di->dynstr_data = elf_getdata(elf_getscn(elf, shdr.sh_link), NULL);

	if (!di->plt_got_data || !di->dynstr_data || !di->dynsym_data || !di->rela_dyn_data)
		return;

	/* Sort into offset order */
	sort_rela_dyn(di);
}

/* Get instruction displacement from a plt entry for x86_64 */
static u32 get_x86_64_plt_disp(const u8 *p)
{
	u8 endbr64[] = {0xf3, 0x0f, 0x1e, 0xfa};
	int n = 0;

	/* Skip endbr64 */
	if (!memcmp(p, endbr64, sizeof(endbr64)))
		n += sizeof(endbr64);
	/* Skip bnd prefix */
	if (p[n] == 0xf2)
		n += 1;
	/* jmp with 4-byte displacement */
	if (p[n] == 0xff && p[n + 1] == 0x25) {
		u32 disp;

		n += 2;
		/* Also add offset from start of entry to end of instruction */
		memcpy(&disp, p + n, sizeof(disp));
		return n + 4 + le32toh(disp);
	}
	return 0;
}

static bool get_plt_got_name(GElf_Shdr *shdr, size_t i,
			     struct rela_dyn_info *di,
			     char *buf, size_t buf_sz)
{
	struct rela_dyn vi, *vr;
	const char *sym_name;
	char *demangled;
	GElf_Sym sym;
	bool result;
	u32 disp;

	if (!di->sorted)
		return false;

	disp = get_x86_64_plt_disp(di->plt_got_data->d_buf + i);
	if (!disp)
		return false;

	/* Compute target offset of the .plt.got entry */
	vi.offset = shdr->sh_offset + di->plt_got_data->d_off + i + disp;

	/* Find that offset in .rela.dyn (sorted by offset) */
	vr = bsearch(&vi, di->sorted, di->nr_entries, sizeof(di->sorted[0]), cmp_offset);
	if (!vr)
		return false;

	/* Get the associated symbol */
	gelf_getsym(di->dynsym_data, vr->sym_idx, &sym);
	sym_name = elf_sym__name(&sym, di->dynstr_data);
	demangled = demangle_sym(di->dso, 0, sym_name);
	if (demangled != NULL)
		sym_name = demangled;

	snprintf(buf, buf_sz, "%s@plt", sym_name);

	result = *sym_name;

	free(demangled);

	return result;
}

static int dso__synthesize_plt_got_symbols(struct dso *dso, Elf *elf,
					   GElf_Ehdr *ehdr,
					   char *buf, size_t buf_sz)
{
	struct rela_dyn_info di = { .dso = dso };
	struct symbol *sym;
	GElf_Shdr shdr;
	Elf_Scn *scn;
	int err = -1;
	size_t i;

	scn = elf_section_by_name(elf, ehdr, &shdr, ".plt.got", NULL);
	if (!scn || !shdr.sh_entsize)
		return 0;

	if (ehdr->e_machine == EM_X86_64)
		get_rela_dyn_info(elf, ehdr, &di, scn);

	for (i = 0; i < shdr.sh_size; i += shdr.sh_entsize) {
		if (!get_plt_got_name(&shdr, i, &di, buf, buf_sz))
			snprintf(buf, buf_sz, "offset_%#" PRIx64 "@plt", (u64)shdr.sh_offset + i);
		sym = symbol__new(shdr.sh_offset + i, shdr.sh_entsize, STB_GLOBAL, STT_FUNC, buf);
		if (!sym)
			goto out;
		symbols__insert(&dso->symbols, sym);
	}
	err = 0;
out:
	exit_rela_dyn(&di);
	return err;
}

/*
 * We need to check if we have a .dynsym, so that we can handle the
 * .plt, synthesizing its symbols, that aren't on the symtabs (be it
 * .dynsym or .symtab).
 * And always look at the original dso, not at debuginfo packages, that
 * have the PLT data stripped out (shdr_rel_plt.sh_type == SHT_NOBITS).
 */
int dso__synthesize_plt_symbols(struct dso *dso, struct symsrc *ss)
{
	uint32_t idx;
	GElf_Sym sym;
	u64 plt_offset, plt_header_size, plt_entry_size;
	GElf_Shdr shdr_plt, plt_sec_shdr;
	struct symbol *f, *plt_sym;
	GElf_Shdr shdr_rel_plt, shdr_dynsym;
	Elf_Data *syms, *symstrs;
	Elf_Scn *scn_plt_rel, *scn_symstrs, *scn_dynsym;
	GElf_Ehdr ehdr;
	char sympltname[1024];
	Elf *elf;
	int nr = 0, err = -1;
	struct rel_info ri = { .is_rela = false };
	bool lazy_plt;

	elf = ss->elf;
	ehdr = ss->ehdr;

	if (!elf_section_by_name(elf, &ehdr, &shdr_plt, ".plt", NULL))
		return 0;

	/*
	 * A symbol from a previous section (e.g. .init) can have been expanded
	 * by symbols__fixup_end() to overlap .plt. Truncate it before adding
	 * a symbol for .plt header.
	 */
	f = dso__find_symbol_nocache(dso, shdr_plt.sh_offset);
	if (f && f->start < shdr_plt.sh_offset && f->end > shdr_plt.sh_offset)
		f->end = shdr_plt.sh_offset;

	if (!get_plt_sizes(dso, &ehdr, &shdr_plt, &plt_header_size, &plt_entry_size))
		return 0;

	/* Add a symbol for .plt header */
	plt_sym = symbol__new(shdr_plt.sh_offset, plt_header_size, STB_GLOBAL, STT_FUNC, ".plt");
	if (!plt_sym)
		goto out_elf_end;
	symbols__insert(&dso->symbols, plt_sym);

	/* Only x86 has .plt.got */
	if (machine_is_x86(ehdr.e_machine) &&
	    dso__synthesize_plt_got_symbols(dso, elf, &ehdr, sympltname, sizeof(sympltname)))
		goto out_elf_end;

	/* Only x86 has .plt.sec */
	if (machine_is_x86(ehdr.e_machine) &&
	    elf_section_by_name(elf, &ehdr, &plt_sec_shdr, ".plt.sec", NULL)) {
		if (!get_plt_sizes(dso, &ehdr, &plt_sec_shdr, &plt_header_size, &plt_entry_size))
			return 0;
		/* Extend .plt symbol to entire .plt */
		plt_sym->end = plt_sym->start + shdr_plt.sh_size;
		/* Use .plt.sec offset */
		plt_offset = plt_sec_shdr.sh_offset;
		lazy_plt = false;
	} else {
		plt_offset = shdr_plt.sh_offset;
		lazy_plt = true;
	}

	scn_plt_rel = elf_section_by_name(elf, &ehdr, &shdr_rel_plt,
					  ".rela.plt", NULL);
	if (scn_plt_rel == NULL) {
		scn_plt_rel = elf_section_by_name(elf, &ehdr, &shdr_rel_plt,
						  ".rel.plt", NULL);
		if (scn_plt_rel == NULL)
			return 0;
	}

	if (shdr_rel_plt.sh_type != SHT_RELA &&
	    shdr_rel_plt.sh_type != SHT_REL)
		return 0;

	if (!shdr_rel_plt.sh_link)
		return 0;

	if (shdr_rel_plt.sh_link == ss->dynsym_idx) {
		scn_dynsym = ss->dynsym;
		shdr_dynsym = ss->dynshdr;
	} else if (shdr_rel_plt.sh_link == ss->symtab_idx) {
		/*
		 * A static executable can have a .plt due to IFUNCs, in which
		 * case .symtab is used not .dynsym.
		 */
		scn_dynsym = ss->symtab;
		shdr_dynsym = ss->symshdr;
	} else {
		goto out_elf_end;
	}

	if (!scn_dynsym)
		return 0;

	/*
	 * Fetch the relocation section to find the idxes to the GOT
	 * and the symbols in the .dynsym they refer to.
	 */
	ri.reldata = elf_getdata(scn_plt_rel, NULL);
	if (!ri.reldata)
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

	ri.nr_entries = shdr_rel_plt.sh_size / shdr_rel_plt.sh_entsize;

	ri.is_rela = shdr_rel_plt.sh_type == SHT_RELA;

	if (lazy_plt) {
		/*
		 * Assume a .plt with the same number of entries as the number
		 * of relocation entries is not lazy and does not have a header.
		 */
		if (ri.nr_entries * plt_entry_size == shdr_plt.sh_size)
			dso__delete_symbol(dso, plt_sym);
		else
			plt_offset += plt_header_size;
	}

	/*
	 * x86 doesn't insert IFUNC relocations in .plt order, so sort to get
	 * back in order.
	 */
	if (machine_is_x86(ehdr.e_machine) && sort_rel(&ri))
		goto out_elf_end;

	for (idx = 0; idx < ri.nr_entries; idx++) {
		const char *elf_name = NULL;
		char *demangled = NULL;

		gelf_getsym(syms, get_rel_symidx(&ri, idx), &sym);

		elf_name = elf_sym__name(&sym, symstrs);
		demangled = demangle_sym(dso, 0, elf_name);
		if (demangled)
			elf_name = demangled;
		if (*elf_name)
			snprintf(sympltname, sizeof(sympltname), "%s@plt", elf_name);
		else if (!get_ifunc_name(elf, dso, &ehdr, &ri, sympltname, sizeof(sympltname)))
			snprintf(sympltname, sizeof(sympltname),
				 "offset_%#" PRIx64 "@plt", plt_offset);
		free(demangled);

		f = symbol__new(plt_offset, plt_entry_size, STB_GLOBAL, STT_FUNC, sympltname);
		if (!f)
			goto out_elf_end;

		plt_offset += plt_entry_size;
		symbols__insert(&dso->symbols, f);
		++nr;
	}

	err = 0;
out_elf_end:
	exit_rel(&ri);
	if (err == 0)
		return nr;
	pr_debug("%s: problems reading %s PLT info.\n",
		 __func__, dso->long_name);
	return 0;
}

char *dso__demangle_sym(struct dso *dso, int kmodule, const char *elf_name)
{
	return demangle_sym(dso, kmodule, elf_name);
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
				err = sz;
				break;
			}
		}
		ptr += descsz;
	}

out:
	return err;
}

#ifdef HAVE_LIBBFD_BUILDID_SUPPORT

static int read_build_id(const char *filename, struct build_id *bid)
{
	size_t size = sizeof(bid->data);
	int err = -1;
	bfd *abfd;

	abfd = bfd_openr(filename, NULL);
	if (!abfd)
		return -1;

	if (!bfd_check_format(abfd, bfd_object)) {
		pr_debug2("%s: cannot read %s bfd file.\n", __func__, filename);
		goto out_close;
	}

	if (!abfd->build_id || abfd->build_id->size > size)
		goto out_close;

	memcpy(bid->data, abfd->build_id->data, abfd->build_id->size);
	memset(bid->data + abfd->build_id->size, 0, size - abfd->build_id->size);
	err = bid->size = abfd->build_id->size;

out_close:
	bfd_close(abfd);
	return err;
}

#else // HAVE_LIBBFD_BUILDID_SUPPORT

static int read_build_id(const char *filename, struct build_id *bid)
{
	size_t size = sizeof(bid->data);
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

	err = elf_read_build_id(elf, bid->data, size);
	if (err > 0)
		bid->size = err;

	elf_end(elf);
out_close:
	close(fd);
out:
	return err;
}

#endif // HAVE_LIBBFD_BUILDID_SUPPORT

int filename__read_build_id(const char *filename, struct build_id *bid)
{
	struct kmod_path m = { .name = NULL, };
	char path[PATH_MAX];
	int err;

	if (!filename)
		return -EFAULT;

	err = kmod_path__parse(&m, filename);
	if (err)
		return -1;

	if (m.comp) {
		int error = 0, fd;

		fd = filename__decompress(filename, path, sizeof(path), m.comp, &error);
		if (fd < 0) {
			pr_debug("Failed to decompress (error %d) %s\n",
				 error, filename);
			return -1;
		}
		close(fd);
		filename = path;
	}

	err = read_build_id(filename, bid);

	if (m.comp)
		unlink(filename);
	return err;
}

int sysfs__read_build_id(const char *filename, struct build_id *bid)
{
	size_t size = sizeof(bid->data);
	int fd, err = -1;

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
				if (read(fd, bid->data, sz) == (ssize_t)sz) {
					memset(bid->data + sz, 0, size - sz);
					bid->size = sz;
					err = 0;
					break;
				}
			} else if (read(fd, bf, descsz) != (ssize_t)descsz)
				break;
		} else {
			int n = namesz + descsz;

			if (n > (int)sizeof(bf)) {
				n = sizeof(bf);
				pr_debug("%s: truncating reading of build id in sysfs file %s: n_namesz=%u, n_descsz=%u.\n",
					 __func__, filename, nhdr.n_namesz, nhdr.n_descsz);
			}
			if (read(fd, bf, n) != n)
				break;
		}
	}
	close(fd);
out:
	return err;
}

#ifdef HAVE_LIBBFD_SUPPORT

int filename__read_debuglink(const char *filename, char *debuglink,
			     size_t size)
{
	int err = -1;
	asection *section;
	bfd *abfd;

	abfd = bfd_openr(filename, NULL);
	if (!abfd)
		return -1;

	if (!bfd_check_format(abfd, bfd_object)) {
		pr_debug2("%s: cannot read %s bfd file.\n", __func__, filename);
		goto out_close;
	}

	section = bfd_get_section_by_name(abfd, ".gnu_debuglink");
	if (!section)
		goto out_close;

	if (section->size > size)
		goto out_close;

	if (!bfd_get_section_contents(abfd, section, debuglink, 0,
				      section->size))
		goto out_close;

	err = 0;

out_close:
	bfd_close(abfd);
	return err;
}

#else

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
		goto out_elf_end;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		pr_err("%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	sec = elf_section_by_name(elf, &ehdr, &shdr,
				  ".gnu_debuglink", NULL);
	if (sec == NULL)
		goto out_elf_end;

	data = elf_getdata(sec, NULL);
	if (data == NULL)
		goto out_elf_end;

	/* the start of this section is a zero-terminated string */
	strncpy(debuglink, data->d_buf, size);

	err = 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
out:
	return err;
}

#endif

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
	zfree(&ss->name);
	elf_end(ss->elf);
	close(ss->fd);
}

bool elf__needs_adjust_symbols(GElf_Ehdr ehdr)
{
	/*
	 * Usually vmlinux is an ELF file with type ET_EXEC for most
	 * architectures; except Arm64 kernel is linked with option
	 * '-share', so need to check type ET_DYN.
	 */
	return ehdr.e_type == ET_EXEC || ehdr.e_type == ET_REL ||
	       ehdr.e_type == ET_DYN;
}

int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name,
		 enum dso_binary_type type)
{
	GElf_Ehdr ehdr;
	Elf *elf;
	int fd;

	if (dso__needs_decompress(dso)) {
		fd = dso__decompress_kmodule_fd(dso, name);
		if (fd < 0)
			return -1;

		type = dso->symtab_type;
	} else {
		fd = open(name, O_RDONLY);
		if (fd < 0) {
			dso->load_errno = errno;
			return -1;
		}
	}

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		pr_debug("%s: cannot read %s ELF file.\n", __func__, name);
		dso->load_errno = DSO_LOAD_ERRNO__INVALID_ELF;
		goto out_close;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		dso->load_errno = DSO_LOAD_ERRNO__INVALID_ELF;
		pr_debug("%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	if (dso__swap_init(dso, ehdr.e_ident[EI_DATA])) {
		dso->load_errno = DSO_LOAD_ERRNO__INTERNAL_ERROR;
		goto out_elf_end;
	}

	/* Always reject images with a mismatched build-id: */
	if (dso->has_build_id && !symbol_conf.ignore_vmlinux_buildid) {
		u8 build_id[BUILD_ID_SIZE];
		struct build_id bid;
		int size;

		size = elf_read_build_id(elf, build_id, BUILD_ID_SIZE);
		if (size <= 0) {
			dso->load_errno = DSO_LOAD_ERRNO__CANNOT_READ_BUILDID;
			goto out_elf_end;
		}

		build_id__init(&bid, build_id, size);
		if (!dso__build_id_equal(dso, &bid)) {
			pr_debug("%s: build id mismatch for %s.\n", __func__, name);
			dso->load_errno = DSO_LOAD_ERRNO__MISMATCHING_BUILDID;
			goto out_elf_end;
		}
	}

	ss->is_64_bit = (gelf_getclass(elf) == ELFCLASS64);

	ss->symtab_idx = 0;
	ss->symtab = elf_section_by_name(elf, &ehdr, &ss->symshdr, ".symtab",
			&ss->symtab_idx);
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

	if (dso->kernel == DSO_SPACE__USER)
		ss->adjust_symbols = true;
	else
		ss->adjust_symbols = elf__needs_adjust_symbols(ehdr);

	ss->name   = strdup(name);
	if (!ss->name) {
		dso->load_errno = errno;
		goto out_elf_end;
	}

	ss->elf    = elf;
	ss->fd     = fd;
	ss->ehdr   = ehdr;
	ss->type   = type;

	return 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
	return -1;
}

/**
 * ref_reloc_sym_not_found - has kernel relocation symbol been found.
 * @kmap: kernel maps and relocation reference symbol
 *
 * This function returns %true if we are dealing with the kernel maps and the
 * relocation reference symbol has not yet been found.  Otherwise %false is
 * returned.
 */
static bool ref_reloc_sym_not_found(struct kmap *kmap)
{
	return kmap && kmap->ref_reloc_sym && kmap->ref_reloc_sym->name &&
	       !kmap->ref_reloc_sym->unrelocated_addr;
}

/**
 * ref_reloc - kernel relocation offset.
 * @kmap: kernel maps and relocation reference symbol
 *
 * This function returns the offset of kernel addresses as determined by using
 * the relocation reference symbol i.e. if the kernel has not been relocated
 * then the return value is zero.
 */
static u64 ref_reloc(struct kmap *kmap)
{
	if (kmap && kmap->ref_reloc_sym &&
	    kmap->ref_reloc_sym->unrelocated_addr)
		return kmap->ref_reloc_sym->addr -
		       kmap->ref_reloc_sym->unrelocated_addr;
	return 0;
}

void __weak arch__sym_update(struct symbol *s __maybe_unused,
		GElf_Sym *sym __maybe_unused) { }

static int dso__process_kernel_symbol(struct dso *dso, struct map *map,
				      GElf_Sym *sym, GElf_Shdr *shdr,
				      struct maps *kmaps, struct kmap *kmap,
				      struct dso **curr_dsop, struct map **curr_mapp,
				      const char *section_name,
				      bool adjust_kernel_syms, bool kmodule, bool *remap_kernel)
{
	struct dso *curr_dso = *curr_dsop;
	struct map *curr_map;
	char dso_name[PATH_MAX];

	/* Adjust symbol to map to file offset */
	if (adjust_kernel_syms)
		sym->st_value -= shdr->sh_addr - shdr->sh_offset;

	if (strcmp(section_name, (curr_dso->short_name + dso->short_name_len)) == 0)
		return 0;

	if (strcmp(section_name, ".text") == 0) {
		/*
		 * The initial kernel mapping is based on
		 * kallsyms and identity maps.  Overwrite it to
		 * map to the kernel dso.
		 */
		if (*remap_kernel && dso->kernel && !kmodule) {
			*remap_kernel = false;
			map__set_start(map, shdr->sh_addr + ref_reloc(kmap));
			map__set_end(map, map__start(map) + shdr->sh_size);
			map__set_pgoff(map, shdr->sh_offset);
			map__set_mapping_type(map, MAPPING_TYPE__DSO);
			/* Ensure maps are correctly ordered */
			if (kmaps) {
				int err;
				struct map *tmp = map__get(map);

				maps__remove(kmaps, map);
				err = maps__insert(kmaps, map);
				map__put(tmp);
				if (err)
					return err;
			}
		}

		/*
		 * The initial module mapping is based on
		 * /proc/modules mapped to offset zero.
		 * Overwrite it to map to the module dso.
		 */
		if (*remap_kernel && kmodule) {
			*remap_kernel = false;
			map__set_pgoff(map, shdr->sh_offset);
		}

		*curr_mapp = map;
		*curr_dsop = dso;
		return 0;
	}

	if (!kmap)
		return 0;

	snprintf(dso_name, sizeof(dso_name), "%s%s", dso->short_name, section_name);

	curr_map = maps__find_by_name(kmaps, dso_name);
	if (curr_map == NULL) {
		u64 start = sym->st_value;

		if (kmodule)
			start += map__start(map) + shdr->sh_offset;

		curr_dso = dso__new(dso_name);
		if (curr_dso == NULL)
			return -1;
		curr_dso->kernel = dso->kernel;
		curr_dso->long_name = dso->long_name;
		curr_dso->long_name_len = dso->long_name_len;
		curr_dso->binary_type = dso->binary_type;
		curr_dso->adjust_symbols = dso->adjust_symbols;
		curr_map = map__new2(start, curr_dso);
		dso__put(curr_dso);
		if (curr_map == NULL)
			return -1;

		if (curr_dso->kernel)
			map__kmap(curr_map)->kmaps = kmaps;

		if (adjust_kernel_syms) {
			map__set_start(curr_map, shdr->sh_addr + ref_reloc(kmap));
			map__set_end(curr_map, map__start(curr_map) + shdr->sh_size);
			map__set_pgoff(curr_map, shdr->sh_offset);
		} else {
			map__set_mapping_type(curr_map, MAPPING_TYPE__IDENTITY);
		}
		curr_dso->symtab_type = dso->symtab_type;
		if (maps__insert(kmaps, curr_map))
			return -1;
		/*
		 * Add it before we drop the reference to curr_map, i.e. while
		 * we still are sure to have a reference to this DSO via
		 * *curr_map->dso.
		 */
		dsos__add(&maps__machine(kmaps)->dsos, curr_dso);
		/* kmaps already got it */
		map__put(curr_map);
		dso__set_loaded(curr_dso);
		*curr_mapp = curr_map;
		*curr_dsop = curr_dso;
	} else
		*curr_dsop = map__dso(curr_map);

	return 0;
}

static int
dso__load_sym_internal(struct dso *dso, struct map *map, struct symsrc *syms_ss,
		       struct symsrc *runtime_ss, int kmodule, int dynsym)
{
	struct kmap *kmap = dso->kernel ? map__kmap(map) : NULL;
	struct maps *kmaps = kmap ? map__kmaps(map) : NULL;
	struct map *curr_map = map;
	struct dso *curr_dso = dso;
	Elf_Data *symstrs, *secstrs, *secstrs_run, *secstrs_sym;
	uint32_t nr_syms;
	int err = -1;
	uint32_t idx;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	GElf_Shdr tshdr;
	Elf_Data *syms, *opddata = NULL;
	GElf_Sym sym;
	Elf_Scn *sec, *sec_strndx;
	Elf *elf;
	int nr = 0;
	bool remap_kernel = false, adjust_kernel_syms = false;

	if (kmap && !kmaps)
		return -1;

	elf = syms_ss->elf;
	ehdr = syms_ss->ehdr;
	if (dynsym) {
		sec  = syms_ss->dynsym;
		shdr = syms_ss->dynshdr;
	} else {
		sec =  syms_ss->symtab;
		shdr = syms_ss->symshdr;
	}

	if (elf_section_by_name(runtime_ss->elf, &runtime_ss->ehdr, &tshdr,
				".text", NULL)) {
		dso->text_offset = tshdr.sh_addr - tshdr.sh_offset;
		dso->text_end = tshdr.sh_offset + tshdr.sh_size;
	}

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

	sec_strndx = elf_getscn(runtime_ss->elf, runtime_ss->ehdr.e_shstrndx);
	if (sec_strndx == NULL)
		goto out_elf_end;

	secstrs_run = elf_getdata(sec_strndx, NULL);
	if (secstrs_run == NULL)
		goto out_elf_end;

	sec_strndx = elf_getscn(elf, ehdr.e_shstrndx);
	if (sec_strndx == NULL)
		goto out_elf_end;

	secstrs_sym = elf_getdata(sec_strndx, NULL);
	if (secstrs_sym == NULL)
		goto out_elf_end;

	nr_syms = shdr.sh_size / shdr.sh_entsize;

	memset(&sym, 0, sizeof(sym));

	/*
	 * The kernel relocation symbol is needed in advance in order to adjust
	 * kernel maps correctly.
	 */
	if (ref_reloc_sym_not_found(kmap)) {
		elf_symtab__for_each_symbol(syms, nr_syms, idx, sym) {
			const char *elf_name = elf_sym__name(&sym, symstrs);

			if (strcmp(elf_name, kmap->ref_reloc_sym->name))
				continue;
			kmap->ref_reloc_sym->unrelocated_addr = sym.st_value;
			map__set_reloc(map, kmap->ref_reloc_sym->addr - kmap->ref_reloc_sym->unrelocated_addr);
			break;
		}
	}

	/*
	 * Handle any relocation of vdso necessary because older kernels
	 * attempted to prelink vdso to its virtual address.
	 */
	if (dso__is_vdso(dso))
		map__set_reloc(map, map__start(map) - dso->text_offset);

	dso->adjust_symbols = runtime_ss->adjust_symbols || ref_reloc(kmap);
	/*
	 * Initial kernel and module mappings do not map to the dso.
	 * Flag the fixups.
	 */
	if (dso->kernel) {
		remap_kernel = true;
		adjust_kernel_syms = dso->adjust_symbols;
	}
	elf_symtab__for_each_symbol(syms, nr_syms, idx, sym) {
		struct symbol *f;
		const char *elf_name = elf_sym__name(&sym, symstrs);
		char *demangled = NULL;
		int is_label = elf_sym__is_label(&sym);
		const char *section_name;
		bool used_opd = false;

		if (!is_label && !elf_sym__filter(&sym))
			continue;

		/* Reject ARM ELF "mapping symbols": these aren't unique and
		 * don't identify functions, so will confuse the profile
		 * output: */
		if (ehdr.e_machine == EM_ARM || ehdr.e_machine == EM_AARCH64) {
			if (elf_name[0] == '$' && strchr("adtx", elf_name[1])
			    && (elf_name[2] == '\0' || elf_name[2] == '.'))
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

		/*
		 * When loading symbols in a data mapping, ABS symbols (which
		 * has a value of SHN_ABS in its st_shndx) failed at
		 * elf_getscn().  And it marks the loading as a failure so
		 * already loaded symbols cannot be fixed up.
		 *
		 * I'm not sure what should be done. Just ignore them for now.
		 * - Namhyung Kim
		 */
		if (sym.st_shndx == SHN_ABS)
			continue;

		sec = elf_getscn(syms_ss->elf, sym.st_shndx);
		if (!sec)
			goto out_elf_end;

		gelf_getshdr(sec, &shdr);

		/*
		 * If the attribute bit SHF_ALLOC is not set, the section
		 * doesn't occupy memory during process execution.
		 * E.g. ".gnu.warning.*" section is used by linker to generate
		 * warnings when calling deprecated functions, the symbols in
		 * the section aren't loaded to memory during process execution,
		 * so skip them.
		 */
		if (!(shdr.sh_flags & SHF_ALLOC))
			continue;

		secstrs = secstrs_sym;

		/*
		 * We have to fallback to runtime when syms' section header has
		 * NOBITS set. NOBITS results in file offset (sh_offset) not
		 * being incremented. So sh_offset used below has different
		 * values for syms (invalid) and runtime (valid).
		 */
		if (shdr.sh_type == SHT_NOBITS) {
			sec = elf_getscn(runtime_ss->elf, sym.st_shndx);
			if (!sec)
				goto out_elf_end;

			gelf_getshdr(sec, &shdr);
			secstrs = secstrs_run;
		}

		if (is_label && !elf_sec__filter(&shdr, secstrs))
			continue;

		section_name = elf_sec__name(&shdr, secstrs);

		/* On ARM, symbols for thumb functions have 1 added to
		 * the symbol address as a flag - remove it */
		if ((ehdr.e_machine == EM_ARM) &&
		    (GELF_ST_TYPE(sym.st_info) == STT_FUNC) &&
		    (sym.st_value & 1))
			--sym.st_value;

		if (dso->kernel) {
			if (dso__process_kernel_symbol(dso, map, &sym, &shdr, kmaps, kmap, &curr_dso, &curr_map,
						       section_name, adjust_kernel_syms, kmodule, &remap_kernel))
				goto out_elf_end;
		} else if ((used_opd && runtime_ss->adjust_symbols) ||
			   (!used_opd && syms_ss->adjust_symbols)) {
			GElf_Phdr phdr;

			if (elf_read_program_header(runtime_ss->elf,
						    (u64)sym.st_value, &phdr)) {
				pr_debug4("%s: failed to find program header for "
					   "symbol: %s st_value: %#" PRIx64 "\n",
					   __func__, elf_name, (u64)sym.st_value);
				pr_debug4("%s: adjusting symbol: st_value: %#" PRIx64 " "
					"sh_addr: %#" PRIx64 " sh_offset: %#" PRIx64 "\n",
					__func__, (u64)sym.st_value, (u64)shdr.sh_addr,
					(u64)shdr.sh_offset);
				/*
				 * Fail to find program header, let's rollback
				 * to use shdr.sh_addr and shdr.sh_offset to
				 * calibrate symbol's file address, though this
				 * is not necessary for normal C ELF file, we
				 * still need to handle java JIT symbols in this
				 * case.
				 */
				sym.st_value -= shdr.sh_addr - shdr.sh_offset;
			} else {
				pr_debug4("%s: adjusting symbol: st_value: %#" PRIx64 " "
					"p_vaddr: %#" PRIx64 " p_offset: %#" PRIx64 "\n",
					__func__, (u64)sym.st_value, (u64)phdr.p_vaddr,
					(u64)phdr.p_offset);
				sym.st_value -= phdr.p_vaddr - phdr.p_offset;
			}
		}

		demangled = demangle_sym(dso, kmodule, elf_name);
		if (demangled != NULL)
			elf_name = demangled;

		f = symbol__new(sym.st_value, sym.st_size,
				GELF_ST_BIND(sym.st_info),
				GELF_ST_TYPE(sym.st_info), elf_name);
		free(demangled);
		if (!f)
			goto out_elf_end;

		arch__sym_update(f, &sym);

		__symbols__insert(&curr_dso->symbols, f, dso->kernel);
		nr++;
	}

	/*
	 * For misannotated, zeroed, ASM function sizes.
	 */
	if (nr > 0) {
		symbols__fixup_end(&dso->symbols, false);
		symbols__fixup_duplicate(&dso->symbols);
		if (kmap) {
			/*
			 * We need to fixup this here too because we create new
			 * maps here, for things like vsyscall sections.
			 */
			maps__fixup_end(kmaps);
		}
	}
	err = nr;
out_elf_end:
	return err;
}

int dso__load_sym(struct dso *dso, struct map *map, struct symsrc *syms_ss,
		  struct symsrc *runtime_ss, int kmodule)
{
	int nr = 0;
	int err = -1;

	dso->symtab_type = syms_ss->type;
	dso->is_64_bit = syms_ss->is_64_bit;
	dso->rel = syms_ss->ehdr.e_type == ET_REL;

	/*
	 * Modules may already have symbols from kallsyms, but those symbols
	 * have the wrong values for the dso maps, so remove them.
	 */
	if (kmodule && syms_ss->symtab)
		symbols__delete(&dso->symbols);

	if (!syms_ss->symtab) {
		/*
		 * If the vmlinux is stripped, fail so we will fall back
		 * to using kallsyms. The vmlinux runtime symbols aren't
		 * of much use.
		 */
		if (dso->kernel)
			return err;
	} else  {
		err = dso__load_sym_internal(dso, map, syms_ss, runtime_ss,
					     kmodule, 0);
		if (err < 0)
			return err;
		nr = err;
	}

	if (syms_ss->dynsym) {
		err = dso__load_sym_internal(dso, map, syms_ss, runtime_ss,
					     kmodule, 1);
		if (err < 0)
			return err;
		err += nr;
	}

	return err;
}

static int elf_read_maps(Elf *elf, bool exe, mapfn_t mapfn, void *data)
{
	GElf_Phdr phdr;
	size_t i, phdrnum;
	int err;
	u64 sz;

	if (elf_getphdrnum(elf, &phdrnum))
		return -1;

	for (i = 0; i < phdrnum; i++) {
		if (gelf_getphdr(elf, i, &phdr) == NULL)
			return -1;
		if (phdr.p_type != PT_LOAD)
			continue;
		if (exe) {
			if (!(phdr.p_flags & PF_X))
				continue;
		} else {
			if (!(phdr.p_flags & PF_R))
				continue;
		}
		sz = min(phdr.p_memsz, phdr.p_filesz);
		if (!sz)
			continue;
		err = mapfn(phdr.p_vaddr, sz, phdr.p_offset, data);
		if (err)
			return err;
	}
	return 0;
}

int file__read_maps(int fd, bool exe, mapfn_t mapfn, void *data,
		    bool *is_64_bit)
{
	int err;
	Elf *elf;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		return -1;

	if (is_64_bit)
		*is_64_bit = (gelf_getclass(elf) == ELFCLASS64);

	err = elf_read_maps(elf, exe, mapfn, data);

	elf_end(elf);
	return err;
}

enum dso_type dso__type_fd(int fd)
{
	enum dso_type dso_type = DSO__TYPE_UNKNOWN;
	GElf_Ehdr ehdr;
	Elf_Kind ek;
	Elf *elf;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (elf == NULL)
		goto out;

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		goto out_end;

	if (gelf_getclass(elf) == ELFCLASS64) {
		dso_type = DSO__TYPE_64BIT;
		goto out_end;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL)
		goto out_end;

	if (ehdr.e_machine == EM_X86_64)
		dso_type = DSO__TYPE_X32BIT;
	else
		dso_type = DSO__TYPE_32BIT;
out_end:
	elf_end(elf);
out:
	return dso_type;
}

static int copy_bytes(int from, off_t from_offs, int to, off_t to_offs, u64 len)
{
	ssize_t r;
	size_t n;
	int err = -1;
	char *buf = malloc(page_size);

	if (buf == NULL)
		return -1;

	if (lseek(to, to_offs, SEEK_SET) != to_offs)
		goto out;

	if (lseek(from, from_offs, SEEK_SET) != from_offs)
		goto out;

	while (len) {
		n = page_size;
		if (len < n)
			n = len;
		/* Use read because mmap won't work on proc files */
		r = read(from, buf, n);
		if (r < 0)
			goto out;
		if (!r)
			break;
		n = r;
		r = write(to, buf, n);
		if (r < 0)
			goto out;
		if ((size_t)r != n)
			goto out;
		len -= n;
	}

	err = 0;
out:
	free(buf);
	return err;
}

struct kcore {
	int fd;
	int elfclass;
	Elf *elf;
	GElf_Ehdr ehdr;
};

static int kcore__open(struct kcore *kcore, const char *filename)
{
	GElf_Ehdr *ehdr;

	kcore->fd = open(filename, O_RDONLY);
	if (kcore->fd == -1)
		return -1;

	kcore->elf = elf_begin(kcore->fd, ELF_C_READ, NULL);
	if (!kcore->elf)
		goto out_close;

	kcore->elfclass = gelf_getclass(kcore->elf);
	if (kcore->elfclass == ELFCLASSNONE)
		goto out_end;

	ehdr = gelf_getehdr(kcore->elf, &kcore->ehdr);
	if (!ehdr)
		goto out_end;

	return 0;

out_end:
	elf_end(kcore->elf);
out_close:
	close(kcore->fd);
	return -1;
}

static int kcore__init(struct kcore *kcore, char *filename, int elfclass,
		       bool temp)
{
	kcore->elfclass = elfclass;

	if (temp)
		kcore->fd = mkstemp(filename);
	else
		kcore->fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0400);
	if (kcore->fd == -1)
		return -1;

	kcore->elf = elf_begin(kcore->fd, ELF_C_WRITE, NULL);
	if (!kcore->elf)
		goto out_close;

	if (!gelf_newehdr(kcore->elf, elfclass))
		goto out_end;

	memset(&kcore->ehdr, 0, sizeof(GElf_Ehdr));

	return 0;

out_end:
	elf_end(kcore->elf);
out_close:
	close(kcore->fd);
	unlink(filename);
	return -1;
}

static void kcore__close(struct kcore *kcore)
{
	elf_end(kcore->elf);
	close(kcore->fd);
}

static int kcore__copy_hdr(struct kcore *from, struct kcore *to, size_t count)
{
	GElf_Ehdr *ehdr = &to->ehdr;
	GElf_Ehdr *kehdr = &from->ehdr;

	memcpy(ehdr->e_ident, kehdr->e_ident, EI_NIDENT);
	ehdr->e_type      = kehdr->e_type;
	ehdr->e_machine   = kehdr->e_machine;
	ehdr->e_version   = kehdr->e_version;
	ehdr->e_entry     = 0;
	ehdr->e_shoff     = 0;
	ehdr->e_flags     = kehdr->e_flags;
	ehdr->e_phnum     = count;
	ehdr->e_shentsize = 0;
	ehdr->e_shnum     = 0;
	ehdr->e_shstrndx  = 0;

	if (from->elfclass == ELFCLASS32) {
		ehdr->e_phoff     = sizeof(Elf32_Ehdr);
		ehdr->e_ehsize    = sizeof(Elf32_Ehdr);
		ehdr->e_phentsize = sizeof(Elf32_Phdr);
	} else {
		ehdr->e_phoff     = sizeof(Elf64_Ehdr);
		ehdr->e_ehsize    = sizeof(Elf64_Ehdr);
		ehdr->e_phentsize = sizeof(Elf64_Phdr);
	}

	if (!gelf_update_ehdr(to->elf, ehdr))
		return -1;

	if (!gelf_newphdr(to->elf, count))
		return -1;

	return 0;
}

static int kcore__add_phdr(struct kcore *kcore, int idx, off_t offset,
			   u64 addr, u64 len)
{
	GElf_Phdr phdr = {
		.p_type		= PT_LOAD,
		.p_flags	= PF_R | PF_W | PF_X,
		.p_offset	= offset,
		.p_vaddr	= addr,
		.p_paddr	= 0,
		.p_filesz	= len,
		.p_memsz	= len,
		.p_align	= page_size,
	};

	if (!gelf_update_phdr(kcore->elf, idx, &phdr))
		return -1;

	return 0;
}

static off_t kcore__write(struct kcore *kcore)
{
	return elf_update(kcore->elf, ELF_C_WRITE);
}

struct phdr_data {
	off_t offset;
	off_t rel;
	u64 addr;
	u64 len;
	struct list_head node;
	struct phdr_data *remaps;
};

struct sym_data {
	u64 addr;
	struct list_head node;
};

struct kcore_copy_info {
	u64 stext;
	u64 etext;
	u64 first_symbol;
	u64 last_symbol;
	u64 first_module;
	u64 first_module_symbol;
	u64 last_module_symbol;
	size_t phnum;
	struct list_head phdrs;
	struct list_head syms;
};

#define kcore_copy__for_each_phdr(k, p) \
	list_for_each_entry((p), &(k)->phdrs, node)

static struct phdr_data *phdr_data__new(u64 addr, u64 len, off_t offset)
{
	struct phdr_data *p = zalloc(sizeof(*p));

	if (p) {
		p->addr   = addr;
		p->len    = len;
		p->offset = offset;
	}

	return p;
}

static struct phdr_data *kcore_copy_info__addnew(struct kcore_copy_info *kci,
						 u64 addr, u64 len,
						 off_t offset)
{
	struct phdr_data *p = phdr_data__new(addr, len, offset);

	if (p)
		list_add_tail(&p->node, &kci->phdrs);

	return p;
}

static void kcore_copy__free_phdrs(struct kcore_copy_info *kci)
{
	struct phdr_data *p, *tmp;

	list_for_each_entry_safe(p, tmp, &kci->phdrs, node) {
		list_del_init(&p->node);
		free(p);
	}
}

static struct sym_data *kcore_copy__new_sym(struct kcore_copy_info *kci,
					    u64 addr)
{
	struct sym_data *s = zalloc(sizeof(*s));

	if (s) {
		s->addr = addr;
		list_add_tail(&s->node, &kci->syms);
	}

	return s;
}

static void kcore_copy__free_syms(struct kcore_copy_info *kci)
{
	struct sym_data *s, *tmp;

	list_for_each_entry_safe(s, tmp, &kci->syms, node) {
		list_del_init(&s->node);
		free(s);
	}
}

static int kcore_copy__process_kallsyms(void *arg, const char *name, char type,
					u64 start)
{
	struct kcore_copy_info *kci = arg;

	if (!kallsyms__is_function(type))
		return 0;

	if (strchr(name, '[')) {
		if (!kci->first_module_symbol || start < kci->first_module_symbol)
			kci->first_module_symbol = start;
		if (start > kci->last_module_symbol)
			kci->last_module_symbol = start;
		return 0;
	}

	if (!kci->first_symbol || start < kci->first_symbol)
		kci->first_symbol = start;

	if (!kci->last_symbol || start > kci->last_symbol)
		kci->last_symbol = start;

	if (!strcmp(name, "_stext")) {
		kci->stext = start;
		return 0;
	}

	if (!strcmp(name, "_etext")) {
		kci->etext = start;
		return 0;
	}

	if (is_entry_trampoline(name) && !kcore_copy__new_sym(kci, start))
		return -1;

	return 0;
}

static int kcore_copy__parse_kallsyms(struct kcore_copy_info *kci,
				      const char *dir)
{
	char kallsyms_filename[PATH_MAX];

	scnprintf(kallsyms_filename, PATH_MAX, "%s/kallsyms", dir);

	if (symbol__restricted_filename(kallsyms_filename, "/proc/kallsyms"))
		return -1;

	if (kallsyms__parse(kallsyms_filename, kci,
			    kcore_copy__process_kallsyms) < 0)
		return -1;

	return 0;
}

static int kcore_copy__process_modules(void *arg,
				       const char *name __maybe_unused,
				       u64 start, u64 size __maybe_unused)
{
	struct kcore_copy_info *kci = arg;

	if (!kci->first_module || start < kci->first_module)
		kci->first_module = start;

	return 0;
}

static int kcore_copy__parse_modules(struct kcore_copy_info *kci,
				     const char *dir)
{
	char modules_filename[PATH_MAX];

	scnprintf(modules_filename, PATH_MAX, "%s/modules", dir);

	if (symbol__restricted_filename(modules_filename, "/proc/modules"))
		return -1;

	if (modules__parse(modules_filename, kci,
			   kcore_copy__process_modules) < 0)
		return -1;

	return 0;
}

static int kcore_copy__map(struct kcore_copy_info *kci, u64 start, u64 end,
			   u64 pgoff, u64 s, u64 e)
{
	u64 len, offset;

	if (s < start || s >= end)
		return 0;

	offset = (s - start) + pgoff;
	len = e < end ? e - s : end - s;

	return kcore_copy_info__addnew(kci, s, len, offset) ? 0 : -1;
}

static int kcore_copy__read_map(u64 start, u64 len, u64 pgoff, void *data)
{
	struct kcore_copy_info *kci = data;
	u64 end = start + len;
	struct sym_data *sdat;

	if (kcore_copy__map(kci, start, end, pgoff, kci->stext, kci->etext))
		return -1;

	if (kcore_copy__map(kci, start, end, pgoff, kci->first_module,
			    kci->last_module_symbol))
		return -1;

	list_for_each_entry(sdat, &kci->syms, node) {
		u64 s = round_down(sdat->addr, page_size);

		if (kcore_copy__map(kci, start, end, pgoff, s, s + len))
			return -1;
	}

	return 0;
}

static int kcore_copy__read_maps(struct kcore_copy_info *kci, Elf *elf)
{
	if (elf_read_maps(elf, true, kcore_copy__read_map, kci) < 0)
		return -1;

	return 0;
}

static void kcore_copy__find_remaps(struct kcore_copy_info *kci)
{
	struct phdr_data *p, *k = NULL;
	u64 kend;

	if (!kci->stext)
		return;

	/* Find phdr that corresponds to the kernel map (contains stext) */
	kcore_copy__for_each_phdr(kci, p) {
		u64 pend = p->addr + p->len - 1;

		if (p->addr <= kci->stext && pend >= kci->stext) {
			k = p;
			break;
		}
	}

	if (!k)
		return;

	kend = k->offset + k->len;

	/* Find phdrs that remap the kernel */
	kcore_copy__for_each_phdr(kci, p) {
		u64 pend = p->offset + p->len;

		if (p == k)
			continue;

		if (p->offset >= k->offset && pend <= kend)
			p->remaps = k;
	}
}

static void kcore_copy__layout(struct kcore_copy_info *kci)
{
	struct phdr_data *p;
	off_t rel = 0;

	kcore_copy__find_remaps(kci);

	kcore_copy__for_each_phdr(kci, p) {
		if (!p->remaps) {
			p->rel = rel;
			rel += p->len;
		}
		kci->phnum += 1;
	}

	kcore_copy__for_each_phdr(kci, p) {
		struct phdr_data *k = p->remaps;

		if (k)
			p->rel = p->offset - k->offset + k->rel;
	}
}

static int kcore_copy__calc_maps(struct kcore_copy_info *kci, const char *dir,
				 Elf *elf)
{
	if (kcore_copy__parse_kallsyms(kci, dir))
		return -1;

	if (kcore_copy__parse_modules(kci, dir))
		return -1;

	if (kci->stext)
		kci->stext = round_down(kci->stext, page_size);
	else
		kci->stext = round_down(kci->first_symbol, page_size);

	if (kci->etext) {
		kci->etext = round_up(kci->etext, page_size);
	} else if (kci->last_symbol) {
		kci->etext = round_up(kci->last_symbol, page_size);
		kci->etext += page_size;
	}

	if (kci->first_module_symbol &&
	    (!kci->first_module || kci->first_module_symbol < kci->first_module))
		kci->first_module = kci->first_module_symbol;

	kci->first_module = round_down(kci->first_module, page_size);

	if (kci->last_module_symbol) {
		kci->last_module_symbol = round_up(kci->last_module_symbol,
						   page_size);
		kci->last_module_symbol += page_size;
	}

	if (!kci->stext || !kci->etext)
		return -1;

	if (kci->first_module && !kci->last_module_symbol)
		return -1;

	if (kcore_copy__read_maps(kci, elf))
		return -1;

	kcore_copy__layout(kci);

	return 0;
}

static int kcore_copy__copy_file(const char *from_dir, const char *to_dir,
				 const char *name)
{
	char from_filename[PATH_MAX];
	char to_filename[PATH_MAX];

	scnprintf(from_filename, PATH_MAX, "%s/%s", from_dir, name);
	scnprintf(to_filename, PATH_MAX, "%s/%s", to_dir, name);

	return copyfile_mode(from_filename, to_filename, 0400);
}

static int kcore_copy__unlink(const char *dir, const char *name)
{
	char filename[PATH_MAX];

	scnprintf(filename, PATH_MAX, "%s/%s", dir, name);

	return unlink(filename);
}

static int kcore_copy__compare_fds(int from, int to)
{
	char *buf_from;
	char *buf_to;
	ssize_t ret;
	size_t len;
	int err = -1;

	buf_from = malloc(page_size);
	buf_to = malloc(page_size);
	if (!buf_from || !buf_to)
		goto out;

	while (1) {
		/* Use read because mmap won't work on proc files */
		ret = read(from, buf_from, page_size);
		if (ret < 0)
			goto out;

		if (!ret)
			break;

		len = ret;

		if (readn(to, buf_to, len) != (int)len)
			goto out;

		if (memcmp(buf_from, buf_to, len))
			goto out;
	}

	err = 0;
out:
	free(buf_to);
	free(buf_from);
	return err;
}

static int kcore_copy__compare_files(const char *from_filename,
				     const char *to_filename)
{
	int from, to, err = -1;

	from = open(from_filename, O_RDONLY);
	if (from < 0)
		return -1;

	to = open(to_filename, O_RDONLY);
	if (to < 0)
		goto out_close_from;

	err = kcore_copy__compare_fds(from, to);

	close(to);
out_close_from:
	close(from);
	return err;
}

static int kcore_copy__compare_file(const char *from_dir, const char *to_dir,
				    const char *name)
{
	char from_filename[PATH_MAX];
	char to_filename[PATH_MAX];

	scnprintf(from_filename, PATH_MAX, "%s/%s", from_dir, name);
	scnprintf(to_filename, PATH_MAX, "%s/%s", to_dir, name);

	return kcore_copy__compare_files(from_filename, to_filename);
}

/**
 * kcore_copy - copy kallsyms, modules and kcore from one directory to another.
 * @from_dir: from directory
 * @to_dir: to directory
 *
 * This function copies kallsyms, modules and kcore files from one directory to
 * another.  kallsyms and modules are copied entirely.  Only code segments are
 * copied from kcore.  It is assumed that two segments suffice: one for the
 * kernel proper and one for all the modules.  The code segments are determined
 * from kallsyms and modules files.  The kernel map starts at _stext or the
 * lowest function symbol, and ends at _etext or the highest function symbol.
 * The module map starts at the lowest module address and ends at the highest
 * module symbol.  Start addresses are rounded down to the nearest page.  End
 * addresses are rounded up to the nearest page.  An extra page is added to the
 * highest kernel symbol and highest module symbol to, hopefully, encompass that
 * symbol too.  Because it contains only code sections, the resulting kcore is
 * unusual.  One significant peculiarity is that the mapping (start -> pgoff)
 * is not the same for the kernel map and the modules map.  That happens because
 * the data is copied adjacently whereas the original kcore has gaps.  Finally,
 * kallsyms file is compared with its copy to check that modules have not been
 * loaded or unloaded while the copies were taking place.
 *
 * Return: %0 on success, %-1 on failure.
 */
int kcore_copy(const char *from_dir, const char *to_dir)
{
	struct kcore kcore;
	struct kcore extract;
	int idx = 0, err = -1;
	off_t offset, sz;
	struct kcore_copy_info kci = { .stext = 0, };
	char kcore_filename[PATH_MAX];
	char extract_filename[PATH_MAX];
	struct phdr_data *p;

	INIT_LIST_HEAD(&kci.phdrs);
	INIT_LIST_HEAD(&kci.syms);

	if (kcore_copy__copy_file(from_dir, to_dir, "kallsyms"))
		return -1;

	if (kcore_copy__copy_file(from_dir, to_dir, "modules"))
		goto out_unlink_kallsyms;

	scnprintf(kcore_filename, PATH_MAX, "%s/kcore", from_dir);
	scnprintf(extract_filename, PATH_MAX, "%s/kcore", to_dir);

	if (kcore__open(&kcore, kcore_filename))
		goto out_unlink_modules;

	if (kcore_copy__calc_maps(&kci, from_dir, kcore.elf))
		goto out_kcore_close;

	if (kcore__init(&extract, extract_filename, kcore.elfclass, false))
		goto out_kcore_close;

	if (kcore__copy_hdr(&kcore, &extract, kci.phnum))
		goto out_extract_close;

	offset = gelf_fsize(extract.elf, ELF_T_EHDR, 1, EV_CURRENT) +
		 gelf_fsize(extract.elf, ELF_T_PHDR, kci.phnum, EV_CURRENT);
	offset = round_up(offset, page_size);

	kcore_copy__for_each_phdr(&kci, p) {
		off_t offs = p->rel + offset;

		if (kcore__add_phdr(&extract, idx++, offs, p->addr, p->len))
			goto out_extract_close;
	}

	sz = kcore__write(&extract);
	if (sz < 0 || sz > offset)
		goto out_extract_close;

	kcore_copy__for_each_phdr(&kci, p) {
		off_t offs = p->rel + offset;

		if (p->remaps)
			continue;
		if (copy_bytes(kcore.fd, p->offset, extract.fd, offs, p->len))
			goto out_extract_close;
	}

	if (kcore_copy__compare_file(from_dir, to_dir, "kallsyms"))
		goto out_extract_close;

	err = 0;

out_extract_close:
	kcore__close(&extract);
	if (err)
		unlink(extract_filename);
out_kcore_close:
	kcore__close(&kcore);
out_unlink_modules:
	if (err)
		kcore_copy__unlink(to_dir, "modules");
out_unlink_kallsyms:
	if (err)
		kcore_copy__unlink(to_dir, "kallsyms");

	kcore_copy__free_phdrs(&kci);
	kcore_copy__free_syms(&kci);

	return err;
}

int kcore_extract__create(struct kcore_extract *kce)
{
	struct kcore kcore;
	struct kcore extract;
	size_t count = 1;
	int idx = 0, err = -1;
	off_t offset = page_size, sz;

	if (kcore__open(&kcore, kce->kcore_filename))
		return -1;

	strcpy(kce->extract_filename, PERF_KCORE_EXTRACT);
	if (kcore__init(&extract, kce->extract_filename, kcore.elfclass, true))
		goto out_kcore_close;

	if (kcore__copy_hdr(&kcore, &extract, count))
		goto out_extract_close;

	if (kcore__add_phdr(&extract, idx, offset, kce->addr, kce->len))
		goto out_extract_close;

	sz = kcore__write(&extract);
	if (sz < 0 || sz > offset)
		goto out_extract_close;

	if (copy_bytes(kcore.fd, kce->offs, extract.fd, offset, kce->len))
		goto out_extract_close;

	err = 0;

out_extract_close:
	kcore__close(&extract);
	if (err)
		unlink(kce->extract_filename);
out_kcore_close:
	kcore__close(&kcore);

	return err;
}

void kcore_extract__delete(struct kcore_extract *kce)
{
	unlink(kce->extract_filename);
}

#ifdef HAVE_GELF_GETNOTE_SUPPORT

static void sdt_adjust_loc(struct sdt_note *tmp, GElf_Addr base_off)
{
	if (!base_off)
		return;

	if (tmp->bit32)
		tmp->addr.a32[SDT_NOTE_IDX_LOC] =
			tmp->addr.a32[SDT_NOTE_IDX_LOC] + base_off -
			tmp->addr.a32[SDT_NOTE_IDX_BASE];
	else
		tmp->addr.a64[SDT_NOTE_IDX_LOC] =
			tmp->addr.a64[SDT_NOTE_IDX_LOC] + base_off -
			tmp->addr.a64[SDT_NOTE_IDX_BASE];
}

static void sdt_adjust_refctr(struct sdt_note *tmp, GElf_Addr base_addr,
			      GElf_Addr base_off)
{
	if (!base_off)
		return;

	if (tmp->bit32 && tmp->addr.a32[SDT_NOTE_IDX_REFCTR])
		tmp->addr.a32[SDT_NOTE_IDX_REFCTR] -= (base_addr - base_off);
	else if (tmp->addr.a64[SDT_NOTE_IDX_REFCTR])
		tmp->addr.a64[SDT_NOTE_IDX_REFCTR] -= (base_addr - base_off);
}

/**
 * populate_sdt_note : Parse raw data and identify SDT note
 * @elf: elf of the opened file
 * @data: raw data of a section with description offset applied
 * @len: note description size
 * @type: type of the note
 * @sdt_notes: List to add the SDT note
 *
 * Responsible for parsing the @data in section .note.stapsdt in @elf and
 * if its an SDT note, it appends to @sdt_notes list.
 */
static int populate_sdt_note(Elf **elf, const char *data, size_t len,
			     struct list_head *sdt_notes)
{
	const char *provider, *name, *args;
	struct sdt_note *tmp = NULL;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	int ret = -EINVAL;

	union {
		Elf64_Addr a64[NR_ADDR];
		Elf32_Addr a32[NR_ADDR];
	} buf;

	Elf_Data dst = {
		.d_buf = &buf, .d_type = ELF_T_ADDR, .d_version = EV_CURRENT,
		.d_size = gelf_fsize((*elf), ELF_T_ADDR, NR_ADDR, EV_CURRENT),
		.d_off = 0, .d_align = 0
	};
	Elf_Data src = {
		.d_buf = (void *) data, .d_type = ELF_T_ADDR,
		.d_version = EV_CURRENT, .d_size = dst.d_size, .d_off = 0,
		.d_align = 0
	};

	tmp = (struct sdt_note *)calloc(1, sizeof(struct sdt_note));
	if (!tmp) {
		ret = -ENOMEM;
		goto out_err;
	}

	INIT_LIST_HEAD(&tmp->note_list);

	if (len < dst.d_size + 3)
		goto out_free_note;

	/* Translation from file representation to memory representation */
	if (gelf_xlatetom(*elf, &dst, &src,
			  elf_getident(*elf, NULL)[EI_DATA]) == NULL) {
		pr_err("gelf_xlatetom : %s\n", elf_errmsg(-1));
		goto out_free_note;
	}

	/* Populate the fields of sdt_note */
	provider = data + dst.d_size;

	name = (const char *)memchr(provider, '\0', data + len - provider);
	if (name++ == NULL)
		goto out_free_note;

	tmp->provider = strdup(provider);
	if (!tmp->provider) {
		ret = -ENOMEM;
		goto out_free_note;
	}
	tmp->name = strdup(name);
	if (!tmp->name) {
		ret = -ENOMEM;
		goto out_free_prov;
	}

	args = memchr(name, '\0', data + len - name);

	/*
	 * There is no argument if:
	 * - We reached the end of the note;
	 * - There is not enough room to hold a potential string;
	 * - The argument string is empty or just contains ':'.
	 */
	if (args == NULL || data + len - args < 2 ||
		args[1] == ':' || args[1] == '\0')
		tmp->args = NULL;
	else {
		tmp->args = strdup(++args);
		if (!tmp->args) {
			ret = -ENOMEM;
			goto out_free_name;
		}
	}

	if (gelf_getclass(*elf) == ELFCLASS32) {
		memcpy(&tmp->addr, &buf, 3 * sizeof(Elf32_Addr));
		tmp->bit32 = true;
	} else {
		memcpy(&tmp->addr, &buf, 3 * sizeof(Elf64_Addr));
		tmp->bit32 = false;
	}

	if (!gelf_getehdr(*elf, &ehdr)) {
		pr_debug("%s : cannot get elf header.\n", __func__);
		ret = -EBADF;
		goto out_free_args;
	}

	/* Adjust the prelink effect :
	 * Find out the .stapsdt.base section.
	 * This scn will help us to handle prelinking (if present).
	 * Compare the retrieved file offset of the base section with the
	 * base address in the description of the SDT note. If its different,
	 * then accordingly, adjust the note location.
	 */
	if (elf_section_by_name(*elf, &ehdr, &shdr, SDT_BASE_SCN, NULL))
		sdt_adjust_loc(tmp, shdr.sh_offset);

	/* Adjust reference counter offset */
	if (elf_section_by_name(*elf, &ehdr, &shdr, SDT_PROBES_SCN, NULL))
		sdt_adjust_refctr(tmp, shdr.sh_addr, shdr.sh_offset);

	list_add_tail(&tmp->note_list, sdt_notes);
	return 0;

out_free_args:
	zfree(&tmp->args);
out_free_name:
	zfree(&tmp->name);
out_free_prov:
	zfree(&tmp->provider);
out_free_note:
	free(tmp);
out_err:
	return ret;
}

/**
 * construct_sdt_notes_list : constructs a list of SDT notes
 * @elf : elf to look into
 * @sdt_notes : empty list_head
 *
 * Scans the sections in 'elf' for the section
 * .note.stapsdt. It, then calls populate_sdt_note to find
 * out the SDT events and populates the 'sdt_notes'.
 */
static int construct_sdt_notes_list(Elf *elf, struct list_head *sdt_notes)
{
	GElf_Ehdr ehdr;
	Elf_Scn *scn = NULL;
	Elf_Data *data;
	GElf_Shdr shdr;
	size_t shstrndx, next;
	GElf_Nhdr nhdr;
	size_t name_off, desc_off, offset;
	int ret = 0;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		ret = -EBADF;
		goto out_ret;
	}
	if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
		ret = -EBADF;
		goto out_ret;
	}

	/* Look for the required section */
	scn = elf_section_by_name(elf, &ehdr, &shdr, SDT_NOTE_SCN, NULL);
	if (!scn) {
		ret = -ENOENT;
		goto out_ret;
	}

	if ((shdr.sh_type != SHT_NOTE) || (shdr.sh_flags & SHF_ALLOC)) {
		ret = -ENOENT;
		goto out_ret;
	}

	data = elf_getdata(scn, NULL);

	/* Get the SDT notes */
	for (offset = 0; (next = gelf_getnote(data, offset, &nhdr, &name_off,
					      &desc_off)) > 0; offset = next) {
		if (nhdr.n_namesz == sizeof(SDT_NOTE_NAME) &&
		    !memcmp(data->d_buf + name_off, SDT_NOTE_NAME,
			    sizeof(SDT_NOTE_NAME))) {
			/* Check the type of the note */
			if (nhdr.n_type != SDT_NOTE_TYPE)
				goto out_ret;

			ret = populate_sdt_note(&elf, ((data->d_buf) + desc_off),
						nhdr.n_descsz, sdt_notes);
			if (ret < 0)
				goto out_ret;
		}
	}
	if (list_empty(sdt_notes))
		ret = -ENOENT;

out_ret:
	return ret;
}

/**
 * get_sdt_note_list : Wrapper to construct a list of sdt notes
 * @head : empty list_head
 * @target : file to find SDT notes from
 *
 * This opens the file, initializes
 * the ELF and then calls construct_sdt_notes_list.
 */
int get_sdt_note_list(struct list_head *head, const char *target)
{
	Elf *elf;
	int fd, ret;

	fd = open(target, O_RDONLY);
	if (fd < 0)
		return -EBADF;

	elf = elf_begin(fd, PERF_ELF_C_READ_MMAP, NULL);
	if (!elf) {
		ret = -EBADF;
		goto out_close;
	}
	ret = construct_sdt_notes_list(elf, head);
	elf_end(elf);
out_close:
	close(fd);
	return ret;
}

/**
 * cleanup_sdt_note_list : free the sdt notes' list
 * @sdt_notes: sdt notes' list
 *
 * Free up the SDT notes in @sdt_notes.
 * Returns the number of SDT notes free'd.
 */
int cleanup_sdt_note_list(struct list_head *sdt_notes)
{
	struct sdt_note *tmp, *pos;
	int nr_free = 0;

	list_for_each_entry_safe(pos, tmp, sdt_notes, note_list) {
		list_del_init(&pos->note_list);
		zfree(&pos->args);
		zfree(&pos->name);
		zfree(&pos->provider);
		free(pos);
		nr_free++;
	}
	return nr_free;
}

/**
 * sdt_notes__get_count: Counts the number of sdt events
 * @start: list_head to sdt_notes list
 *
 * Returns the number of SDT notes in a list
 */
int sdt_notes__get_count(struct list_head *start)
{
	struct sdt_note *sdt_ptr;
	int count = 0;

	list_for_each_entry(sdt_ptr, start, note_list)
		count++;
	return count;
}
#endif

void symbol__elf_init(void)
{
	elf_version(EV_CURRENT);
}
