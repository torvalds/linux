/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GENL_MAGIC_STRUCT_H
#define GENL_MAGIC_STRUCT_H

#ifndef GENL_MAGIC_FAMILY
# error "you need to define GENL_MAGIC_FAMILY before inclusion"
#endif

#ifndef GENL_MAGIC_VERSION
# error "you need to define GENL_MAGIC_VERSION before inclusion"
#endif

#ifndef GENL_MAGIC_INCLUDE_FILE
# error "you need to define GENL_MAGIC_INCLUDE_FILE before inclusion"
#endif

#include <linux/args.h>
#include <linux/types.h>
#include <net/genetlink.h>

extern int CONCATENATE(GENL_MAGIC_FAMILY, _genl_register)(void);
extern void CONCATENATE(GENL_MAGIC_FAMILY, _genl_unregister)(void);

/*
 * Extension of genl attribute validation policies			{{{2
 */

/*
 * @DRBD_GENLA_F_MANDATORY: By default, netlink ignores attributes it does not
 * know about.  This flag can be set in nlattr->nla_type to indicate that this
 * attribute must not be ignored.
 *
 * We check and remove this flag in drbd_nla_check_mandatory() before
 * validating the attribute types and lengths via nla_parse_nested().
 */
#define DRBD_GENLA_F_MANDATORY (1 << 14)

/*
 * Flags specific to drbd and not visible at the netlink layer, used in
 * <struct>_from_attrs and <struct>_to_skb:
 *
 * @DRBD_F_REQUIRED: Attribute is required; a request without this attribute is
 * invalid.
 *
 * @DRBD_F_SENSITIVE: Attribute includes sensitive information and must not be
 * included in unpriviledged get requests or broadcasts.
 *
 * @DRBD_F_INVARIANT: Attribute is set when an object is initially created, but
 * cannot subsequently be changed.
 */
#define DRBD_F_REQUIRED (1 << 0)
#define DRBD_F_SENSITIVE (1 << 1)
#define DRBD_F_INVARIANT (1 << 2)

#define __nla_type(x)	((__u16)((x) & NLA_TYPE_MASK & ~DRBD_GENLA_F_MANDATORY))

/*									}}}1
 * MAGIC
 * multi-include macro expansion magic starts here
 */

/* MAGIC helpers							{{{2 */

static inline int nla_put_u64_0pad(struct sk_buff *skb, int attrtype, u64 value)
{
	return nla_put_64bit(skb, attrtype, sizeof(u64), &value, 0);
}

/* possible field types */
#define __flg_field(attr_nr, attr_flag, name) \
	__field(attr_nr, attr_flag, name, NLA_U8, char, \
			nla_get_u8, nla_put_u8, false)
#define __u8_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U8, unsigned char, \
			nla_get_u8, nla_put_u8, false)
#define __u16_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U16, __u16, \
			nla_get_u16, nla_put_u16, false)
#define __u32_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U32, __u32, \
			nla_get_u32, nla_put_u32, false)
#define __s32_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U32, __s32, \
			nla_get_u32, nla_put_u32, true)
#define __u64_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U64, __u64, \
			nla_get_u64, nla_put_u64_0pad, false)
#define __str_field(attr_nr, attr_flag, name, maxlen) \
	__array(attr_nr, attr_flag, name, NLA_NUL_STRING, char, maxlen, \
			nla_strscpy, nla_put, false)
#define __bin_field(attr_nr, attr_flag, name, maxlen) \
	__array(attr_nr, attr_flag, name, NLA_BINARY, char, maxlen, \
			nla_memcpy, nla_put, false)

/* fields with default values */
#define __flg_field_def(attr_nr, attr_flag, name, default) \
	__flg_field(attr_nr, attr_flag, name)
#define __u32_field_def(attr_nr, attr_flag, name, default) \
	__u32_field(attr_nr, attr_flag, name)
#define __s32_field_def(attr_nr, attr_flag, name, default) \
	__s32_field(attr_nr, attr_flag, name)
