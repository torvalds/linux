// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2016-2017 INRIA and Microsoft Corporation.
 * Copyright (C) 2018-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is a machine-generated formally verified implementation of Curve25519
 * ECDH from: <https://github.com/mitls/hacl-star>. Though originally machine
 * generated, it has been tweaked to be suitable for use in the kernel. It is
 * optimized for 64-bit machines that can efficiently work with 128-bit
 * integer types.
 */

#include <asm/unaligned.h>
#include <crypto/curve25519.h>
#include <linux/string.h>

static __always_inline u64 u64_eq_mask(u64 a, u64 b)
{
	u64 x = a ^ b;
	u64 minus_x = ~x + (u64)1U;
	u64 x_or_minus_x = x | minus_x;
	u64 xnx = x_or_minus_x >> (u32)63U;
	u64 c = xnx - (u64)1U;
	return c;
}

static __always_inline u64 u64_gte_mask(u64 a, u64 b)
{
	u64 x = a;
	u64 y = b;
	u64 x_xor_y = x ^ y;
	u64 x_sub_y = x - y;
	u64 x_sub_y_xor_y = x_sub_y ^ y;
	u64 q = x_xor_y | x_sub_y_xor_y;
	u64 x_xor_q = x ^ q;
	u64 x_xor_q_ = x_xor_q >> (u32)63U;
	u64 c = x_xor_q_ - (u64)1U;
	return c;
}

static __always_inline void modulo_carry_top(u64 *b)
{
	u64 b4 = b[4];
	u64 b0 = b[0];
	u64 b4_ = b4 & 0x7ffffffffffffLLU;
	u64 b0_ = b0 + 19 * (b4 >> 51);
	b[4] = b4_;
	b[0] = b0_;
}

static __always_inline void fproduct_copy_from_wide_(u64 *output, u128 *input)
{
	{
		u128 xi = input[0];
		output[0] = ((u64)(xi));
	}
	{
		u128 xi = input[1];
		output[1] = ((u64)(xi));
	}
	{
		u128 xi = input[2];
		output[2] = ((u64)(xi));
	}
	{
		u128 xi = input[3];
		output[3] = ((u64)(xi));
	}
	{
		u128 xi = input[4];
		output[4] = ((u64)(xi));
	}
}

static __always_inline void
fproduct_sum_scalar_multiplication_(u128 *output, u64 *input, u64 s)
{
	output[0] += (u128)input[0] * s;
	output[1] += (u128)input[1] * s;
	output[2] += (u128)input[2] * s;
	output[3] += (u128)input[3] * s;
	output[4] += (u128)input[4] * s;
}

static __always_inline void fproduct_carry_wide_(u128 *tmp)
{
	{
		u32 ctr = 0;
		u128 tctr = tmp[ctr];
		u128 tctrp1 = tmp[ctr + 1];
		u64 r0 = ((u64)(tctr)) & 0x7ffffffffffffLLU;
		u128 c = ((tctr) >> (51));
		tmp[ctr] = ((u128)(r0));
		tmp[ctr + 1] = ((tctrp1) + (c));
	}
	{
		u32 ctr = 1;
		u128 tctr = tmp[ctr];
		u128 tctrp1 = tmp[ctr + 1];
		u64 r0 = ((u64)(tctr)) & 0x7ffffffffffffLLU;
		u128 c = ((tctr) >> (51));
		tmp[ctr] = ((u128)(r0));
		tmp[ctr + 1] = ((tctrp1) + (c));
	}

	{
		u32 ctr = 2;
		u128 tctr = tmp[ctr];
		u128 tctrp1 = tmp[ctr + 1];
		u64 r0 = ((u64)(tctr)) & 0x7ffffffffffffLLU;
		u128 c = ((tctr) >> (51));
		tmp[ctr] = ((u128)(r0));
		tmp[ctr + 1] = ((tctrp1) + (c));
	}
	{
		u32 ctr = 3;
		u128 tctr = tmp[ctr];
		u128 tctrp1 = tmp[ctr + 1];
		u64 r0 = ((u64)(tctr)) & 0x7ffffffffffffLLU;
		u128 c = ((tctr) >> (51));
		tmp[ctr] = ((u128)(r0));
		tmp[ctr + 1] = ((tctrp1) + (c));
	}
}

