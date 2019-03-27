/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2018 The FreeBSD Foundation. All rights reserved.
 * Copyright (C) 2018, 2019 Andrew Turner
 *
 * This software was developed by Mitchell Horne under sponsorship of
 * the FreeBSD Foundation.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/coverage.h>

#include <machine/atomic.h>

void __sanitizer_cov_trace_pc(void);
void __sanitizer_cov_trace_cmp1(uint8_t, uint8_t);
void __sanitizer_cov_trace_cmp2(uint16_t, uint16_t);
void __sanitizer_cov_trace_cmp4(uint32_t, uint32_t);
void __sanitizer_cov_trace_cmp8(uint64_t, uint64_t);
void __sanitizer_cov_trace_const_cmp1(uint8_t, uint8_t);
void __sanitizer_cov_trace_const_cmp2(uint16_t, uint16_t);
void __sanitizer_cov_trace_const_cmp4(uint32_t, uint32_t);
void __sanitizer_cov_trace_const_cmp8(uint64_t, uint64_t);
void __sanitizer_cov_trace_switch(uint64_t, uint64_t *);

static cov_trace_pc_t cov_trace_pc;
static cov_trace_cmp_t cov_trace_cmp;

void
cov_register_pc(cov_trace_pc_t trace_pc)
{

	atomic_store_ptr(&cov_trace_pc, trace_pc);
}

void
cov_unregister_pc(void)
{

	atomic_store_ptr(&cov_trace_pc, NULL);
}

void
cov_register_cmp(cov_trace_cmp_t trace_cmp)
{

	atomic_store_ptr(&cov_trace_cmp, trace_cmp);
}

void
cov_unregister_cmp(void)
{

	atomic_store_ptr(&cov_trace_cmp, NULL);
}

/*
 * Main entry point. A call to this function will be inserted
 * at every edge, and if coverage is enabled for the thread
 * this function will add the PC to the buffer.
 */
void
__sanitizer_cov_trace_pc(void)
{
	cov_trace_pc_t trace_pc;

	trace_pc = (cov_trace_pc_t)atomic_load_ptr(&cov_trace_pc);
	if (trace_pc != NULL)
		trace_pc((uint64_t)__builtin_return_address(0));
}

/*
 * Comparison entry points. When the kernel performs a comparison
 * operation the compiler inserts a call to one of the following
 * functions to record the operation.
 */
void
__sanitizer_cov_trace_cmp1(uint8_t arg1, uint8_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(0), arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_cmp2(uint16_t arg1, uint16_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(1), arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_cmp4(uint32_t arg1, uint32_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(2), arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_cmp8(uint64_t arg1, uint64_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(3), arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_const_cmp1(uint8_t arg1, uint8_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(0) | COV_CMP_CONST, arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_const_cmp2(uint16_t arg1, uint16_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(1) | COV_CMP_CONST, arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_const_cmp4(uint32_t arg1, uint32_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(2) | COV_CMP_CONST, arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

void
__sanitizer_cov_trace_const_cmp8(uint64_t arg1, uint64_t arg2)
{
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp != NULL)
		trace_cmp(COV_CMP_SIZE(3) | COV_CMP_CONST, arg1, arg2,
		    (uint64_t)__builtin_return_address(0));
}

/*
 * val is the switch operand
 * cases[0] is the number of case constants
 * cases[1] is the size of val in bits
 * cases[2..n] are the case constants
 */
void
__sanitizer_cov_trace_switch(uint64_t val, uint64_t *cases)
{
	uint64_t i, count, ret, type;
	cov_trace_cmp_t trace_cmp;

	trace_cmp = (cov_trace_cmp_t)atomic_load_ptr(&cov_trace_cmp);
	if (trace_cmp == NULL)
		return;

	count = cases[0];
	ret = (uint64_t)__builtin_return_address(0);

	switch (cases[1]) {
	case 8:
		type = COV_CMP_SIZE(0);
		break;
	case 16:
		type = COV_CMP_SIZE(1);
		break;
	case 32:
		type = COV_CMP_SIZE(2);
		break;
	case 64:
		type = COV_CMP_SIZE(3);
		break;
	default:
		return;
	}

	val |= COV_CMP_CONST;

	for (i = 0; i < count; i++)
		if (!trace_cmp(type, val, cases[i + 2], ret))
			return;
}
