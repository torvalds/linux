/*	$OpenBSD: exec_elf.h,v 1.108 2025/07/31 16:09:59 kettenis Exp $	*/
/*
 * Copyright (c) 1995, 1996 Erik Theisen.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is the ELF ABI header file
 * formerly known as "elf_abi.h".
 */

#ifndef _SYS_EXEC_ELF_H_
#define _SYS_EXEC_ELF_H_

#include <sys/types.h>
#include <machine/exec.h>

typedef __uint32_t	Elf32_Addr;	/* Unsigned program address */
typedef __uint32_t	Elf32_Off;	/* Unsigned file offset */
typedef __int32_t	Elf32_Sword;	/* Signed large integer */
typedef __uint32_t	Elf32_Word;	/* Unsigned large integer */
typedef __uint16_t	Elf32_Half;	/* Unsigned medium integer */
typedef __uint64_t	Elf32_Lword;

typedef __uint64_t	Elf64_Addr;
typedef __uint64_t	Elf64_Off;
typedef __int32_t	Elf64_Shalf;

typedef __int32_t	Elf64_Sword;
typedef __uint32_t	Elf64_Word;

typedef __int64_t	Elf64_Sxword;
typedef __uint64_t	Elf64_Xword;
typedef __uint64_t	Elf64_Lword;

typedef __uint16_t	Elf64_Half;

/*
 * e_ident[] identification indexes
 * See http://www.sco.com/developers/gabi/latest/ch4.eheader.html
 */
#define EI_MAG0		0		/* file ID */
#define EI_MAG1		1		/* file ID */
#define EI_MAG2		2		/* file ID */
#define EI_MAG3		3		/* file ID */
#define EI_CLASS	4		/* file class */
#define EI_DATA		5		/* data encoding */
#define EI_VERSION	6		/* ELF header version */
#define EI_OSABI	7		/* OS/ABI ID */
#define EI_ABIVERSION	8		/* ABI version */
#define EI_PAD		9		/* start of pad bytes */
#define EI_NIDENT	16		/* Size of e_ident[] */

/* e_ident[] magic number */
#define	ELFMAG0		0x7f		/* e_ident[EI_MAG0] */
#define	ELFMAG1		'E'		/* e_ident[EI_MAG1] */
#define	ELFMAG2		'L'		/* e_ident[EI_MAG2] */
#define	ELFMAG3		'F'		/* e_ident[EI_MAG3] */
#define	ELFMAG		"\177ELF"	/* magic */
#define	SELFMAG		4		/* size of magic */

/* e_ident[] file class */
#define	ELFCLASSNONE	0		/* invalid */
#define	ELFCLASS32	1		/* 32-bit objs */
#define	ELFCLASS64	2		/* 64-bit objs */
#define	ELFCLASSNUM	3		/* number of classes */

/* e_ident[] data encoding */
#define ELFDATANONE	0		/* invalid */
#define ELFDATA2LSB	1		/* Little-Endian */
#define ELFDATA2MSB	2		/* Big-Endian */
#define ELFDATANUM	3		/* number of data encode defines */

/* e_ident[] Operating System/ABI */
#define ELFOSABI_SYSV		0	/* UNIX System V ABI */
#define ELFOSABI_HPUX		1	/* HP-UX operating system */
#define ELFOSABI_NETBSD		2	/* NetBSD */
#define ELFOSABI_LINUX		3	/* GNU/Linux */
#define ELFOSABI_HURD		4	/* GNU/Hurd */
#define ELFOSABI_86OPEN		5	/* 86Open common IA32 ABI */
#define ELFOSABI_SOLARIS	6	/* Solaris */
#define ELFOSABI_MONTEREY	7	/* Monterey */
#define ELFOSABI_IRIX		8	/* IRIX */
#define ELFOSABI_FREEBSD	9	/* FreeBSD */
#define ELFOSABI_TRU64		10	/* TRU64 UNIX */
#define ELFOSABI_MODESTO	11	/* Novell Modesto */
#define ELFOSABI_OPENBSD	12	/* OpenBSD */
#define ELFOSABI_ARM		97	/* ARM */
#define ELFOSABI_STANDALONE	255	/* Standalone (embedded) application */

/* e_ident */
#define IS_ELF(ehdr) ((ehdr).e_ident[EI_MAG0] == ELFMAG0 && \
                      (ehdr).e_ident[EI_MAG1] == ELFMAG1 && \
                      (ehdr).e_ident[EI_MAG2] == ELFMAG2 && \
                      (ehdr).e_ident[EI_MAG3] == ELFMAG3)

