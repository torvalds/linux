/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */

#ifndef _TEST_BTF_H
#define _TEST_BTF_H

#define BTF_INFO_ENC(kind, kind_flag, vlen)			\
	((!!(kind_flag) << 31) | ((kind) << 24) | ((vlen) & BTF_MAX_VLEN))

#define BTF_TYPE_ENC(name, info, size_or_type)	\
	(name), (info), (size_or_type)

#define BTF_INT_ENC(encoding, bits_offset, nr_bits)	\
	((encoding) << 24 | (bits_offset) << 16 | (nr_bits))
#define BTF_TYPE_INT_ENC(name, encoding, bits_offset, bits, sz)	\
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_INT, 0, 0), sz),	\
	BTF_INT_ENC(encoding, bits_offset, bits)

#define BTF_FWD_ENC(name, kind_flag) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_FWD, kind_flag, 0), 0)

#define BTF_ARRAY_ENC(type, index_type, nr_elems)	\
	(type), (index_type), (nr_elems)
#define BTF_TYPE_ARRAY_ENC(type, index_type, nr_elems) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ARRAY, 0, 0), 0), \
	BTF_ARRAY_ENC(type, index_type, nr_elems)

#define BTF_STRUCT_ENC(name, nr_elems, sz)	\
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, nr_elems), sz)

#define BTF_UNION_ENC(name, nr_elems, sz)	\
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_UNION, 0, nr_elems), sz)

#define BTF_VAR_ENC(name, type, linkage)	\
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_VAR, 0, 0), type), (linkage)
#define BTF_VAR_SECINFO_ENC(type, offset, size)	\
	(type), (offset), (size)

#define BTF_MEMBER_ENC(name, type, bits_offset)	\
	(name), (type), (bits_offset)
#define BTF_ENUM_ENC(name, val) (name), (val)
#define BTF_MEMBER_OFFSET(bitfield_size, bits_offset) \
	((bitfield_size) << 24 | (bits_offset))

#define BTF_TYPEDEF_ENC(name, type) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), type)

#define BTF_PTR_ENC(type) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), type)

#define BTF_CONST_ENC(type) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), type)

#define BTF_VOLATILE_ENC(type) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_VOLATILE, 0, 0), type)

#define BTF_RESTRICT_ENC(type) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_RESTRICT, 0, 0), type)

#define BTF_FUNC_PROTO_ENC(ret_type, nargs) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, nargs), ret_type)

#define BTF_FUNC_PROTO_ARG_ENC(name, type) \
	(name), (type)

#define BTF_FUNC_ENC(name, func_proto) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_FUNC, 0, 0), func_proto)

#define BTF_TYPE_FLOAT_ENC(name, sz) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_FLOAT, 0, 0), sz)

#endif /* _TEST_BTF_H */
