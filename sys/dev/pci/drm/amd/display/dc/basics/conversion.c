/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "basics/conversion.h"

#define DIVIDER 10000

/* S2D13 value in [-3.00...0.9999] */
#define S2D13_MIN (-3 * DIVIDER)
#define S2D13_MAX (3 * DIVIDER)

uint16_t fixed_point_to_int_frac(
	struct fixed31_32 arg,
	uint8_t integer_bits,
	uint8_t fractional_bits)
{
	int32_t numerator;
	int32_t divisor = 1 << fractional_bits;

	uint16_t result;

	uint16_t d = (uint16_t)dc_fixpt_floor(
		dc_fixpt_abs(
			arg));

	if (d <= (uint16_t)(1 << integer_bits) - (1 / (uint16_t)divisor))
		numerator = (uint16_t)dc_fixpt_round(
			dc_fixpt_mul_int(
				arg,
				divisor));
	else {
		numerator = dc_fixpt_floor(
			dc_fixpt_sub(
				dc_fixpt_from_int(
					1LL << integer_bits),
				dc_fixpt_recip(
					dc_fixpt_from_int(
						divisor))));
	}

	if (numerator >= 0)
		result = (uint16_t)numerator;
	else
		result = (uint16_t)(
		(1 << (integer_bits + fractional_bits + 1)) + numerator);

	if ((result != 0) && dc_fixpt_lt(
		arg, dc_fixpt_zero))
		result |= 1 << (integer_bits + fractional_bits);

	return result;
}
/*
 * convert_float_matrix - This converts a double into HW register spec defined format S2D13.
 */
void convert_float_matrix(
	uint16_t *matrix,
	struct fixed31_32 *flt,
	uint32_t buffer_size)
{
	const struct fixed31_32 min_2_13 =
		dc_fixpt_from_fraction(S2D13_MIN, DIVIDER);
	const struct fixed31_32 max_2_13 =
		dc_fixpt_from_fraction(S2D13_MAX, DIVIDER);
	uint32_t i;

	for (i = 0; i < buffer_size; ++i) {
		uint32_t reg_value =
				fixed_point_to_int_frac(
					dc_fixpt_clamp(
						flt[i],
						min_2_13,
						max_2_13),
						2,
						13);

		matrix[i] = (uint16_t)reg_value;
	}
}

static struct fixed31_32 int_frac_to_fixed_point(uint16_t arg,
						 uint8_t integer_bits,
						 uint8_t fractional_bits)
{
	struct fixed31_32 result;
	uint16_t sign_mask = 1 << (fractional_bits + integer_bits);
	uint16_t value_mask = sign_mask - 1;

	result.value = (long long)(arg & value_mask) <<
		       (FIXED31_32_BITS_PER_FRACTIONAL_PART - fractional_bits);

	if (arg & sign_mask)
		result = dc_fixpt_neg(result);

	return result;
}

/**
 * convert_hw_matrix - converts HW values into fixed31_32 matrix.
 * @matrix: fixed point 31.32 matrix
 * @reg: array of register values
 * @buffer_size: size of the array of register values
 *
 * Converts HW register spec defined format S2D13 into a fixed-point 31.32
 * matrix.
 */
void convert_hw_matrix(struct fixed31_32 *matrix,
		       uint16_t *reg,
		       uint32_t buffer_size)
{
	for (int i = 0; i < buffer_size; ++i)
		matrix[i] = int_frac_to_fixed_point(reg[i], 2, 13);
}

static uint32_t find_gcd(uint32_t a, uint32_t b)
{
	uint32_t remainder;

	while (b != 0) {
		remainder = a % b;
		a = b;
		b = remainder;
	}
	return a;
}

void reduce_fraction(uint32_t num, uint32_t den,
		uint32_t *out_num, uint32_t *out_den)
{
	uint32_t gcd = 0;

	gcd = find_gcd(num, den);
	*out_num = num / gcd;
	*out_den = den / gcd;
}
