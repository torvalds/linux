/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2016-2018 NXP
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _LINUX_PACKING_H
#define _LINUX_PACKING_H

#include <linux/types.h>
#include <linux/bitops.h>

#define GEN_PACKED_FIELD_STRUCT(__type) \
	struct packed_field_ ## __type { \
		__type startbit; \
		__type endbit; \
		__type offset; \
		__type size; \
	}

/* struct packed_field_u8. Use with bit offsets < 256, buffers < 32B and
 * unpacked structures < 256B.
 */
GEN_PACKED_FIELD_STRUCT(u8);

/* struct packed_field_u16. Use with bit offsets < 65536, buffers < 8KB and
 * unpacked structures < 64KB.
 */
GEN_PACKED_FIELD_STRUCT(u16);

#define PACKED_FIELD(start, end, struct_name, struct_field) \
{ \
	(start), \
	(end), \
	offsetof(struct_name, struct_field), \
	sizeof_field(struct_name, struct_field), \
}

#define CHECK_PACKED_FIELD_OVERLAP(fields, index1, index2) ({ \
	typeof(&(fields)[0]) __f = (fields); \
	typeof(__f[0]) _f1 = __f[index1]; typeof(__f[0]) _f2 = __f[index2]; \
	const bool _ascending = __f[0].startbit < __f[1].startbit; \
	BUILD_BUG_ON_MSG(_ascending && _f1.startbit >= _f2.startbit, \
			 __stringify(fields) " field " __stringify(index2) \
			 " breaks ascending order"); \
	BUILD_BUG_ON_MSG(!_ascending && _f1.startbit <= _f2.startbit, \
			 __stringify(fields) " field " __stringify(index2) \
			 " breaks descending order"); \
	BUILD_BUG_ON_MSG(max(_f1.endbit, _f2.endbit) <= \
			 min(_f1.startbit, _f2.startbit), \
			 __stringify(fields) " field " __stringify(index2) \
			 " overlaps with previous field"); \
})

#define CHECK_PACKED_FIELD(fields, index) ({ \
	typeof(&(fields)[0]) _f = (fields); \
	typeof(_f[0]) __f = _f[index]; \
	BUILD_BUG_ON_MSG(__f.startbit < __f.endbit, \
			 __stringify(fields) " field " __stringify(index) \
			 " start bit must not be smaller than end bit"); \
	BUILD_BUG_ON_MSG(__f.size != 1 && __f.size != 2 && \
			 __f.size != 4 && __f.size != 8, \
			 __stringify(fields) " field " __stringify(index) \
			" has unsupported unpacked storage size"); \
	BUILD_BUG_ON_MSG(__f.startbit - __f.endbit >= BITS_PER_BYTE * __f.size, \
			 __stringify(fields) " field " __stringify(index) \
			 " exceeds unpacked storage size"); \
	__builtin_choose_expr(index != 0, \
			      CHECK_PACKED_FIELD_OVERLAP(fields, index - 1, index), \
			      1); \
})

/* Note that the packed fields may be either in ascending or descending order.
 * Thus, we must check that both the first and last field wit within the
 * packed buffer size.
 */
#define CHECK_PACKED_FIELDS_SIZE(fields, pbuflen) ({ \
	typeof(&(fields)[0]) _f = (fields); \
	typeof(pbuflen) _len = (pbuflen); \
	const size_t num_fields = ARRAY_SIZE(fields); \
	BUILD_BUG_ON_MSG(!__builtin_constant_p(_len), \
			 __stringify(fields) " pbuflen " __stringify(pbuflen) \
			 " must be a compile time constant"); \
	BUILD_BUG_ON_MSG(_f[0].startbit >= BITS_PER_BYTE * _len, \
			 __stringify(fields) " first field exceeds packed buffer size"); \
	BUILD_BUG_ON_MSG(_f[num_fields - 1].startbit >= BITS_PER_BYTE * _len, \
			 __stringify(fields) " last field exceeds packed buffer size"); \
})

