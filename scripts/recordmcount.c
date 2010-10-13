/*
 * recordmcount.c: construct a table of the locations of calls to 'mcount'
 * so that ftrace can find them quickly.
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 * Licensed under the GNU General Public License, version 2 (GPLv2).
 *
 * Restructured to fit Linux format, as well as other updates:
 *  Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 */

/*
 * Strategy: alter the .o file in-place.
 *
 * Append a new STRTAB that has the new section names, followed by a new array
 * ElfXX_Shdr[] that has the new section headers, followed by the section
 * contents for __mcount_loc and its relocations.  The old shstrtab strings,
 * and the old ElfXX_Shdr[] array, remain as "garbage" (commonly, a couple
 * kilobytes.)  Subsequent processing by /bin/ld (or the kernel module loader)
 * will ignore the garbage regions, because they are not designated by the
 * new .e_shoff nor the new ElfXX_Shdr[].  [In order to remove the garbage,
 * then use "ld -r" to create a new file that omits the garbage.]
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd_map;	/* File descriptor for file being modified. */
static int mmap_failed; /* Boolean flag. */
static void *ehdr_curr; /* current ElfXX_Ehdr *  for resource cleanup */
static char gpfx;	/* prefix for global symbol name (sometimes '_') */
static struct stat sb;	/* Remember .st_size, etc. */
static jmp_buf jmpenv;	/* setjmp/longjmp per-file error escape */

/* setjmp() return values */
enum {
	SJ_SETJMP = 0,  /* hardwired first return */
	SJ_FAIL,
	SJ_SUCCEED
};

/* Per-file resource cleanup when multiple files. */
static void
cleanup(void)
{
	if (!mmap_failed)
		munmap(ehdr_curr, sb.st_size);
	else
		free(ehdr_curr);
	close(fd_map);
}

static void __attribute__((noreturn))
fail_file(void)
{
	cleanup();
	longjmp(jmpenv, SJ_FAIL);
}

static void __attribute__((noreturn))
succeed_file(void)
{
	cleanup();
	longjmp(jmpenv, SJ_SUCCEED);
}

/* ulseek, uread, ...:  Check return value for errors. */

static off_t
ulseek(int const fd, off_t const offset, int const whence)
{
	off_t const w = lseek(fd, offset, whence);
	if ((off_t)-1 == w) {
		perror("lseek");
		fail_file();
	}
	return w;
}

static size_t
uread(int const fd, void *const buf, size_t const count)
{
	size_t const n = read(fd, buf, count);
	if (n != count) {
		perror("read");
		fail_file();
	}
	return n;
}

static size_t
uwrite(int const fd, void const *const buf, size_t const count)
{
	size_t const n = write(fd, buf, count);
	if (n != count) {
		perror("write");
		fail_file();
	}
	return n;
}

static void *
umalloc(size_t size)
{
	void *const addr = malloc(size);
	if (0 == addr) {
		fprintf(stderr, "malloc failed: %zu bytes\n", size);
		fail_file();
	}
	return addr;
}

/*
 * Get the whole file as a programming convenience in order to avoid
 * malloc+lseek+read+free of many pieces.  If successful, then mmap
 * avoids copying unused pieces; else just read the whole file.
 * Open for both read and write; new info will be appended to the file.
 * Use MAP_PRIVATE so that a few changes to the in-memory ElfXX_Ehdr
 * do not propagate to the file until an explicit overwrite at the last.
 * This preserves most aspects of consistency (all except .st_size)
 * for simultaneous readers of the file while we are appending to it.
 * However, multiple writers still are bad.  We choose not to use
 * locking because it is expensive and the use case of kernel build
 * makes multiple writers unlikely.
 */
static void *mmap_file(char const *fname)
{
	void *addr;

	fd_map = open(fname, O_RDWR);
	if (0 > fd_map || 0 > fstat(fd_map, &sb)) {
		perror(fname);
		fail_file();
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file: %s\n", fname);
		fail_file();
	}
	addr = mmap(0, sb.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE,
		    fd_map, 0);
	mmap_failed = 0;
	if (MAP_FAILED == addr) {
		mmap_failed = 1;
		addr = umalloc(sb.st_size);
		uread(fd_map, addr, sb.st_size);
	}
	return addr;
}

