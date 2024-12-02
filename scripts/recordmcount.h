/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * recordmcount.h
 *
 * This code was taken out of recordmcount.c written by
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 *
 * The original code had the same algorithms for both 32bit
 * and 64bit ELF files, but the code was duplicated to support
 * the difference in structures that were used. This
 * file creates a macro of everything that is different between
 * the 64 and 32 bit code, such that by including this header
 * twice we can create both sets of functions by including this
 * header once with RECORD_MCOUNT_64 undefined, and again with
 * it defined.
 *
 * This conversion to macros was done by:
 * Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 */
#undef append_func
#undef is_fake_mcount
#undef fn_is_fake_mcount
#undef MIPS_is_fake_mcount
#undef mcount_adjust
#undef sift_rel_mcount
#undef nop_mcount
#undef find_secsym_ndx
#undef __has_rel_mcount
#undef has_rel_mcount
#undef tot_relsize
#undef get_mcountsym
#undef find_symtab
#undef get_shnum
#undef set_shnum
#undef get_shstrndx
#undef get_symindex
#undef get_sym_str_and_relp
#undef do_func
#undef Elf_Addr
#undef Elf_Ehdr
#undef Elf_Shdr
#undef Elf_Rel
#undef Elf_Rela
#undef Elf_Sym
#undef ELF_R_SYM
#undef Elf_r_sym
#undef ELF_R_INFO
#undef Elf_r_info
#undef ELF_ST_BIND
#undef ELF_ST_TYPE
#undef fn_ELF_R_SYM
#undef fn_ELF_R_INFO
#undef uint_t
#undef _w
#undef _align
#undef _size

#ifdef RECORD_MCOUNT_64
# define append_func		append64
# define sift_rel_mcount	sift64_rel_mcount
# define nop_mcount		nop_mcount_64
# define find_secsym_ndx	find64_secsym_ndx
# define __has_rel_mcount	__has64_rel_mcount
# define has_rel_mcount		has64_rel_mcount
# define tot_relsize		tot64_relsize
# define find_symtab		find_symtab64
# define get_shnum		get_shnum64
# define set_shnum		set_shnum64
# define get_shstrndx		get_shstrndx64
# define get_symindex		get_symindex64
# define get_sym_str_and_relp	get_sym_str_and_relp_64
# define do_func		do64
# define get_mcountsym		get_mcountsym_64
# define is_fake_mcount		is_fake_mcount64
# define fn_is_fake_mcount	fn_is_fake_mcount64
# define MIPS_is_fake_mcount	MIPS64_is_fake_mcount
# define mcount_adjust		mcount_adjust_64
# define Elf_Addr		Elf64_Addr
# define Elf_Ehdr		Elf64_Ehdr
# define Elf_Shdr		Elf64_Shdr
# define Elf_Rel		Elf64_Rel
# define Elf_Rela		Elf64_Rela
# define Elf_Sym		Elf64_Sym
# define ELF_R_SYM		ELF64_R_SYM
# define Elf_r_sym		Elf64_r_sym
# define ELF_R_INFO		ELF64_R_INFO
# define Elf_r_info		Elf64_r_info
# define ELF_ST_BIND		ELF64_ST_BIND
# define ELF_ST_TYPE		ELF64_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF64_R_SYM
# define fn_ELF_R_INFO		fn_ELF64_R_INFO
# define uint_t			uint64_t
# define _w			w8
# define _align			7u
# define _size			8
#else
# define append_func		append32
# define sift_rel_mcount	sift32_rel_mcount
# define nop_mcount		nop_mcount_32
# define find_secsym_ndx	find32_secsym_ndx
# define __has_rel_mcount	__has32_rel_mcount
# define has_rel_mcount		has32_rel_mcount
# define tot_relsize		tot32_relsize
# define find_symtab		find_symtab32
# define get_shnum		get_shnum32
# define set_shnum		set_shnum32
# define get_shstrndx		get_shstrndx32
# define get_symindex		get_symindex32
# define get_sym_str_and_relp	get_sym_str_and_relp_32
# define do_func		do32
# define get_mcountsym		get_mcountsym_32
# define is_fake_mcount		is_fake_mcount32
# define fn_is_fake_mcount	fn_is_fake_mcount32
# define MIPS_is_fake_mcount	MIPS32_is_fake_mcount
# define mcount_adjust		mcount_adjust_32
# define Elf_Addr		Elf32_Addr
# define Elf_Ehdr		Elf32_Ehdr
# define Elf_Shdr		Elf32_Shdr
# define Elf_Rel		Elf32_Rel
# define Elf_Rela		Elf32_Rela
# define Elf_Sym		Elf32_Sym
# define ELF_R_SYM		ELF32_R_SYM
# define Elf_r_sym		Elf32_r_sym
# define ELF_R_INFO		ELF32_R_INFO
# define Elf_r_info		Elf32_r_info
# define ELF_ST_BIND		ELF32_ST_BIND
# define ELF_ST_TYPE		ELF32_ST_TYPE
# define fn_ELF_R_SYM		fn_ELF32_R_SYM
# define fn_ELF_R_INFO		fn_ELF32_R_INFO
# define uint_t			uint32_t
# define _w			w
# define _align			3u
# define _size			4
#endif