static __always_inline void fmul_shift_reduce(u64 *output)
{
	u64 tmp = output[4];
	u64 b0;
	{
		u32 ctr = 5 - 0 - 1;
		u64 z = output[ctr - 1];
		output[ctr] = z;
	}
	{
		u32 ctr = 5 - 1 - 1;
		u64 z = output[ctr - 1];
		output[ctr] = z;
	}
	{
		u32 ctr = 5 - 2 - 1;
		u64 z = output[ctr - 1];
		output[ctr] = z;
	}
	{
		u32 ctr = 5 - 3 - 1;
		u64 z = output[ctr - 1];
		output[ctr] = z;
	}
	output[0] = tmp;
	b0 = output[0];
	output[0] = 19 * b0;
}

static __always_inline void fmul_mul_shift_reduce_(u128 *output, u64 *input,
						   u64 *input21)
{
	u32 i;
	u64 input2i;
	{
		u64 input2i = input21[0];
		fproduct_sum_scalar_multiplication_(output, input, input2i);
		fmul_shift_reduce(input);
	}
	{
		u64 input2i = input21[1];
		fproduct_sum_scalar_multiplication_(output, input, input2i);
		fmul_shift_reduce(input);
	}
	{
		u64 input2i = input21[2];
		fproduct_sum_scalar_multiplication_(output, input, input2i);
		fmul_shift_reduce(input);
	}
	{
		u64 input2i = input21[3];
		fproduct_sum_scalar_multiplication_(output, input, input2i);
		fmul_shift_reduce(input);
	}
	i = 4;
	input2i = input21[i];
	fproduct_sum_scalar_multiplication_(output, input, input2i);
}

static __always_inline void fmul_fmul(u64 *output, u64 *input, u64 *input21)
{
	u64 tmp[5] = { input[0], input[1], input[2], input[3], input[4] };
	{
		u128 b4;
		u128 b0;
		u128 b4_;
		u128 b0_;
		u64 i0;
		u64 i1;
		u64 i0_;
		u64 i1_;
		u128 t[5] = { 0 };
		fmul_mul_shift_reduce_(t, tmp, input21);
		fproduct_carry_wide_(t);
		b4 = t[4];
		b0 = t[0];
		b4_ = ((b4) & (((u128)(0x7ffffffffffffLLU))));
		b0_ = ((b0) + (((u128)(19) * (((u64)(((b4) >> (51))))))));
		t[4] = b4_;
		t[0] = b0_;
		fproduct_copy_from_wide_(output, t);
		i0 = output[0];
		i1 = output[1];
		i0_ = i0 & 0x7ffffffffffffLLU;
		i1_ = i1 + (i0 >> 51);
		output[0] = i0_;
		output[1] = i1_;
	}
}

static __always_inline void fsquare_fsquare__(u128 *tmp, u64 *output)
{
	u64 r0 = output[0];
	u64 r1 = output[1];
	u64 r2 = output[2];
	u64 r3 = output[3];
	u64 r4 = output[4];
	u64 d0 = r0 * 2;
	u64 d1 = r1 * 2;
	u64 d2 = r2 * 2 * 19;
	u64 d419 = r4 * 19;
	u64 d4 = d419 * 2;
	u128 s0 = ((((((u128)(r0) * (r0))) + (((u128)(d4) * (r1))))) +
		   (((u128)(d2) * (r3))));
	u128 s1 = ((((((u128)(d0) * (r1))) + (((u128)(d4) * (r2))))) +
		   (((u128)(r3 * 19) * (r3))));
	u128 s2 = ((((((u128)(d0) * (r2))) + (((u128)(r1) * (r1))))) +
		   (((u128)(d4) * (r3))));
	u128 s3 = ((((((u128)(d0) * (r3))) + (((u128)(d1) * (r2))))) +
		   (((u128)(r4) * (d419))));
	u128 s4 = ((((((u128)(d0) * (r4))) + (((u128)(d1) * (r3))))) +
		   (((u128)(r2) * (r2))));
	tmp[0] = s0;
	tmp[1] = s1;
	tmp[2] = s2;
	tmp[3] = s3;
	tmp[4] = s4;
}

