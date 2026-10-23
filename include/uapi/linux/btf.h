/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018 Facebook */
#ifndef _UAPI__LINUX_BTF_H__
#define _UAPI__LINUX_BTF_H__

#include <linux/types.h>

#define BTF_MAGIC	0xeB9F
#define BTF_VERSION	1

/*
 * BTF layout section consists of a struct btf_layout for each known
 * kind at BTF encoding time.
 */
struct btf_layout {
	__u8 info_sz;		/* size of singular element after btf_type */
	__u8 elem_sz;		/* size of each of btf_vlen(t) elements */
	__u16 flags;		/* currently unused */
};

struct btf_header {
	__u16	magic;
	__u8	version;
	__u8	flags;
	__u32	hdr_len;

	/* All offsets are in bytes relative to the end of this header */
	__u32	type_off;	/* offset of type section	*/
	__u32	type_len;	/* length of type section	*/
	__u32	str_off;	/* offset of string section	*/
	__u32	str_len;	/* length of string section	*/
	__u32	layout_off;	/* offset of layout section	*/
	__u32	layout_len;	/* length of layout section	*/
};

enum btf_max {
	/* Max possible kind */
	BTF_MAX_KIND =		0x0000007f,
	/* Max # of type identifier */
	BTF_MAX_TYPE =		0x000fffff,
	/* Max offset into the string section */
	BTF_MAX_NAME_OFFSET =	0x00ffffff,
	/* Max # of struct/union/enum members or func args */
	BTF_MAX_VLEN =		0x00ffffff,
};

struct btf_type {
	__u32 name_off;
	/* "info" bits arrangement
	 * bits  0-23: vlen (e.g. # of struct's members)
	 * bits 24-30: kind (e.g. int, ptr, array...etc)
	 * bit     31: kind_flag, currently used by
	 *             struct, union, enum, fwd, enum64,
	 *             decl_tag and type_tag
	 */
	__u32 info;
	/* "size" is used by INT, ENUM, STRUCT, UNION, DATASEC, ENUM64
	 * and LOC_PARAM. "size" tells the size of the type it is describing.
	 *
	 * "type" is used by PTR, TYPEDEF, VOLATILE, CONST, RESTRICT,
	 * FUNC, FUNC_PROTO, VAR, DECL_TAG and TYPE_TAG.
	 * "type" is a type_id referring to another type.
	 */
	union {
		__u32 size;
		__u32 type;
	};
};

#define BTF_INFO_KIND(info)	(((info) >> 24) & 0x7f)
#define BTF_INFO_VLEN(info)	((info) & 0xffffff)
#define BTF_INFO_KFLAG(info)	((info) >> 31)

enum {
	BTF_KIND_UNKN		= 0,	/* Unknown	*/
	BTF_KIND_INT		= 1,	/* Integer	*/
	BTF_KIND_PTR		= 2,	/* Pointer	*/
	BTF_KIND_ARRAY		= 3,	/* Array	*/
	BTF_KIND_STRUCT		= 4,	/* Struct	*/
	BTF_KIND_UNION		= 5,	/* Union	*/
	BTF_KIND_ENUM		= 6,	/* Enumeration up to 32-bit values */
	BTF_KIND_FWD		= 7,	/* Forward	*/
	BTF_KIND_TYPEDEF	= 8,	/* Typedef	*/
	BTF_KIND_VOLATILE	= 9,	/* Volatile	*/
	BTF_KIND_CONST		= 10,	/* Const	*/
	BTF_KIND_RESTRICT	= 11,	/* Restrict	*/
	BTF_KIND_FUNC		= 12,	/* Function	*/
	BTF_KIND_FUNC_PROTO	= 13,	/* Function Proto	*/
	BTF_KIND_VAR		= 14,	/* Variable	*/
	BTF_KIND_DATASEC	= 15,	/* Section	*/
	BTF_KIND_FLOAT		= 16,	/* Floating point	*/
	BTF_KIND_DECL_TAG	= 17,	/* Decl Tag */
	BTF_KIND_TYPE_TAG	= 18,	/* Type Tag */
	BTF_KIND_ENUM64		= 19,	/* Enumeration up to 64-bit values */
	BTF_KIND_LOC_PARAM	= 20,	/* Location parameter information */
	BTF_KIND_LOC_PROTO	= 21,	/* Location prototype for site */
	BTF_KIND_LOCSEC		= 22,	/* Location section */
	NR_BTF_KINDS,
	BTF_KIND_MAX		= NR_BTF_KINDS - 1,
};

