/*
 * Copyright (C) 2013 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef AEABI_VFP_H
#define	AEABI_VFP_H

#include <machine/acle-compat.h>

/*
 * ASM helper macros. These allow the functions to be changed depending on
 * the endian-ness we are building for.
 */

/* Allow the name of the function to be changed depending on the ABI */
#ifndef __ARM_PCS_VFP
#define	AEABI_ENTRY(x)	ENTRY(__aeabi_ ## x ## _vfp)
#define	AEABI_END(x)	END(__aeabi_ ## x ## _vfp)
#else
#define	AEABI_ENTRY(x)	ENTRY(__aeabi_ ## x)
#define	AEABI_END(x)	END(__aeabi_ ## x)
#endif

/*
 * These should be used when a function either takes, or returns a floating
 * point falue. They will load the data from an ARM to a VFP register(s),
 * or from a VFP to an ARM register
 */
#ifdef __ARM_BIG_ENDIAN
#define	LOAD_DREG(vreg, reg0, reg1)   vmov vreg, reg1, reg0
#define	UNLOAD_DREG(reg0, reg1, vreg) vmov reg1, reg0, vreg
#else
#define	LOAD_DREG(vreg, reg0, reg1)   vmov vreg, reg0, reg1
#define	UNLOAD_DREG(reg0, reg1, vreg) vmov reg0, reg1, vreg
#endif

#define	LOAD_SREGS(vreg0, vreg1, reg0, reg1) vmov vreg0, vreg1, reg0, reg1
#define	LOAD_SREG(vreg, reg)                 vmov vreg, reg
#define	UNLOAD_SREG(reg, vreg)               vmov reg, vreg

/*
 * C Helper macros
 */

#if __ARM_ARCH >= 6
/*
 * Generate a function that will either call into the VFP implementation,
 * or the soft float version for a given __aeabi_* helper. The function
 * will take a single argument of the type given by in_type.
 */
#define	AEABI_FUNC(name, in_type, soft_func)			\
__aeabi_ ## name(in_type a)					\
{								\
	if (_libc_arm_fpu_present)				\
		return __aeabi_ ## name ## _vfp(a);		\
	else							\
		return soft_func (a);				\
}

/* As above, but takes two arguments of the same type */
#define	AEABI_FUNC2(name, in_type, soft_func)			\
__aeabi_ ## name(in_type a, in_type b)				\
{								\
	if (_libc_arm_fpu_present)				\
		return __aeabi_ ## name ## _vfp(a, b);	\
	else							\
		return soft_func (a, b);			\
}

/* As above, but with the soft float arguments reversed */
#define	AEABI_FUNC2_REV(name, in_type, soft_func)		\
__aeabi_ ## name(in_type a, in_type b)				\
{								\
	if (_libc_arm_fpu_present)				\
		return __aeabi_ ## name ## _vfp(a, b);	\
	else							\
		return soft_func (b, a);			\
}
#else
/*
 * Helper macros for when we are only able to use the softfloat
 * version of these functions, i.e. on arm before armv6.
 */
#define	AEABI_FUNC(name, in_type, soft_func)			\
__aeabi_ ## name(in_type a)					\
{								\
	return soft_func (a);					\
}

/* As above, but takes two arguments of the same type */
#define	AEABI_FUNC2(name, in_type, soft_func)			\
__aeabi_ ## name(in_type a, in_type b)				\
{								\
	return soft_func (a, b);				\
}

/* As above, but with the soft float arguments reversed */
#define	AEABI_FUNC2_REV(name, in_type, soft_func)		\
__aeabi_ ## name(in_type a, in_type b)				\
{								\
	return soft_func (b, a);				\
}
#endif

#endif