static __always_inline void fsquare_fsquare_(u128 *tmp, u64 *output)
{
	u128 b4;
	u128 b0;
	u128 b4_;
	u128 b0_;
	u64 i0;
	u64 i1;
	u64 i0_;
	u64 i1_;
	fsquare_fsquare__(tmp, output);
	fproduct_carry_wide_(tmp);
	b4 = tmp[4];
	b0 = tmp[0];
	b4_ = ((b4) & (((u128)(0x7ffffffffffffLLU))));
	b0_ = ((b0) + (((u128)(19) * (((u64)(((b4) >> (51))))))));
	tmp[4] = b4_;
	tmp[0] = b0_;
	fproduct_copy_from_wide_(output, tmp);
	i0 = output[0];
	i1 = output[1];
	i0_ = i0 & 0x7ffffffffffffLLU;
	i1_ = i1 + (i0 >> 51);
	output[0] = i0_;
	output[1] = i1_;
}

static __always_inline void fsquare_fsquare_times_(u64 *output, u128 *tmp,
						   u32 count1)
{
	u32 i;
	fsquare_fsquare_(tmp, output);
	for (i = 1; i < count1; ++i)
		fsquare_fsquare_(tmp, output);
}

static __always_inline void fsquare_fsquare_times(u64 *output, u64 *input,
						  u32 count1)
{
	u128 t[5];
	memcpy(output, input, 5 * sizeof(*input));
	fsquare_fsquare_times_(output, t, count1);
}

static __always_inline void fsquare_fsquare_times_inplace(u64 *output,
							  u32 count1)
{
	u128 t[5];
	fsquare_fsquare_times_(output, t, count1);
}

static __always_inline void crecip_crecip(u64 *out, u64 *z)
{
	u64 buf[20] = { 0 };
	u64 *a0 = buf;
	u64 *t00 = buf + 5;
	u64 *b0 = buf + 10;
	u64 *t01;
	u64 *b1;
	u64 *c0;
	u64 *a;
	u64 *t0;
	u64 *b;
	u64 *c;
	fsquare_fsquare_times(a0, z, 1);
	fsquare_fsquare_times(t00, a0, 2);
	fmul_fmul(b0, t00, z);
	fmul_fmul(a0, b0, a0);
	fsquare_fsquare_times(t00, a0, 1);
	fmul_fmul(b0, t00, b0);
	fsquare_fsquare_times(t00, b0, 5);
	t01 = buf + 5;
	b1 = buf + 10;
	c0 = buf + 15;
	fmul_fmul(b1, t01, b1);
	fsquare_fsquare_times(t01, b1, 10);
	fmul_fmul(c0, t01, b1);
	fsquare_fsquare_times(t01, c0, 20);
	fmul_fmul(t01, t01, c0);
	fsquare_fsquare_times_inplace(t01, 10);
	fmul_fmul(b1, t01, b1);
	fsquare_fsquare_times(t01, b1, 50);
	a = buf;
	t0 = buf + 5;
	b = buf + 10;
	c = buf + 15;
	fmul_fmul(c, t0, b);
	fsquare_fsquare_times(t0, c, 100);
	fmul_fmul(t0, t0, c);
	fsquare_fsquare_times_inplace(t0, 50);
	fmul_fmul(t0, t0, b);
	fsquare_fsquare_times_inplace(t0, 5);
	fmul_fmul(out, t0, a);
}

static __always_inline void fsum(u64 *a, u64 *b)
{
	a[0] += b[0];
	a[1] += b[1];
	a[2] += b[2];
	a[3] += b[3];
	a[4] += b[4];
}