/* For some specific BTF_KIND, "struct btf_type" is immediately
 * followed by extra data.
 */

/* BTF_KIND_INT is followed by a u32 and the following
 * is the 32 bits arrangement:
 */
#define BTF_INT_ENCODING(VAL)	(((VAL) & 0x0f000000) >> 24)
#define BTF_INT_OFFSET(VAL)	(((VAL) & 0x00ff0000) >> 16)
#define BTF_INT_BITS(VAL)	((VAL)  & 0x000000ff)

/* Attributes stored in the BTF_INT_ENCODING */
#define BTF_INT_SIGNED	(1 << 0)
#define BTF_INT_CHAR	(1 << 1)
#define BTF_INT_BOOL	(1 << 2)

/* BTF_KIND_ENUM is followed by multiple "struct btf_enum".
 * The exact number of btf_enum is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_enum {
	__u32	name_off;
	__s32	val;
};

/* BTF_KIND_ARRAY is followed by one "struct btf_array" */
struct btf_array {
	__u32	type;
	__u32	index_type;
	__u32	nelems;
};

/* BTF_KIND_STRUCT and BTF_KIND_UNION are followed
 * by multiple "struct btf_member".  The exact number
 * of btf_member is stored in the vlen (of the info in
 * "struct btf_type").
 */
struct btf_member {
	__u32	name_off;
	__u32	type;
	/* If the type info kind_flag is set, the btf_member offset
	 * contains both member bitfield size and bit offset. The
	 * bitfield size is set for bitfield members. If the type
	 * info kind_flag is not set, the offset contains only bit
	 * offset.
	 */
	__u32	offset;
};

/* If the struct/union type info kind_flag is set, the
 * following two macros are used to access bitfield_size
 * and bit_offset from btf_member.offset.
 */
#define BTF_MEMBER_BITFIELD_SIZE(val)	((val) >> 24)
#define BTF_MEMBER_BIT_OFFSET(val)	((val) & 0xffffff)

/* BTF_KIND_FUNC_PROTO is followed by multiple "struct btf_param".
 * The exact number of btf_param is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_param {
	__u32	name_off;
	__u32	type;
};

enum {
	BTF_VAR_STATIC = 0,
	BTF_VAR_GLOBAL_ALLOCATED = 1,
	BTF_VAR_GLOBAL_EXTERN = 2,
};

enum btf_func_linkage {
	BTF_FUNC_STATIC = 0,
	BTF_FUNC_GLOBAL = 1,
	BTF_FUNC_EXTERN = 2,
};

/* BTF_KIND_VAR is followed by a single "struct btf_var" to describe
 * additional information related to the variable such as its linkage.
 */
struct btf_var {
	__u32	linkage;
};

/* BTF_KIND_DATASEC is followed by multiple "struct btf_var_secinfo"
 * to describe all BTF_KIND_VAR types it contains along with it's
 * in-section offset as well as size.
 */
struct btf_var_secinfo {
	__u32	type;
	__u32	offset;
	__u32	size;
};

/* BTF_KIND_DECL_TAG is followed by a single "struct btf_decl_tag" to describe
 * additional information related to the tag applied location.
 * If component_idx == -1, the tag is applied to a struct, union,
 * variable or function. Otherwise, it is applied to a struct/union
 * member or a func argument, and component_idx indicates which member
 * or argument (0 ... vlen-1).
 */
