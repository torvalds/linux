/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved.
 */
#ifndef _IBA_DEFS_H_
#define _IBA_DEFS_H_

#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <asm/unaligned.h>

static inline u32 _iba_get8(const u8 *ptr)
{
	return *ptr;
}

static inline void _iba_set8(u8 *ptr, u32 mask, u32 prep_value)
{
	*ptr = (*ptr & ~mask) | prep_value;
}

static inline u16 _iba_get16(const __be16 *ptr)
{
	return be16_to_cpu(*ptr);
}

static inline void _iba_set16(__be16 *ptr, u16 mask, u16 prep_value)
{
	*ptr = cpu_to_be16((be16_to_cpu(*ptr) & ~mask) | prep_value);
}

static inline u32 _iba_get32(const __be32 *ptr)
{
	return be32_to_cpu(*ptr);
}

static inline void _iba_set32(__be32 *ptr, u32 mask, u32 prep_value)
{
	*ptr = cpu_to_be32((be32_to_cpu(*ptr) & ~mask) | prep_value);
}

static inline u64 _iba_get64(const __be64 *ptr)
{
	/*
	 * The mads are constructed so that 32 bit and smaller are naturally
	 * aligned, everything larger has a max alignment of 4 bytes.
	 */
	return be64_to_cpu(get_unaligned(ptr));
}

static inline void _iba_set64(__be64 *ptr, u64 mask, u64 prep_value)
{
	put_unaligned(cpu_to_be64((_iba_get64(ptr) & ~mask) | prep_value), ptr);
}

#define _IBA_SET(field_struct, field_offset, field_mask, num_bits, ptr, value) \
	({                                                                     \
		field_struct *_ptr = ptr;                                      \
		_iba_set##num_bits((void *)_ptr + (field_offset), field_mask,  \
				   FIELD_PREP(field_mask, value));             \
	})
#define IBA_SET(field, ptr, value) _IBA_SET(field, ptr, value)

#define _IBA_GET_MEM_PTR(field_struct, field_offset, type, num_bits, ptr)      \
	({                                                                     \
		field_struct *_ptr = ptr;                                      \
		(type *)((void *)_ptr + (field_offset));                       \
	})
#define IBA_GET_MEM_PTR(field, ptr) _IBA_GET_MEM_PTR(field, ptr)

/* FIXME: A set should always set the entire field, meaning we should zero the trailing bytes */
#define _IBA_SET_MEM(field_struct, field_offset, type, num_bits, ptr, in,      \
		     bytes)                                                    \
	({                                                                     \
		const type *_in_ptr = in;                                      \
		WARN_ON(bytes * 8 > num_bits);                                 \
		if (in && bytes)                                               \
			memcpy(_IBA_GET_MEM_PTR(field_struct, field_offset,    \
						type, num_bits, ptr),          \
			       _in_ptr, bytes);                                \
	})
#define IBA_SET_MEM(field, ptr, in, bytes) _IBA_SET_MEM(field, ptr, in, bytes)

#define _IBA_GET(field_struct, field_offset, field_mask, num_bits, ptr)        \
	({                                                                     \
		const field_struct *_ptr = ptr;                                \
		(u##num_bits) FIELD_GET(                                       \
			field_mask, _iba_get##num_bits((const void *)_ptr +    \
						       (field_offset)));       \
	})
#define IBA_GET(field, ptr) _IBA_GET(field, ptr)

#define _IBA_GET_MEM(field_struct, field_offset, type, num_bits, ptr, out,     \
		     bytes)                                                    \
	({                                                                     \
		type *_out_ptr = out;                                          \
		WARN_ON(bytes * 8 > num_bits);                                 \
		if (out && bytes)                                              \
			memcpy(_out_ptr,                                       \
			       _IBA_GET_MEM_PTR(field_struct, field_offset,    \
						type, num_bits, ptr),          \
			       bytes);                                         \
	})
#define IBA_GET_MEM(field, ptr, out, bytes) _IBA_GET_MEM(field, ptr, out, bytes)

/*
 * The generated list becomes the parameters to the macros, the order is:
 *  - struct this applies to
 *  - starting offset of the max
 *  - GENMASK or GENMASK_ULL in CPU order
 *  - The width of data the mask operations should work on, in bits
 */

/*
 * Extraction using a tabular description like table 106. bit_offset is from
 * the Byte[Bit] notation.
 */
#define IBA_FIELD_BLOC(field_struct, byte_offset, bit_offset, num_bits)        \
	field_struct, byte_offset,                                             \
		GENMASK(7 - (bit_offset), 7 - (bit_offset) - (num_bits - 1)),  \
		8
#define IBA_FIELD8_LOC(field_struct, byte_offset, num_bits)                    \
	IBA_FIELD_BLOC(field_struct, byte_offset, 0, num_bits)

#define IBA_FIELD16_LOC(field_struct, byte_offset, num_bits)                   \
	field_struct, (byte_offset)&0xFFFE,                                    \
		GENMASK(15 - (((byte_offset) % 2) * 8),                        \
			15 - (((byte_offset) % 2) * 8) - (num_bits - 1)),      \
		16

#define IBA_FIELD32_LOC(field_struct, byte_offset, num_bits)                   \
	field_struct, (byte_offset)&0xFFFC,                                    \
		GENMASK(31 - (((byte_offset) % 4) * 8),                        \
			31 - (((byte_offset) % 4) * 8) - (num_bits - 1)),      \
		32

#define IBA_FIELD64_LOC(field_struct, byte_offset)                             \
	field_struct, byte_offset, GENMASK_ULL(63, 0), 64
/*
 * In IBTA spec, everything that is more than 64bits is multiple
 * of bytes without leftover bits.
 */
#define IBA_FIELD_MLOC(field_struct, byte_offset, num_bits, type)              \
	field_struct, byte_offset, type, num_bits

#endif /* _IBA_DEFS_H_ */
