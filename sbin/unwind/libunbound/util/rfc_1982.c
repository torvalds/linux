/*
 * util/rfc_1982.c - RFC 1982 Serial Number Arithmetic
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions for RFC 1982 serial number arithmetic.
 */
#include "config.h"
#include "util/rfc_1982.h"

int
compare_1982(uint32_t a, uint32_t b)
{
	/* for 32 bit values */
	const uint32_t cutoff = ((uint32_t) 1 << (32 - 1));

	if (a == b) {
		return 0;
	} else if ((a < b && b - a < cutoff) || (a > b && a - b > cutoff)) {
		return -1;
	} else {
		return 1;
	}
}

uint32_t
subtract_1982(uint32_t a, uint32_t b)
{
	/* for 32 bit values */
	const uint32_t cutoff = ((uint32_t) 1 << (32 - 1));

	if(a == b)
		return 0;
	if(a < b && b - a < cutoff) {
		return b-a;
	}
	if(a > b && a - b > cutoff) {
		return ((uint32_t)0xffffffff) - (a-b-1);
	}
	/* wrong case, b smaller than a */
	return 0;
}
