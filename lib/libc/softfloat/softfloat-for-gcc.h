/*	$OpenBSD: softfloat-for-gcc.h,v 1.3 2022/09/10 06:48:31 miod Exp $	*/
/* $NetBSD: softfloat-for-gcc.h,v 1.6 2003/07/26 19:24:51 salo Exp $ */

/*
 * Move private identifiers with external linkage into implementation
 * namespace.  -- Klaus Klein <kleink@NetBSD.org>, May 5, 1999
 */
#define float_exception_flags	_softfloat_float_exception_flags
#define float_exception_mask	_softfloat_float_exception_mask
#define float_rounding_mode	_softfloat_float_rounding_mode
#define float_raise		_softfloat_float_raise
/* The following batch are called by GCC through wrappers */
#define float32_eq		_softfloat_float32_eq
#define float32_le		_softfloat_float32_le
#define float32_lt		_softfloat_float32_lt
#define float64_eq		_softfloat_float64_eq
#define float64_le		_softfloat_float64_le
#define float64_lt		_softfloat_float64_lt

/*
 * Macros to define functions with the GCC expected names
 */

#define float32_add			__addsf3
#define float64_add			__adddf3
#define float32_sub			__subsf3
#define float64_sub			__subdf3
#define float32_mul			__mulsf3
#define float64_mul			__muldf3
#define float32_div			__divsf3
#define float64_div			__divdf3
#define int32_to_float32		__floatsisf
#define int32_to_float64		__floatsidf
#define int64_to_float32		__floatdisf
#define int64_to_float64		__floatdidf
#define float32_to_int32_round_to_zero	__fixsfsi
#define float64_to_int32_round_to_zero	__fixdfsi
#define float32_to_int64_round_to_zero	__fixsfdi
#define float64_to_int64_round_to_zero	__fixdfdi
#define float32_to_uint32_round_to_zero	__fixunssfsi
#define float64_to_uint32_round_to_zero	__fixunsdfsi
#define float32_to_float64		__extendsfdf2
#define float64_to_float32		__truncdfsf2

#ifdef __ARM_EABI__
__strong_alias(__aeabi_fadd, __addsf3);
__strong_alias(__aeabi_dadd, __adddf3);
__strong_alias(__aeabi_fsub, __subsf3);
__strong_alias(__aeabi_dsub, __subdf3);
__strong_alias(__aeabi_fmul, __mulsf3);
__strong_alias(__aeabi_dmul, __muldf3);
__strong_alias(__aeabi_fdiv, __divsf3);
__strong_alias(__aeabi_ddiv, __divdf3);
__strong_alias(__aeabi_i2f, __floatsisf);
__strong_alias(__aeabi_i2d, __floatsidf);
__strong_alias(__aeabi_l2f, __floatdisf);
__strong_alias(__aeabi_l2d, __floatdidf);
__strong_alias(__aeabi_f2iz, __fixsfsi);
__strong_alias(__aeabi_d2iz, __fixdfsi);
__strong_alias(__aeabi_f2lz, __fixsfdi);
__strong_alias(__aeabi_d2lz, __fixdfdi);
__strong_alias(__aeabi_f2uiz, __fixunssfsi);
__strong_alias(__aeabi_d2uiz, __fixunsdfsi);
__strong_alias(__aeabi_f2ulz, __fixunssfdi);
__strong_alias(__aeabi_d2ulz, __fixunsdfdi);
__strong_alias(__aeabi_f2d, __extendsfdf2);
__strong_alias(__aeabi_d2f, __truncdfsf2);
#endif