static __always_inline void fdifference(u64 *a, u64 *b)
{
	u64 tmp[5] = { 0 };
	u64 b0;
	u64 b1;
	u64 b2;
	u64 b3;
	u64 b4;
	memcpy(tmp, b, 5 * sizeof(*b));
	b0 = tmp[0];
	b1 = tmp[1];
	b2 = tmp[2];
	b3 = tmp[3];
	b4 = tmp[4];
	tmp[0] = b0 + 0x3fffffffffff68LLU;
	tmp[1] = b1 + 0x3ffffffffffff8LLU;
	tmp[2] = b2 + 0x3ffffffffffff8LLU;
	tmp[3] = b3 + 0x3ffffffffffff8LLU;
	tmp[4] = b4 + 0x3ffffffffffff8LLU;
	{
		u64 xi = a[0];
		u64 yi = tmp[0];
		a[0] = yi - xi;
	}
	{
		u64 xi = a[1];
		u64 yi = tmp[1];
		a[1] = yi - xi;
	}
	{
		u64 xi = a[2];
		u64 yi = tmp[2];
		a[2] = yi - xi;
	}
	{
		u64 xi = a[3];
		u64 yi = tmp[3];
		a[3] = yi - xi;
	}
	{
		u64 xi = a[4];
		u64 yi = tmp[4];
		a[4] = yi - xi;
	}
}

static __always_inline void fscalar(u64 *output, u64 *b, u64 s)
{
	u128 tmp[5];
	u128 b4;
	u128 b0;
	u128 b4_;
	u128 b0_;
	{
		u64 xi = b[0];
		tmp[0] = ((u128)(xi) * (s));
	}
	{
		u64 xi = b[1];
		tmp[1] = ((u128)(xi) * (s));
	}
	{
		u64 xi = b[2];
		tmp[2] = ((u128)(xi) * (s));
	}
	{
		u64 xi = b[3];
		tmp[3] = ((u128)(xi) * (s));
	}
	{
		u64 xi = b[4];
		tmp[4] = ((u128)(xi) * (s));
	}
	fproduct_carry_wide_(tmp);
	b4 = tmp[4];
	b0 = tmp[0];
	b4_ = ((b4) & (((u128)(0x7ffffffffffffLLU))));
	b0_ = ((b0) + (((u128)(19) * (((u64)(((b4) >> (51))))))));
	tmp[4] = b4_;
	tmp[0] = b0_;
	fproduct_copy_from_wide_(output, tmp);
}

static __always_inline void fmul(u64 *output, u64 *a, u64 *b)
{
	fmul_fmul(output, a, b);
}

static __always_inline void crecip(u64 *output, u64 *input)
{
	crecip_crecip(output, input);
}

static __always_inline void point_swap_conditional_step(u64 *a, u64 *b,
							u64 swap1, u32 ctr)
{
	u32 i = ctr - 1;
	u64 ai = a[i];
	u64 bi = b[i];
	u64 x = swap1 & (ai ^ bi);
	u64 ai1 = ai ^ x;
	u64 bi1 = bi ^ x;
	a[i] = ai1;
	b[i] = bi1;
}

static __always_inline void point_swap_conditional5(u64 *a, u64 *b, u64 swap1)
{
	point_swap_conditional_step(a, b, swap1, 5);
	point_swap_conditional_step(a, b, swap1, 4);
	point_swap_conditional_step(a, b, swap1, 3);
	point_swap_conditional_step(a, b, swap1, 2);
	point_swap_conditional_step(a, b, swap1, 1);
}

static __always_inline void point_swap_conditional(u64 *a, u64 *b, u64 iswap)
{
	u64 swap1 = 0 - iswap;
	point_swap_conditional5(a, b, swap1);
	point_swap_conditional5(a + 5, b + 5, swap1);
}

static __always_inline void point_copy(u64 *output, u64 *input)
{
	memcpy(output, input, 5 * sizeof(*input));
	memcpy(output + 5, input + 5, 5 * sizeof(*input));
}

