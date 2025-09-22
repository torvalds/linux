/*-
 * Copyright (c) 2006,2008-2010 Joseph Koshy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: libelf.h,v 1.1 2019/02/01 05:27:38 jsg Exp $
 */

#ifndef	_LIBELF_H_
#define	_LIBELF_H_

#include <sys/types.h>

#include <sys/exec_elf.h>

/* Non-standard public data structures */

/* Capability */
typedef struct {
	Elf32_Word	c_tag;
	union {
		Elf32_Word	c_val;
		Elf32_Addr	c_ptr;
	} c_un;
} Elf32_Cap;

typedef struct {
	Elf64_Xword	c_tag;
	union {
		Elf64_Xword	c_val;
		Elf64_Addr	c_ptr;
	} c_un;
} Elf64_Cap;

/* Move */
typedef struct {
	Elf32_Lword	m_value;
	Elf32_Word	m_info;
	Elf32_Word	m_poffset;
	Elf32_Half	m_repeat;
	Elf32_Half	m_stride;
} Elf32_Move;

typedef struct {
	Elf64_Lword	m_value;
	Elf64_Xword	m_info;
	Elf64_Xword	m_poffset;
	Elf64_Half	m_repeat;
	Elf64_Half	m_stride;
} Elf64_Move;

/* Syminfo Table */
typedef struct {
	Elf32_Half	si_boundto;
	Elf32_Half	si_flags;
} Elf32_Syminfo;

typedef struct {
	Elf64_Half	si_boundto;
	Elf64_Half	si_flags;
} Elf64_Syminfo;

/* Version Definition */
typedef struct {
	Elf32_Half	vd_version;
	Elf32_Half	vd_flags;
	Elf32_Half	vd_ndx;
	Elf32_Half	vd_cnt;
	Elf32_Word	vd_hash;
	Elf32_Word	vd_aux;
	Elf32_Word	vd_next;
} Elf32_Verdef;

typedef struct {
	Elf32_Word	vda_name;
	Elf32_Word	vda_next;
} Elf32_Verdaux;

typedef struct {
	Elf64_Half	vd_version;
	Elf64_Half	vd_flags;
	Elf64_Half	vd_ndx;
	Elf64_Half	vd_cnt;
	Elf64_Word	vd_hash;
	Elf64_Word	vd_aux;
	Elf64_Word	vd_next;
} Elf64_Verdef;

typedef struct {
	Elf64_Word	vda_name;
	Elf64_Word	vda_next;
} Elf64_Verdaux;

/* Version Dependency */

typedef struct {
	Elf32_Half	vn_version;
	Elf32_Half	vn_cnt;
	Elf32_Word	vn_file;
	Elf32_Word	vn_aux;
	Elf32_Word	vn_next;
} Elf32_Verneed;

typedef struct {
	Elf32_Word	vna_hash;
	Elf32_Half	vna_flags;
	Elf32_Half	vna_other;
	Elf32_Word	vna_name;
	Elf32_Word	vna_next;
} Elf32_Vernaux;

typedef struct {
        Elf64_Half	vn_version;
        Elf64_Half	vn_cnt;
        Elf64_Word	vn_file;
        Elf64_Word	vn_aux;
        Elf64_Word	vn_next;
} Elf64_Verneed;

typedef struct {
	Elf64_Word	vna_hash;
	Elf64_Half	vna_flags;
	Elf64_Half	vna_other;
	Elf64_Word	vna_name;
	Elf64_Word	vna_next;
} Elf64_Vernaux;

/* GNU-style hash sections */
typedef struct {
	uint32_t	gh_nbuckets;	/* Number of hash buckets. */
	uint32_t	gh_symndx;	/* First visible symbol in .dynsym. */
	uint32_t	gh_maskwords;	/* #maskwords used in bloom filter. */
	uint32_t	gh_shift2;	/* Bloom filter shift count. */
} Elf_GNU_Hash_Header;

/* Library private data structures */
typedef struct _Elf Elf;
typedef struct _Elf_Scn Elf_Scn;

/* File types */
typedef enum {
	ELF_K_NONE = 0,
	ELF_K_AR,	/* `ar' archives */
	ELF_K_COFF,	/* COFF files (unsupported) */
	ELF_K_ELF,	/* ELF files */
	ELF_K_NUM
} Elf_Kind;

#define	ELF_K_FIRST	ELF_K_NONE
#define	ELF_K_LAST	ELF_K_NUM

