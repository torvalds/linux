/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2018 Facebook */
#ifndef _UAPI__LINUX_BTF_H__
#define _UAPI__LINUX_BTF_H__

#include <linux/types.h>

#define BTF_MAGIC	0xeB9F
#define BTF_MAGIC_SWAP	0x9FeB
#define BTF_VERSION	1
#define BTF_FLAGS_COMPR	0x01

struct btf_header {
	__u16	magic;
	__u8	version;
	__u8	flags;

	__u32	parent_label;
	__u32	parent_name;

	/* All offsets are in bytes relative to the end of this header */
	__u32	label_off;	/* offset of label section	*/
	__u32	object_off;	/* offset of data object section*/
	__u32	func_off;	/* offset of function section	*/
	__u32	type_off;	/* offset of type section	*/
	__u32	str_off;	/* offset of string section	*/
	__u32	str_len;	/* length of string section	*/
};

/* Max # of type identifier */
#define BTF_MAX_TYPE	0x7fffffff
/* Max offset into the string section */
#define BTF_MAX_NAME_OFFSET	0x7fffffff
/* Max # of struct/union/enum members or func args */
#define BTF_MAX_VLEN	0xffff

/* The type id is referring to a parent BTF */
#define BTF_TYPE_PARENT(id)	(((id) >> 31) & 0x1)
#define BTF_TYPE_ID(id)		((id) & BTF_MAX_TYPE)

/* String is in the ELF string section */
#define BTF_STR_TBL_ELF_ID(ref)	(((ref) >> 31) & 0x1)
#define BTF_STR_OFFSET(ref)	((ref) & BTF_MAX_NAME_OFFSET)

struct btf_type {
	__u32 name;
	/* "info" bits arrangement
	 * bits  0-15: vlen (e.g. # of struct's members)
	 * bits 16-23: unused
	 * bits 24-28: kind (e.g. int, ptr, array...etc)
	 * bits 29-30: unused
	 * bits    31: root
	 */
	__u32 info;
	/* "size" is used by INT, ENUM, STRUCT and UNION.
	 * "size" tells the size of the type it is describing.
	 *
	 * "type" is used by PTR, TYPEDEF, VOLATILE, CONST and RESTRICT.
	 * "type" is a type_id referring to another type.
	 */
	union {
		__u32 size;
		__u32 type;
	};
};

#define BTF_INFO_KIND(info)	(((info) >> 24) & 0x1f)
#define BTF_INFO_ISROOT(info)	(!!(((info) >> 24) & 0x80))
#define BTF_INFO_VLEN(info)	((info) & 0xffff)

#define BTF_KIND_UNKN		0	/* Unknown	*/
#define BTF_KIND_INT		1	/* Integer	*/
#define BTF_KIND_PTR		2	/* Pointer	*/
#define BTF_KIND_ARRAY		3	/* Array	*/
#define BTF_KIND_STRUCT		4	/* Struct	*/
#define BTF_KIND_UNION		5	/* Union	*/
#define BTF_KIND_ENUM		6	/* Enumeration	*/
#define BTF_KIND_FWD		7	/* Forward	*/
#define BTF_KIND_TYPEDEF	8	/* Typedef	*/
#define BTF_KIND_VOLATILE	9	/* Volatile	*/
#define BTF_KIND_CONST		10	/* Const	*/
#define BTF_KIND_RESTRICT	11	/* Restrict	*/
#define BTF_KIND_MAX		11
#define NR_BTF_KINDS		12

/* For some specific BTF_KIND, "struct btf_type" is immediately
 * followed by extra data.
 */

/* BTF_KIND_INT is followed by a u32 and the following
 * is the 32 bits arrangement:
 */
#define BTF_INT_ENCODING(VAL)	(((VAL) & 0xff000000) >> 24)
#define BTF_INT_OFFSET(VAL)	(((VAL  & 0x00ff0000)) >> 16)
#define BTF_INT_BITS(VAL)	((VAL)  & 0x0000ffff)

/* Attributes stored in the BTF_INT_ENCODING */
#define BTF_INT_SIGNED	0x1
#define BTF_INT_CHAR	0x2
#define BTF_INT_BOOL	0x4
#define BTF_INT_VARARGS	0x8

/* BTF_KIND_ENUM is followed by multiple "struct btf_enum".
 * The exact number of btf_enum is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_enum {
	__u32	name;
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
	__u32	name;
	__u32	type;
	__u32	offset;	/* offset in bits */
};

#endif /* _UAPI__LINUX_BTF_H__ */
