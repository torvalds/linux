/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 4 definitions for creating trace events */

#define ALIGN_STRUCTFIELD(type) ((int)(offsetof(struct {char a; type b;}, b)))

#undef __field_ext
#define __field_ext(_type, _item, _filter_type) {			\
	.type = #_type, .name = #_item,					\
	.size = sizeof(_type), .align = ALIGN_STRUCTFIELD(_type),	\
	.is_signed = is_signed_type(_type), .filter_type = _filter_type },

#undef __field_struct_ext
#define __field_struct_ext(_type, _item, _filter_type) {		\
	.type = #_type, .name = #_item,					\
	.size = sizeof(_type), .align = ALIGN_STRUCTFIELD(_type),	\
	0, .filter_type = _filter_type },

#undef __field
#define __field(type, item)	__field_ext(type, item, FILTER_OTHER)

#undef __field_struct
#define __field_struct(type, item) __field_struct_ext(type, item, FILTER_OTHER)

#undef __array
#define __array(_type, _item, _len) {					\
	.type = #_type"["__stringify(_len)"]", .name = #_item,		\
	.size = sizeof(_type[_len]), .align = ALIGN_STRUCTFIELD(_type),	\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __dynamic_array
#define __dynamic_array(_type, _item, _len) {				\
	.type = "__data_loc " #_type "[]", .name = #_item,		\
	.size = 4, .align = 4,						\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __vstring
#define __vstring(item, fmt, ap) __dynamic_array(char, item, -1)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef __sockaddr
#define __sockaddr(field, len) __dynamic_array(u8, field, len)

#undef __rel_dynamic_array
#define __rel_dynamic_array(_type, _item, _len) {			\
	.type = "__rel_loc " #_type "[]", .name = #_item,		\
	.size = 4, .align = 4,						\
	.is_signed = is_signed_type(_type), .filter_type = FILTER_OTHER },

#undef __rel_string
#define __rel_string(item, src) __rel_dynamic_array(char, item, -1)

#undef __rel_string_len
#define __rel_string_len(item, src, len) __rel_dynamic_array(char, item, -1)

#undef __rel_bitmask
#define __rel_bitmask(item, nr_bits) __rel_dynamic_array(unsigned long, item, -1)

#undef __rel_sockaddr
#define __rel_sockaddr(field, len) __rel_dynamic_array(u8, field, len)