static __always_inline void addanddouble_fmonty(u64 *pp, u64 *ppq, u64 *p,
						u64 *pq, u64 *qmqp)
{
	u64 *qx = qmqp;
	u64 *x2 = pp;
	u64 *z2 = pp + 5;
	u64 *x3 = ppq;
	u64 *z3 = ppq + 5;
	u64 *x = p;
	u64 *z = p + 5;
	u64 *xprime = pq;
	u64 *zprime = pq + 5;
	u64 buf[40] = { 0 };
	u64 *origx = buf;
	u64 *origxprime0 = buf + 5;
	u64 *xxprime0;
	u64 *zzprime0;
	u64 *origxprime;
	xxprime0 = buf + 25;
	zzprime0 = buf + 30;
	memcpy(origx, x, 5 * sizeof(*x));
	fsum(x, z);
	fdifference(z, origx);
	memcpy(origxprime0, xprime, 5 * sizeof(*xprime));
	fsum(xprime, zprime);
	fdifference(zprime, origxprime0);
	fmul(xxprime0, xprime, z);
	fmul(zzprime0, x, zprime);
	origxprime = buf + 5;
	{
		u64 *xx0;
		u64 *zz0;
		u64 *xxprime;
		u64 *zzprime;
		u64 *zzzprime;
		xx0 = buf + 15;
		zz0 = buf + 20;
		xxprime = buf + 25;
		zzprime = buf + 30;
		zzzprime = buf + 35;
		memcpy(origxprime, xxprime, 5 * sizeof(*xxprime));
		fsum(xxprime, zzprime);
		fdifference(zzprime, origxprime);
		fsquare_fsquare_times(x3, xxprime, 1);
		fsquare_fsquare_times(zzzprime, zzprime, 1);
		fmul(z3, zzzprime, qx);
		fsquare_fsquare_times(xx0, x, 1);
		fsquare_fsquare_times(zz0, z, 1);
		{
			u64 *zzz;
			u64 *xx;
			u64 *zz;
			u64 scalar;
			zzz = buf + 10;
			xx = buf + 15;
			zz = buf + 20;
			fmul(x2, xx, zz);
			fdifference(zz, xx);
			scalar = 121665;
			fscalar(zzz, zz, scalar);
			fsum(zzz, xx);
			fmul(z2, zzz, zz);
		}
	}
}

static __always_inline void
ladder_smallloop_cmult_small_loop_step(u64 *nq, u64 *nqpq, u64 *nq2, u64 *nqpq2,
				       u64 *q, u8 byt)
{
	u64 bit0 = (u64)(byt >> 7);
	u64 bit;
	point_swap_conditional(nq, nqpq, bit0);
	addanddouble_fmonty(nq2, nqpq2, nq, nqpq, q);
	bit = (u64)(byt >> 7);
	point_swap_conditional(nq2, nqpq2, bit);
}

static __always_inline void
ladder_smallloop_cmult_small_loop_double_step(u64 *nq, u64 *nqpq, u64 *nq2,
					      u64 *nqpq2, u64 *q, u8 byt)
{
	u8 byt1;
	ladder_smallloop_cmult_small_loop_step(nq, nqpq, nq2, nqpq2, q, byt);
	byt1 = byt << 1;
	ladder_smallloop_cmult_small_loop_step(nq2, nqpq2, nq, nqpq, q, byt1);
}

static __always_inline void
ladder_smallloop_cmult_small_loop(u64 *nq, u64 *nqpq, u64 *nq2, u64 *nqpq2,
				  u64 *q, u8 byt, u32 i)
{
	while (i--) {
		ladder_smallloop_cmult_small_loop_double_step(nq, nqpq, nq2,
							      nqpq2, q, byt);
		byt <<= 2;
	}
}

static __always_inline void ladder_bigloop_cmult_big_loop(u8 *n1, u64 *nq,
							  u64 *nqpq, u64 *nq2,
							  u64 *nqpq2, u64 *q,
							  u32 i)
{
	while (i--) {
		u8 byte = n1[i];
		ladder_smallloop_cmult_small_loop(nq, nqpq, nq2, nqpq2, q,
						  byte, 4);
	}
}