/* w8rev, w8nat, ...: Handle endianness. */

static uint64_t w8rev(uint64_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (7 * 8))
	       | ((0xff & (x >> (1 * 8))) << (6 * 8))
	       | ((0xff & (x >> (2 * 8))) << (5 * 8))
	       | ((0xff & (x >> (3 * 8))) << (4 * 8))
	       | ((0xff & (x >> (4 * 8))) << (3 * 8))
	       | ((0xff & (x >> (5 * 8))) << (2 * 8))
	       | ((0xff & (x >> (6 * 8))) << (1 * 8))
	       | ((0xff & (x >> (7 * 8))) << (0 * 8));
}

static uint32_t w4rev(uint32_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (3 * 8))
	       | ((0xff & (x >> (1 * 8))) << (2 * 8))
	       | ((0xff & (x >> (2 * 8))) << (1 * 8))
	       | ((0xff & (x >> (3 * 8))) << (0 * 8));
}

static uint32_t w2rev(uint16_t const x)
{
	return   ((0xff & (x >> (0 * 8))) << (1 * 8))
	       | ((0xff & (x >> (1 * 8))) << (0 * 8));
}

static uint64_t w8nat(uint64_t const x)
{
	return x;
}

static uint32_t w4nat(uint32_t const x)
{
	return x;
}

static uint32_t w2nat(uint16_t const x)
{
	return x;
}

static uint64_t (*w8)(uint64_t);
static uint32_t (*w)(uint32_t);
static uint32_t (*w2)(uint16_t);

/* Names of the sections that could contain calls to mcount. */
static int
is_mcounted_section_name(char const *const txtname)
{
	return 0 == strcmp(".text",          txtname) ||
		0 == strcmp(".sched.text",    txtname) ||
		0 == strcmp(".spinlock.text", txtname) ||
		0 == strcmp(".irqentry.text", txtname) ||
		0 == strcmp(".text.unlikely", txtname);
}

/* Append the new shstrtab, Elf32_Shdr[], __mcount_loc and its relocations. */
static void append32(Elf32_Ehdr *const ehdr,
		     Elf32_Shdr *const shstr,
		     uint32_t const *const mloc0,
		     uint32_t const *const mlocp,
		     Elf32_Rel const *const mrel0,
		     Elf32_Rel const *const mrelp,
		     unsigned int const rel_entsize,
		     unsigned int const symsec_sh_link)
{
	/* Begin constructing output file */
	Elf32_Shdr mcsec;
	char const *mc_name = (sizeof(Elf32_Rela) == rel_entsize)
		? ".rela__mcount_loc"
		:  ".rel__mcount_loc";
	unsigned const old_shnum = w2(ehdr->e_shnum);
	uint32_t const old_shoff = w(ehdr->e_shoff);
	uint32_t const old_shstr_sh_size   = w(shstr->sh_size);
	uint32_t const old_shstr_sh_offset = w(shstr->sh_offset);
	uint32_t t = 1 + strlen(mc_name) + w(shstr->sh_size);
	uint32_t new_e_shoff;

	shstr->sh_size = w(t);
	shstr->sh_offset = w(sb.st_size);
	t += sb.st_size;
	t += (3u & -t);  /* 4-byte align */
	new_e_shoff = t;

	/* body for new shstrtab */
	ulseek(fd_map, sb.st_size, SEEK_SET);
	uwrite(fd_map, old_shstr_sh_offset + (void *)ehdr, old_shstr_sh_size);
	uwrite(fd_map, mc_name, 1 + strlen(mc_name));

	/* old(modified) Elf32_Shdr table, 4-byte aligned */
	ulseek(fd_map, t, SEEK_SET);
	t += sizeof(Elf32_Shdr) * old_shnum;
	uwrite(fd_map, old_shoff + (void *)ehdr,
	       sizeof(Elf32_Shdr) * old_shnum);

	/* new sections __mcount_loc and .rel__mcount_loc */
	t += 2*sizeof(mcsec);
	mcsec.sh_name = w((sizeof(Elf32_Rela) == rel_entsize) + strlen(".rel")
		+ old_shstr_sh_size);
	mcsec.sh_type = w(SHT_PROGBITS);
	mcsec.sh_flags = w(SHF_ALLOC);
	mcsec.sh_addr = 0;
	mcsec.sh_offset = w(t);
	mcsec.sh_size = w((void *)mlocp - (void *)mloc0);
	mcsec.sh_link = 0;
	mcsec.sh_info = 0;
	mcsec.sh_addralign = w(4);
	mcsec.sh_entsize = w(4);
	uwrite(fd_map, &mcsec, sizeof(mcsec));

	mcsec.sh_name = w(old_shstr_sh_size);
	mcsec.sh_type = (sizeof(Elf32_Rela) == rel_entsize)
		? w(SHT_RELA)
		: w(SHT_REL);
	mcsec.sh_flags = 0;
	mcsec.sh_addr = 0;
	mcsec.sh_offset = w((void *)mlocp - (void *)mloc0 + t);
	mcsec.sh_size   = w((void *)mrelp - (void *)mrel0);
	mcsec.sh_link = w(symsec_sh_link);
	mcsec.sh_info = w(old_shnum);
	mcsec.sh_addralign = w(4);
	mcsec.sh_entsize = w(rel_entsize);
	uwrite(fd_map, &mcsec, sizeof(mcsec));

	uwrite(fd_map, mloc0, (void *)mlocp - (void *)mloc0);
	uwrite(fd_map, mrel0, (void *)mrelp - (void *)mrel0);

	ehdr->e_shoff = w(new_e_shoff);
	ehdr->e_shnum = w2(2 + w2(ehdr->e_shnum));  /* {.rel,}__mcount_loc */
	ulseek(fd_map, 0, SEEK_SET);
	uwrite(fd_map, ehdr, sizeof(*ehdr));
}