/* ELF Header */
typedef struct elfhdr {
	unsigned char	e_ident[EI_NIDENT]; /* ELF Identification */
	Elf32_Half	e_type;		/* object file type */
	Elf32_Half	e_machine;	/* machine */
	Elf32_Word	e_version;	/* object file version */
	Elf32_Addr	e_entry;	/* virtual entry point */
	Elf32_Off	e_phoff;	/* program header table offset */
	Elf32_Off	e_shoff;	/* section header table offset */
	Elf32_Word	e_flags;	/* processor-specific flags */
	Elf32_Half	e_ehsize;	/* ELF header size */
	Elf32_Half	e_phentsize;	/* program header entry size */
	Elf32_Half	e_phnum;	/* number of program header entries */
	Elf32_Half	e_shentsize;	/* section header entry size */
	Elf32_Half	e_shnum;	/* number of section header entries */
	Elf32_Half	e_shstrndx;	/* section header table's "section
					   header string table" entry offset */
} Elf32_Ehdr;

typedef struct {
	unsigned char	e_ident[EI_NIDENT];	/* Id bytes */
	Elf64_Half	e_type;			/* file type */
	Elf64_Half	e_machine;		/* machine type */
	Elf64_Word	e_version;		/* version number */
	Elf64_Addr	e_entry;		/* entry point */
	Elf64_Off	e_phoff;		/* Program hdr offset */
	Elf64_Off	e_shoff;		/* Section hdr offset */
	Elf64_Word	e_flags;		/* Processor flags */
	Elf64_Half	e_ehsize;		/* sizeof ehdr */
	Elf64_Half	e_phentsize;		/* Program header entry size */
	Elf64_Half	e_phnum;		/* Number of program headers */
	Elf64_Half	e_shentsize;		/* Section header entry size */
	Elf64_Half	e_shnum;		/* Number of section headers */
	Elf64_Half	e_shstrndx;		/* String table index */
} Elf64_Ehdr;

/* e_type */
#define ET_NONE		0		/* No file type */
#define ET_REL		1		/* relocatable file */
#define ET_EXEC		2		/* executable file */
#define ET_DYN		3		/* shared object file */
#define ET_CORE		4		/* core file */
#define ET_NUM		5		/* number of types */
#define ET_LOPROC	0xff00		/* reserved range for processor */
#define ET_HIPROC	0xffff		/*  specific e_type */

/* e_machine */
#define EM_NONE		0		/* No Machine */
#define EM_M32		1		/* AT&T WE 32100 */
#define EM_SPARC	2		/* SPARC */
#define EM_386		3		/* Intel 80386 */
#define EM_68K		4		/* Motorola 68000 */
#define EM_88K		5		/* Motorola 88000 */
#define EM_486		6		/* Intel 80486 - unused? */
#define EM_860		7		/* Intel 80860 */
#define EM_MIPS		8		/* MIPS R3000 Big-Endian only */
/*
 * Don't know if EM_MIPS_RS4_BE,
 * EM_SPARC64, EM_PARISC,
 * or EM_PPC are ABI compliant
 */
#define EM_MIPS_RS4_BE	10		/* MIPS R4000 Big-Endian */
#define EM_SPARC64	11		/* SPARC v9 64-bit unofficial */
#define EM_PARISC	15		/* HPPA */
#define EM_SPARC32PLUS	18		/* Enhanced instruction set SPARC */
#define EM_PPC		20		/* PowerPC */
#define EM_PPC64	21		/* PowerPC 64 */
#define EM_ARM		40		/* Advanced RISC Machines ARM */
#define EM_ALPHA	41		/* DEC ALPHA */
#define EM_SH		42		/* Hitachi/Renesas Super-H */
#define EM_SPARCV9	43		/* SPARC version 9 */
#define EM_IA_64	50		/* Intel IA-64 Processor */
#define EM_AMD64	62		/* AMD64 architecture */
#define EM_X86_64	EM_AMD64
#define EM_VAX		75		/* DEC VAX */
#define EM_AARCH64	183		/* ARM 64-bit architecture (AArch64) */
#define EM_RISCV	243		/* RISC-V */

/* Non-standard */
#define EM_ALPHA_EXP	0x9026		/* DEC ALPHA */
#define EM__LAST__	(EM_ALPHA_EXP + 1)

/* Version */
#define EV_NONE		0		/* Invalid */
#define EV_CURRENT	1		/* Current */
#define EV_NUM		2		/* number of versions */

/* Magic for e_phnum: get real value from sh_info of first section header */
#define PN_XNUM		0xffff

/* Section Header */
typedef struct {
	Elf32_Word	sh_name;	/* name - index into section header
					   string table section */
	Elf32_Word	sh_type;	/* type */
	Elf32_Word	sh_flags;	/* flags */
	Elf32_Addr	sh_addr;	/* address */
	Elf32_Off	sh_offset;	/* file offset */
	Elf32_Word	sh_size;	/* section size */
	Elf32_Word	sh_link;	/* section header table index link */
	Elf32_Word	sh_info;	/* extra information */
	Elf32_Word	sh_addralign;	/* address alignment */
	Elf32_Word	sh_entsize;	/* section entry size */
} Elf32_Shdr;

