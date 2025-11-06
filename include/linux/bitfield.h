/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
 */

#ifndef _LINUX_BITFIELD_H
#define _LINUX_BITFIELD_H

#include <linux/build_bug.h>
#include <linux/typecheck.h>
#include <asm/byteorder.h>

/*
 * Bitfield access macros
 *
 * FIELD_{GET,PREP} macros take as first parameter shifted mask
 * from which they extract the base mask and shift amount.
 * Mask must be a compilation time constant.
 * field_{get,prep} are variants that take a non-const mask.
 *
 * Example:
 *
 *  #include <linux/bitfield.h>
 *  #include <linux/bits.h>
 *
 *  #define REG_FIELD_A  GENMASK(6, 0)
 *  #define REG_FIELD_B  BIT(7)
 *  #define REG_FIELD_C  GENMASK(15, 8)
 *  #define REG_FIELD_D  GENMASK(31, 16)
 *
 * Get:
 *  a = FIELD_GET(REG_FIELD_A, reg);
 *  b = FIELD_GET(REG_FIELD_B, reg);
 *
 * Set:
 *  reg = FIELD_PREP(REG_FIELD_A, 1) |
 *	  FIELD_PREP(REG_FIELD_B, 0) |
 *	  FIELD_PREP(REG_FIELD_C, c) |
 *	  FIELD_PREP(REG_FIELD_D, 0x40);
 *
 * Modify:
 *  FIELD_MODIFY(REG_FIELD_C, &reg, c);
 */

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define __scalar_type_to_unsigned_cases(type)				\
		unsigned type:	(unsigned type)0,			\
		signed type:	(unsigned type)0

#define __unsigned_scalar_typeof(x) typeof(				\
		_Generic((x),						\
			char:	(unsigned char)0,			\
			__scalar_type_to_unsigned_cases(char),		\
			__scalar_type_to_unsigned_cases(short),		\
			__scalar_type_to_unsigned_cases(int),		\
			__scalar_type_to_unsigned_cases(long),		\
			__scalar_type_to_unsigned_cases(long long),	\
			default: (x)))

#define __bf_cast_unsigned(type, x)	((__unsigned_scalar_typeof(type))(x))

#define __BF_FIELD_CHECK_MASK(_mask, _val, _pfx)			\
	({								\
		BUILD_BUG_ON_MSG(!__builtin_constant_p(_mask),		\
				 _pfx "mask is not constant");		\
		BUILD_BUG_ON_MSG((_mask) == 0, _pfx "mask is zero");	\
		BUILD_BUG_ON_MSG(__builtin_constant_p(_val) ?		\
				 ~((_mask) >> __bf_shf(_mask)) &	\
					(0 + (_val)) : 0,		\
				 _pfx "value too large for the field"); \
		__BUILD_BUG_ON_NOT_POWER_OF_2((_mask) +			\
					      (1ULL << __bf_shf(_mask))); \
	})

#define __BF_FIELD_CHECK_REG(mask, reg, pfx)				\
	BUILD_BUG_ON_MSG(__bf_cast_unsigned(mask, mask) >		\
			 __bf_cast_unsigned(reg, ~0ull),		\
			 pfx "type of reg too small for mask")

#define __BF_FIELD_CHECK(mask, reg, val, pfx)				\
	({								\
		__BF_FIELD_CHECK_MASK(mask, val, pfx);			\
		__BF_FIELD_CHECK_REG(mask, reg, pfx);			\
	})

#define __FIELD_PREP(mask, val, pfx)					\
	({								\
		__BF_FIELD_CHECK_MASK(mask, val, pfx);			\
		((typeof(mask))(val) << __bf_shf(mask)) & (mask);	\
	})

#define __FIELD_GET(mask, reg, pfx)					\
	({								\
		__BF_FIELD_CHECK_MASK(mask, 0U, pfx);			\
		(typeof(mask))(((reg) & (mask)) >> __bf_shf(mask));	\
	})

/**
 * FIELD_MAX() - produce the maximum value representable by a field
 * @_mask: shifted mask defining the field's length and position
 *
 * FIELD_MAX() returns the maximum value that can be held in the field
 * specified by @_mask.
 */