/* Functions and pointers that do_file() may override for specific e_machine. */
static int fn_is_fake_mcount(Elf_Rel const *rp)
{
	return 0;
}
static int (*is_fake_mcount)(Elf_Rel const *rp) = fn_is_fake_mcount;

static uint_t fn_ELF_R_SYM(Elf_Rel const *rp)
{
	return ELF_R_SYM(_w(rp->r_info));
}
static uint_t (*Elf_r_sym)(Elf_Rel const *rp) = fn_ELF_R_SYM;

static void fn_ELF_R_INFO(Elf_Rel *const rp, unsigned sym, unsigned type)
{
	rp->r_info = _w(ELF_R_INFO(sym, type));
}
static void (*Elf_r_info)(Elf_Rel *const rp, unsigned sym, unsigned type) = fn_ELF_R_INFO;

static int mcount_adjust = 0;

/*
 * MIPS mcount long call has 2 _mcount symbols, only the position of the 1st
 * _mcount symbol is needed for dynamic function tracer, with it, to disable
 * tracing(ftrace_make_nop), the instruction in the position is replaced with
 * the "b label" instruction, to enable tracing(ftrace_make_call), replace the
 * instruction back. So, here, we set the 2nd one as fake and filter it.
 *
 * c:	3c030000	lui	v1,0x0		<-->	b	label
 *		c: R_MIPS_HI16	_mcount
 *		c: R_MIPS_NONE	*ABS*
 *		c: R_MIPS_NONE	*ABS*
 * 10:	64630000	daddiu	v1,v1,0
 *		10: R_MIPS_LO16	_mcount
 *		10: R_MIPS_NONE	*ABS*
 *		10: R_MIPS_NONE	*ABS*
 * 14:	03e0082d	move	at,ra
 * 18:	0060f809	jalr	v1
 * label:
 */
#define MIPS_FAKEMCOUNT_OFFSET	4

static int MIPS_is_fake_mcount(Elf_Rel const *rp)
{
	static Elf_Addr old_r_offset = ~(Elf_Addr)0;
	Elf_Addr current_r_offset = _w(rp->r_offset);
	int is_fake;

	is_fake = (old_r_offset != ~(Elf_Addr)0) &&
		(current_r_offset - old_r_offset == MIPS_FAKEMCOUNT_OFFSET);
	old_r_offset = current_r_offset;

	return is_fake;
}