typedef struct {
	Elf64_Word	sh_name;	/* section name */
	Elf64_Word	sh_type;	/* section type */
	Elf64_Xword	sh_flags;	/* section flags */
	Elf64_Addr	sh_addr;	/* virtual address */
	Elf64_Off	sh_offset;	/* file offset */
	Elf64_Xword	sh_size;	/* section size */
	Elf64_Word	sh_link;	/* link to another */
	Elf64_Word	sh_info;	/* misc info */
	Elf64_Xword	sh_addralign;	/* memory alignment */
	Elf64_Xword	sh_entsize;	/* table entry size */
} Elf64_Shdr;

/* Special Section Indexes */
#define SHN_UNDEF	0		/* undefined */
#define SHN_LORESERVE	0xff00		/* lower bounds of reserved indexes */
#define SHN_LOPROC	0xff00		/* reserved range for processor */
#define SHN_HIPROC	0xff1f		/*   specific section indexes */
#define SHN_ABS		0xfff1		/* absolute value */
#define SHN_COMMON	0xfff2		/* common symbol */
#define SHN_XINDEX	0xffff		/* Escape -- index stored elsewhere. */
#define SHN_HIRESERVE	0xffff		/* upper bounds of reserved indexes */

/* sh_type */
#define SHT_NULL		0	/* inactive */
#define SHT_PROGBITS		1	/* program defined information */
#define SHT_SYMTAB		2	/* symbol table section */
#define SHT_STRTAB		3	/* string table section */
#define SHT_RELA		4	/* relocation section with addends*/
#define SHT_HASH		5	/* symbol hash table section */
#define SHT_DYNAMIC		6	/* dynamic section */
#define SHT_NOTE		7	/* note section */
#define SHT_NOBITS		8	/* no space section */
#define SHT_REL			9	/* relocation section without addends */
#define SHT_SHLIB		10	/* reserved - purpose unknown */
#define SHT_DYNSYM		11	/* dynamic symbol table section */
#define SHT_NUM			12	/* number of section types */
#define SHT_INIT_ARRAY		14	/* pointers to init functions */
#define SHT_FINI_ARRAY		15	/* pointers to termination functions */
#define SHT_PREINIT_ARRAY	16	/* ptrs to funcs called before init */
#define SHT_GROUP		17	/* defines a section group */
#define SHT_SYMTAB_SHNDX	18	/* Section indexes (see SHN_XINDEX). */
#define SHT_RELR		19	/* relative-only relocation section */
#define SHT_LOOS	0x60000000	/* reserved range for OS specific */
#define SHT_SUNW_dof	0x6ffffff4	/* used by dtrace */
#define SHT_GNU_LIBLIST	0x6ffffff7	/* libraries to be prelinked */
#define SHT_SUNW_move	0x6ffffffa	/* inf for partially init'ed symbols */
#define SHT_SUNW_syminfo	0x6ffffffc	/* ad symbol information */ 
#define SHT_SUNW_verdef		0x6ffffffd	/* symbol versioning inf */
#define SHT_SUNW_verneed	0x6ffffffe	/* symbol versioning req */
#define SHT_SUNW_versym		0x6fffffff	/* symbol versioning table */
#define SHT_HIOS	0x6fffffff	/*  section header types */
#define SHT_LOPROC	0x70000000	/* reserved range for processor */
#define SHT_HIPROC	0x7fffffff	/*  specific section header types */
#define SHT_LOUSER	0x80000000	/* reserved range for application */
#define SHT_HIUSER	0xffffffff	/*  specific indexes */

#define SHT_GNU_HASH	0x6ffffff6	/* GNU-style hash table section */

/* Section names */
#define ELF_BSS         ".bss"		/* uninitialized data */
#define ELF_DATA        ".data"		/* initialized data */
#define ELF_CTF		".SUNW_ctf"	/* CTF data */
#define ELF_DEBUG       ".debug"	/* debug */
#define ELF_DYNAMIC     ".dynamic"	/* dynamic linking information */
#define ELF_DYNSTR      ".dynstr"	/* dynamic string table */
#define ELF_DYNSYM      ".dynsym"	/* dynamic symbol table */
#define ELF_FINI        ".fini"		/* termination code */
#define ELF_GOT         ".got"		/* global offset table */
#define ELF_HASH        ".hash"		/* symbol hash table */
#define ELF_INIT        ".init"		/* initialization code */
#define ELF_REL_DATA    ".rel.data"	/* relocation data */
#define ELF_REL_FINI    ".rel.fini"	/* relocation termination code */
#define ELF_REL_INIT    ".rel.init"	/* relocation initialization code */
#define ELF_REL_DYN     ".rel.dyn"	/* relocation dynamic link info */
#define ELF_REL_RODATA  ".rel.rodata"	/* relocation read-only data */
#define ELF_REL_TEXT    ".rel.text"	/* relocation code */
#define ELF_RODATA      ".rodata"	/* read-only data */
#define ELF_SHSTRTAB    ".shstrtab"	/* section header string table */
#define ELF_STRTAB      ".strtab"	/* string table */
#define ELF_SYMTAB      ".symtab"	/* symbol table */
#define ELF_TEXT        ".text"		/* code */
#define ELF_OPENBSDRANDOMDATA ".openbsd.randomdata" /* constant randomdata */
#define ELF_OPENBSDMUTABLE ".openbsd.mutable" /* mutable bss */


