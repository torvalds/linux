/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 1 definitions for creating trace events */

#undef __field
#define __field(type, item)		type	item;

#undef __field_ext
#define __field_ext(type, item, filter_type)	type	item;

#undef __field_struct
#define __field_struct(type, item)	type	item;

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)	type	item;

#undef __array
#define __array(type, item, len)	type	item[len];

#undef __dynamic_array
#define __dynamic_array(type, item, len) u32 __data_loc_##item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __vstring
#define __vstring(item, fmt, ap) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(char, item, -1)

#undef __sockaddr
#define __sockaddr(field, len) __dynamic_array(u8, field, len)

#undef __rel_dynamic_array
#define __rel_dynamic_array(type, item, len) u32 __rel_loc_##item;

#undef __rel_string
#define __rel_string(item, src) __rel_dynamic_array(char, item, -1)

#undef __rel_string_len
#define __rel_string_len(item, src, len) __rel_dynamic_array(char, item, -1)

#undef __rel_bitmask
#define __rel_bitmask(item, nr_bits) __rel_dynamic_array(char, item, -1)

#undef __rel_sockaddr
#define __rel_sockaddr(field, len) __rel_dynamic_array(u8, field, len)

#undef TP_STRUCT__entry
#define TP_STRUCT__entry(args...) args