#define FIELD_MAX(_mask)						\
	({								\
		__BF_FIELD_CHECK(_mask, 0ULL, 0ULL, "FIELD_MAX: ");	\
		(typeof(_mask))((_mask) >> __bf_shf(_mask));		\
	})

/**
 * FIELD_FIT() - check if value fits in the field
 * @_mask: shifted mask defining the field's length and position
 * @_val:  value to test against the field
 *
 * Return: true if @_val can fit inside @_mask, false if @_val is too big.
 */
#define FIELD_FIT(_mask, _val)						\
	({								\
		__BF_FIELD_CHECK(_mask, 0ULL, 0ULL, "FIELD_FIT: ");	\
		!((((typeof(_mask))_val) << __bf_shf(_mask)) & ~(_mask)); \
	})

/**
 * FIELD_PREP() - prepare a bitfield element
 * @_mask: shifted mask defining the field's length and position
 * @_val:  value to put in the field
 *
 * FIELD_PREP() masks and shifts up the value.  The result should
 * be combined with other fields of the bitfield using logical OR.
 */
#define FIELD_PREP(_mask, _val)						\
	({								\
		__BF_FIELD_CHECK_REG(_mask, 0ULL, "FIELD_PREP: ");	\
		__FIELD_PREP(_mask, _val, "FIELD_PREP: ");		\
	})

#define __BF_CHECK_POW2(n)	BUILD_BUG_ON_ZERO(((n) & ((n) - 1)) != 0)

/**
 * FIELD_PREP_CONST() - prepare a constant bitfield element
 * @_mask: shifted mask defining the field's length and position
 * @_val:  value to put in the field
 *
 * FIELD_PREP_CONST() masks and shifts up the value.  The result should
 * be combined with other fields of the bitfield using logical OR.
 *
 * Unlike FIELD_PREP() this is a constant expression and can therefore
 * be used in initializers. Error checking is less comfortable for this
 * version, and non-constant masks cannot be used.
 */