/* Section Attribute Flags - sh_flags */
#define SHF_WRITE		0x1	/* Writable */
#define SHF_ALLOC		0x2	/* occupies memory */
#define SHF_EXECINSTR		0x4	/* executable */
#define SHF_MERGE		0x10	/* may be merged */
#define SHF_STRINGS		0x20	/* contains strings */
#define SHF_INFO_LINK		0x40	/* sh_info holds section index */
#define SHF_LINK_ORDER		0x80	/* ordering requirements */
#define SHF_OS_NONCONFORMING	0x100	/* OS-specific processing required */
#define SHF_GROUP		0x200	/* member of section group */
#define SHF_TLS			0x400	/* thread local storage */
#define SHF_COMPRESSED		0x800	/* contains compressed data */
#define SHF_MASKOS	0x0ff00000	/* OS-specific semantics */
#define SHF_MASKPROC	0xf0000000	/* reserved bits for processor */
					/*  specific section attributes */

/* Symbol Table Entry */
typedef struct elf32_sym {
	Elf32_Word	st_name;	/* name - index into string table */
	Elf32_Addr	st_value;	/* symbol value */
	Elf32_Word	st_size;	/* symbol size */
	unsigned char	st_info;	/* type and binding */
	unsigned char	st_other;	/* 0 - no defined meaning */
	Elf32_Half	st_shndx;	/* section header index */
} Elf32_Sym;

typedef struct {
	Elf64_Word	st_name;	/* Symbol name index in str table */
	unsigned char	st_info;	/* type / binding attrs */
	unsigned char	st_other;	/* unused */
	Elf64_Half	st_shndx;	/* section index of symbol */
	Elf64_Addr	st_value;	/* value of symbol */
	Elf64_Xword	st_size;	/* size of symbol */
} Elf64_Sym;

/* Symbol table index */
#define STN_UNDEF	0		/* undefined */

/* Extract symbol info - st_info */
#define ELF32_ST_BIND(x)	((x) >> 4)
#define ELF32_ST_TYPE(x)	(((unsigned int) x) & 0xf)
#define ELF32_ST_INFO(b,t)	(((b) << 4) + ((t) & 0xf))

#define ELF64_ST_BIND(x)	((x) >> 4)
#define ELF64_ST_TYPE(x)	(((unsigned int) x) & 0xf)
#define ELF64_ST_INFO(b,t)	(((b) << 4) + ((t) & 0xf))

/* Symbol Binding - ELF32_ST_BIND - st_info */
#define STB_LOCAL	0		/* Local symbol */
#define STB_GLOBAL	1		/* Global symbol */
#define STB_WEAK	2		/* like global - lower precedence */
#define STB_NUM		3		/* number of symbol bindings */
#define STB_LOPROC	13		/* reserved range for processor */
#define STB_HIPROC	15		/*  specific symbol bindings */

/* Symbol type - ELF32_ST_TYPE - st_info */
#define STT_NOTYPE	0		/* not specified */
#define STT_OBJECT	1		/* data object */
#define STT_FUNC	2		/* function */
#define STT_SECTION	3		/* section */
#define STT_FILE	4		/* file */
#define STT_TLS		6		/* thread local storage */
#define STT_LOPROC	13		/* reserved range for processor */
#define STT_HIPROC	15		/*  specific symbol types */

/* Extract symbol visibility - st_other */
#define ELF_ST_VISIBILITY(v)		((v) & 0x3)
#define ELF32_ST_VISIBILITY		ELF_ST_VISIBILITY
#define ELF64_ST_VISIBILITY		ELF_ST_VISIBILITY

#define STV_DEFAULT	0		/* Visibility set by binding type */
#define STV_INTERNAL	1		/* OS specific version of STV_HIDDEN */
#define STV_HIDDEN	2		/* can only be seen inside own .so */
#define STV_PROTECTED	3		/* HIDDEN inside, DEFAULT outside */

/* Relocation entry with implicit addend */
typedef struct {
	Elf32_Addr	r_offset;	/* offset of relocation */
	Elf32_Word	r_info;		/* symbol table index and type */
} Elf32_Rel;

/* Relocation entry with explicit addend */
typedef struct {
	Elf32_Addr	r_offset;	/* offset of relocation */
	Elf32_Word	r_info;		/* symbol table index and type */
	Elf32_Sword	r_addend;
} Elf32_Rela;

/* Extract relocation info - r_info */
#define ELF32_R_SYM(i)		((i) >> 8)
#define ELF32_R_TYPE(i)		((unsigned char) (i))
#define ELF32_R_INFO(s,t) 	(((s) << 8) + (unsigned char)(t))

typedef struct {
	Elf64_Addr	r_offset;	/* where to do it */
	Elf64_Xword	r_info;		/* index & type of relocation */
} Elf64_Rel;