struct btf_decl_tag {
       __s32   component_idx;
};

/* BTF_KIND_ENUM64 is followed by multiple "struct btf_enum64".
 * The exact number of btf_enum64 is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_enum64 {
	__u32	name_off;
	__u32	val_lo32;
	__u32	val_hi32;
};

/*
 * BTF_KIND_LOC_PARAM is followed by a single "struct btf_loc_param"
 * that contains flags specifying the contents of the vlen-specified
 * number of 4-byte values that follow.
 */
struct btf_loc_param {
	__u32 flags;
	__u32 values[];
};

/*
 * The combination of size, vlen and flags gives us the means to interpret
 * the following vlen-specified set of 4-byte values:
 *
 * - a BTF_LOC_PARAM_CONST is a constant value; combination
 *   of size, vlen and _SIGNED flag determines it. If the value requires
 *   64 bits it is stored in {lo,hi} order.
 * - a BTF_LOC_PARAM_ADDR|BTF_LOC_PARAM_CONST is an address that should be
 *   normalized with respect to kernel/module base address.
 * - a BTF_LOC_PARAM_REG with vlen 1 is a simple register number;
 *   with vlen 2 it is a multi-register parameter.  Register numbers are
 *   numbers derived from DW_OP_reg values, i.e. 0 is DW_OP_reg0; since
 *   DW_OP_fbreg has its own special DW_OP_value and DW_OP_regx can refer
 *   to an arbitrary register number, we reserve BTF_LOC_PARAM_FBREG for
 *   the frame base register to avoid collisions.
 * - a _REG | OFFSET describes an address without dereferencing it.
 * - a _REG | DEREF with vlen 1 dereferences the value in the register
 *   number specified.
 * - a REG | DEREF | OFFSET with vlen > 1 specifies the register number
 *   in the first 4-byte value and the offset in the remainder.
 *   In the case of REG and OFFSET combinations, the OFFSET has the width
 *   of the value words while the type size describes the represented parameter,
 *   so for example a REG | DEREF | OFFSET with size 8 and vlen 2 would be
 *   an 8-byte register dereference with signed 4-byte offset, since the vlen 2
 *   values consist of a register value and the signed value.
 */
enum btf_loc_param_flags {
	BTF_LOC_PARAM_SIGNED		=	0x1,
	BTF_LOC_PARAM_CONST		=	0x2,
	BTF_LOC_PARAM_ADDR		=	0x4,
	BTF_LOC_PARAM_REG		=	0x8,
	BTF_LOC_PARAM_DEREF		=	0x10,
	BTF_LOC_PARAM_OFFSET		=	0x20,
};

enum {
	BTF_LOC_PARAM_FBREG		=	0xffffffff
};

/*
 * BTF_KIND_LOC_PROTO specifies location prototypes; i.e. how locations relate
 * to parameters; a struct btf_type of BTF_KIND_LOC_PROTO is followed by a
 * vlen-specified number of __u32 BTF type ids which specify the associated
 * BTF_KIND_LOC_PARAM for each function parameter associated with the
 * location.  The type should either be 0 (no location info) or point at
 * a BTF_KIND_LOC_PARAM.
 */

/*
 * BTF_KIND_LOCSEC consists of vlen-specified number of "struct btf_loc"
 * containing location site-specific information for a specific ELF section;
 * for example locations in ".text" are in a LOCSEC named "inline.text".
 *
 * - function (func)
 * - location prototype type id (loc_proto)
 * - address offset (offset) relative to the runtime base address of the
 *   ELF section associated with the LOCSEC
 */

struct btf_loc {
	__u32 func;
	__u32 loc_proto;
	__u32 offset;
};

#endif /* _UAPI__LINUX_BTF_H__ */