static void ladder_cmult(u64 *result, u8 *n1, u64 *q)
{
	u64 point_buf[40] = { 0 };
	u64 *nq = point_buf;
	u64 *nqpq = point_buf + 10;
	u64 *nq2 = point_buf + 20;
	u64 *nqpq2 = point_buf + 30;
	point_copy(nqpq, q);
	nq[0] = 1;
	ladder_bigloop_cmult_big_loop(n1, nq, nqpq, nq2, nqpq2, q, 32);
	point_copy(result, nq);
}

static __always_inline void format_fexpand(u64 *output, const u8 *input)
{
	const u8 *x00 = input + 6;
	const u8 *x01 = input + 12;
	const u8 *x02 = input + 19;
	const u8 *x0 = input + 24;
	u64 i0, i1, i2, i3, i4, output0, output1, output2, output3, output4;
	i0 = get_unaligned_le64(input);
	i1 = get_unaligned_le64(x00);
	i2 = get_unaligned_le64(x01);
	i3 = get_unaligned_le64(x02);
	i4 = get_unaligned_le64(x0);
	output0 = i0 & 0x7ffffffffffffLLU;
	output1 = i1 >> 3 & 0x7ffffffffffffLLU;
	output2 = i2 >> 6 & 0x7ffffffffffffLLU;
	output3 = i3 >> 1 & 0x7ffffffffffffLLU;
	output4 = i4 >> 12 & 0x7ffffffffffffLLU;
	output[0] = output0;
	output[1] = output1;
	output[2] = output2;
	output[3] = output3;
	output[4] = output4;
}

static __always_inline void format_fcontract_first_carry_pass(u64 *input)
{
	u64 t0 = input[0];
	u64 t1 = input[1];
	u64 t2 = input[2];
	u64 t3 = input[3];
	u64 t4 = input[4];
	u64 t1_ = t1 + (t0 >> 51);
	u64 t0_ = t0 & 0x7ffffffffffffLLU;
	u64 t2_ = t2 + (t1_ >> 51);
	u64 t1__ = t1_ & 0x7ffffffffffffLLU;
	u64 t3_ = t3 + (t2_ >> 51);
	u64 t2__ = t2_ & 0x7ffffffffffffLLU;
	u64 t4_ = t4 + (t3_ >> 51);
	u64 t3__ = t3_ & 0x7ffffffffffffLLU;
	input[0] = t0_;
	input[1] = t1__;
	input[2] = t2__;
	input[3] = t3__;
	input[4] = t4_;
}

static __always_inline void format_fcontract_first_carry_full(u64 *input)
{
	format_fcontract_first_carry_pass(input);
	modulo_carry_top(input);
}

static __always_inline void format_fcontract_second_carry_pass(u64 *input)
{
	u64 t0 = input[0];
	u64 t1 = input[1];
	u64 t2 = input[2];
	u64 t3 = input[3];
	u64 t4 = input[4];
	u64 t1_ = t1 + (t0 >> 51);
	u64 t0_ = t0 & 0x7ffffffffffffLLU;
	u64 t2_ = t2 + (t1_ >> 51);
	u64 t1__ = t1_ & 0x7ffffffffffffLLU;
	u64 t3_ = t3 + (t2_ >> 51);
	u64 t2__ = t2_ & 0x7ffffffffffffLLU;
	u64 t4_ = t4 + (t3_ >> 51);
	u64 t3__ = t3_ & 0x7ffffffffffffLLU;
	input[0] = t0_;
	input[1] = t1__;
	input[2] = t2__;
	input[3] = t3__;
	input[4] = t4_;
}

static __always_inline void format_fcontract_second_carry_full(u64 *input)
{
	u64 i0;
	u64 i1;
	u64 i0_;
	u64 i1_;
	format_fcontract_second_carry_pass(input);
	modulo_carry_top(input);
	i0 = input[0];
	i1 = input[1];
	i0_ = i0 & 0x7ffffffffffffLLU;
	i1_ = i1 + (i0 >> 51);
	input[0] = i0_;
	input[1] = i1_;
}