#define QUIRK_MSB_ON_THE_RIGHT	BIT(0)
#define QUIRK_LITTLE_ENDIAN	BIT(1)
#define QUIRK_LSW32_IS_FIRST	BIT(2)

enum packing_op {
	PACK,
	UNPACK,
};

int packing(void *pbuf, u64 *uval, int startbit, int endbit, size_t pbuflen,
	    enum packing_op op, u8 quirks);

int pack(void *pbuf, u64 uval, size_t startbit, size_t endbit, size_t pbuflen,
	 u8 quirks);

int unpack(const void *pbuf, u64 *uval, size_t startbit, size_t endbit,
	   size_t pbuflen, u8 quirks);

void pack_fields_u8(void *pbuf, size_t pbuflen, const void *ustruct,
		    const struct packed_field_u8 *fields, size_t num_fields,
		    u8 quirks);

void pack_fields_u16(void *pbuf, size_t pbuflen, const void *ustruct,
		     const struct packed_field_u16 *fields, size_t num_fields,
		     u8 quirks);

void unpack_fields_u8(const void *pbuf, size_t pbuflen, void *ustruct,
		      const struct packed_field_u8 *fields, size_t num_fields,
		      u8 quirks);

void unpack_fields_u16(const void *pbuf, size_t pbuflen, void *ustruct,
		       const struct packed_field_u16 *fields, size_t num_fields,
		       u8 quirks);

/* Do not hand-edit the following packed field check macros!
 *
 * They are generated using scripts/gen_packed_field_checks.c, which may be
 * built via "make scripts_gen_packed_field_checks". If larger macro sizes are
 * needed in the future, please use this program to re-generate the macros and
 * insert them here.
 */

#define CHECK_PACKED_FIELDS_1(fields) \
	CHECK_PACKED_FIELD(fields, 0)

#define CHECK_PACKED_FIELDS_2(fields) do { \
	CHECK_PACKED_FIELDS_1(fields); \
	CHECK_PACKED_FIELD(fields, 1); \
} while (0)

#define CHECK_PACKED_FIELDS_3(fields) do { \
	CHECK_PACKED_FIELDS_2(fields); \
	CHECK_PACKED_FIELD(fields, 2); \
} while (0)

#define CHECK_PACKED_FIELDS_4(fields) do { \
	CHECK_PACKED_FIELDS_3(fields); \
	CHECK_PACKED_FIELD(fields, 3); \
} while (0)

#define CHECK_PACKED_FIELDS_5(fields) do { \
	CHECK_PACKED_FIELDS_4(fields); \
	CHECK_PACKED_FIELD(fields, 4); \
} while (0)

#define CHECK_PACKED_FIELDS_6(fields) do { \
	CHECK_PACKED_FIELDS_5(fields); \
	CHECK_PACKED_FIELD(fields, 5); \
} while (0)

#define CHECK_PACKED_FIELDS_7(fields) do { \
	CHECK_PACKED_FIELDS_6(fields); \
	CHECK_PACKED_FIELD(fields, 6); \
} while (0)

#define CHECK_PACKED_FIELDS_8(fields) do { \
	CHECK_PACKED_FIELDS_7(fields); \
	CHECK_PACKED_FIELD(fields, 7); \
} while (0)

#define CHECK_PACKED_FIELDS_9(fields) do { \
	CHECK_PACKED_FIELDS_8(fields); \
	CHECK_PACKED_FIELD(fields, 8); \
} while (0)

#define CHECK_PACKED_FIELDS_10(fields) do { \
	CHECK_PACKED_FIELDS_9(fields); \
	CHECK_PACKED_FIELD(fields, 9); \
} while (0)

#define CHECK_PACKED_FIELDS_11(fields) do { \
	CHECK_PACKED_FIELDS_10(fields); \
	CHECK_PACKED_FIELD(fields, 10); \
} while (0)

#define CHECK_PACKED_FIELDS_12(fields) do { \
	CHECK_PACKED_FIELDS_11(fields); \
	CHECK_PACKED_FIELD(fields, 11); \
} while (0)

#define CHECK_PACKED_FIELDS_13(fields) do { \
	CHECK_PACKED_FIELDS_12(fields); \
	CHECK_PACKED_FIELD(fields, 12); \
} while (0)