/*
 * append64 and append32 (and other analogous pairs) could be templated
 * using C++, but the complexity is high.  (For an example, look at p_elf.h
 * in the source for UPX, http://upx.sourceforge.net)  So: remember to make
 * the corresponding change in the routine for the other size.
 */
static void append64(Elf64_Ehdr *const ehdr,
		     Elf64_Shdr *const shstr,
		     uint64_t const *const mloc0,
		     uint64_t const *const mlocp,
		     Elf64_Rel const *const mrel0,
		     Elf64_Rel const *const mrelp,
		     unsigned int const rel_entsize,
		     unsigned int const symsec_sh_link)
{
	/* Begin constructing output file */
	Elf64_Shdr mcsec;
	char const *mc_name = (sizeof(Elf64_Rela) == rel_entsize)
		? ".rela__mcount_loc"
		:  ".rel__mcount_loc";
	unsigned const old_shnum = w2(ehdr->e_shnum);
	uint64_t const old_shoff = w8(ehdr->e_shoff);
	uint64_t const old_shstr_sh_size   = w8(shstr->sh_size);
	uint64_t const old_shstr_sh_offset = w8(shstr->sh_offset);
	uint64_t t = 1 + strlen(mc_name) + w8(shstr->sh_size);
	uint64_t new_e_shoff;

	shstr->sh_size = w8(t);
	shstr->sh_offset = w8(sb.st_size);
	t += sb.st_size;
	t += (7u & -t);  /* 8-byte align */
	new_e_shoff = t;

	/* body for new shstrtab */
	ulseek(fd_map, sb.st_size, SEEK_SET);
	uwrite(fd_map, old_shstr_sh_offset + (void *)ehdr, old_shstr_sh_size);
	uwrite(fd_map, mc_name, 1 + strlen(mc_name));

	/* old(modified) Elf64_Shdr table, 8-byte aligned */
	ulseek(fd_map, t, SEEK_SET);
	t += sizeof(Elf64_Shdr) * old_shnum;
	uwrite(fd_map, old_shoff + (void *)ehdr,
		sizeof(Elf64_Shdr) * old_shnum);

	/* new sections __mcount_loc and .rel__mcount_loc */
	t += 2*sizeof(mcsec);
	mcsec.sh_name = w((sizeof(Elf64_Rela) == rel_entsize) + strlen(".rel")
		+ old_shstr_sh_size);
	mcsec.sh_type = w(SHT_PROGBITS);
	mcsec.sh_flags = w8(SHF_ALLOC);
	mcsec.sh_addr = 0;
	mcsec.sh_offset = w8(t);
	mcsec.sh_size = w8((void *)mlocp - (void *)mloc0);
	mcsec.sh_link = 0;
	mcsec.sh_info = 0;
	mcsec.sh_addralign = w8(8);
	mcsec.sh_entsize = w8(8);
	uwrite(fd_map, &mcsec, sizeof(mcsec));

	mcsec.sh_name = w(old_shstr_sh_size);
	mcsec.sh_type = (sizeof(Elf64_Rela) == rel_entsize)
		? w(SHT_RELA)
		: w(SHT_REL);
	mcsec.sh_flags = 0;
	mcsec.sh_addr = 0;
	mcsec.sh_offset = w8((void *)mlocp - (void *)mloc0 + t);
	mcsec.sh_size   = w8((void *)mrelp - (void *)mrel0);
	mcsec.sh_link = w(symsec_sh_link);
	mcsec.sh_info = w(old_shnum);
	mcsec.sh_addralign = w8(8);
	mcsec.sh_entsize = w8(rel_entsize);
	uwrite(fd_map, &mcsec, sizeof(mcsec));

	uwrite(fd_map, mloc0, (void *)mlocp - (void *)mloc0);
	uwrite(fd_map, mrel0, (void *)mrelp - (void *)mrel0);

	ehdr->e_shoff = w8(new_e_shoff);
	ehdr->e_shnum = w2(2 + w2(ehdr->e_shnum));  /* {.rel,}__mcount_loc */
	ulseek(fd_map, 0, SEEK_SET);
	uwrite(fd_map, ehdr, sizeof(*ehdr));
}

