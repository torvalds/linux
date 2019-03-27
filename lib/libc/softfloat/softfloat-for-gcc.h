/* $NetBSD: softfloat-for-gcc.h,v 1.8 2009/12/14 01:07:42 matt Exp $ */
/* $FreeBSD$ */

/*
 * Move private identifiers with external linkage into implementation
 * namespace.  -- Klaus Klein <kleink@NetBSD.org>, May 5, 1999
 */
#define float_exception_flags	__softfloat_float_exception_flags
#define float_exception_mask	__softfloat_float_exception_mask
#define float_rounding_mode	__softfloat_float_rounding_mode
#define float_raise		__softfloat_float_raise
/* The following batch are called by GCC through wrappers */
#define float32_eq		__softfloat_float32_eq
#define float32_le		__softfloat_float32_le
#define float32_lt		__softfloat_float32_lt
#define float64_eq		__softfloat_float64_eq
#define float64_le		__softfloat_float64_le
#define float64_lt		__softfloat_float64_lt
#define float128_eq		__softfloat_float128_eq
#define float128_le		__softfloat_float128_le
#define float128_lt		__softfloat_float128_lt

/*
 * Macros to define functions with the GCC expected names
 */

#define float32_add			__addsf3
#define float64_add			__adddf3
#define floatx80_add			__addxf3
#define float128_add			__addtf3

#define float32_sub			__subsf3
#define float64_sub			__subdf3
#define floatx80_sub			__subxf3
#define float128_sub			__subtf3

#define float32_mul			__mulsf3
#define float64_mul			__muldf3
#define floatx80_mul			__mulxf3
#define float128_mul			__multf3

#define float32_div			__divsf3
#define float64_div			__divdf3
#define floatx80_div			__divxf3
#define float128_div			__divtf3

#if 0
#define float32_neg			__negsf2
#define float64_neg			__negdf2
#define floatx80_neg			__negxf2
#define float128_neg			__negtf2
#endif

#define int32_to_float32		__floatsisf
#define int32_to_float64		__floatsidf
#define int32_to_floatx80		__floatsixf
#define int32_to_float128		__floatsitf

#define int64_to_float32		__floatdisf
#define int64_to_float64		__floatdidf
#define int64_to_floatx80		__floatdixf
#define int64_to_float128		__floatditf

#define int128_to_float32		__floattisf
#define int128_to_float64		__floattidf
#define int128_to_floatx80		__floattixf
#define int128_to_float128		__floattitf

#define uint32_to_float32		__floatunsisf
#define uint32_to_float64		__floatunsidf
#define uint32_to_floatx80		__floatunsixf
#define uint32_to_float128		__floatunsitf

#define uint64_to_float32		__floatundisf
#define uint64_to_float64		__floatundidf
#define uint64_to_floatx80		__floatundixf
#define uint64_to_float128		__floatunditf

#define uint128_to_float32		__floatuntisf
#define uint128_to_float64		__floatuntidf
#define uint128_to_floatx80		__floatuntixf
#define uint128_to_float128		__floatuntitf

#define float32_to_int32_round_to_zero	__fixsfsi
#define float64_to_int32_round_to_zero	__fixdfsi
#define floatx80_to_int32_round_to_zero __fixxfsi
#define float128_to_int32_round_to_zero __fixtfsi

#define float32_to_int64_round_to_zero	__fixsfdi
#define float64_to_int64_round_to_zero	__fixdfdi
#define floatx80_to_int64_round_to_zero	__fixxfdi
#define float128_to_int64_round_to_zero	__fixtfdi

#define float32_to_int128_round_to_zero __fixsfti
#define float64_to_int128_round_to_zero __fixdfti
#define floatx80_to_int128_round_to_zero __fixxfti
#define float128_to_int128_round_to_zero __fixtfti

#define float32_to_uint32_round_to_zero	__fixunssfsi
#define float64_to_uint32_round_to_zero	__fixunsdfsi
#define floatx80_to_uint32_round_to_zero	__fixunsxfsi
#define float128_to_uint32_round_to_zero	__fixunstfsi

#define float32_to_uint64_round_to_zero	__fixunssfdi
#define float64_to_uint64_round_to_zero	__fixunsdfdi
#define floatx80_to_uint64_round_to_zero	__fixunsxfdi
#define float128_to_uint64_round_to_zero	__fixunstfdi

#define float32_to_uint128_round_to_zero	__fixunssfti
#define float64_to_uint128_round_to_zero	__fixunsdfti
#define floatx80_to_uint128_round_to_zero	__fixunsxfti
#define float128_to_uint128_round_to_zero	__fixunstfti

#define float32_to_float64		__extendsfdf2
#define float32_to_floatx80		__extendsfxf2
#define float32_to_float128		__extendsftf2
#define float64_to_floatx80		__extenddfxf2
#define float64_to_float128		__extenddftf2

#define float128_to_float64		__trunctfdf2
#define floatx80_to_float64		__truncxfdf2
#define float128_to_float32		__trunctfsf2
#define floatx80_to_float32		__truncxfsf2
#define float64_to_float32		__truncdfsf2

#if 0
#define float32_cmp			__cmpsf2
#define float32_unord			__unordsf2
#define float32_eq			__eqsf2
#define float32_ne			__nesf2
#define float32_ge			__gesf2
#define float32_lt			__ltsf2
#define float32_le			__lesf2
#define float32_gt			__gtsf2
#endif

#if 0
#define float64_cmp			__cmpdf2
#define float64_unord			__unorddf2
#define float64_eq			__eqdf2
#define float64_ne			__nedf2
#define float64_ge			__gedf2
#define float64_lt			__ltdf2
#define float64_le			__ledf2
#define float64_gt			__gtdf2
#endif

/* XXX not in libgcc */
#if 1
#define floatx80_cmp			__cmpxf2
#define floatx80_unord			__unordxf2
#define floatx80_eq			__eqxf2
#define floatx80_ne			__nexf2
#define floatx80_ge			__gexf2
#define floatx80_lt			__ltxf2
#define floatx80_le			__lexf2
#define floatx80_gt			__gtxf2
#endif

#if 0
#define float128_cmp			__cmptf2
#define float128_unord			__unordtf2
#define float128_eq			__eqtf2
#define float128_ne			__netf2
#define float128_ge			__getf2
#define float128_lt			__lttf2
#define float128_le			__letf2
#define float128_gt			__gttf2
#endif
