/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 David Chisnall
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
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

#ifndef _UTIL_HH_
#define _UTIL_HH_

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

// If we aren't using C++11, then just ignore static asserts.
#if __cplusplus < 201103L
#ifndef static_assert
#define static_assert(x, y) ((void)0)
#endif
#endif

namespace dtc {

/**
 * Type for a buffer of bytes.  This is used for a lot of short-lived temporary
 * variables, so may eventually be changed to something like LLVM's
 * SmallVector, but currently the program runs in a tiny fraction of a second,
 * so this is not an issue.
 */
typedef std::vector<uint8_t> byte_buffer;

/**
 * Helper function to push a big endian value into a byte buffer.  We use
 * native-endian values for all of the in-memory data structures and only
 * transform them into big endian form for output.
 */
template<typename T>
inline void push_big_endian(byte_buffer &v, T val)
{
	static_assert(sizeof(T) > 1,
		"Big endian doesn't make sense for single-byte values");
	for (int bit=(sizeof(T) - 1)*8 ; bit>=0 ; bit-= 8)
	{
		v.push_back((val >> bit) & 0xff);
	}
}

void push_string(byte_buffer &v, const std::string &s, bool escapes=false);

/**
 * Simple inline non-locale-aware check that this is a valid ASCII
 * digit.
 */
inline bool isdigit(char c)
{
	return (c >= '0') && (c <= '9');
}

/**
 * Simple inline non-locale-aware check that this is a valid ASCII
 * hex digit.
 */
inline bool ishexdigit(char c)
{
	return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
		((c >= 'A') && (c <= 'F'));
}

/**
 * Simple inline non-locale-aware check that this is a valid ASCII
 * letter.
 */
inline bool isalpha(char c)
{
	return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}

/**
 * A wrapper around dirname(3) that handles inconsistencies relating to memory
 * management between platforms and provides a std::string interface.
 */
std::string dirname(const std::string&);

/**
 * A wrapper around basename(3) that handles inconsistencies relating to memory
 * management between platforms and provides a std::string interface.
 */
std::string basename(const std::string&);

}// namespace dtc

#endif // !_UTIL_HH_