typedef struct {
	Elf64_Addr	r_offset;	/* where to do it */
	Elf64_Xword	r_info;		/* index & type of relocation */
	Elf64_Sxword	r_addend;	/* adjustment value */
} Elf64_Rela;

#define	ELF64_R_SYM(info)	((info) >> 32)
#define	ELF64_R_TYPE(info)	((info) & 0xFFFFFFFF)
#define ELF64_R_INFO(s,t) 	(((s) << 32) + (__uint32_t)(t))

#if defined(__mips64__) && defined(__MIPSEL__)
/*
 * The 64-bit MIPS ELF ABI uses a slightly different relocation format
 * than the regular ELF ABI: the r_info field is split into several
 * pieces (see gnu/usr.bin/binutils-2.17/include/elf/mips.h for details).
 */
#undef	ELF64_R_SYM
#undef	ELF64_R_TYPE
#undef	ELF64_R_INFO
#define	ELF64_R_TYPE(info)	((__uint64_t)swap32((info) >> 32))
#define	ELF64_R_SYM(info)	((info) & 0xFFFFFFFF)
#define	ELF64_R_INFO(s,t)	(((__uint64_t)swap32(t) << 32) + (__uint32_t)(s))
#endif	/* __mips64__ && __MIPSEL__ */

/*
 * Relative Relocation info.
 * c.f. decode_relrs() in gnu/llvm/llvm/lib/Object/ELF.cpp
 */
typedef Elf32_Word	Elf32_Relr;
typedef Elf64_Xword	Elf64_Relr;

/* Program Header */
typedef struct {
	Elf32_Word	p_type;		/* segment type */
	Elf32_Off	p_offset;	/* segment offset */
	Elf32_Addr	p_vaddr;	/* virtual address of segment */
	Elf32_Addr	p_paddr;	/* physical address - ignored? */
	Elf32_Word	p_filesz;	/* number of bytes in file for seg. */
	Elf32_Word	p_memsz;	/* number of bytes in mem. for seg. */
	Elf32_Word	p_flags;	/* flags */
	Elf32_Word	p_align;	/* memory alignment */
} Elf32_Phdr;

typedef struct {
	Elf64_Word	p_type;		/* entry type */
	Elf64_Word	p_flags;	/* flags */
	Elf64_Off	p_offset;	/* offset */
	Elf64_Addr	p_vaddr;	/* virtual address */
	Elf64_Addr	p_paddr;	/* physical address */
	Elf64_Xword	p_filesz;	/* file size */
	Elf64_Xword	p_memsz;	/* memory size */
	Elf64_Xword	p_align;	/* memory & file alignment */
} Elf64_Phdr;

/* Segment types - p_type */
#define PT_NULL		0		/* unused */
#define PT_LOAD		1		/* loadable segment */
#define PT_DYNAMIC	2		/* dynamic linking section */
#define PT_INTERP	3		/* the RTLD */
#define PT_NOTE		4		/* auxiliary information */
#define PT_SHLIB	5		/* reserved - purpose undefined */
#define PT_PHDR		6		/* program header */
#define PT_TLS		7		/* thread local storage */
#define PT_LOOS		0x60000000	/* reserved range for OS */
#define PT_HIOS		0x6fffffff	/*  specific segment types */
#define PT_LOPROC	0x70000000	/* reserved range for processor */
#define PT_HIPROC	0x7fffffff	/*  specific segment types */

#define PT_GNU_EH_FRAME		0x6474e550	/* Exception handling info */
#define PT_GNU_RELRO		0x6474e552	/* Read-only after relocation */
#define PT_GNU_PROPERTY		0x6474e553	/* Program property note */ 

#define PT_OPENBSD_MUTABLE	0x65a3dbe5	/* like bss, but not immutable */
#define PT_OPENBSD_RANDOMIZE	0x65a3dbe6	/* fill with random data */
#define PT_OPENBSD_WXNEEDED	0x65a3dbe7	/* program performs W^X violations */
#define PT_OPENBSD_NOBTCFI	0x65a3dbe8	/* no branch target CFI */
#define PT_OPENBSD_SYSCALLS	0x65a3dbe9	/* syscall locations */
#define PT_OPENBSD_BOOTDATA	0x65a41be6	/* section for boot arguments */

/* Segment flags - p_flags */
#define PF_X		0x1		/* Executable */
#define PF_W		0x2		/* Writable */
#define PF_R		0x4		/* Readable */
#define PF_MASKOS	0x0ff00000	/* reserved bits for OS */
					/*  specific segment flags */
#define PF_MASKPROC	0xf0000000	/* reserved bits for processor */
					/*  specific segment flags */

#define PF_OPENBSD_MUTABLE	0x08000000	/* Mutable */

#ifdef	_KERNEL
#define PF_ISVNODE	0x00100000	/* For coredump segments */
#endif

