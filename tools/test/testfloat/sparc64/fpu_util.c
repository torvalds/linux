/*-
 * Copyright (c) 2010 by Peter Jeremy <peterjeremy@acm.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "__sparc_utrap_private.h"
#include "fpu_extern.h"
#include "fpu_reg.h"

static u_long	ireg[32];

void
__utrap_panic(const char *msg)
{

	fprintf(stderr, "panic: %s\n", msg);
	exit(1);
}

void __utrap_write(const char *msg)
{

	fprintf(stderr, "%s", msg);
}

u_long
__emul_fetch_reg(struct utrapframe *uf, int reg)
{

	return (ireg[reg]);
}

typedef unsigned char int8;
typedef unsigned int int32;
typedef unsigned long int64;
typedef unsigned int float32;
typedef unsigned long float64;
typedef struct {
	unsigned long high, low;
} float128;
typedef unsigned long flag;

struct utrapframe utf;

u_int32_t __fpreg[64];

static __inline float128
__fpu_getreg128(int r)
{
	float128 v;

	v.high = ((u_int64_t)__fpreg[r] << 32 | (u_int64_t)__fpreg[r + 1]);
	v.low = ((u_int64_t)__fpreg[r + 2] << 32 | (u_int64_t)__fpreg[r + 3]);
	return (v);
}

static __inline void
__fpu_setreg128(int r, float128 v)
{

	__fpreg[r] = (u_int32_t)(v.high >> 32);
	__fpreg[r + 1] = (u_int32_t)v.high;
	__fpreg[r + 2] = (u_int32_t)(v.low >> 32);
	__fpreg[r + 3] = (u_int32_t)v.low;
}

/*
-------------------------------------------------------------------------------
Clears the system's IEC/IEEE floating-point exception flags.  Returns the
previous value of the flags.
-------------------------------------------------------------------------------
*/
#include <fenv.h>
#include <ieeefp.h>

int8 syst_float_flags_clear(void)
{
	int32 flags;

	flags = (utf.uf_fsr & FE_ALL_EXCEPT) >> 5;
	utf.uf_fsr &= ~(u_long)FE_ALL_EXCEPT;
	return (flags);
}

static void
emul_trap(const u_int *insn, u_long mask)
{
	u_int32_t savreg[64];
	int i;

	for (i = 0; i < 64; i++)
	    savreg[i] = __fpreg[i];

	utf.uf_fsr = (utf.uf_fsr & ~FSR_FTT_MASK) |
		(FSR_FTT_UNFIN << FSR_FTT_SHIFT);
	utf.uf_pc = (u_long)insn;
	if (__fpu_exception(&utf) == 0)
	    __asm("stx %%fsr,%0" : "=m" (utf.uf_fsr));
	
	for (i = 0; i < 64; i++) {
		if (!(mask & (1UL << i)) && savreg[i] != __fpreg[i]) {
			fprintf(stderr, "### %2d %08x != %08x\n",
			    i, savreg[i], __fpreg[i]);
		}
	}
}

extern u_int insn_int32_to_float32;
extern u_int insn_int32_to_float64;
extern u_int insn_int32_to_float128;
extern u_int insn_int64_to_float32;
extern u_int insn_int64_to_float64;
extern u_int insn_int64_to_float128;
extern u_int insn_float32_to_int32_round_to_zero;
extern u_int insn_float32_to_int64_round_to_zero;
extern u_int insn_float32_to_float64;
extern u_int insn_float32_to_float128;
extern u_int insn_float32_add;
extern u_int insn_float32_sub;
extern u_int insn_float32_mul;
extern u_int insn_float32_div;
extern u_int insn_float32_sqrt;
extern u_int insn_float32_cmp;
extern u_int insn_float32_cmpe;
extern u_int insn_float64_to_int32_round_to_zero;
extern u_int insn_float64_to_int64_round_to_zero;
extern u_int insn_float64_to_float32;
extern u_int insn_float64_to_float128;
extern u_int insn_float64_add;
extern u_int insn_float64_sub;
extern u_int insn_float64_mul;
extern u_int insn_float64_div;
extern u_int insn_float64_sqrt;
extern u_int insn_float64_cmp;
extern u_int insn_float64_cmpe;
extern u_int insn_float128_to_int32_round_to_zero;
extern u_int insn_float128_to_int64_round_to_zero;
extern u_int insn_float128_to_float32;
extern u_int insn_float128_to_float64;
extern u_int insn_float128_add;
extern u_int insn_float128_sub;
extern u_int insn_float128_mul;
extern u_int insn_float128_div;
extern u_int insn_float128_sqrt;
extern u_int insn_float128_cmp;
extern u_int insn_float128_cmpe;