#define CHECK_PACKED_FIELDS_14(fields) do { \
	CHECK_PACKED_FIELDS_13(fields); \
	CHECK_PACKED_FIELD(fields, 13); \
} while (0)

#define CHECK_PACKED_FIELDS_15(fields) do { \
	CHECK_PACKED_FIELDS_14(fields); \
	CHECK_PACKED_FIELD(fields, 14); \
} while (0)

#define CHECK_PACKED_FIELDS_16(fields) do { \
	CHECK_PACKED_FIELDS_15(fields); \
	CHECK_PACKED_FIELD(fields, 15); \
} while (0)

#define CHECK_PACKED_FIELDS_17(fields) do { \
	CHECK_PACKED_FIELDS_16(fields); \
	CHECK_PACKED_FIELD(fields, 16); \
} while (0)

#define CHECK_PACKED_FIELDS_18(fields) do { \
	CHECK_PACKED_FIELDS_17(fields); \
	CHECK_PACKED_FIELD(fields, 17); \
} while (0)

#define CHECK_PACKED_FIELDS_19(fields) do { \
	CHECK_PACKED_FIELDS_18(fields); \
	CHECK_PACKED_FIELD(fields, 18); \
} while (0)

#define CHECK_PACKED_FIELDS_20(fields) do { \
	CHECK_PACKED_FIELDS_19(fields); \
	CHECK_PACKED_FIELD(fields, 19); \
} while (0)

#define CHECK_PACKED_FIELDS_21(fields) do { \
	CHECK_PACKED_FIELDS_20(fields); \
	CHECK_PACKED_FIELD(fields, 20); \
} while (0)

#define CHECK_PACKED_FIELDS_22(fields) do { \
	CHECK_PACKED_FIELDS_21(fields); \
	CHECK_PACKED_FIELD(fields, 21); \
} while (0)

#define CHECK_PACKED_FIELDS_23(fields) do { \
	CHECK_PACKED_FIELDS_22(fields); \
	CHECK_PACKED_FIELD(fields, 22); \
} while (0)

#define CHECK_PACKED_FIELDS_24(fields) do { \
	CHECK_PACKED_FIELDS_23(fields); \
	CHECK_PACKED_FIELD(fields, 23); \
} while (0)

#define CHECK_PACKED_FIELDS_25(fields) do { \
	CHECK_PACKED_FIELDS_24(fields); \
	CHECK_PACKED_FIELD(fields, 24); \
} while (0)

#define CHECK_PACKED_FIELDS_26(fields) do { \
	CHECK_PACKED_FIELDS_25(fields); \
	CHECK_PACKED_FIELD(fields, 25); \
} while (0)

#define CHECK_PACKED_FIELDS_27(fields) do { \
	CHECK_PACKED_FIELDS_26(fields); \
	CHECK_PACKED_FIELD(fields, 26); \
} while (0)

#define CHECK_PACKED_FIELDS_28(fields) do { \
	CHECK_PACKED_FIELDS_27(fields); \
	CHECK_PACKED_FIELD(fields, 27); \
} while (0)

#define CHECK_PACKED_FIELDS_29(fields) do { \
	CHECK_PACKED_FIELDS_28(fields); \
	CHECK_PACKED_FIELD(fields, 28); \
} while (0)

#define CHECK_PACKED_FIELDS_30(fields) do { \
	CHECK_PACKED_FIELDS_29(fields); \
	CHECK_PACKED_FIELD(fields, 29); \
} while (0)

#define CHECK_PACKED_FIELDS_31(fields) do { \
	CHECK_PACKED_FIELDS_30(fields); \
	CHECK_PACKED_FIELD(fields, 30); \
} while (0)

#define CHECK_PACKED_FIELDS_32(fields) do { \
	CHECK_PACKED_FIELDS_31(fields); \
	CHECK_PACKED_FIELD(fields, 31); \
} while (0)

#define CHECK_PACKED_FIELDS_33(fields) do { \
	CHECK_PACKED_FIELDS_32(fields); \
	CHECK_PACKED_FIELD(fields, 32); \
} while (0)

