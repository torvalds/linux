// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "dc_trace.h"

#include <linux/fpu.h>

/**
 * DOC: DC FPU manipulation overview
 *
 * DC core uses FPU operations in multiple parts of the code, which requires a
 * more specialized way to manage these areas' entrance. To fulfill this
 * requirement, we created some wrapper functions that encapsulate
 * kernel_fpu_begin/end to better fit our need in the display component. In
 * summary, in this file, you can find functions related to FPU operation
 * management.
 */

#ifdef notyet
static DEFINE_PER_CPU(int, fpu_recursion_depth);
#else
static int fpu_recursion_depth;

#define __this_cpu_read(x)		atomic_read(&x)
#define __this_cpu_inc_return(x)	atomic_inc_return(&x)
#define __this_cpu_dec_return(x)	atomic_dec_return(&x)
#endif

/**
 * dc_assert_fp_enabled - Check if FPU protection is enabled
 *
 * This function tells if the code is already under FPU protection or not. A
 * function that works as an API for a set of FPU operations can use this
 * function for checking if the caller invoked it after DC_FP_START(). For
 * example, take a look at dcn20_fpu.c file.
 */
void dc_assert_fp_enabled(void)
{
	int depth;

	depth = __this_cpu_read(fpu_recursion_depth);

	ASSERT(depth >= 1);
}

/**
 * dc_fpu_begin - Enables FPU protection
 * @function_name: A string containing the function name for debug purposes
 *   (usually __func__)
 *
 * @line: A line number where DC_FP_START was invoked for debug purpose
 *   (usually __LINE__)
 *
 * This function is responsible for managing the use of kernel_fpu_begin() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_START().
 */
void dc_fpu_begin(const char *function_name, const int line)
{
	int depth;

	WARN_ON_ONCE(!in_task());
	preempt_disable();
	depth = __this_cpu_inc_return(fpu_recursion_depth);
	if (depth == 1) {
		BUG_ON(!kernel_fpu_available());
		kernel_fpu_begin();
	}

	TRACE_DCN_FPU(true, function_name, line, depth);
}

/**
 * dc_fpu_end - Disable FPU protection
 * @function_name: A string containing the function name for debug purposes
 * @line: A-line number where DC_FP_END was invoked for debug purpose
 *
 * This function is responsible for managing the use of kernel_fpu_end() with
 * the advantage of providing an event trace for debugging.
 *
 * Note: Do not call this function directly; always use DC_FP_END().
 */
void dc_fpu_end(const char *function_name, const int line)
{
	int depth;

	depth = __this_cpu_dec_return(fpu_recursion_depth);
	if (depth == 0) {
		kernel_fpu_end();
	} else {
		WARN_ON_ONCE(depth < 0);
	}

	TRACE_DCN_FPU(false, function_name, line, depth);
	preempt_enable();
}