/* Data types */
typedef enum {
	ELF_T_ADDR,
	ELF_T_BYTE,
	ELF_T_CAP,
	ELF_T_DYN,
	ELF_T_EHDR,
	ELF_T_HALF,
	ELF_T_LWORD,
	ELF_T_MOVE,
	ELF_T_MOVEP,
	ELF_T_NOTE,
	ELF_T_OFF,
	ELF_T_PHDR,
	ELF_T_REL,
	ELF_T_RELA,
	ELF_T_SHDR,
	ELF_T_SWORD,
	ELF_T_SXWORD,
	ELF_T_SYMINFO,
	ELF_T_SYM,
	ELF_T_VDEF,
	ELF_T_VNEED,
	ELF_T_WORD,
	ELF_T_XWORD,
	ELF_T_GNUHASH,	/* GNU style hash tables. */
	ELF_T_NUM
} Elf_Type;

#define	ELF_T_FIRST	ELF_T_ADDR
#define	ELF_T_LAST	ELF_T_GNUHASH

/* Commands */
typedef enum {
	ELF_C_NULL = 0,
	ELF_C_CLR,
	ELF_C_FDDONE,
	ELF_C_FDREAD,
	ELF_C_RDWR,
	ELF_C_READ,
	ELF_C_SET,
	ELF_C_WRITE,
	ELF_C_NUM
} Elf_Cmd;

#define	ELF_C_FIRST	ELF_C_NULL
#define	ELF_C_LAST	ELF_C_NUM

/*
 * An `Elf_Data' structure describes data in an
 * ELF section.
 */
typedef struct _Elf_Data {
	/*
	 * `Public' members that are part of the ELF(3) API.
	 */
	uint64_t	d_align;
	void		*d_buf;
	uint64_t	d_off;
	uint64_t	d_size;
	Elf_Type	d_type;
	unsigned int	d_version;
} Elf_Data;

/*
 * An `Elf_Arhdr' structure describes an archive
 * header.
 */
typedef struct {
	time_t		ar_date;
	char		*ar_name;	/* archive member name */
	gid_t		ar_gid;
	mode_t		ar_mode;
	char		*ar_rawname;	/* 'raw' member name */
	size_t		ar_size;
	uid_t		ar_uid;

	/*
	 * Members that are not part of the public API.
	 */
	unsigned int	ar_flags;
} Elf_Arhdr;

/*
 * An `Elf_Arsym' describes an entry in the archive
 * symbol table.
 */
typedef struct {
	off_t		as_off;		/* byte offset to member's header */
	unsigned long	as_hash;	/* elf_hash() value for name */
	char		*as_name; 	/* null terminated symbol name */
} Elf_Arsym;

/*
 * Error numbers.
 */

enum Elf_Error {
	ELF_E_NONE,	/* No error */
	ELF_E_ARCHIVE,	/* Malformed ar(1) archive */
	ELF_E_ARGUMENT,	/* Invalid argument */
	ELF_E_CLASS,	/* Mismatched ELF class */
	ELF_E_DATA,	/* Invalid data descriptor */
	ELF_E_HEADER,	/* Missing or malformed ELF header */
	ELF_E_IO,	/* I/O error */
	ELF_E_LAYOUT,	/* Layout constraint violation */
	ELF_E_MODE,	/* Wrong mode for ELF descriptor */
	ELF_E_RANGE,	/* Value out of range */
	ELF_E_RESOURCE,	/* Resource exhaustion */
	ELF_E_SECTION,	/* Invalid section descriptor */
	ELF_E_SEQUENCE,	/* API calls out of sequence */
	ELF_E_UNIMPL,	/* Feature is unimplemented */
	ELF_E_VERSION,	/* Unknown API version */
	ELF_E_NUM	/* Max error number */
};

/*
 * Flags defined by the API.
 */

#define	ELF_F_LAYOUT	0x001U	/* application will layout the file */
#define	ELF_F_DIRTY	0x002U	/* a section or ELF file is dirty */

/* ELF(3) API extensions. */
#define	ELF_F_ARCHIVE	   0x100U /* archive creation */
#define	ELF_F_ARCHIVE_SYSV 0x200U /* SYSV style archive */