static unsigned int get_symindex(Elf_Sym const *sym, Elf32_Word const *symtab,
				 Elf32_Word const *symtab_shndx)
{
	unsigned long offset;
	unsigned short shndx = w2(sym->st_shndx);
	int index;

	if (shndx > SHN_UNDEF && shndx < SHN_LORESERVE)
		return shndx;

	if (shndx == SHN_XINDEX) {
		offset = (unsigned long)sym - (unsigned long)symtab;
		index = offset / sizeof(*sym);

		return w(symtab_shndx[index]);
	}

	return 0;
}

static unsigned int get_shnum(Elf_Ehdr const *ehdr, Elf_Shdr const *shdr0)
{
	if (shdr0 && !ehdr->e_shnum)
		return w(shdr0->sh_size);

	return w2(ehdr->e_shnum);
}

static void set_shnum(Elf_Ehdr *ehdr, Elf_Shdr *shdr0, unsigned int new_shnum)
{
	if (new_shnum >= SHN_LORESERVE) {
		ehdr->e_shnum = 0;
		shdr0->sh_size = w(new_shnum);
	} else
		ehdr->e_shnum = w2(new_shnum);
}

static int get_shstrndx(Elf_Ehdr const *ehdr, Elf_Shdr const *shdr0)
{
	if (ehdr->e_shstrndx != SHN_XINDEX)
		return w2(ehdr->e_shstrndx);

	return w(shdr0->sh_link);
}

static void find_symtab(Elf_Ehdr *const ehdr, Elf_Shdr const *shdr0,
			unsigned const nhdr, Elf32_Word **symtab,
			Elf32_Word **symtab_shndx)
{
	Elf_Shdr const *relhdr;
	unsigned k;

	*symtab = NULL;
	*symtab_shndx = NULL;

	for (relhdr = shdr0, k = nhdr; k; --k, ++relhdr) {
		if (relhdr->sh_type == SHT_SYMTAB)
			*symtab = (void *)ehdr + relhdr->sh_offset;
		else if (relhdr->sh_type == SHT_SYMTAB_SHNDX)
			*symtab_shndx = (void *)ehdr + relhdr->sh_offset;

		if (*symtab && *symtab_shndx)
			break;
	}
}

