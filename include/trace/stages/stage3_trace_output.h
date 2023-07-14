/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 3 definitions for creating trace events */

#undef __entry
#define __entry field

#undef TP_printk
#define TP_printk(fmt, args...) fmt "\n", args

#undef __get_dynamic_array
#define __get_dynamic_array(field)	\
		((void *)__entry + (__entry->__data_loc_##field & 0xffff))

#undef __get_dynamic_array_len
#define __get_dynamic_array_len(field)	\
		((__entry->__data_loc_##field >> 16) & 0xffff)

#undef __get_str
#define __get_str(field) ((char *)__get_dynamic_array(field))

#undef __get_rel_dynamic_array
#define __get_rel_dynamic_array(field)					\
		((void *)__entry + 					\
		 offsetof(typeof(*__entry), __rel_loc_##field) +	\
		 sizeof(__entry->__rel_loc_##field) +			\
		 (__entry->__rel_loc_##field & 0xffff))

#undef __get_rel_dynamic_array_len
#define __get_rel_dynamic_array_len(field)	\
		((__entry->__rel_loc_##field >> 16) & 0xffff)

#undef __get_rel_str
#define __get_rel_str(field) ((char *)__get_rel_dynamic_array(field))

#undef __get_bitmask
#define __get_bitmask(field)						\
	({								\
		void *__bitmask = __get_dynamic_array(field);		\
		unsigned int __bitmask_size;				\
		__bitmask_size = __get_dynamic_array_len(field);	\
		trace_print_bitmask_seq(p, __bitmask, __bitmask_size);	\
	})

#undef __get_cpumask
#define __get_cpumask(field) __get_bitmask(field)

#undef __get_rel_bitmask
#define __get_rel_bitmask(field)						\
	({								\
		void *__bitmask = __get_rel_dynamic_array(field);		\
		unsigned int __bitmask_size;				\
		__bitmask_size = __get_rel_dynamic_array_len(field);	\
		trace_print_bitmask_seq(p, __bitmask, __bitmask_size);	\
	})

#undef __get_rel_cpumask
#define __get_rel_cpumask(field) __get_rel_bitmask(field)

#undef __get_sockaddr
#define __get_sockaddr(field)	((struct sockaddr *)__get_dynamic_array(field))

#undef __get_rel_sockaddr
#define __get_rel_sockaddr(field)	((struct sockaddr *)__get_rel_dynamic_array(field))

#undef __print_flags
#define __print_flags(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags __flags[] =	\
			{ flag_array, { -1, NULL }};			\
		trace_print_flags_seq(p, delim, flag, __flags);	\
	})

#undef __print_symbolic
#define __print_symbolic(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags symbols[] =	\
			{ symbol_array, { -1, NULL }};			\
		trace_print_symbols_seq(p, value, symbols);		\
	})

#undef __print_flags_u64
#undef __print_symbolic_u64
#if BITS_PER_LONG == 32
#define __print_flags_u64(flag, delim, flag_array...)			\
	({								\
		static const struct trace_print_flags_u64 __flags[] =	\
			{ flag_array, { -1, NULL } };			\
		trace_print_flags_seq_u64(p, delim, flag, __flags);	\
	})

#define __print_symbolic_u64(value, symbol_array...)			\
	({								\
		static const struct trace_print_flags_u64 symbols[] =	\
			{ symbol_array, { -1, NULL } };			\
		trace_print_symbols_seq_u64(p, value, symbols);	\
	})
#else
#define __print_flags_u64(flag, delim, flag_array...)			\
			__print_flags(flag, delim, flag_array)

#define __print_symbolic_u64(value, symbol_array...)			\
			__print_symbolic(value, symbol_array)
#endif

#undef __print_hex
#define __print_hex(buf, buf_len)					\
	trace_print_hex_seq(p, buf, buf_len, false)

#undef __print_hex_str
#define __print_hex_str(buf, buf_len)					\
	trace_print_hex_seq(p, buf, buf_len, true)

#undef __print_array
#define __print_array(array, count, el_size)				\
	({								\
		BUILD_BUG_ON(el_size != 1 && el_size != 2 &&		\
			     el_size != 4 && el_size != 8);		\
		trace_print_array_seq(p, array, count, el_size);	\
	})

#undef __print_hex_dump
#define __print_hex_dump(prefix_str, prefix_type,			\
			 rowsize, groupsize, buf, len, ascii)		\
	trace_print_hex_dump_seq(p, prefix_str, prefix_type,		\
				 rowsize, groupsize, buf, len, ascii)

#undef __print_ns_to_secs
#define __print_ns_to_secs(value)			\
	({						\
		u64 ____val = (u64)(value);		\
		do_div(____val, NSEC_PER_SEC);		\
		____val;				\
	})

#undef __print_ns_without_secs
#define __print_ns_without_secs(value)			\
	({						\
		u64 ____val = (u64)(value);		\
		(u32) do_div(____val, NSEC_PER_SEC);	\
	})

#undef __get_buf
#define __get_buf(len)		trace_seq_acquire(p, (len))
