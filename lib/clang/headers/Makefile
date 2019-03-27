# $FreeBSD$

.include "../clang.pre.mk"

.PATH:		${CLANG_SRCS}/lib/Headers

INCSDIR=	${LIBDIR}/clang/8.0.0/include

GENINCS+=	arm_fp16.h
GENINCS+=	arm_neon.h

INCS+=		__clang_cuda_builtin_vars.h
INCS+=		__clang_cuda_cmath.h
INCS+=		__clang_cuda_complex_builtins.h
INCS+=		__clang_cuda_device_functions.h
INCS+=		__clang_cuda_intrinsics.h
INCS+=		__clang_cuda_libdevice_declares.h
INCS+=		__clang_cuda_math_forward_declares.h
INCS+=		__clang_cuda_runtime_wrapper.h
INCS+=		__stddef_max_align_t.h
INCS+=		__wmmintrin_aes.h
INCS+=		__wmmintrin_pclmul.h
INCS+=		adxintrin.h
INCS+=		altivec.h
INCS+=		ammintrin.h
INCS+=		arm64intr.h
INCS+=		arm_acle.h
INCS+=		armintr.h
INCS+=		avx2intrin.h
INCS+=		avx512bitalgintrin.h
INCS+=		avx512bwintrin.h
INCS+=		avx512cdintrin.h
INCS+=		avx512dqintrin.h
INCS+=		avx512erintrin.h
INCS+=		avx512fintrin.h
INCS+=		avx512ifmaintrin.h
INCS+=		avx512ifmavlintrin.h
INCS+=		avx512pfintrin.h
INCS+=		avx512vbmi2intrin.h
INCS+=		avx512vbmiintrin.h
INCS+=		avx512vbmivlintrin.h
INCS+=		avx512vlbitalgintrin.h
INCS+=		avx512vlbwintrin.h
INCS+=		avx512vlcdintrin.h
INCS+=		avx512vldqintrin.h
INCS+=		avx512vlintrin.h
INCS+=		avx512vlvbmi2intrin.h
INCS+=		avx512vlvnniintrin.h
INCS+=		avx512vnniintrin.h
INCS+=		avx512vpopcntdqintrin.h
INCS+=		avx512vpopcntdqvlintrin.h
INCS+=		avxintrin.h
INCS+=		bmi2intrin.h
INCS+=		bmiintrin.h
INCS+=		cetintrin.h
INCS+=		cldemoteintrin.h
INCS+=		clflushoptintrin.h
INCS+=		clwbintrin.h
INCS+=		clzerointrin.h
INCS+=		cpuid.h
INCS+=		emmintrin.h
INCS+=		f16cintrin.h
INCS+=		fma4intrin.h
INCS+=		fmaintrin.h
INCS+=		fxsrintrin.h
INCS+=		gfniintrin.h
INCS+=		htmintrin.h
INCS+=		htmxlintrin.h
INCS+=		ia32intrin.h
INCS+=		immintrin.h
INCS+=		invpcidintrin.h
INCS+=		lwpintrin.h
INCS+=		lzcntintrin.h
INCS+=		mm3dnow.h
INCS+=		mm_malloc.h
INCS+=		mmintrin.h
INCS+=		module.modulemap
INCS+=		movdirintrin.h
INCS+=		msa.h
INCS+=		mwaitxintrin.h
INCS+=		nmmintrin.h
INCS+=		opencl-c.h
INCS+=		pconfigintrin.h
INCS+=		pkuintrin.h
INCS+=		pmmintrin.h
INCS+=		popcntintrin.h
INCS+=		prfchwintrin.h
INCS+=		ptwriteintrin.h
INCS+=		rdseedintrin.h
INCS+=		rtmintrin.h
INCS+=		s390intrin.h
INCS+=		sgxintrin.h
INCS+=		shaintrin.h
INCS+=		smmintrin.h
INCS+=		tbmintrin.h
INCS+=		tmmintrin.h
INCS+=		vadefs.h
INCS+=		vaesintrin.h
INCS+=		vecintrin.h
INCS+=		vpclmulqdqintrin.h
INCS+=		waitpkgintrin.h
INCS+=		wbnoinvdintrin.h
INCS+=		wmmintrin.h
INCS+=		x86intrin.h
INCS+=		xmmintrin.h
INCS+=		xopintrin.h
INCS+=		xsavecintrin.h
INCS+=		xsaveintrin.h
INCS+=		xsaveoptintrin.h
INCS+=		xsavesintrin.h
INCS+=		xtestintrin.h
INCS+=		${GENINCS}

# Headers which possibly conflict with our own versions:
.if defined(INSTALL_CONFLICTING_CLANG_HEADERS)
INCS+=		float.h
INCS+=		intrin.h
INCS+=		inttypes.h
INCS+=		iso646.h
INCS+=		limits.h
INCS+=		stdalign.h
INCS+=		stdarg.h
INCS+=		stdatomic.h
INCS+=		stdbool.h
INCS+=		stddef.h
INCS+=		stdint.h
INCS+=		stdnoreturn.h
INCS+=		tgmath.h
INCS+=		unwind.h
INCS+=		varargs.h
.endif

arm_fp16.h: ${CLANG_SRCS}/include/clang/Basic/arm_fp16.td
	${CLANG_TBLGEN} -gen-arm-fp16 \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/arm_fp16.td

arm_neon.h: ${CLANG_SRCS}/include/clang/Basic/arm_neon.td
	${CLANG_TBLGEN} -gen-arm-neon \
	    -I ${CLANG_SRCS}/include/clang/Basic -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${CLANG_SRCS}/include/clang/Basic/arm_neon.td

CLEANFILES=	${GENINCS} ${GENINCS:C/$/.d/}

.include <bsd.prog.mk>