/* Dynamic structure */
typedef struct {
	Elf32_Sword	d_tag;		/* controls meaning of d_val */
	union {
		Elf32_Word	d_val;	/* Multiple meanings - see d_tag */
		Elf32_Addr	d_ptr;	/* program virtual address */
	} d_un;
} Elf32_Dyn;

typedef struct {
	Elf64_Xword	d_tag;		/* controls meaning of d_val */
	union {
		Elf64_Addr	d_ptr;
		Elf64_Xword	d_val;
	} d_un;
} Elf64_Dyn;

/* Dynamic Array Tags - d_tag */
#define DT_NULL		0		/* marks end of _DYNAMIC array */
#define DT_NEEDED	1		/* string table offset of needed lib */
#define DT_PLTRELSZ	2		/* size of relocation entries in PLT */
#define DT_PLTGOT	3		/* address PLT/GOT */
#define DT_HASH		4		/* address of symbol hash table */
#define DT_STRTAB	5		/* address of string table */
#define DT_SYMTAB	6		/* address of symbol table */
#define DT_RELA		7		/* address of relocation table */
#define DT_RELASZ	8		/* size of relocation table */
#define DT_RELAENT	9		/* size of relocation entry */
#define DT_STRSZ	10		/* size of string table */
#define DT_SYMENT	11		/* size of symbol table entry */
#define DT_INIT		12		/* address of initialization func. */
#define DT_FINI		13		/* address of termination function */
#define DT_SONAME	14		/* string table offset of shared obj */
#define DT_RPATH	15		/* string table offset of library
					   search path */
#define DT_SYMBOLIC	16		/* start sym search in shared obj. */
#define DT_REL		17		/* address of rel. tbl. w addends */
#define DT_RELSZ	18		/* size of DT_REL relocation table */
#define DT_RELENT	19		/* size of DT_REL relocation entry */
#define DT_PLTREL	20		/* PLT referenced relocation entry */
#define DT_DEBUG	21		/* bugger */
#define DT_TEXTREL	22		/* Allow rel. mod. to unwritable seg */
#define DT_JMPREL	23		/* add. of PLT's relocation entries */
#define DT_BIND_NOW	24		/* Bind now regardless of env setting */
#define DT_INIT_ARRAY	25		/* address of array of init func */
#define DT_FINI_ARRAY	26		/* address of array of term func */
#define DT_INIT_ARRAYSZ	27		/* size of array of init func */
#define DT_FINI_ARRAYSZ	28		/* size of array of term func */
#define DT_RUNPATH	29		/* strtab offset of lib search path */
#define DT_FLAGS	30		/* Set of DF_* flags */
#define DT_ENCODING	31		/* further DT_* follow encoding rules */
#define DT_PREINIT_ARRAY	32	/* address of array of preinit func */
#define DT_PREINIT_ARRAYSZ	33	/* size of array of preinit func */
#define DT_RELRSZ	35		/* size of DT_RELR relocation table */
#define DT_RELR		36		/* addr of DT_RELR relocation table */
#define DT_RELRENT	37		/* size of DT_RELR relocation entry */
#define DT_LOOS		0x6000000d	/* reserved range for OS */
#define DT_HIOS		0x6ffff000	/*  specific dynamic array tags */
#define DT_LOPROC	0x70000000	/* reserved range for processor */
#define DT_HIPROC	0x7fffffff	/*  specific dynamic array tags */

/* some other useful tags */
#define DT_GNU_HASH	0x6ffffef5	/* address of GNU hash table */
#define DT_RELACOUNT	0x6ffffff9	/* if present, number of RELATIVE */
#define DT_RELCOUNT	0x6ffffffa	/* relocs, which must come first */
#define DT_FLAGS_1      0x6ffffffb

/* Dynamic Flags - DT_FLAGS .dynamic entry */
#define DF_ORIGIN       0x00000001
#define DF_SYMBOLIC     0x00000002
#define DF_TEXTREL      0x00000004
#define DF_BIND_NOW     0x00000008
#define DF_STATIC_TLS   0x00000010

/* Dynamic Flags - DT_FLAGS_1 .dynamic entry */
#define DF_1_NOW	0x00000001
#define DF_1_GLOBAL	0x00000002
#define DF_1_GROUP	0x00000004
#define DF_1_NODELETE	0x00000008
#define DF_1_LOADFLTR	0x00000010
#define DF_1_INITFIRST	0x00000020
#define DF_1_NOOPEN	0x00000040
#define DF_1_ORIGIN	0x00000080
#define DF_1_DIRECT	0x00000100
#define DF_1_TRANS	0x00000200
#define DF_1_INTERPOSE	0x00000400
#define DF_1_NODEFLIB	0x00000800
#define DF_1_NODUMP	0x00001000
#define DF_1_CONLFAT	0x00002000
#define DF_1_ENDFILTEE	0x00004000
#define DF_1_DISPRELDNE	0x00008000
#define DF_1_DISPRELPND	0x00010000
#define DF_1_NODIRECT	0x00020000
#define DF_1_IGNMULDEF	0x00040000
#define DF_1_NOKSYMS	0x00080000
#define DF_1_NOHDR	0x00100000
#define DF_1_EDITED	0x00200000
#define DF_1_NORELOC	0x00400000
#define DF_1_SYMINTPOSE	0x00800000
#define DF_1_GLOBAUDIT	0x01000000
#define DF_1_SINGLETON	0x02000000
#define DF_1_PIE	0x08000000