float32
syst_int32_to_float32(int32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_int32_to_float32, 0x1UL);
	return (__fpu_getreg(0));
}

float64
syst_int32_to_float64(int32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_int32_to_float64, 0x3UL);
	return (__fpu_getreg64(0));
}

float128
syst_int32_to_float128(int32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_int32_to_float128, 0xfUL);
	return (__fpu_getreg128(0));
}

float32
syst_int64_to_float32(int64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_int64_to_float32, 0x1UL);
	return (__fpu_getreg(0));
}

float64
syst_int64_to_float64(int64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_int64_to_float64, 0x3UL);
	return (__fpu_getreg64(0));
}


float128
syst_int64_to_float128(int64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_int64_to_float128, 0xfUL);
	return (__fpu_getreg128(0));
}

int32
syst_float32_to_int32_round_to_zero(float32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_float32_to_int32_round_to_zero, 0x1UL);
	return (__fpu_getreg(0));
}

int64
syst_float32_to_int64_round_to_zero(float32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_float32_to_int64_round_to_zero, 0x3UL);
	return (__fpu_getreg64(0));
}

float64
syst_float32_to_float64(float32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_float32_to_float64, 0x3UL);
	return (__fpu_getreg64(0));
}

float128
syst_float32_to_float128(float32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_float32_to_float128, 0xfUL);
	return (__fpu_getreg128(0));
}

float32
syst_float32_add(float32 a, float32 b)
{

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_add, 0x1UL);
	return (__fpu_getreg(0));
}

float32
syst_float32_sub(float32 a, float32 b)
{

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_sub, 0x1UL);
	return (__fpu_getreg(0));
}

float32
syst_float32_mul(float32 a, float32 b)
{

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_mul, 0x1UL);
	return (__fpu_getreg(0));
}

float32
syst_float32_div(float32 a, float32 b)
{

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_div, 0x1UL);
	return (__fpu_getreg(0));
}

float32
syst_float32_sqrt(float32 a)
{

	__fpu_setreg(0, a);
	emul_trap(&insn_float32_sqrt, 0x1UL);
	return (__fpu_getreg(0));
}

flag syst_float32_eq(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float32_le(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float32_lt(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float32_eq_signaling(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float32_le_quiet(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float32_lt_quiet(float32 a, float32 b)
{
	u_long r;

	__fpu_setreg(0, a);
	__fpu_setreg(1, b);
	emul_trap(&insn_float32_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}

int32
syst_float64_to_int32_round_to_zero(float64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_float64_to_int32_round_to_zero, 0x1UL);
	return (__fpu_getreg(0));
}

int64
syst_float64_to_int64_round_to_zero(float64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_float64_to_int64_round_to_zero, 0x3UL);
	return (__fpu_getreg64(0));
}

float32
syst_float64_to_float32(float64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_float64_to_float32, 0x1UL);
	return (__fpu_getreg(0));
}

float128
syst_float64_to_float128(float64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_float64_to_float128, 0xfUL);
	return (__fpu_getreg128(0));
}

float64
syst_float64_add(float64 a, float64 b)
{

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_add, 0x3UL);
	return (__fpu_getreg64(0));
}

float64
syst_float64_sub(float64 a, float64 b)
{

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_sub, 0x3UL);
	return (__fpu_getreg64(0));
}