/* Append the new shstrtab, Elf_Shdr[], __mcount_loc and its relocations. */
static int append_func(Elf_Ehdr *const ehdr,
			Elf_Shdr *const shstr,
			uint_t const *const mloc0,
			uint_t const *const mlocp,
			Elf_Rel const *const mrel0,
			Elf_Rel const *const mrelp,
			unsigned int const rel_entsize,
			unsigned int const symsec_sh_link)
{
	/* Begin constructing output file */
	Elf_Shdr mcsec;
	char const *mc_name = (sizeof(Elf_Rela) == rel_entsize)
		? ".rela__mcount_loc"
		:  ".rel__mcount_loc";
	uint_t const old_shoff = _w(ehdr->e_shoff);
	uint_t const old_shstr_sh_size   = _w(shstr->sh_size);
	uint_t const old_shstr_sh_offset = _w(shstr->sh_offset);
	Elf_Shdr *const shdr0 = (Elf_Shdr *)(old_shoff + (void *)ehdr);
	unsigned int const old_shnum = get_shnum(ehdr, shdr0);
	unsigned int const new_shnum = 2 + old_shnum; /* {.rel,}__mcount_loc */
	uint_t t = 1 + strlen(mc_name) + _w(shstr->sh_size);
	uint_t new_e_shoff;

	shstr->sh_size = _w(t);
	shstr->sh_offset = _w(sb.st_size);
	t += sb.st_size;
	t += (_align & -t);  /* word-byte align */
	new_e_shoff = t;

	set_shnum(ehdr, shdr0, new_shnum);

	/* body for new shstrtab */
	if (ulseek(sb.st_size, SEEK_SET) < 0)
		return -1;
	if (uwrite(old_shstr_sh_offset + (void *)ehdr, old_shstr_sh_size) < 0)
		return -1;
	if (uwrite(mc_name, 1 + strlen(mc_name)) < 0)
		return -1;

	/* old(modified) Elf_Shdr table, word-byte aligned */
	if (ulseek(t, SEEK_SET) < 0)
		return -1;
	t += sizeof(Elf_Shdr) * old_shnum;
	if (uwrite(old_shoff + (void *)ehdr,
	       sizeof(Elf_Shdr) * old_shnum) < 0)
		return -1;

	/* new sections __mcount_loc and .rel__mcount_loc */
	t += 2*sizeof(mcsec);
	mcsec.sh_name = w((sizeof(Elf_Rela) == rel_entsize) + strlen(".rel")
		+ old_shstr_sh_size);
	mcsec.sh_type = w(SHT_PROGBITS);
	mcsec.sh_flags = _w(SHF_ALLOC);
	mcsec.sh_addr = 0;
	mcsec.sh_offset = _w(t);
	mcsec.sh_size = _w((void *)mlocp - (void *)mloc0);
	mcsec.sh_link = 0;
	mcsec.sh_info = 0;
	mcsec.sh_addralign = _w(_size);
	mcsec.sh_entsize = _w(_size);
	if (uwrite(&mcsec, sizeof(mcsec)) < 0)
		return -1;

	mcsec.sh_name = w(old_shstr_sh_size);
	mcsec.sh_type = (sizeof(Elf_Rela) == rel_entsize)
		? w(SHT_RELA)
		: w(SHT_REL);
	mcsec.sh_flags = 0;
	mcsec.sh_addr = 0;
	mcsec.sh_offset = _w((void *)mlocp - (void *)mloc0 + t);
	mcsec.sh_size   = _w((void *)mrelp - (void *)mrel0);
	mcsec.sh_link = w(symsec_sh_link);
	mcsec.sh_info = w(old_shnum);
	mcsec.sh_addralign = _w(_size);
	mcsec.sh_entsize = _w(rel_entsize);

	if (uwrite(&mcsec, sizeof(mcsec)) < 0)
		return -1;

	if (uwrite(mloc0, (void *)mlocp - (void *)mloc0) < 0)
		return -1;
	if (uwrite(mrel0, (void *)mrelp - (void *)mrel0) < 0)
		return -1;

	ehdr->e_shoff = _w(new_e_shoff);
	if (ulseek(0, SEEK_SET) < 0)
		return -1;
	if (uwrite(ehdr, sizeof(*ehdr)) < 0)
		return -1;
	return 0;
}

static unsigned get_mcountsym(Elf_Sym const *const sym0,
			      Elf_Rel const *relp,
			      char const *const str0)
{
	unsigned mcountsym = 0;

	Elf_Sym const *const symp =
		&sym0[Elf_r_sym(relp)];
	char const *symname = &str0[w(symp->st_name)];
	char const *mcount = gpfx == '_' ? "_mcount" : "mcount";
	char const *fentry = "__fentry__";

	if (symname[0] == '.')
		++symname;  /* ppc64 hack */
	if (strcmp(mcount, symname) == 0 ||
	    (altmcount && strcmp(altmcount, symname) == 0) ||
	    (strcmp(fentry, symname) == 0))
		mcountsym = Elf_r_sym(relp);

	return mcountsym;
}