#ifdef __cplusplus
extern "C" {
#endif
Elf		*elf_begin(int _fd, Elf_Cmd _cmd, Elf *_elf);
int		elf_cntl(Elf *_elf, Elf_Cmd _cmd);
int		elf_end(Elf *_elf);
const char	*elf_errmsg(int _error);
int		elf_errno(void);
void		elf_fill(int _fill);
unsigned int	elf_flagarhdr(Elf_Arhdr *_arh, Elf_Cmd _cmd,
			unsigned int _flags);
unsigned int	elf_flagdata(Elf_Data *_data, Elf_Cmd _cmd,
			unsigned int _flags);
unsigned int	elf_flagehdr(Elf *_elf, Elf_Cmd _cmd, unsigned int _flags);
unsigned int	elf_flagelf(Elf *_elf, Elf_Cmd _cmd, unsigned int _flags);
unsigned int	elf_flagphdr(Elf *_elf, Elf_Cmd _cmd, unsigned int _flags);
unsigned int	elf_flagscn(Elf_Scn *_scn, Elf_Cmd _cmd, unsigned int _flags);
unsigned int	elf_flagshdr(Elf_Scn *_scn, Elf_Cmd _cmd, unsigned int _flags);
Elf_Arhdr	*elf_getarhdr(Elf *_elf);
Elf_Arsym	*elf_getarsym(Elf *_elf, size_t *_ptr);
off_t		elf_getbase(Elf *_elf);
Elf_Data	*elf_getdata(Elf_Scn *, Elf_Data *);
char		*elf_getident(Elf *_elf, size_t *_ptr);
int		elf_getphdrnum(Elf *_elf, size_t *_dst);
int		elf_getphnum(Elf *_elf, size_t *_dst);	/* Deprecated */
Elf_Scn		*elf_getscn(Elf *_elf, size_t _index);
int		elf_getshdrnum(Elf *_elf, size_t *_dst);
int		elf_getshnum(Elf *_elf, size_t *_dst);	/* Deprecated */
int		elf_getshdrstrndx(Elf *_elf, size_t *_dst);
int		elf_getshstrndx(Elf *_elf, size_t *_dst); /* Deprecated */
unsigned long	elf_hash(const char *_name);
Elf_Kind	elf_kind(Elf *_elf);
Elf		*elf_memory(char *_image, size_t _size);
size_t		elf_ndxscn(Elf_Scn *_scn);
Elf_Data	*elf_newdata(Elf_Scn *_scn);
Elf_Scn		*elf_newscn(Elf *_elf);
Elf_Scn		*elf_nextscn(Elf *_elf, Elf_Scn *_scn);
Elf_Cmd		elf_next(Elf *_elf);
Elf		*elf_open(int _fd);
Elf		*elf_openmemory(char *_image, size_t _size);
off_t		elf_rand(Elf *_elf, off_t _off);
Elf_Data	*elf_rawdata(Elf_Scn *_scn, Elf_Data *_data);
char		*elf_rawfile(Elf *_elf, size_t *_size);
int		elf_setshstrndx(Elf *_elf, size_t _shnum);
char		*elf_strptr(Elf *_elf, size_t _section, size_t _offset);
off_t		elf_update(Elf *_elf, Elf_Cmd _cmd);
unsigned int	elf_version(unsigned int _version);

long		elf32_checksum(Elf *_elf);
size_t		elf32_fsize(Elf_Type _type, size_t _count,
			unsigned int _version);
Elf32_Ehdr	*elf32_getehdr(Elf *_elf);
Elf32_Phdr	*elf32_getphdr(Elf *_elf);
Elf32_Shdr	*elf32_getshdr(Elf_Scn *_scn);
Elf32_Ehdr	*elf32_newehdr(Elf *_elf);
Elf32_Phdr	*elf32_newphdr(Elf *_elf, size_t _count);
Elf_Data	*elf32_xlatetof(Elf_Data *_dst, const Elf_Data *_src,
			unsigned int _enc);
Elf_Data	*elf32_xlatetom(Elf_Data *_dst, const Elf_Data *_src,
			unsigned int _enc);

long		elf64_checksum(Elf *_elf);
size_t		elf64_fsize(Elf_Type _type, size_t _count,
			unsigned int _version);
Elf64_Ehdr	*elf64_getehdr(Elf *_elf);
Elf64_Phdr	*elf64_getphdr(Elf *_elf);
Elf64_Shdr	*elf64_getshdr(Elf_Scn *_scn);
Elf64_Ehdr	*elf64_newehdr(Elf *_elf);
Elf64_Phdr	*elf64_newphdr(Elf *_elf, size_t _count);
Elf_Data	*elf64_xlatetof(Elf_Data *_dst, const Elf_Data *_src,
			unsigned int _enc);
Elf_Data	*elf64_xlatetom(Elf_Data *_dst, const Elf_Data *_src,
			unsigned int _enc);
#ifdef __cplusplus
}
#endif

#endif	/* _LIBELF_H_ */