#define FIELD_PREP_CONST(_mask, _val)					\
	(								\
		/* mask must be non-zero */				\
		BUILD_BUG_ON_ZERO((_mask) == 0) +			\
		/* check if value fits */				\
		BUILD_BUG_ON_ZERO(~((_mask) >> __bf_shf(_mask)) & (_val)) + \
		/* check if mask is contiguous */			\
		__BF_CHECK_POW2((_mask) + (1ULL << __bf_shf(_mask))) +	\
		/* and create the value */				\
		(((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask))	\
	)

/**
 * FIELD_GET() - extract a bitfield element
 * @_mask: shifted mask defining the field's length and position
 * @_reg:  value of entire bitfield
 *
 * FIELD_GET() extracts the field specified by @_mask from the
 * bitfield passed in as @_reg by masking and shifting it down.
 */
#define FIELD_GET(_mask, _reg)						\
	({								\
		__BF_FIELD_CHECK_REG(_mask, _reg, "FIELD_GET: ");	\
		__FIELD_GET(_mask, _reg, "FIELD_GET: ");		\
	})

/**
 * FIELD_MODIFY() - modify a bitfield element
 * @_mask: shifted mask defining the field's length and position
 * @_reg_p: pointer to the memory that should be updated
 * @_val: value to store in the bitfield
 *
 * FIELD_MODIFY() modifies the set of bits in @_reg_p specified by @_mask,
 * by replacing them with the bitfield value passed in as @_val.
 */
#define FIELD_MODIFY(_mask, _reg_p, _val)						\
	({										\
		typecheck_pointer(_reg_p);						\
		__BF_FIELD_CHECK(_mask, *(_reg_p), _val, "FIELD_MODIFY: ");		\
		*(_reg_p) &= ~(_mask);							\
		*(_reg_p) |= (((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask));	\
	})

extern void __compiletime_error("value doesn't fit into mask")
__field_overflow(void);
extern void __compiletime_error("bad bitfield mask")
__bad_mask(void);
static __always_inline u64 field_multiplier(u64 field)
{
	if ((field | (field - 1)) & ((field | (field - 1)) + 1))
		__bad_mask();
	return field & -field;
}
static __always_inline u64 field_mask(u64 field)
{
	return field / field_multiplier(field);
}
#define field_max(field)	((typeof(field))field_mask(field))
#define ____MAKE_OP(type,base,to,from)					\
static __always_inline __##type __must_check type##_encode_bits(base v, base field)	\
{									\
	if (__builtin_constant_p(v) && (v & ~field_mask(field)))	\
		__field_overflow();					\
	return to((v & field_mask(field)) * field_multiplier(field));	\
}									\
static __always_inline __##type __must_check type##_replace_bits(__##type old,	\
							base val, base field)	\
{									\
	return (old & ~to(field)) | type##_encode_bits(val, field);	\
}									\
static __always_inline void type##p_replace_bits(__##type *p,		\
					base val, base field)		\
{									\
	*p = (*p & ~to(field)) | type##_encode_bits(val, field);	\
}									\
static __always_inline base __must_check type##_get_bits(__##type v, base field)	\
{									\
	return (from(v) & field)/field_multiplier(field);		\
}
#define __MAKE_OP(size)							\
	____MAKE_OP(le##size,u##size,cpu_to_le##size,le##size##_to_cpu)	\
	____MAKE_OP(be##size,u##size,cpu_to_be##size,be##size##_to_cpu)	\
	____MAKE_OP(u##size,u##size,,)
____MAKE_OP(u8,u8,,)
__MAKE_OP(16)
__MAKE_OP(32)
__MAKE_OP(64)
#undef __MAKE_OP
#undef ____MAKE_OP

#define __field_prep(mask, val)						\
	({								\
		__auto_type __mask = (mask);				\
		typeof(__mask) __val = (val);				\
		unsigned int __shift = BITS_PER_TYPE(__mask) <= 32 ?	\
				       __ffs(__mask) : __ffs64(__mask);	\
		(__val << __shift) & __mask;				\
	})

#define __field_get(mask, reg)						\
	({								\
		__auto_type __mask = (mask);				\
		typeof(__mask) __reg =  (reg);				\
		unsigned int __shift = BITS_PER_TYPE(__mask) <= 32 ?	\
				       __ffs(__mask) : __ffs64(__mask);	\
		(__reg & __mask) >> __shift;				\
	})

/**
 * field_prep() - prepare a bitfield element
 * @mask: shifted mask defining the field's length and position, must be
 *        non-zero
 * @val:  value to put in the field
 *
 * Return: field value masked and shifted to its final destination
 *
 * field_prep() masks and shifts up the value.  The result should be
 * combined with other fields of the bitfield using logical OR.
 * Unlike FIELD_PREP(), @mask is not limited to a compile-time constant.
 * Typical usage patterns are a value stored in a table, or calculated by
 * shifting a constant by a variable number of bits.
 * If you want to ensure that @mask is a compile-time constant, please use
 * FIELD_PREP() directly instead.
 */
#define field_prep(mask, val)						\
	(__builtin_constant_p(mask) ? __FIELD_PREP(mask, val, "field_prep: ") \
				    : __field_prep(mask, val))

/**
 * field_get() - extract a bitfield element
 * @mask: shifted mask defining the field's length and position, must be
 *        non-zero
 * @reg:  value of entire bitfield
 *
 * Return: extracted field value
 *
 * field_get() extracts the field specified by @mask from the
 * bitfield passed in as @reg by masking and shifting it down.
 * Unlike FIELD_GET(), @mask is not limited to a compile-time constant.
 * Typical usage patterns are a value stored in a table, or calculated by
 * shifting a constant by a variable number of bits.
 * If you want to ensure that @mask is a compile-time constant, please use
 * FIELD_GET() directly instead.
 */
#define field_get(mask, reg)						\
	(__builtin_constant_p(mask) ? __FIELD_GET(mask, reg, "field_get: ") \
				    : __field_get(mask, reg))

#endif