/*
 * Note header
 */
typedef struct {
	Elf32_Word n_namesz;
	Elf32_Word n_descsz;
	Elf32_Word n_type;
} Elf32_Nhdr;

typedef struct {
	Elf64_Word n_namesz;
	Elf64_Word n_descsz;
	Elf64_Word n_type;
} Elf64_Nhdr;

/*
 * Note Definitions
 */
typedef struct {
	Elf32_Word namesz;
	Elf32_Word descsz;
	Elf32_Word type;
} Elf32_Note;

typedef struct {
	Elf64_Word namesz;
	Elf64_Word descsz;
	Elf64_Word type;
} Elf64_Note;

/* Values for n_type. */
#define NT_PRSTATUS		1	/* Process status. */
#define NT_FPREGSET		2	/* Floating point registers. */
#define NT_PRPSINFO		3	/* Process state info. */

/*
 * OpenBSD-specific core file information.
 *
 * OpenBSD ELF core files use notes to provide information about
 * the process's state.  The note name is "OpenBSD" for information
 * that is global to the process, and "OpenBSD@nn", where "nn" is the
 * thread ID of the thread that the information belongs to (such as
 * register state).
 *
 * We use the following note identifiers:
 *
 *	NT_OPENBSD_PROCINFO
 *		Note is a "elfcore_procinfo" structure.
 *	NT_OPENBSD_AUXV
 *		Note is a bunch of Auxiliary Vectors, terminated by
 *		an AT_NULL entry.
 *	NT_OPENBSD_REGS
 *		Note is a "reg" structure.
 *	NT_OPENBSD_FPREGS
 *		Note is a "fpreg" structure.
 *
 * Please try to keep the members of the "elfcore_procinfo" structure
 * nicely aligned, and if you add elements, add them to the end and
 * bump the version.
 */

#define NT_OPENBSD_PROF		2

#define NT_OPENBSD_PROCINFO	10
#define NT_OPENBSD_AUXV		11

#define NT_OPENBSD_REGS		20
#define NT_OPENBSD_FPREGS	21
#define NT_OPENBSD_XFPREGS	22
#define NT_OPENBSD_WCOOKIE	23
#define NT_OPENBSD_PACMASK	24

struct elfcore_procinfo {
	/* Version 1 fields start here. */
	uint32_t	cpi_version;	/* elfcore_procinfo version */
#define ELFCORE_PROCINFO_VERSION	1
	uint32_t	cpi_cpisize;	/* sizeof(elfcore_procinfo) */
	uint32_t	cpi_signo;	/* killing signal */
	uint32_t	cpi_sigcode;	/* signal code */
	uint32_t	cpi_sigpend;	/* pending signals */
	uint32_t	cpi_sigmask;	/* blocked signals */
	uint32_t	cpi_sigignore;	/* ignored signals */
	uint32_t	cpi_sigcatch;	/* signals being caught by user */
	int32_t		cpi_pid;	/* process ID */
	int32_t		cpi_ppid;	/* parent process ID */
	int32_t		cpi_pgrp;	/* process group ID */
	int32_t		cpi_sid;	/* session ID */
	uint32_t	cpi_ruid;	/* real user ID */
	uint32_t	cpi_euid;	/* effective user ID */
	uint32_t	cpi_svuid;	/* saved user ID */
	uint32_t	cpi_rgid;	/* real group ID */
	uint32_t	cpi_egid;	/* effective group ID */
	uint32_t	cpi_svgid;	/* saved group ID */
	int8_t		cpi_name[32];	/* copy of pr->ps_comm */
};

/*
 * XXX - these _KERNEL items aren't part of the ABI!
 */
#if defined(_KERNEL) || defined(_DYN_LOADER)

#define ELF32_NO_ADDR	((uint32_t) ~0)	/* Indicates addr. not yet filled in */

typedef struct {
	Elf32_Sword	au_id;				/* 32-bit id */
	Elf32_Word	au_v;				/* 32-bit value */
} Aux32Info;

#define ELF64_NO_ADDR	((__uint64_t) ~0)/* Indicates addr. not yet filled in */

typedef struct {
	Elf64_Shalf	au_id;				/* 32-bit id */
	Elf64_Xword	au_v;				/* 64-bit value */
} Aux64Info;

