/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VSTRUCTS_H
#define _VSTRUCTS_H

#include "util.h"

/*
 * NOTE: we can't differentiate between __le64 and u64 with type_is - this
 * assumes u64 is little endian:
 */
#define __vstruct_u64s(_s)						\
({									\
	( type_is((_s)->u64s, u64) ? le64_to_cpu((__force __le64) (_s)->u64s)		\
	: type_is((_s)->u64s, u32) ? le32_to_cpu((__force __le32) (_s)->u64s)		\
	: type_is((_s)->u64s, u16) ? le16_to_cpu((__force __le16) (_s)->u64s)		\
	: ((__force u8) ((_s)->u64s)));						\
})

#define __vstruct_bytes(_type, _u64s)					\
({									\
	BUILD_BUG_ON(offsetof(_type, _data) % sizeof(u64));		\
									\
	(size_t) (offsetof(_type, _data) + (_u64s) * sizeof(u64));	\
})

#define vstruct_bytes(_s)						\
	__vstruct_bytes(typeof(*(_s)), __vstruct_u64s(_s))

#define __vstruct_blocks(_type, _sector_block_bits, _u64s)		\
	(round_up(__vstruct_bytes(_type, _u64s),			\
		  512 << (_sector_block_bits)) >> (9 + (_sector_block_bits)))

#define vstruct_blocks(_s, _sector_block_bits)				\
	__vstruct_blocks(typeof(*(_s)), _sector_block_bits, __vstruct_u64s(_s))

#define vstruct_blocks_plus(_s, _sector_block_bits, _u64s)		\
	__vstruct_blocks(typeof(*(_s)), _sector_block_bits,		\
			 __vstruct_u64s(_s) + (_u64s))

#define vstruct_sectors(_s, _sector_block_bits)				\
	(round_up(vstruct_bytes(_s), 512 << (_sector_block_bits)) >> 9)

#define vstruct_next(_s)						\
	((typeof(_s))			((u64 *) (_s)->_data + __vstruct_u64s(_s)))
#define vstruct_last(_s)						\
	((typeof(&(_s)->start[0]))	((u64 *) (_s)->_data + __vstruct_u64s(_s)))
#define vstruct_end(_s)							\
	((void *)			((u64 *) (_s)->_data + __vstruct_u64s(_s)))

#define vstruct_for_each(_s, _i)					\
	for (_i = (_s)->start;						\
	     _i < vstruct_last(_s);					\
	     _i = vstruct_next(_i))

#define vstruct_for_each_safe(_s, _i, _t)				\
	for (_i = (_s)->start;						\
	     _i < vstruct_last(_s) && (_t = vstruct_next(_i), true);	\
	     _i = _t)

#define vstruct_idx(_s, _idx)						\
	((typeof(&(_s)->start[0])) ((_s)->_data + (_idx)))

#endif /* _VSTRUCTS_H */
