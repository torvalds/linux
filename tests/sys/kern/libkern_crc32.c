/*
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>

#include <stdint.h>

#include <atf-c.h>

#if defined(__amd64__) || defined(__i386__)
extern uint32_t sse42_crc32c(uint32_t, const unsigned char *, unsigned);
#elif defined(__aarch64__)
extern uint32_t armv8_crc32c(uint32_t, const unsigned char *, unsigned);
#else
#error These tests are not supported on this platform
#endif

ATF_TC_WITHOUT_HEAD(crc32c_basic_correctness);
ATF_TC_BODY(crc32c_basic_correctness, tc)
{
	const uint64_t inputs[] = {
		0xf408c634b3a9142,
		0x80539e8c7c352e2b,
		0x62e9121db6e4d649,
		0x899345850ed0a286,
		0x2302df11b4a43b15,
		0xe943de7b3d35d70,
		0xdf1ff2bf41abf56b,
		0x9bc138abae315de2,
		0x31cc82e56234f0ff,
		0xce63c0cd6988e847,
		0x3e42f6b78ee352fa,
		0xfa4085436078cfa6,
		0x53349558bf670a4b,
		0x2714e10e7d722c61,
		0xc0d3261addfc6908,
		0xd1567c3181d3a1bf,
	};
	const uint32_t results[] = {
		0x2ce33ede,
		0xc49cc573,
		0xb8683c96,
		0x6918660d,
		0xa904e522,
		0x52dbc42c,
		0x98863c22,
		0x894d5d2c,
		0xb003745d,
		0xfc496dbd,
		0x97d2fbb5,
		0x3c062ef1,
		0xcc2eff18,
		0x6a9b09f6,
		0x420242c1,
		0xfd562dc3,
	};
	size_t i;
	uint32_t act;

	ATF_REQUIRE(nitems(inputs) == nitems(results));

	for (i = 0; i < nitems(inputs); i++) {
#if defined(__amd64__) || defined(__i386__)
		act = sse42_crc32c(~0, (const void *)&inputs[i],
		    sizeof(inputs[0]));
#else
		act = armv8_crc32c(~0, (const void *)&inputs[i],
		    sizeof(inputs[0]));
#endif
		ATF_REQUIRE_MSG(act == results[i],
		    "crc32c(0x%jx) = 0x%08x, got 0x%08x", (uintmax_t)inputs[i],
		    results[i], act);
	}
}

ATF_TC_WITHOUT_HEAD(crc32c_alignment);
ATF_TC_BODY(crc32c_alignment, tc)
{
	const uint64_t input = 0xf408c634b3a9142;
	const uint32_t result = 0x2ce33ede;
	unsigned char buf[15];
	size_t i;
	uint32_t act;


	for (i = 1; i < 8; i++) {
		memcpy(&buf[i], &input, sizeof(input));

#if defined(__amd64__) || defined(__i386__)
		act = sse42_crc32c(~0, (const void *)&buf[i], sizeof(input));
#else
		act = armv8_crc32c(~0, (const void *)&buf[i], sizeof(input));
#endif
		ATF_REQUIRE_MSG(act == result,
		    "crc32c(0x%jx) = 0x%08x, got 0x%08x", (uintmax_t)input,
		    result, act);
	}
}

ATF_TC_WITHOUT_HEAD(crc32c_trailing_bytes);
ATF_TC_BODY(crc32c_trailing_bytes, tc)
{
	const unsigned char input[] = {
		0x87, 0x54, 0x74, 0xd2, 0xb, 0x9b, 0xdd, 0xf6, 0x68, 0x37,
		0xd4, 0x4, 0x5e, 0xa9, 0xb3
	};
	const uint32_t result = 0xec638d62;
	uint32_t act;

#if defined(__amd64__) || defined(__i386__)
	act = sse42_crc32c(~0, input, sizeof(input));
#else
	act = armv8_crc32c(~0, input, sizeof(input));
#endif
	ATF_REQUIRE_MSG(act == result, "expected 0x%08x, got 0x%08x", result,
	    act);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, crc32c_basic_correctness);
	ATF_TP_ADD_TC(tp, crc32c_alignment);
	ATF_TP_ADD_TC(tp, crc32c_trailing_bytes);
	return (atf_no_error());
}