#define CHECK_PACKED_FIELDS_34(fields) do { \
	CHECK_PACKED_FIELDS_33(fields); \
	CHECK_PACKED_FIELD(fields, 33); \
} while (0)

#define CHECK_PACKED_FIELDS_35(fields) do { \
	CHECK_PACKED_FIELDS_34(fields); \
	CHECK_PACKED_FIELD(fields, 34); \
} while (0)

#define CHECK_PACKED_FIELDS_36(fields) do { \
	CHECK_PACKED_FIELDS_35(fields); \
	CHECK_PACKED_FIELD(fields, 35); \
} while (0)

#define CHECK_PACKED_FIELDS_37(fields) do { \
	CHECK_PACKED_FIELDS_36(fields); \
	CHECK_PACKED_FIELD(fields, 36); \
} while (0)

#define CHECK_PACKED_FIELDS_38(fields) do { \
	CHECK_PACKED_FIELDS_37(fields); \
	CHECK_PACKED_FIELD(fields, 37); \
} while (0)

#define CHECK_PACKED_FIELDS_39(fields) do { \
	CHECK_PACKED_FIELDS_38(fields); \
	CHECK_PACKED_FIELD(fields, 38); \
} while (0)

#define CHECK_PACKED_FIELDS_40(fields) do { \
	CHECK_PACKED_FIELDS_39(fields); \
	CHECK_PACKED_FIELD(fields, 39); \
} while (0)

#define CHECK_PACKED_FIELDS_41(fields) do { \
	CHECK_PACKED_FIELDS_40(fields); \
	CHECK_PACKED_FIELD(fields, 40); \
} while (0)

#define CHECK_PACKED_FIELDS_42(fields) do { \
	CHECK_PACKED_FIELDS_41(fields); \
	CHECK_PACKED_FIELD(fields, 41); \
} while (0)

#define CHECK_PACKED_FIELDS_43(fields) do { \
	CHECK_PACKED_FIELDS_42(fields); \
	CHECK_PACKED_FIELD(fields, 42); \
} while (0)

#define CHECK_PACKED_FIELDS_44(fields) do { \
	CHECK_PACKED_FIELDS_43(fields); \
	CHECK_PACKED_FIELD(fields, 43); \
} while (0)

#define CHECK_PACKED_FIELDS_45(fields) do { \
	CHECK_PACKED_FIELDS_44(fields); \
	CHECK_PACKED_FIELD(fields, 44); \
} while (0)

#define CHECK_PACKED_FIELDS_46(fields) do { \
	CHECK_PACKED_FIELDS_45(fields); \
	CHECK_PACKED_FIELD(fields, 45); \
} while (0)

#define CHECK_PACKED_FIELDS_47(fields) do { \
	CHECK_PACKED_FIELDS_46(fields); \
	CHECK_PACKED_FIELD(fields, 46); \
} while (0)

#define CHECK_PACKED_FIELDS_48(fields) do { \
	CHECK_PACKED_FIELDS_47(fields); \
	CHECK_PACKED_FIELD(fields, 47); \
} while (0)

#define CHECK_PACKED_FIELDS_49(fields) do { \
	CHECK_PACKED_FIELDS_48(fields); \
	CHECK_PACKED_FIELD(fields, 48); \
} while (0)

#define CHECK_PACKED_FIELDS_50(fields) do { \
	CHECK_PACKED_FIELDS_49(fields); \
	CHECK_PACKED_FIELD(fields, 49); \
} while (0)

