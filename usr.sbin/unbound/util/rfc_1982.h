/*
 * util/rfc_1982.h - RFC 1982 Serial Number Arithmetic
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
#ifndef RFC_1982_H
#define RFC_1982_H

/**
 * RFC 1982 comparison, uses unsigned integers, and tries to avoid
 * compiler optimization (eg. by avoiding a-b<0 comparisons).
 * @param a: value to compare.
 * @param b: value to compare.
 * @return 0 if equal, 1 if a > b, else -1.
 */
int compare_1982(uint32_t a, uint32_t b);

/**
 * RFC 1982 subtraction, uses unsigned integers, and tries to avoid
 * compiler optimization (eg. by avoiding a-b<0 comparisons).
 * @param a: value to subtract from.
 * @param b: value to subtract.
 * @return the difference between them if we know that b is larger than a,
 *	that is the distance between them in serial number arithmetic.
 */
uint32_t subtract_1982(uint32_t a, uint32_t b);

#endif /* RFC_1982_H */