enum AuxID {
	AUX_null = 0,
	AUX_ignore = 1,
	AUX_execfd = 2,
	AUX_phdr = 3,			/* &phdr[0] */
	AUX_phent = 4,			/* sizeof(phdr[0]) */
	AUX_phnum = 5,			/* # phdr entries */
	AUX_pagesz = 6,			/* PAGESIZE */
	AUX_base = 7,			/* base addr for ld.so or static PIE */
	AUX_flags = 8,			/* processor flags */
	AUX_entry = 9,			/* a.out entry */
	AUX_hwcap = 25,			/* processor flags */
	AUX_hwcap2 = 26,		/* processor flags (continued) */
	AUX_sun_uid = 2000,		/* euid */
	AUX_sun_ruid = 2001,		/* ruid */
	AUX_sun_gid = 2002,		/* egid */
	AUX_sun_rgid = 2003,		/* rgid */
	AUX_openbsd_timekeep = 4000,	/* userland clock_gettime */
};

struct elf_args {
        u_long  arg_entry;		/* program entry point */
        u_long  arg_interp;		/* Interpreter load address */
        u_long  arg_phaddr;		/* program header address */
        u_long  arg_phentsize;		/* Size of program header */
        u_long  arg_phnum;		/* Number of program headers */
};

#endif

#if !defined(ELFSIZE) && defined(ARCH_ELFSIZE)
#define ELFSIZE ARCH_ELFSIZE
#endif

#if defined(ELFSIZE)
#define CONCAT(x,y)	__CONCAT(x,y)
#define ELFNAME(x)	CONCAT(elf,CONCAT(ELFSIZE,CONCAT(_,x)))
#define ELFDEFNNAME(x)	CONCAT(ELF,CONCAT(ELFSIZE,CONCAT(_,x)))
#endif

#if defined(ELFSIZE) && (ELFSIZE == 32)
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define Elf_Rel		Elf32_Rel
#define Elf_RelA	Elf32_Rela
#define Elf_Relr	Elf32_Relr
#define Elf_Dyn		Elf32_Dyn
#define Elf_Half	Elf32_Half
#define Elf_Word	Elf32_Word
#define Elf_Sword	Elf32_Sword
#define Elf_Addr	Elf32_Addr
#define Elf_Off		Elf32_Off
#define Elf_Nhdr	Elf32_Nhdr
#define Elf_Note	Elf32_Note

#define ELF_R_SYM	ELF32_R_SYM
#define ELF_R_TYPE	ELF32_R_TYPE
#define ELF_R_INFO	ELF32_R_INFO
#define ELFCLASS	ELFCLASS32

#define ELF_ST_BIND	ELF32_ST_BIND
#define ELF_ST_TYPE	ELF32_ST_TYPE
#define ELF_ST_INFO	ELF32_ST_INFO

#define ELF_NO_ADDR	ELF32_NO_ADDR
#define AuxInfo		Aux32Info
#elif defined(ELFSIZE) && (ELFSIZE == 64)
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Phdr	Elf64_Phdr
#define Elf_Shdr	Elf64_Shdr
#define Elf_Sym		Elf64_Sym
#define Elf_Rel		Elf64_Rel
#define Elf_RelA	Elf64_Rela
#define Elf_Relr	Elf64_Relr
#define Elf_Dyn		Elf64_Dyn
#define Elf_Half	Elf64_Half
#define Elf_Word	Elf64_Word
#define Elf_Sword	Elf64_Sword
#define Elf_Addr	Elf64_Addr
#define Elf_Off		Elf64_Off
#define Elf_Nhdr	Elf64_Nhdr
#define Elf_Note	Elf64_Note

#define ELF_R_SYM	ELF64_R_SYM
#define ELF_R_TYPE	ELF64_R_TYPE
#define ELF_R_INFO	ELF64_R_INFO
#define ELFCLASS	ELFCLASS64

#define ELF_ST_BIND	ELF64_ST_BIND
#define ELF_ST_TYPE	ELF64_ST_TYPE
#define ELF_ST_INFO	ELF64_ST_INFO

#define ELF_NO_ADDR	ELF64_NO_ADDR
#define AuxInfo		Aux64Info
#endif

#ifndef _KERNEL
extern Elf_Dyn		_DYNAMIC[];
#endif

#ifdef	_KERNEL
/*
 * How many entries are in the AuxInfo array we pass to the process?
 */
#define	ELF_AUX_ENTRIES	11
#define	ELF_AUX_WORDS	(sizeof(AuxInfo) * ELF_AUX_ENTRIES / sizeof(char *))

#define	ELFROUNDSIZE	sizeof(Elf_Word)
#define	elfround(x)	roundup((x), ELFROUNDSIZE)

struct exec_package;

int	exec_elf_makecmds(struct proc *, struct exec_package *);
int	exec_elf_fixup(struct proc *, struct exec_package *);
int	coredump_elf(struct proc *, void *);
int	coredump_note_elf_md(struct proc *, void *, const char *, size_t *);
int	coredump_writenote_elf(struct proc *, void *, Elf_Note *,
	    const char *, void *);
#endif /* _KERNEL */

#define ELF_TARG_VER	1	/* The ver for which this code is intended */

#endif /* _SYS_EXEC_ELF_H_ */
