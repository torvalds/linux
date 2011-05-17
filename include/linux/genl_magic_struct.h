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

#include <linux/genetlink.h>
#include <linux/types.h>

#define CONCAT__(a,b)	a ## b
#define CONCAT_(a,b)	CONCAT__(a,b)

extern int CONCAT_(GENL_MAGIC_FAMILY, _genl_register)(void);
extern void CONCAT_(GENL_MAGIC_FAMILY, _genl_unregister)(void);

/*
 * Extension of genl attribute validation policies			{{{2
 */

/**
 * GENLA_F_FLAGS - policy type flags to ease compatible ABI evolvement
 *
 * @GENLA_F_REQUIRED: attribute has to be present, or message is considered invalid.
 * Adding new REQUIRED attributes breaks ABI compatibility, so don't do that.
 *
 * @GENLA_F_MANDATORY: if present, receiver _must_ understand it.
 * Without this, unknown attributes (> maxtype) are _silently_ ignored
 * by validate_nla().
 *
 * To be used for API extensions, so older kernel can reject requests for not
 * yet implemented features, if newer userland tries to use them even though
 * the genl_family version clearly indicates they are not available.
 *
 * @GENLA_F_MAY_IGNORE: To clearly document the fact, for good measure.
 * To be used for API extensions for things that have sane defaults,
 * so newer userland can still talk to older kernel, knowing it will
 * silently ignore these attributes if not yet known.
 *
 * NOTE: These flags overload
 *   NLA_F_NESTED		(1 << 15)
 *   NLA_F_NET_BYTEORDER	(1 << 14)
 * from linux/netlink.h, which are not useful for validate_nla():
 * NET_BYTEORDER is not used anywhere, and NESTED would be specified by setting
 * .type = NLA_NESTED in the appropriate policy.
 *
 * See also: nla_type()
 */
enum {
	GENLA_F_MAY_IGNORE	= 0,
	GENLA_F_MANDATORY	= 1 << 14,
	GENLA_F_REQUIRED	= 1 << 15,

	/* Below will not be present in the __u16 .nla_type, but can be
	 * triggered on in <struct>_to_skb resp. <struct>_from_attrs */

	/* To exclude "sensitive" information from broadcasts, or on
	 * unpriviledged get requests.  This is useful because genetlink
	 * multicast groups can be listened in on by anyone.  */
	GENLA_F_SENSITIVE	= 1 << 16,

	/* INVARIAN options cannot be changed at runtime.
	 * Useful to share an attribute policy and struct definition,
	 * between some "create" and "change" commands,
	 * but disallow certain fields to be changed online.
	 */
	GENLA_F_INVARIANT	= 1 << 17,
};

#define __nla_type(x)	((__u16)((__u16)(x) & (__u16)NLA_TYPE_MASK))

/*									}}}1
 * MAGIC
 * multi-include macro expansion magic starts here
 */

/* MAGIC helpers							{{{2 */

/* possible field types */
#define __flg_field(attr_nr, attr_flag, name) \
	__field(attr_nr, attr_flag, name, NLA_U8, char, \
			nla_get_u8, NLA_PUT_U8, false)
#define __u8_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U8, unsigned char, \
			nla_get_u8, NLA_PUT_U8, false)
#define __u16_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U16, __u16, \
			nla_get_u16, NLA_PUT_U16, false)
#define __u32_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U32, __u32, \
			nla_get_u32, NLA_PUT_U32, false)
#define __s32_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U32, __s32, \
			nla_get_u32, NLA_PUT_U32, true)
#define __u64_field(attr_nr, attr_flag, name)	\
	__field(attr_nr, attr_flag, name, NLA_U64, __u64, \
			nla_get_u64, NLA_PUT_U64, false)
#define __str_field(attr_nr, attr_flag, name, maxlen) \
	__array(attr_nr, attr_flag, name, NLA_NUL_STRING, char, maxlen, \
			nla_strlcpy, NLA_PUT, false)
#define __bin_field(attr_nr, attr_flag, name, maxlen) \
	__array(attr_nr, attr_flag, name, NLA_BINARY, char, maxlen, \
			nla_memcpy, NLA_PUT, false)

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
	T_ ## name = (__u16)(attr_nr | attr_flag),

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type,	\
		maxlen, __get, __put, __is_signed)		\
	T_ ## name = (__u16)(attr_nr | attr_flag),

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
		;
	}
}

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
static inline void ct_assert_unique_ ## s_name ## _attributes(void)	\
{									\
	switch (0) {							\
		s_fields						\
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
/* vim: set foldmethod=marker nofoldenable : */