static __always_inline void format_fcontract_trim(u64 *input)
{
	u64 a0 = input[0];
	u64 a1 = input[1];
	u64 a2 = input[2];
	u64 a3 = input[3];
	u64 a4 = input[4];
	u64 mask0 = u64_gte_mask(a0, 0x7ffffffffffedLLU);
	u64 mask1 = u64_eq_mask(a1, 0x7ffffffffffffLLU);
	u64 mask2 = u64_eq_mask(a2, 0x7ffffffffffffLLU);
	u64 mask3 = u64_eq_mask(a3, 0x7ffffffffffffLLU);
	u64 mask4 = u64_eq_mask(a4, 0x7ffffffffffffLLU);
	u64 mask = (((mask0 & mask1) & mask2) & mask3) & mask4;
	u64 a0_ = a0 - (0x7ffffffffffedLLU & mask);
	u64 a1_ = a1 - (0x7ffffffffffffLLU & mask);
	u64 a2_ = a2 - (0x7ffffffffffffLLU & mask);
	u64 a3_ = a3 - (0x7ffffffffffffLLU & mask);
	u64 a4_ = a4 - (0x7ffffffffffffLLU & mask);
	input[0] = a0_;
	input[1] = a1_;
	input[2] = a2_;
	input[3] = a3_;
	input[4] = a4_;
}

static __always_inline void format_fcontract_store(u8 *output, u64 *input)
{
	u64 t0 = input[0];
	u64 t1 = input[1];
	u64 t2 = input[2];
	u64 t3 = input[3];
	u64 t4 = input[4];
	u64 o0 = t1 << 51 | t0;
	u64 o1 = t2 << 38 | t1 >> 13;
	u64 o2 = t3 << 25 | t2 >> 26;
	u64 o3 = t4 << 12 | t3 >> 39;
	u8 *b0 = output;
	u8 *b1 = output + 8;
	u8 *b2 = output + 16;
	u8 *b3 = output + 24;
	put_unaligned_le64(o0, b0);
	put_unaligned_le64(o1, b1);
	put_unaligned_le64(o2, b2);
	put_unaligned_le64(o3, b3);
}

static __always_inline void format_fcontract(u8 *output, u64 *input)
{
	format_fcontract_first_carry_full(input);
	format_fcontract_second_carry_full(input);
	format_fcontract_trim(input);
	format_fcontract_store(output, input);
}

static __always_inline void format_scalar_of_point(u8 *scalar, u64 *point)
{
	u64 *x = point;
	u64 *z = point + 5;
	u64 buf[10] __aligned(32) = { 0 };
	u64 *zmone = buf;
	u64 *sc = buf + 5;
	crecip(zmone, z);
	fmul(sc, x, zmone);
	format_fcontract(scalar, sc);
}

void curve25519_generic(u8 mypublic[CURVE25519_KEY_SIZE],
			const u8 secret[CURVE25519_KEY_SIZE],
			const u8 basepoint[CURVE25519_KEY_SIZE])
{
	u64 buf0[10] __aligned(32) = { 0 };
	u64 *x0 = buf0;
	u64 *z = buf0 + 5;
	u64 *q;
	format_fexpand(x0, basepoint);
	z[0] = 1;
	q = buf0;
	{
		u8 e[32] __aligned(32) = { 0 };
		u8 *scalar;
		memcpy(e, secret, 32);
		curve25519_clamp_secret(e);
		scalar = e;
		{
			u64 buf[15] = { 0 };
			u64 *nq = buf;
			u64 *x = nq;
			x[0] = 1;
			ladder_cmult(nq, scalar, q);
			format_scalar_of_point(mypublic, nq);
			memzero_explicit(buf, sizeof(buf));
		}
		memzero_explicit(e, sizeof(e));
	}
	memzero_explicit(buf0, sizeof(buf0));
}