#define __str_field_def(attr_nr, attr_flag, name, maxlen) \
	__str_field(attr_nr, attr_flag, name, maxlen)

#define GENL_op_init(args...)	args
#define GENL_doit(handler)		\
	.doit = handler,		\
	.flags = GENL_ADMIN_PERM,
#define GENL_dumpit(handler)		\
	.dumpit = handler,		\
	.flags = GENL_ADMIN_PERM,

/*									}}}1
 * Magic: define the enum symbols for genl_ops
 * Magic: define the enum symbols for top level attributes
 * Magic: define the enum symbols for nested attributes
 *									{{{2
 */

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)

#undef GENL_mc_group
#define GENL_mc_group(group)

#undef GENL_notification
#define GENL_notification(op_name, op_num, mcast_group, tla_list)	\
	op_name = op_num,

#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)			\
	op_name = op_num,

enum {
#include GENL_MAGIC_INCLUDE_FILE
};

#undef GENL_notification
#define GENL_notification(op_name, op_num, mcast_group, tla_list)

#undef GENL_op
#define GENL_op(op_name, op_num, handler, attr_list)

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields) \
		tag_name = tag_number,

enum {
#include GENL_MAGIC_INCLUDE_FILE
};

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)	\
enum {								\
	s_fields						\
};

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type,	\
		__get, __put, __is_signed)			\
	T_ ## name = (__u16)(attr_nr | ((attr_flag) & DRBD_GENLA_F_MANDATORY)),

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type,	\
		maxlen, __get, __put, __is_signed)		\
	T_ ## name = (__u16)(attr_nr | ((attr_flag) & DRBD_GENLA_F_MANDATORY)),

#include GENL_MAGIC_INCLUDE_FILE

/*									}}}1
 * Magic: compile time assert unique numbers for operations
 * Magic: -"- unique numbers for top level attributes
 * Magic: -"- unique numbers for nested attributes
 *									{{{2
 */

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)

#undef GENL_op
#define GENL_op(op_name, op_num, handler, attr_list)	\
	case op_name:

#undef GENL_notification
#define GENL_notification(op_name, op_num, mcast_group, tla_list)	\
	case op_name:

static inline void ct_assert_unique_operations(void)
{
	switch (0) {
#include GENL_MAGIC_INCLUDE_FILE
	case 0:
		;
	}
}

#undef GENL_op
#define GENL_op(op_name, op_num, handler, attr_list)

#undef GENL_notification
#define GENL_notification(op_name, op_num, mcast_group, tla_list)

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
		case tag_number:

static inline void ct_assert_unique_top_level_attributes(void)
{
	switch (0) {
#include GENL_MAGIC_INCLUDE_FILE
	case 0:
		;
	}
}

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
static inline void ct_assert_unique_ ## s_name ## _attributes(void)	\
{									\
	switch (0) {							\
	s_fields							\
	case 0:								\
			;						\
	}								\
}

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type, __get, __put,	\
		__is_signed)						\
	case attr_nr:

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type, maxlen,	\
		__get, __put, __is_signed)				\
	case attr_nr:

#include GENL_MAGIC_INCLUDE_FILE

/*									}}}1
 * Magic: declare structs
 * struct <name> {
 *	fields
 * };
 *									{{{2
 */

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
struct s_name { s_fields };

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type, __get, __put,	\
		__is_signed)						\
	type name;

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type, maxlen,	\
		__get, __put, __is_signed)				\
	type name[maxlen];	\
	__u32 name ## _len;

#include GENL_MAGIC_INCLUDE_FILE

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
enum {									\
	s_fields							\
};

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type, __get, __put,	\
		is_signed)						\
	F_ ## name ## _IS_SIGNED = is_signed,

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type, maxlen,	\
		__get, __put, is_signed)				\
	F_ ## name ## _IS_SIGNED = is_signed,

#include GENL_MAGIC_INCLUDE_FILE

/* }}}1 */
#endif /* GENL_MAGIC_STRUCT_H */
