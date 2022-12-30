/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 5 definitions for creating trace events */

/*
 * remember the offset of each array from the beginning of the event.
 */

#undef __entry
#define __entry entry

#undef __field
#define __field(type, item)

#undef __field_ext
#define __field_ext(type, item, filter_type)

#undef __field_struct
#define __field_struct(type, item)

#undef __field_struct_ext
#define __field_struct_ext(type, item, filter_type)

#undef __array
#define __array(type, item, len)

#undef __dynamic_array
#define __dynamic_array(type, item, len)				\
	__item_length = (len) * sizeof(type);				\
	__data_offsets->item = __data_size +				\
			       offsetof(typeof(*entry), __data);	\
	__data_offsets->item |= __item_length << 16;			\
	__data_size += __item_length;

#undef __string
#define __string(item, src) __dynamic_array(char, item,			\
		    strlen((src) ? (const char *)(src) : "(null)") + 1)

#undef __string_len
#define __string_len(item, src, len) __dynamic_array(char, item, (len) + 1)

#undef __vstring
#define __vstring(item, fmt, ap) __dynamic_array(char, item,		\
		      __trace_event_vstr_len(fmt, ap))

#undef __rel_dynamic_array
#define __rel_dynamic_array(type, item, len)				\
	__item_length = (len) * sizeof(type);				\
	__data_offsets->item = __data_size +				\
			       offsetof(typeof(*entry), __data) -	\
			       offsetof(typeof(*entry), __rel_loc_##item) -	\
			       sizeof(u32);				\
	__data_offsets->item |= __item_length << 16;			\
	__data_size += __item_length;

#undef __rel_string
#define __rel_string(item, src) __rel_dynamic_array(char, item,			\
		    strlen((src) ? (const char *)(src) : "(null)") + 1)

#undef __rel_string_len
#define __rel_string_len(item, src, len) __rel_dynamic_array(char, item, (len) + 1)
/*
 * __bitmask_size_in_bytes_raw is the number of bytes needed to hold
 * num_possible_cpus().
 */
#define __bitmask_size_in_bytes_raw(nr_bits)	\
	(((nr_bits) + 7) / 8)

#define __bitmask_size_in_longs(nr_bits)			\
	((__bitmask_size_in_bytes_raw(nr_bits) +		\
	  ((BITS_PER_LONG / 8) - 1)) / (BITS_PER_LONG / 8))

/*
 * __bitmask_size_in_bytes is the number of bytes needed to hold
 * num_possible_cpus() padded out to the nearest long. This is what
 * is saved in the buffer, just to be consistent.
 */
#define __bitmask_size_in_bytes(nr_bits)				\
	(__bitmask_size_in_longs(nr_bits) * (BITS_PER_LONG / 8))

#undef __bitmask
#define __bitmask(item, nr_bits) __dynamic_array(unsigned long, item,	\
					 __bitmask_size_in_longs(nr_bits))

#undef __cpumask
#define __cpumask(item) __bitmask(item, nr_cpumask_bits)

#undef __rel_bitmask
#define __rel_bitmask(item, nr_bits) __rel_dynamic_array(unsigned long, item,	\
					 __bitmask_size_in_longs(nr_bits))

#undef __rel_cpumask
#define __rel_cpumask(item) __rel_bitmask(item, nr_cpumask_bits)

#undef __sockaddr
#define __sockaddr(field, len) __dynamic_array(u8, field, len)

#undef __rel_sockaddr
#define __rel_sockaddr(field, len) __rel_dynamic_array(u8, field, len)
