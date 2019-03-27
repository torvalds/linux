/*
 * Copyright 2011-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
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
 */

/*
 * Several of these are from: http://graphics.stanford.edu/~seander/bithacks.html
 */

#define CK_INTERNAL_LOG_0 (0xAAAAAAAA)
#define CK_INTERNAL_LOG_1 (0xCCCCCCCC)
#define CK_INTERNAL_LOG_2 (0xF0F0F0F0)
#define CK_INTERNAL_LOG_3 (0xFF00FF00)
#define CK_INTERNAL_LOG_4 (0xFFFF0000)

CK_CC_INLINE static uint32_t
ck_internal_log(uint32_t v)
{
        uint32_t r = (v & CK_INTERNAL_LOG_0) != 0;

	r |= ((v & CK_INTERNAL_LOG_4) != 0) << 4;
	r |= ((v & CK_INTERNAL_LOG_3) != 0) << 3;
	r |= ((v & CK_INTERNAL_LOG_2) != 0) << 2;
	r |= ((v & CK_INTERNAL_LOG_1) != 0) << 1;
        return (r);
}

CK_CC_INLINE static uint32_t
ck_internal_power_2(uint32_t v)
{

        --v;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return (++v);
}

CK_CC_INLINE static unsigned long
ck_internal_max(unsigned long x, unsigned long y)
{

	return x ^ ((x ^ y) & -(x < y));
}

CK_CC_INLINE static uint64_t
ck_internal_max_64(uint64_t x, uint64_t y)
{

	return x ^ ((x ^ y) & -(x < y));
}

CK_CC_INLINE static uint32_t
ck_internal_max_32(uint32_t x, uint32_t y)
{

	return x ^ ((x ^ y) & -(x < y));
}
