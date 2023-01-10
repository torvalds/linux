/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 6 definitions for creating trace events */

#undef __entry
#define __entry entry

#undef __field
#define __field(type, item)

#undef __field_struct
#define __field_struct(type, item)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__entry->__data_loc_##item = __data_offsets.item;

#undef __string
#define __string(item, src) __dynamic_array(char, item, -1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, -1)

#undef __vstring
#define __vstring(item, fmt, ap) __dynamic_array(char, item, -1)

#undef __assign_str
#define __assign_str(dst, src)						\
	strcpy(__get_str(dst), (src) ? (const char *)(src) : "(null)");

#undef __assign_str_len
#define __assign_str_len(dst, src, len)					\
	do {								\
		memcpy(__get_str(dst), (src), (len));			\
		__get_str(dst)[len] = '\0';				\
	} while(0)

#undef __assign_vstr
#define __assign_vstr(dst, fmt, va)					\
	do {								\
		va_list __cp_va;					\
		va_copy(__cp_va, *(va));				\
		vsnprintf(__get_str(dst), TRACE_EVENT_STR_MAX, fmt, __cp_va); \
		va_end(__cp_va);					\
	} while (0)

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item, -1)

#undef __get_bitmask
#define __get_bitmask(field) (char *)__get_dynamic_array(field)

#undef __assign_bitmask
#define __assign_bitmask(dst, src, nr_bits)					\
	memcpy(__get_bitmask(dst), (src), __bitmask_size_in_bytes(nr_bits))

#undef __cpumask
#define __cpumask(item) __dynamic_array(unsigned long, item, -1)

#undef __get_cpumask
#define __get_cpumask(field) (char *)__get_dynamic_array(field)

#undef __assign_cpumask
#define __assign_cpumask(dst, src)					\
	memcpy(__get_cpumask(dst), (src), __bitmask_size_in_bytes(nr_cpumask_bits))

#undef __sockaddr
#define __sockaddr(field, len) __dynamic_array(u8, field, len)

#undef __get_sockaddr
#define __get_sockaddr(field)	((struct sockaddr *)__get_dynamic_array(field))

#undef __assign_sockaddr
#define __assign_sockaddr(dest, src, len)					\
	memcpy(__get_dynamic_array(dest), src, len)

#undef __rel_dynamic_array
#define __rel_dynamic_array(type, item, len)				\
	__entry->__rel_loc_##item = __data_offsets.item;

#undef __rel_string
#define __rel_string(item, src) __rel_dynamic_array(char, item, -1)

#undef __rel_string_len
#define __rel_string_len(item, src, len) __rel_dynamic_array(char, item, -1)

#undef __assign_rel_str
#define __assign_rel_str(dst, src)					\
	strcpy(__get_rel_str(dst), (src) ? (const char *)(src) : "(null)");

#undef __assign_rel_str_len
#define __assign_rel_str_len(dst, src, len)				\
	do {								\
		memcpy(__get_rel_str(dst), (src), (len));		\
		__get_rel_str(dst)[len] = '\0';				\
	} while (0)

#undef __rel_bitmask
#define __rel_bitmask(item, nr_bits) __rel_dynamic_array(unsigned long, item, -1)

#undef __get_rel_bitmask
#define __get_rel_bitmask(field) (char *)__get_rel_dynamic_array(field)

#undef __assign_rel_bitmask
#define __assign_rel_bitmask(dst, src, nr_bits)					\
	memcpy(__get_rel_bitmask(dst), (src), __bitmask_size_in_bytes(nr_bits))

#undef __rel_cpumask
#define __rel_cpumask(item) __rel_dynamic_array(unsigned long, item, -1)

#undef __get_rel_cpumask
#define __get_rel_cpumask(field) (char *)__get_rel_dynamic_array(field)

#undef __assign_rel_cpumask
#define __assign_rel_cpumask(dst, src)					\
	memcpy(__get_rel_cpumask(dst), (src), __bitmask_size_in_bytes(nr_cpumask_bits))

#undef __rel_sockaddr
#define __rel_sockaddr(field, len) __rel_dynamic_array(u8, field, len)

#undef __get_rel_sockaddr
#define __get_rel_sockaddr(field)	((struct sockaddr *)__get_rel_dynamic_array(field))

#undef __assign_rel_sockaddr
#define __assign_rel_sockaddr(dest, src, len)					\
	memcpy(__get_rel_dynamic_array(dest), src, len)

#undef TP_fast_assign
#define TP_fast_assign(args...) args

#undef __perf_count
#define __perf_count(c)	(c)

#undef __perf_task
#define __perf_task(t)	(t)
