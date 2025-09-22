/*	$OpenBSD: ctf.h,v 1.5 2017/08/13 14:56:05 nayden Exp $	*/

/*
 * Copyright (c) 2016 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_CTF_H_
#define _SYS_CTF_H_

/*
 * CTF ``Compact ANSI-C Type Format'' ABI header file.
 */

struct ctf_header {
	uint16_t		cth_magic;
	uint8_t			cth_version;
	uint8_t			cth_flags;
	uint32_t		cth_parlabel;
	uint32_t		cth_parname;
	uint32_t		cth_lbloff;
	uint32_t		cth_objtoff;
	uint32_t		cth_funcoff;
	uint32_t		cth_typeoff;
	uint32_t		cth_stroff;
	uint32_t		cth_strlen;
};

#define CTF_F_COMPRESS		(1 << 0)	/* zlib compression */

struct ctf_lblent {
	uint32_t		ctl_label;
	uint32_t		ctl_typeidx;
};

struct ctf_stype {
	uint32_t		cts_name;
	uint16_t		cts_info;
	union {
		uint16_t _size;
		uint16_t _type;
	} _ST;
#define cts_size _ST._size
#define cts_type _ST._type
};

struct ctf_type {
	struct ctf_stype	_ctt_stype;
#define ctt_name _ctt_stype.cts_name
#define ctt_info _ctt_stype.cts_info
#define ctt_size _ctt_stype.cts_size
#define ctt_type _ctt_stype.cts_type
	uint32_t		ctt_lsizehi;
	uint32_t		ctt_lsizelo;
};

struct ctf_array {
	uint16_t		cta_contents;
	uint16_t		cta_index;
	uint32_t		cta_nelems;
};

struct ctf_member {
	uint32_t		ctm_name;
	uint16_t		ctm_type;
	uint16_t		ctm_offset;
};

struct ctf_lmember {
	struct ctf_member	_ctlm_member;
#define ctlm_name _ctlm_member.ctm_name
#define ctlm_type _ctlm_member.ctm_type
#define ctlm_pad0 _ctlm_member.ctm_offset
	uint32_t		ctlm_offsethi;
	uint32_t		ctlm_offsetlo;
};

#define CTF_LSTRUCT_THRESH	8192

struct ctf_enum {
	uint32_t		cte_name;
	int32_t			cte_value;
};

#define CTF_MAGIC		0xcff1
#define CTF_VERSION		2

#define CTF_MAX_NAME		0x7fffffff
#define CTF_MAX_VLEN		0x03ff
#define CTF_MAX_SIZE		0xfffe
#define CTF_LSIZE_SENT		CTF_MAX_SIZE+1	/* sentinel for cts vs ctt */

#define CTF_STRTAB_0		0
#define CTF_STRTAB_1		1

/*
 * Info macro.
 */
#define CTF_INFO_VLEN(i)	(((i) & CTF_MAX_VLEN))
#define CTF_INFO_ISROOT(i)	(((i) & 0x0400) >> 10)
#define CTF_INFO_KIND(i)	(((i) & 0xf800) >> 11)
#define  CTF_K_UNKNOWN		0
#define  CTF_K_INTEGER		1
#define  CTF_K_FLOAT		2
#define  CTF_K_POINTER		3
#define  CTF_K_ARRAY		4
#define  CTF_K_FUNCTION		5
#define  CTF_K_STRUCT		6
#define  CTF_K_UNION		7
#define  CTF_K_ENUM		8
#define  CTF_K_FORWARD		9
#define  CTF_K_TYPEDEF		10
#define  CTF_K_VOLATILE		11
#define  CTF_K_CONST		12
#define  CTF_K_RESTRICT		13
#define  CTF_K_MAX		31

/*
 * Integer/Float Encoding macro.
 */
#define _CTF_ENCODING(e)	(((e) & 0xff000000) >> 24)
#define _CTF_OFFSET(e)		(((e) & 0x00ff0000) >> 16)
#define _CTF_BITS(e)		(((e) & 0x0000ffff))
#define _CTF_DATA(encoding, offset, bits) \
	(((encoding) << 24) | ((offset) << 16) | (bits))

#define CTF_INT_ENCODING(e)	_CTF_ENCODING(e)
#define  CTF_INT_SIGNED		(1 << 0)
#define  CTF_INT_CHAR		(1 << 1)
#define  CTF_INT_BOOL		(1 << 2)
#define  CTF_INT_VARARGS	(1 << 3)
#define CTF_INT_OFFSET(e)	_CTF_OFFSET(e)
#define CTF_INT_BITS(e)		_CTF_BITS(e)
#define CTF_INT_DATA(e, o, b)	_CTF_DATA(e, o, b)

#define CTF_FP_ENCODING(e)	_CTF_ENCODING(e)
#define  CTF_FP_SINGLE		1
#define  CTF_FP_DOUBLE		2
#define  CTF_FP_CPLX		3
#define  CTF_FP_DCPLX		4
#define  CTF_FP_LDCPLX		5
#define  CTF_FP_LDOUBLE		6
#define  CTF_FP_INTRVL		7
#define  CTF_FP_DINTRVL		8
#define  CTF_FP_LDINTRVL	9
#define  CTF_FP_IMAGRY		10
#define  CTF_FP_DIMAGRY		11
#define  CTF_FP_LDIMAGRY	12
#define CTF_FP_OFFSET(e)	_CTF_OFFSET(e)
#define CTF_FP_BITS(e)		_CTF_BITS(e)
#define CTF_FP_DATA(e, o, b)	_CTF_DATA(e, o, b)

/*
 * Name reference macro.
 */
#define CTF_NAME_STID(n)	((n) >> 31)
#define CTF_NAME_OFFSET(n)	((n) & CTF_MAX_NAME)

/*
 * Type macro.
 */
#define CTF_SIZE_TO_LSIZE_HI(s)	((uint32_t)((uint64_t)(s) >> 32))
#define CTF_SIZE_TO_LSIZE_LO(s)	((uint32_t)(s))
#define CTF_TYPE_LSIZE(t)	\
	(((uint64_t)(t)->ctt_lsizehi) << 32 | (t)->ctt_lsizelo)

/*
 * Member macro.
 */
#define CTF_LMEM_OFFSET(m) \
	(((uint64_t)(m)->ctlm_offsethi) << 32 | (m)->ctlm_offsetlo)
#define CTF_OFFSET_TO_LMEMHI(off)	((uint32_t)((uint64_t)(off) >> 32))
#define CTF_OFFSET_TO_LMEMLO(off)	((uint32_t)(off))

#endif /* _SYS_CTF_H_ */