/*
 * Look at the relocations in order to find the calls to mcount.
 * Accumulate the section offsets that are found, and their relocation info,
 * onto the end of the existing arrays.
 */
static uint32_t *sift32_rel_mcount(uint32_t *mlocp,
				   unsigned const offbase,
				   Elf32_Rel **const mrelpp,
				   Elf32_Shdr const *const relhdr,
				   Elf32_Ehdr const *const ehdr,
				   unsigned const recsym,
				   uint32_t const recval,
				   unsigned const reltype)
{
	uint32_t *const mloc0 = mlocp;
	Elf32_Rel *mrelp = *mrelpp;
	Elf32_Shdr *const shdr0 = (Elf32_Shdr *)(w(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const symsec_sh_link = w(relhdr->sh_link);
	Elf32_Shdr const *const symsec = &shdr0[symsec_sh_link];
	Elf32_Sym const *const sym0 = (Elf32_Sym const *)(w(symsec->sh_offset)
		+ (void *)ehdr);

	Elf32_Shdr const *const strsec = &shdr0[w(symsec->sh_link)];
	char const *const str0 = (char const *)(w(strsec->sh_offset)
		+ (void *)ehdr);

	Elf32_Rel const *const rel0 = (Elf32_Rel const *)(w(relhdr->sh_offset)
		+ (void *)ehdr);
	unsigned rel_entsize = w(relhdr->sh_entsize);
	unsigned const nrel = w(relhdr->sh_size) / rel_entsize;
	Elf32_Rel const *relp = rel0;

	unsigned mcountsym = 0;
	unsigned t;

	for (t = nrel; t; --t) {
		if (!mcountsym) {
			Elf32_Sym const *const symp =
				&sym0[ELF32_R_SYM(w(relp->r_info))];

			if (0 == strcmp((('_' == gpfx) ? "_mcount" : "mcount"),
					&str0[w(symp->st_name)]))
				mcountsym = ELF32_R_SYM(w(relp->r_info));
		}
		if (mcountsym == ELF32_R_SYM(w(relp->r_info))) {
			uint32_t const addend = w(w(relp->r_offset) - recval);
			mrelp->r_offset = w(offbase
				+ ((void *)mlocp - (void *)mloc0));
			mrelp->r_info = w(ELF32_R_INFO(recsym, reltype));
			if (sizeof(Elf32_Rela) == rel_entsize) {
				((Elf32_Rela *)mrelp)->r_addend = addend;
				*mlocp++ = 0;
			} else
				*mlocp++ = addend;

			mrelp = (Elf32_Rel *)(rel_entsize + (void *)mrelp);
		}
		relp = (Elf32_Rel const *)(rel_entsize + (void *)relp);
	}
	*mrelpp = mrelp;
	return mlocp;
}

static uint64_t *sift64_rel_mcount(uint64_t *mlocp,
				   unsigned const offbase,
				   Elf64_Rel **const mrelpp,
				   Elf64_Shdr const *const relhdr,
				   Elf64_Ehdr const *const ehdr,
				   unsigned const recsym,
				   uint64_t const recval,
				   unsigned const reltype)
{
	uint64_t *const mloc0 = mlocp;
	Elf64_Rel *mrelp = *mrelpp;
	Elf64_Shdr *const shdr0 = (Elf64_Shdr *)(w8(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const symsec_sh_link = w(relhdr->sh_link);
	Elf64_Shdr const *const symsec = &shdr0[symsec_sh_link];
	Elf64_Sym const *const sym0 = (Elf64_Sym const *)(w8(symsec->sh_offset)
		+ (void *)ehdr);

	Elf64_Shdr const *const strsec = &shdr0[w(symsec->sh_link)];
	char const *const str0 = (char const *)(w8(strsec->sh_offset)
		+ (void *)ehdr);

	Elf64_Rel const *const rel0 = (Elf64_Rel const *)(w8(relhdr->sh_offset)
		+ (void *)ehdr);
	unsigned rel_entsize = w8(relhdr->sh_entsize);
	unsigned const nrel = w8(relhdr->sh_size) / rel_entsize;
	Elf64_Rel const *relp = rel0;

	unsigned mcountsym = 0;
	unsigned t;

	for (t = nrel; 0 != t; --t) {
		if (!mcountsym) {
			Elf64_Sym const *const symp =
				&sym0[ELF64_R_SYM(w8(relp->r_info))];
			char const *symname = &str0[w(symp->st_name)];

			if ('.' == symname[0])
				++symname;  /* ppc64 hack */
			if (0 == strcmp((('_' == gpfx) ? "_mcount" : "mcount"),
					symname))
				mcountsym = ELF64_R_SYM(w8(relp->r_info));
		}

		if (mcountsym == ELF64_R_SYM(w8(relp->r_info))) {
			uint64_t const addend = w8(w8(relp->r_offset) - recval);

			mrelp->r_offset = w8(offbase
				+ ((void *)mlocp - (void *)mloc0));
			mrelp->r_info = w8(ELF64_R_INFO(recsym, reltype));
			if (sizeof(Elf64_Rela) == rel_entsize) {
				((Elf64_Rela *)mrelp)->r_addend = addend;
				*mlocp++ = 0;
			} else
				*mlocp++ = addend;

			mrelp = (Elf64_Rel *)(rel_entsize + (void *)mrelp);
		}
		relp = (Elf64_Rel const *)(rel_entsize + (void *)relp);
	}
	*mrelpp = mrelp;

	return mlocp;
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
static unsigned find32_secsym_ndx(unsigned const txtndx,
				  char const *const txtname,
				  uint32_t *const recvalp,
				  Elf32_Shdr const *const symhdr,
				  Elf32_Ehdr const *const ehdr)
{
	Elf32_Sym const *const sym0 = (Elf32_Sym const *)(w(symhdr->sh_offset)
		+ (void *)ehdr);
	unsigned const nsym = w(symhdr->sh_size) / w(symhdr->sh_entsize);
	Elf32_Sym const *symp;
	unsigned t;

	for (symp = sym0, t = nsym; t; --t, ++symp) {
		unsigned int const st_bind = ELF32_ST_BIND(symp->st_info);

		if (txtndx == w2(symp->st_shndx)
			/* avoid STB_WEAK */
		    && (STB_LOCAL == st_bind || STB_GLOBAL == st_bind)) {
			*recvalp = w(symp->st_value);
			return symp - sym0;
		}
	}
	fprintf(stderr, "Cannot find symbol for section %d: %s.\n",
		txtndx, txtname);
	fail_file();
}

static unsigned find64_secsym_ndx(unsigned const txtndx,
				  char const *const txtname,
				  uint64_t *const recvalp,
				  Elf64_Shdr const *const symhdr,
				  Elf64_Ehdr const *const ehdr)
{
	Elf64_Sym const *const sym0 = (Elf64_Sym const *)(w8(symhdr->sh_offset)
		+ (void *)ehdr);
	unsigned const nsym = w8(symhdr->sh_size) / w8(symhdr->sh_entsize);
	Elf64_Sym const *symp;
	unsigned t;

	for (symp = sym0, t = nsym; t; --t, ++symp) {
		unsigned int const st_bind = ELF64_ST_BIND(symp->st_info);

		if (txtndx == w2(symp->st_shndx)
			/* avoid STB_WEAK */
		    && (STB_LOCAL == st_bind || STB_GLOBAL == st_bind)) {
			*recvalp = w8(symp->st_value);
			return symp - sym0;
		}
	}
	fprintf(stderr, "Cannot find symbol for section %d: %s.\n",
		txtndx, txtname);
	fail_file();
}

/*
 * Evade ISO C restriction: no declaration after statement in
 * has32_rel_mcount.
 */
static char const *
__has32_rel_mcount(Elf32_Shdr const *const relhdr,  /* is SHT_REL or SHT_RELA */
		   Elf32_Shdr const *const shdr0,
		   char const *const shstrtab,
		   char const *const fname)
{
	/* .sh_info depends on .sh_type == SHT_REL[,A] */
	Elf32_Shdr const *const txthdr = &shdr0[w(relhdr->sh_info)];
	char const *const txtname = &shstrtab[w(txthdr->sh_name)];

	if (0 == strcmp("__mcount_loc", txtname)) {
		fprintf(stderr, "warning: __mcount_loc already exists: %s\n",
			fname);
		succeed_file();
	}
	if (SHT_PROGBITS != w(txthdr->sh_type) ||
	    !is_mcounted_section_name(txtname))
		return NULL;
	return txtname;
}

static char const *has32_rel_mcount(Elf32_Shdr const *const relhdr,
				    Elf32_Shdr const *const shdr0,
				    char const *const shstrtab,
				    char const *const fname)
{
	if (SHT_REL  != w(relhdr->sh_type) && SHT_RELA != w(relhdr->sh_type))
		return NULL;
	return __has32_rel_mcount(relhdr, shdr0, shstrtab, fname);
}

static char const *__has64_rel_mcount(Elf64_Shdr const *const relhdr,
				      Elf64_Shdr const *const shdr0,
				      char const *const shstrtab,
				      char const *const fname)
{
	/* .sh_info depends on .sh_type == SHT_REL[,A] */
	Elf64_Shdr const *const txthdr = &shdr0[w(relhdr->sh_info)];
	char const *const txtname = &shstrtab[w(txthdr->sh_name)];

	if (0 == strcmp("__mcount_loc", txtname)) {
		fprintf(stderr, "warning: __mcount_loc already exists: %s\n",
			fname);
		succeed_file();
	}
	if (SHT_PROGBITS != w(txthdr->sh_type) ||
	    !is_mcounted_section_name(txtname))
		return NULL;
	return txtname;
}

static char const *has64_rel_mcount(Elf64_Shdr const *const relhdr,
				    Elf64_Shdr const *const shdr0,
				    char const *const shstrtab,
				    char const *const fname)
{
	if (SHT_REL  != w(relhdr->sh_type) && SHT_RELA != w(relhdr->sh_type))
		return NULL;
	return __has64_rel_mcount(relhdr, shdr0, shstrtab, fname);
}

static unsigned tot32_relsize(Elf32_Shdr const *const shdr0,
			      unsigned nhdr,
			      const char *const shstrtab,
			      const char *const fname)
{
	unsigned totrelsz = 0;
	Elf32_Shdr const *shdrp = shdr0;
	for (; 0 != nhdr; --nhdr, ++shdrp) {
		if (has32_rel_mcount(shdrp, shdr0, shstrtab, fname))
			totrelsz += w(shdrp->sh_size);
	}
	return totrelsz;
}

static unsigned tot64_relsize(Elf64_Shdr const *const shdr0,
			      unsigned nhdr,
			      const char *const shstrtab,
			      const char *const fname)
{
	unsigned totrelsz = 0;
	Elf64_Shdr const *shdrp = shdr0;

	for (; nhdr; --nhdr, ++shdrp) {
		if (has64_rel_mcount(shdrp, shdr0, shstrtab, fname))
			totrelsz += w8(shdrp->sh_size);
	}
	return totrelsz;
}

/* Overall supervision for Elf32 ET_REL file. */
static void
do32(Elf32_Ehdr *const ehdr, char const *const fname, unsigned const reltype)
{
	Elf32_Shdr *const shdr0 = (Elf32_Shdr *)(w(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const nhdr = w2(ehdr->e_shnum);
	Elf32_Shdr *const shstr = &shdr0[w2(ehdr->e_shstrndx)];
	char const *const shstrtab = (char const *)(w(shstr->sh_offset)
		+ (void *)ehdr);

	Elf32_Shdr const *relhdr;
	unsigned k;

	/* Upper bound on space: assume all relevant relocs are for mcount. */
	unsigned const totrelsz = tot32_relsize(shdr0, nhdr, shstrtab, fname);
	Elf32_Rel *const mrel0 = umalloc(totrelsz);
	Elf32_Rel *      mrelp = mrel0;

	/* 2*sizeof(address) <= sizeof(Elf32_Rel) */
	uint32_t *const mloc0 = umalloc(totrelsz>>1);
	uint32_t *      mlocp = mloc0;

	unsigned rel_entsize = 0;
	unsigned symsec_sh_link = 0;

	for (relhdr = shdr0, k = nhdr; k; --k, ++relhdr) {
		char const *const txtname = has32_rel_mcount(relhdr, shdr0,
			shstrtab, fname);
		if (txtname) {
			uint32_t recval = 0;
			unsigned const recsym = find32_secsym_ndx(
				w(relhdr->sh_info), txtname, &recval,
				&shdr0[symsec_sh_link = w(relhdr->sh_link)],
				ehdr);

			rel_entsize = w(relhdr->sh_entsize);
			mlocp = sift32_rel_mcount(mlocp,
				(void *)mlocp - (void *)mloc0, &mrelp,
				relhdr, ehdr, recsym, recval, reltype);
		}
	}
	if (mloc0 != mlocp) {
		append32(ehdr, shstr, mloc0, mlocp, mrel0, mrelp,
			rel_entsize, symsec_sh_link);
	}
	free(mrel0);
	free(mloc0);
}

static void
do64(Elf64_Ehdr *const ehdr, char const *const fname, unsigned const reltype)
{
	Elf64_Shdr *const shdr0 = (Elf64_Shdr *)(w8(ehdr->e_shoff)
		+ (void *)ehdr);
	unsigned const nhdr = w2(ehdr->e_shnum);
	Elf64_Shdr *const shstr = &shdr0[w2(ehdr->e_shstrndx)];
	char const *const shstrtab = (char const *)(w8(shstr->sh_offset)
		+ (void *)ehdr);

	Elf64_Shdr const *relhdr;
	unsigned k;

	/* Upper bound on space: assume all relevant relocs are for mcount. */
	unsigned const totrelsz = tot64_relsize(shdr0, nhdr, shstrtab, fname);
	Elf64_Rel *const mrel0 = umalloc(totrelsz);
	Elf64_Rel *      mrelp = mrel0;

	/* 2*sizeof(address) <= sizeof(Elf64_Rel) */
	uint64_t *const mloc0 = umalloc(totrelsz>>1);
	uint64_t *      mlocp = mloc0;

	unsigned rel_entsize = 0;
	unsigned symsec_sh_link = 0;

	for ((relhdr = shdr0), k = nhdr; k; --k, ++relhdr) {
		char const *const txtname = has64_rel_mcount(relhdr, shdr0,
			shstrtab, fname);
		if (txtname) {
			uint64_t recval = 0;
			unsigned const recsym = find64_secsym_ndx(
				w(relhdr->sh_info), txtname, &recval,
				&shdr0[symsec_sh_link = w(relhdr->sh_link)],
				ehdr);

			rel_entsize = w8(relhdr->sh_entsize);
			mlocp = sift64_rel_mcount(mlocp,
				(void *)mlocp - (void *)mloc0, &mrelp,
				relhdr, ehdr, recsym, recval, reltype);
		}
	}
	if (mloc0 != mlocp) {
		append64(ehdr, shstr, mloc0, mlocp, mrel0, mrelp,
			rel_entsize, symsec_sh_link);
	}
	free(mrel0);
	free(mloc0);
}

static void
do_file(char const *const fname)
{
	Elf32_Ehdr *const ehdr = mmap_file(fname);
	unsigned int reltype = 0;

	ehdr_curr = ehdr;
	w = w4nat;
	w2 = w2nat;
	w8 = w8nat;
	switch (ehdr->e_ident[EI_DATA]) {
		static unsigned int const endian = 1;
	default: {
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e_ident[EI_DATA], fname);
		fail_file();
	} break;
	case ELFDATA2LSB: {
		if (1 != *(unsigned char const *)&endian) {
			/* main() is big endian, file.o is little endian. */
			w = w4rev;
			w2 = w2rev;
			w8 = w8rev;
		}
	} break;
	case ELFDATA2MSB: {
		if (0 != *(unsigned char const *)&endian) {
			/* main() is little endian, file.o is big endian. */
			w = w4rev;
			w2 = w2rev;
			w8 = w8rev;
		}
	} break;
	}  /* end switch */
	if (0 != memcmp(ELFMAG, ehdr->e_ident, SELFMAG)
	||  ET_REL != w2(ehdr->e_type)
	||  EV_CURRENT != ehdr->e_ident[EI_VERSION]) {
		fprintf(stderr, "unrecognized ET_REL file %s\n", fname);
		fail_file();
	}

	gpfx = 0;
	switch (w2(ehdr->e_machine)) {
	default: {
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			w2(ehdr->e_machine), fname);
		fail_file();
	} break;
	case EM_386:	 reltype = R_386_32;                   break;
	case EM_ARM:	 reltype = R_ARM_ABS32;                break;
	case EM_IA_64:	 reltype = R_IA64_IMM64;   gpfx = '_'; break;
	case EM_PPC:	 reltype = R_PPC_ADDR32;   gpfx = '_'; break;
	case EM_PPC64:	 reltype = R_PPC64_ADDR64; gpfx = '_'; break;
	case EM_S390:    /* reltype: e_class    */ gpfx = '_'; break;
	case EM_SH:	 reltype = R_SH_DIR32;                 break;
	case EM_SPARCV9: reltype = R_SPARC_64;     gpfx = '_'; break;
	case EM_X86_64:	 reltype = R_X86_64_64;                break;
	}  /* end switch */

	switch (ehdr->e_ident[EI_CLASS]) {
	default: {
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e_ident[EI_CLASS], fname);
		fail_file();
	} break;
	case ELFCLASS32: {
		if (sizeof(Elf32_Ehdr) != w2(ehdr->e_ehsize)
		||  sizeof(Elf32_Shdr) != w2(ehdr->e_shentsize)) {
			fprintf(stderr,
				"unrecognized ET_REL file: %s\n", fname);
			fail_file();
		}
		if (EM_S390 == w2(ehdr->e_machine))
			reltype = R_390_32;
		do32(ehdr, fname, reltype);
	} break;
	case ELFCLASS64: {
		Elf64_Ehdr *const ghdr = (Elf64_Ehdr *)ehdr;
		if (sizeof(Elf64_Ehdr) != w2(ghdr->e_ehsize)
		||  sizeof(Elf64_Shdr) != w2(ghdr->e_shentsize)) {
			fprintf(stderr,
				"unrecognized ET_REL file: %s\n", fname);
			fail_file();
		}
		if (EM_S390 == w2(ghdr->e_machine))
			reltype = R_390_64;
		do64(ghdr, fname, reltype);
	} break;
	}  /* end switch */

	cleanup();
}

int
main(int argc, char const *argv[])
{
	int n_error = 0;  /* gcc-4.3.0 false positive complaint */
	if (argc <= 1)
		fprintf(stderr, "usage: recordmcount file.o...\n");
	else  /* Process each file in turn, allowing deep failure. */
	for (--argc, ++argv; 0 < argc; --argc, ++argv) {
		int const sjval = setjmp(jmpenv);
		switch (sjval) {
		default: {
			fprintf(stderr, "internal error: %s\n", argv[0]);
			exit(1);
		} break;
		case SJ_SETJMP: {  /* normal sequence */
			/* Avoid problems if early cleanup() */
			fd_map = -1;
			ehdr_curr = NULL;
			mmap_failed = 1;
			do_file(argv[0]);
		} break;
		case SJ_FAIL: {  /* error in do_file or below */
			++n_error;
		} break;
		case SJ_SUCCEED: {  /* premature success */
			/* do nothing */
		} break;
		}  /* end switch */
	}
	return !!n_error;
}


