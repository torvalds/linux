/*	$OpenBSD: abi.h,v 1.5 2016/09/12 19:47:01 guenther Exp $	*/
/*	$NetBSD: abi.h,v 1.2 2003/09/14 21:26:14 fvdl Exp $	*/

/*
 * Written by Frank van der Linden (fvdl@wasabisystems.com)
 */

/*
 * The x86-64 ABI specifies that float, double and long double
 * arguments are passed in SSE2 (xmm) registers. Unfortunately,
 * there is no way to push those on to the FP stack, which is
 * where the fancier instructions get their arguments from.
 *
 * Define some prologues and epilogues to store and retrieve
 * xmm regs to local variables.
 */

#define ARG_DOUBLE_ONE		-8(%rsp)
#define ARG_DOUBLE_TWO		-16(%rsp)
#define ARG_FLOAT_ONE		-4(%rsp)
#define ARG_FLOAT_TWO		-8(%rsp)

#define XMM_ONE_ARG_DOUBLE_PROLOGUE \
	movsd	%xmm0, ARG_DOUBLE_ONE
	
#define XMM_TWO_ARG_DOUBLE_PROLOGUE \
	movsd	%xmm0, ARG_DOUBLE_ONE ; \
	movsd	%xmm1, ARG_DOUBLE_TWO

#define XMM_ONE_ARG_FLOAT_PROLOGUE \
	movss	%xmm0, ARG_FLOAT_ONE

#define XMM_TWO_ARG_FLOAT_PROLOGUE \
	movss	%xmm0, ARG_FLOAT_ONE ; \
	movss	%xmm1, ARG_FLOAT_TWO

#define XMM_DOUBLE_EPILOGUE \
	fstpl ARG_DOUBLE_ONE ; \
	movsd ARG_DOUBLE_ONE, %xmm0

#define XMM_FLOAT_EPILOGUE \
	fstps ARG_FLOAT_ONE ; \
	movss ARG_FLOAT_ONE, %xmm0

#define FLDL_VAR(x)	fldl x(%rip)


/*
 * We define a hidden alias with the prefix "_libm_" for each global symbol
 * that may be used internally.  By referencing _libm_x instead of x, other
 * parts of libm prevent overriding by the application and avoid unnecessary
 * relocations.
 */
#define _HIDDEN(x)		_libm_##x
#define _HIDDEN_ALIAS(x,y)			\
	STRONG_ALIAS(_HIDDEN(x),y);		\
	.hidden _HIDDEN(x)
#define _HIDDEN_FALIAS(x,y)			\
	_HIDDEN_ALIAS(x,y);			\
	.type _HIDDEN(x),@function

/*
 * For functions implemented in ASM that are used internally
 *   END_STD(x)	Like DEF_STD() in C; for standard/reserved C names
 *   END_NONSTD(x)	Like DEF_NONSTD() in C; for non-ISO C names
 */
#define	END_STD(x)	END(x); _HIDDEN_FALIAS(x,x); END(_HIDDEN(x))
#define	END_NONSTD(x)	END_STD(x); .weak x
