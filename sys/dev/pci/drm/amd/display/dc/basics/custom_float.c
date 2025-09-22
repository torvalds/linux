// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "custom_float.h"

static bool build_custom_float(struct fixed31_32 value,
			       const struct custom_float_format *format,
			       bool *negative,
			       uint32_t *mantissa,
			       uint32_t *exponenta)
{
	uint32_t exp_offset = (1 << (format->exponenta_bits - 1)) - 1;

	const struct fixed31_32 mantissa_constant_plus_max_fraction =
		dc_fixpt_from_fraction((1LL << (format->mantissa_bits + 1)) - 1,
				       1LL << format->mantissa_bits);

	struct fixed31_32 mantiss;

	if (dc_fixpt_eq(value, dc_fixpt_zero)) {
		*negative = false;
		*mantissa = 0;
		*exponenta = 0;
		return true;
	}

	if (dc_fixpt_lt(value, dc_fixpt_zero)) {
		*negative = format->sign;
		value = dc_fixpt_neg(value);
	} else {
		*negative = false;
	}

	if (dc_fixpt_lt(value, dc_fixpt_one)) {
		uint32_t i = 1;

		do {
			value = dc_fixpt_shl(value, 1);
			++i;
		} while (dc_fixpt_lt(value, dc_fixpt_one));

		--i;

		if (exp_offset <= i) {
			*mantissa = 0;
			*exponenta = 0;
			return true;
		}

		*exponenta = exp_offset - i;
	} else if (dc_fixpt_le(mantissa_constant_plus_max_fraction, value)) {
		uint32_t i = 1;

		do {
			value = dc_fixpt_shr(value, 1);
			++i;
		} while (dc_fixpt_lt(mantissa_constant_plus_max_fraction, value));

		*exponenta = exp_offset + i - 1;
	} else {
		*exponenta = exp_offset;
	}

	mantiss = dc_fixpt_sub(value, dc_fixpt_one);

	if (dc_fixpt_lt(mantiss, dc_fixpt_zero) ||
	    dc_fixpt_lt(dc_fixpt_one, mantiss))
		mantiss = dc_fixpt_zero;
	else
		mantiss = dc_fixpt_shl(mantiss, format->mantissa_bits);

	*mantissa = dc_fixpt_floor(mantiss);

	return true;
}

static bool setup_custom_float(const struct custom_float_format *format,
			       bool negative,
			       uint32_t mantissa,
			       uint32_t exponenta,
			       uint32_t *result)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t value = 0;

	/* verification code:
	 * once calculation is ok we can remove it
	 */

	const uint32_t mantissa_mask =
		(1 << (format->mantissa_bits + 1)) - 1;

	const uint32_t exponenta_mask =
		(1 << (format->exponenta_bits + 1)) - 1;

	if (mantissa & ~mantissa_mask) {
		BREAK_TO_DEBUGGER();
		mantissa = mantissa_mask;
	}

	if (exponenta & ~exponenta_mask) {
		BREAK_TO_DEBUGGER();
		exponenta = exponenta_mask;
	}

	/* end of verification code */

	while (i < format->mantissa_bits) {
		uint32_t mask = 1 << i;

		if (mantissa & mask)
			value |= mask;

		++i;
	}

	while (j < format->exponenta_bits) {
		uint32_t mask = 1 << j;

		if (exponenta & mask)
			value |= mask << i;

		++j;
	}

	if (negative && format->sign)
		value |= 1 << (i + j);

	*result = value;

	return true;
}

bool convert_to_custom_float_format(struct fixed31_32 value,
				    const struct custom_float_format *format,
				    uint32_t *result)
{
	uint32_t mantissa;
	uint32_t exponenta;
	bool negative;

	return build_custom_float(value, format, &negative, &mantissa, &exponenta) &&
				  setup_custom_float(format,
						     negative,
						     mantissa,
						     exponenta,
						     result);
}

