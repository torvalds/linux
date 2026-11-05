/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_BITFIELD_FIX_WIDTH_H
#define _LINUX_BITFIELD_FIX_WIDTH_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/byteorder.h>

extern void __compiletime_error("value doesn't fit into mask")
__field_overflow(void);

extern void __compiletime_error("bad bitfield mask")
__bad_mask(void);

static __always_inline
u64 field_multiplier(u64 field)
{
	if ((field | (field - 1)) & ((field | (field - 1)) + 1))
		__bad_mask();
	return field & -field;
}

static __always_inline
u64 field_mask(u64 field)
{
	return field / field_multiplier(field);
}

#define field_max(field)	((typeof(field))field_mask(field))

#define __assert_field(v, field)						\
	do {								\
		if (__builtin_constant_p(v) &&				\
		    ((v) & ~field_mask(field)))				\
			__field_overflow();				\
	} while (0)

static __always_inline __must_check
__u8 u8_encode_bits(u8 v, u8 field)
{
	__assert_field(v, field);
	return (v & field_mask(field)) * field_multiplier(field);
}

static __always_inline __must_check
__u8 u8_replace_bits(__u8 old, u8 val, u8 field)
{
	return (old & ~field) | u8_encode_bits(val, field);
}

static __always_inline
void u8p_replace_bits(__u8 *p, u8 val, u8 field)
{
	*p = (*p & ~field) | u8_encode_bits(val, field);
}

static __always_inline __must_check
u8 u8_get_bits(__u8 v, u8 field)
{
	return (v & field) / field_multiplier(field);
}

static __always_inline __must_check
__le16 le16_encode_bits(u16 v, u16 field)
{
	__assert_field(v, field);
	return cpu_to_le16((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline
void le16p_replace_bits(__le16 *p, u16 val, u16 field)
{
	*p = (*p & ~cpu_to_le16(field)) | le16_encode_bits(val, field);
}

static __always_inline __must_check
u16 le16_get_bits(__le16 v, u16 field)
{
	return (le16_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__be16 be16_encode_bits(u16 v, u16 field)
{
	__assert_field(v, field);
	return cpu_to_be16((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline __must_check
u16 be16_get_bits(__be16 v, u16 field)
{
	return (be16_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__be16 be16_replace_bits(__be16 old, u16 val, u16 field)
{
	return (old & ~cpu_to_be16(field)) | be16_encode_bits(val, field);
}

static __always_inline __must_check
__u16 u16_encode_bits(u16 v, u16 field)
{
	__assert_field(v, field);
	return (v & field_mask(field)) * field_multiplier(field);
}

static __always_inline __must_check
__u16 u16_replace_bits(__u16 old, u16 val, u16 field)
{
	return (old & ~field) | u16_encode_bits(val, field);
}

static __always_inline
void u16p_replace_bits(__u16 *p, u16 val, u16 field)
{
	*p = (*p & ~field) | u16_encode_bits(val, field);
}

static __always_inline __must_check
u16 u16_get_bits(__u16 v, u16 field)
{
	return (v & field) / field_multiplier(field);
}

static __always_inline __must_check
__le32 le32_encode_bits(u32 v, u32 field)
{
	__assert_field(v, field);
	return cpu_to_le32((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline
void le32p_replace_bits(__le32 *p, u32 val, u32 field)
{
	*p = (*p & ~cpu_to_le32(field)) | le32_encode_bits(val, field);
}

static __always_inline __must_check
u32 le32_get_bits(__le32 v, u32 field)
{
	return (le32_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__be32 be32_encode_bits(u32 v, u32 field)
{
	__assert_field(v, field);
	return cpu_to_be32((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline
void be32p_replace_bits(__be32 *p, u32 val, u32 field)
{
	*p = (*p & ~cpu_to_be32(field)) | be32_encode_bits(val, field);
}

static __always_inline __must_check
u32 be32_get_bits(__be32 v, u32 field)
{
	return (be32_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__u32 u32_encode_bits(u32 v, u32 field)
{
	__assert_field(v, field);
	return (v & field_mask(field)) * field_multiplier(field);
}

static __always_inline __must_check
__u32 u32_replace_bits(__u32 old, u32 val, u32 field)
{
	return (old & ~field) | u32_encode_bits(val, field);
}

static __always_inline
void u32p_replace_bits(__u32 *p, u32 val, u32 field)
{
	*p = (*p & ~field) | u32_encode_bits(val, field);
}

static __always_inline __must_check
u32 u32_get_bits(__u32 v, u32 field)
{
	return (v & field) / field_multiplier(field);
}

static __always_inline __must_check
__le64 le64_encode_bits(u64 v, u64 field)
{
	__assert_field(v, field);
	return cpu_to_le64((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline __must_check
u64 le64_get_bits(__le64 v, u64 field)
{
	return (le64_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__be64 be64_encode_bits(u64 v, u64 field)
{
	__assert_field(v, field);
	return cpu_to_be64((v & field_mask(field)) * field_multiplier(field));
}

static __always_inline __must_check
u64 be64_get_bits(__be64 v, u64 field)
{
	return (be64_to_cpu(v) & field) / field_multiplier(field);
}

static __always_inline __must_check
__u64 u64_encode_bits(u64 v, u64 field)
{
	__assert_field(v, field);
	return (v & field_mask(field)) * field_multiplier(field);
}

static __always_inline __must_check
__u64 u64_replace_bits(__u64 old, u64 val, u64 field)
{
	return (old & ~field) | u64_encode_bits(val, field);
}

static __always_inline
void u64p_replace_bits(__u64 *p, u64 val, u64 field)
{
	*p = (*p & ~field) | u64_encode_bits(val, field);
}

static __always_inline __must_check
u64 u64_get_bits(__u64 v, u64 field)
{
	return (v & field) / field_multiplier(field);
}

#endif /* _LINUX_BITFIELD_FIX_WIDTH_H */