static void get_sym_str_and_relp(Elf_Shdr const *const relhdr,
				 Elf_Ehdr const *const ehdr,
				 Elf_Sym const **sym0,
				 char const **str0,
				 Elf_Rel const **relp)
{
	Elf_Shdr *const shdr0 = (Elf_Shdr *)(_w(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const symsec_sh_link = w(relhdr->sh_link);
	Elf_Shdr const *const symsec = &shdr0[symsec_sh_link];
	Elf_Shdr const *const strsec = &shdr0[w(symsec->sh_link)];
	Elf_Rel const *const rel0 = (Elf_Rel const *)(_w(relhdr->sh_offset)
		+ (void *)ehdr);

	*sym0 = (Elf_Sym const *)(_w(symsec->sh_offset)
				  + (void *)ehdr);

	*str0 = (char const *)(_w(strsec->sh_offset)
			       + (void *)ehdr);

	*relp = rel0;
}

/*
 * Look at the relocations in order to find the calls to mcount.
 * Accumulate the section offsets that are found, and their relocation info,
 * onto the end of the existing arrays.
 */
static uint_t *sift_rel_mcount(uint_t *mlocp,
			       unsigned const offbase,
			       Elf_Rel **const mrelpp,
			       Elf_Shdr const *const relhdr,
			       Elf_Ehdr const *const ehdr,
			       unsigned const recsym,
			       uint_t const recval,
			       unsigned const reltype)
{
	uint_t *const mloc0 = mlocp;
	Elf_Rel *mrelp = *mrelpp;
	Elf_Sym const *sym0;
	char const *str0;
	Elf_Rel const *relp;
	unsigned rel_entsize = _w(relhdr->sh_entsize);
	unsigned const nrel = _w(relhdr->sh_size) / rel_entsize;
	unsigned mcountsym = 0;
	unsigned t;

	get_sym_str_and_relp(relhdr, ehdr, &sym0, &str0, &relp);

	for (t = nrel; t; --t) {
		if (!mcountsym)
			mcountsym = get_mcountsym(sym0, relp, str0);

		if (mcountsym && mcountsym == Elf_r_sym(relp) &&
				!is_fake_mcount(relp)) {
			uint_t const addend =
				_w(_w(relp->r_offset) - recval + mcount_adjust);
			mrelp->r_offset = _w(offbase
				+ ((void *)mlocp - (void *)mloc0));
			Elf_r_info(mrelp, recsym, reltype);
			if (rel_entsize == sizeof(Elf_Rela)) {
				((Elf_Rela *)mrelp)->r_addend = addend;
				*mlocp++ = 0;
			} else
				*mlocp++ = addend;

			mrelp = (Elf_Rel *)(rel_entsize + (void *)mrelp);
		}
		relp = (Elf_Rel const *)(rel_entsize + (void *)relp);
	}
	*mrelpp = mrelp;
	return mlocp;
}

/*
 * Read the relocation table again, but this time its called on sections
 * that are not going to be traced. The mcount calls here will be converted
 * into nops.
 */
static int nop_mcount(Elf_Shdr const *const relhdr,
		      Elf_Ehdr const *const ehdr,
		      const char *const txtname)
{
	Elf_Shdr *const shdr0 = (Elf_Shdr *)(_w(ehdr->e_shoff)
		+ (void *)ehdr);
	Elf_Sym const *sym0;
	char const *str0;
	Elf_Rel const *relp;
	Elf_Shdr const *const shdr = &shdr0[w(relhdr->sh_info)];
	unsigned rel_entsize = _w(relhdr->sh_entsize);
	unsigned const nrel = _w(relhdr->sh_size) / rel_entsize;
	unsigned mcountsym = 0;
	unsigned t;
	int once = 0;

	get_sym_str_and_relp(relhdr, ehdr, &sym0, &str0, &relp);

	for (t = nrel; t; --t) {
		int ret = -1;

		if (!mcountsym)
			mcountsym = get_mcountsym(sym0, relp, str0);

		if (mcountsym == Elf_r_sym(relp) && !is_fake_mcount(relp)) {
			if (make_nop)
				ret = make_nop((void *)ehdr, _w(shdr->sh_offset) + _w(relp->r_offset));
			if (warn_on_notrace_sect && !once) {
				printf("Section %s has mcount callers being ignored\n",
				       txtname);
				once = 1;
				/* just warn? */
				if (!make_nop)
					return 0;
			}
		}

		/*
		 * If we successfully removed the mcount, mark the relocation
		 * as a nop (don't do anything with it).
		 */
		if (!ret) {
			Elf_Rel rel;
			rel = *(Elf_Rel *)relp;
			Elf_r_info(&rel, Elf_r_sym(relp), rel_type_nop);
			if (ulseek((void *)relp - (void *)ehdr, SEEK_SET) < 0)
				return -1;
			if (uwrite(&rel, sizeof(rel)) < 0)
				return -1;
		}
		relp = (Elf_Rel const *)(rel_entsize + (void *)relp);
	}
	return 0;
}

/*
 * Find a symbol in the given section, to be used as the base for relocating
 * the table of offsets of calls to mcount.  A local or global symbol suffices,
 * but avoid a Weak symbol because it may be overridden; the change in value
 * would invalidate the relocations of the offsets of the calls to mcount.
 * Often the found symbol will be the unnamed local symbol generated by
 * GNU 'as' for the start of each section.  For example:
 *    Num:    Value  Size Type    Bind   Vis      Ndx Name
 *      2: 00000000     0 SECTION LOCAL  DEFAULT    1
 */
static int find_secsym_ndx(unsigned const txtndx,
				char const *const txtname,
				uint_t *const recvalp,
				unsigned int *sym_index,
				Elf_Shdr const *const symhdr,
				Elf32_Word const *symtab,
				Elf32_Word const *symtab_shndx,
				Elf_Ehdr const *const ehdr)
{
	Elf_Sym const *const sym0 = (Elf_Sym const *)(_w(symhdr->sh_offset)
		+ (void *)ehdr);
	unsigned const nsym = _w(symhdr->sh_size) / _w(symhdr->sh_entsize);
	Elf_Sym const *symp;
	unsigned t;

	for (symp = sym0, t = nsym; t; --t, ++symp) {
		unsigned int const st_bind = ELF_ST_BIND(symp->st_info);

		if (txtndx == get_symindex(symp, symtab, symtab_shndx)
			/* avoid STB_WEAK */
		    && (STB_LOCAL == st_bind || STB_GLOBAL == st_bind)) {
			/* function symbols on ARM have quirks, avoid them */
			if (w2(ehdr->e_machine) == EM_ARM
			    && ELF_ST_TYPE(symp->st_info) == STT_FUNC)
				continue;

			*recvalp = _w(symp->st_value);
			*sym_index = symp - sym0;
			return 0;
		}
	}
	fprintf(stderr, "Cannot find symbol for section %u: %s.\n",
		txtndx, txtname);
	return -1;
}

/* Evade ISO C restriction: no declaration after statement in has_rel_mcount. */
static char const * __has_rel_mcount(Elf_Shdr const *const relhdr, /* reltype */
				     Elf_Shdr const *const shdr0,
				     char const *const shstrtab,
				     char const *const fname)
{
	/* .sh_info depends on .sh_type == SHT_REL[,A] */
	Elf_Shdr const *const txthdr = &shdr0[w(relhdr->sh_info)];
	char const *const txtname = &shstrtab[w(txthdr->sh_name)];

	if (strcmp("__mcount_loc", txtname) == 0) {
		fprintf(stderr, "warning: __mcount_loc already exists: %s\n",
			fname);
		return already_has_rel_mcount;
	}
	if (w(txthdr->sh_type) != SHT_PROGBITS ||
	    !(_w(txthdr->sh_flags) & SHF_EXECINSTR))
		return NULL;
	return txtname;
}

static char const *has_rel_mcount(Elf_Shdr const *const relhdr,
				  Elf_Shdr const *const shdr0,
				  char const *const shstrtab,
				  char const *const fname)
{
	if (w(relhdr->sh_type) != SHT_REL && w(relhdr->sh_type) != SHT_RELA)
		return NULL;
	return __has_rel_mcount(relhdr, shdr0, shstrtab, fname);
}


static unsigned tot_relsize(Elf_Shdr const *const shdr0,
			    unsigned nhdr,
			    const char *const shstrtab,
			    const char *const fname)
{
	unsigned totrelsz = 0;
	Elf_Shdr const *shdrp = shdr0;
	char const *txtname;

	for (; nhdr; --nhdr, ++shdrp) {
		txtname = has_rel_mcount(shdrp, shdr0, shstrtab, fname);
		if (txtname == already_has_rel_mcount) {
			totrelsz = 0;
			break;
		}
		if (txtname && is_mcounted_section_name(txtname))
			totrelsz += _w(shdrp->sh_size);
	}
	return totrelsz;
}

/* Overall supervision for Elf32 ET_REL file. */
static int do_func(Elf_Ehdr *const ehdr, char const *const fname,
		   unsigned const reltype)
{
	Elf_Shdr *const shdr0 = (Elf_Shdr *)(_w(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const nhdr = get_shnum(ehdr, shdr0);
	Elf_Shdr *const shstr = &shdr0[get_shstrndx(ehdr, shdr0)];
	char const *const shstrtab = (char const *)(_w(shstr->sh_offset)
		+ (void *)ehdr);

	Elf_Shdr const *relhdr;
	unsigned k;

	Elf32_Word *symtab;
	Elf32_Word *symtab_shndx;

	/* Upper bound on space: assume all relevant relocs are for mcount. */
	unsigned       totrelsz;

	Elf_Rel *      mrel0;
	Elf_Rel *      mrelp;

	uint_t *      mloc0;
	uint_t *      mlocp;

	unsigned rel_entsize = 0;
	unsigned symsec_sh_link = 0;

	int result = 0;

	totrelsz = tot_relsize(shdr0, nhdr, shstrtab, fname);
	if (totrelsz == 0)
		return 0;
	mrel0 = umalloc(totrelsz);
	mrelp = mrel0;
	if (!mrel0)
		return -1;

	/* 2*sizeof(address) <= sizeof(Elf_Rel) */
	mloc0 = umalloc(totrelsz>>1);
	mlocp = mloc0;
	if (!mloc0) {
		free(mrel0);
		return -1;
	}

	find_symtab(ehdr, shdr0, nhdr, &symtab, &symtab_shndx);

	for (relhdr = shdr0, k = nhdr; k; --k, ++relhdr) {
		char const *const txtname = has_rel_mcount(relhdr, shdr0,
			shstrtab, fname);
		if (txtname == already_has_rel_mcount) {
			result = 0;
			file_updated = 0;
			goto out; /* Nothing to be done; don't append! */
		}
		if (txtname && is_mcounted_section_name(txtname)) {
			unsigned int recsym;
			uint_t recval = 0;

			symsec_sh_link = w(relhdr->sh_link);
			result = find_secsym_ndx(w(relhdr->sh_info), txtname,
						&recval, &recsym,
						&shdr0[symsec_sh_link],
						symtab, symtab_shndx,
						ehdr);
			if (result)
				goto out;

			rel_entsize = _w(relhdr->sh_entsize);
			mlocp = sift_rel_mcount(mlocp,
				(void *)mlocp - (void *)mloc0, &mrelp,
				relhdr, ehdr, recsym, recval, reltype);
		} else if (txtname && (warn_on_notrace_sect || make_nop)) {
			/*
			 * This section is ignored by ftrace, but still
			 * has mcount calls. Convert them to nops now.
			 */
			if (nop_mcount(relhdr, ehdr, txtname) < 0) {
				result = -1;
				goto out;
			}
		}
	}
	if (!result && mloc0 != mlocp)
		result = append_func(ehdr, shstr, mloc0, mlocp, mrel0, mrelp,
				     rel_entsize, symsec_sh_link);
out:
	free(mrel0);
	free(mloc0);
	return result;
}