#define CHECK_PACKED_FIELDS(fields) \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 1, ({ CHECK_PACKED_FIELDS_1(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 2, ({ CHECK_PACKED_FIELDS_2(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 3, ({ CHECK_PACKED_FIELDS_3(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 4, ({ CHECK_PACKED_FIELDS_4(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 5, ({ CHECK_PACKED_FIELDS_5(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 6, ({ CHECK_PACKED_FIELDS_6(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 7, ({ CHECK_PACKED_FIELDS_7(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 8, ({ CHECK_PACKED_FIELDS_8(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 9, ({ CHECK_PACKED_FIELDS_9(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 10, ({ CHECK_PACKED_FIELDS_10(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 11, ({ CHECK_PACKED_FIELDS_11(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 12, ({ CHECK_PACKED_FIELDS_12(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 13, ({ CHECK_PACKED_FIELDS_13(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 14, ({ CHECK_PACKED_FIELDS_14(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 15, ({ CHECK_PACKED_FIELDS_15(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 16, ({ CHECK_PACKED_FIELDS_16(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 17, ({ CHECK_PACKED_FIELDS_17(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 18, ({ CHECK_PACKED_FIELDS_18(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 19, ({ CHECK_PACKED_FIELDS_19(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 20, ({ CHECK_PACKED_FIELDS_20(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 21, ({ CHECK_PACKED_FIELDS_21(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 22, ({ CHECK_PACKED_FIELDS_22(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 23, ({ CHECK_PACKED_FIELDS_23(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 24, ({ CHECK_PACKED_FIELDS_24(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 25, ({ CHECK_PACKED_FIELDS_25(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 26, ({ CHECK_PACKED_FIELDS_26(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 27, ({ CHECK_PACKED_FIELDS_27(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 28, ({ CHECK_PACKED_FIELDS_28(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 29, ({ CHECK_PACKED_FIELDS_29(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 30, ({ CHECK_PACKED_FIELDS_30(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 31, ({ CHECK_PACKED_FIELDS_31(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 32, ({ CHECK_PACKED_FIELDS_32(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 33, ({ CHECK_PACKED_FIELDS_33(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 34, ({ CHECK_PACKED_FIELDS_34(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 35, ({ CHECK_PACKED_FIELDS_35(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 36, ({ CHECK_PACKED_FIELDS_36(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 37, ({ CHECK_PACKED_FIELDS_37(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 38, ({ CHECK_PACKED_FIELDS_38(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 39, ({ CHECK_PACKED_FIELDS_39(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 40, ({ CHECK_PACKED_FIELDS_40(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 41, ({ CHECK_PACKED_FIELDS_41(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 42, ({ CHECK_PACKED_FIELDS_42(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 43, ({ CHECK_PACKED_FIELDS_43(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 44, ({ CHECK_PACKED_FIELDS_44(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 45, ({ CHECK_PACKED_FIELDS_45(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 46, ({ CHECK_PACKED_FIELDS_46(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 47, ({ CHECK_PACKED_FIELDS_47(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 48, ({ CHECK_PACKED_FIELDS_48(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 49, ({ CHECK_PACKED_FIELDS_49(fields); }), \
	__builtin_choose_expr(ARRAY_SIZE(fields) == 50, ({ CHECK_PACKED_FIELDS_50(fields); }), \
	({ BUILD_BUG_ON_MSG(1, "CHECK_PACKED_FIELDS() must be regenerated to support array sizes larger than 50."); }) \
))))))))))))))))))))))))))))))))))))))))))))))))))

/* End of generated content */

#define pack_fields(pbuf, pbuflen, ustruct, fields, quirks) \
	({ \
		CHECK_PACKED_FIELDS(fields); \
		CHECK_PACKED_FIELDS_SIZE((fields), (pbuflen)); \
		_Generic((fields), \
			 const struct packed_field_u8 * : pack_fields_u8, \
			 const struct packed_field_u16 * : pack_fields_u16 \
			)((pbuf), (pbuflen), (ustruct), (fields), ARRAY_SIZE(fields), (quirks)); \
	})

#define unpack_fields(pbuf, pbuflen, ustruct, fields, quirks) \
	({ \
		CHECK_PACKED_FIELDS(fields); \
		CHECK_PACKED_FIELDS_SIZE((fields), (pbuflen)); \
		_Generic((fields), \
			 const struct packed_field_u8 * : unpack_fields_u8, \
			 const struct packed_field_u16 * : unpack_fields_u16 \
			)((pbuf), (pbuflen), (ustruct), (fields), ARRAY_SIZE(fields), (quirks)); \
	})

#endif