float64
syst_float64_mul(float64 a, float64 b)
{

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_mul, 0x3UL);
	return (__fpu_getreg64(0));
}

float64
syst_float64_div(float64 a, float64 b)
{

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_div, 0x3UL);
	return (__fpu_getreg64(0));
}

float64
syst_float64_sqrt(float64 a)
{

	__fpu_setreg64(0, a);
	emul_trap(&insn_float64_sqrt, 0x3UL);
	return (__fpu_getreg64(0));
}

flag syst_float64_eq(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float64_le(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float64_lt(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float64_eq_signaling(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float64_le_quiet(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float64_lt_quiet(float64 a, float64 b)
{
	u_long r;

	__fpu_setreg64(0, a);
	__fpu_setreg64(2, b);
	emul_trap(&insn_float64_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}

int32
syst_float128_to_int32_round_to_zero(float128 a)
{

	__fpu_setreg128(0, a);
	emul_trap(&insn_float128_to_int32_round_to_zero, 0x1UL);
	return (__fpu_getreg(0));
}

int64
syst_float128_to_int64_round_to_zero(float128 a)
{

	__fpu_setreg128(0, a);
	emul_trap(&insn_float128_to_int64_round_to_zero, 0x3UL);
	return (__fpu_getreg64(0));
}

float32
syst_float128_to_float32(float128 a)
{

	__fpu_setreg128(0, a);
	emul_trap(&insn_float128_to_float32, 0x1UL);
	return (__fpu_getreg(0));
}

float64
syst_float128_to_float64(float128 a)
{

	__fpu_setreg128(0, a);
	emul_trap(&insn_float128_to_float64, 0x3UL);
	return (__fpu_getreg64(0));
}

float128
syst_float128_add(float128 a, float128 b)
{

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_add, 0xfUL);
	return (__fpu_getreg128(0));
}

float128
syst_float128_sub(float128 a, float128 b)
{

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_sub, 0xfUL);
	return (__fpu_getreg128(0));
}

float128
syst_float128_mul(float128 a, float128 b)
{

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_mul, 0xfUL);
	return (__fpu_getreg128(0));
}

float128
syst_float128_div(float128 a, float128 b)
{

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_div, 0xfUL);
	return (__fpu_getreg128(0));
}

float128
syst_float128_sqrt(float128 a)
{

	__fpu_setreg128(0, a);
	emul_trap(&insn_float128_sqrt, 0xfUL);
	return (__fpu_getreg128(0));
}

flag syst_float128_eq(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float128_le(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float128_lt(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float128_eq_signaling(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmpe, 0x0UL);
	__asm __volatile("mov 0,%0; move %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float128_le_quiet(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movle %%fcc0,1,%0" : "=r" (r));
	return (r);
}

flag syst_float128_lt_quiet(float128 a, float128 b)
{
	u_long r;

	__fpu_setreg128(0, a);
	__fpu_setreg128(4, b);
	emul_trap(&insn_float128_cmp, 0x0UL);
	__asm __volatile("mov 0,%0; movl %%fcc0,1,%0" : "=r" (r));
	return (r);
}


/*
-------------------------------------------------------------------------------
Sets the system's IEC/IEEE floating-point rounding mode.
-------------------------------------------------------------------------------
*/
void syst_float_set_rounding_mode(int8 roundingMode)
{

	utf.uf_fsr &= ~FSR_RD_MASK;
	utf.uf_fsr |= FSR_RD((unsigned int)roundingMode & 0x03);
}

/*
-------------------------------------------------------------------------------
Does nothing.
-------------------------------------------------------------------------------
*/
void syst_float_set_rounding_precision(int8 precision)
{

}
