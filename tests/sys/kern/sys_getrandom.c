/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/random.h>
#include <errno.h>

#include <atf-c.h>

#include <zstd.h>

static const unsigned valid_flags[] = { 0, GRND_NONBLOCK, GRND_RANDOM,
    GRND_NONBLOCK | GRND_RANDOM };

ATF_TC_WITHOUT_HEAD(getrandom_randomness);
ATF_TC_BODY(getrandom_randomness, tc)
{
	char randomb[4096], compressed[5000];
	ssize_t ret;
	size_t i, j, c;
	unsigned mode;

	for (i = 0; i < nitems(valid_flags); i++) {
		mode = valid_flags[i];

		/* Get new random data, filling randomb. */

		memset(randomb, 0, sizeof(randomb));

		for (j = 0; j < sizeof(randomb);) {
			ret = getrandom(&randomb[j], sizeof(randomb) - j, mode);
			if (ret < 0 && (mode & GRND_NONBLOCK) != 0 &&
			    errno == EAGAIN)
				continue;

			ATF_REQUIRE_MSG(ret >= 0, "other error: %d", errno);
			ATF_REQUIRE_MSG(ret > 0, "bogus zero return");

			j += (size_t)ret;
		}

		/* Perform compressibility test */
		c = ZSTD_compress(compressed, sizeof(compressed), randomb,
		    sizeof(randomb), ZSTD_maxCLevel());
		ATF_REQUIRE_MSG(!ZSTD_isError(c), "zstd compress: %s",
		    ZSTD_getErrorName(c));

		/*
		 * If the output is very compressible, it's probably not random
		 */
		ATF_REQUIRE_MSG(c > (sizeof(randomb) * 4 / 5),
		    "purportedly random data was compressible: %zu/%zu or %f%%",
		    c, sizeof(randomb), (double)c / (double)sizeof(randomb));
	}
}

ATF_TC_WITHOUT_HEAD(getrandom_fault);
ATF_TC_BODY(getrandom_fault, tc)
{
	ssize_t ret;

	ret = getrandom(NULL, 1, 0);
	ATF_REQUIRE_EQ(ret, -1);
	ATF_REQUIRE_EQ(errno, EFAULT);
}

ATF_TC_WITHOUT_HEAD(getrandom_count);
ATF_TC_BODY(getrandom_count, tc)
{
	char buf[4096], reference[4096];
	ssize_t ret;

	/* getrandom(2) does not modify buf past the requested length */
	_Static_assert(sizeof(reference) == sizeof(buf), "must match");
	memset(reference, 0x7C, sizeof(reference));

	memset(buf, 0x7C, sizeof(buf));
	ret = getrandom(buf, 1, 0);
	ATF_REQUIRE_EQ(ret, 1);
	ATF_REQUIRE_EQ(memcmp(&buf[1], reference, sizeof(reference) - 1), 0);

	memset(buf, 0x7C, sizeof(buf));
	ATF_REQUIRE_EQ(getrandom(buf, 15, 0), 15);
	ATF_REQUIRE_EQ(memcmp(&buf[15], reference, sizeof(reference) - 15), 0);

	memset(buf, 0x7C, sizeof(buf));
	ATF_REQUIRE_EQ(getrandom(buf, 255, 0), 255);
	ATF_REQUIRE_EQ(memcmp(&buf[255], reference, sizeof(reference) - 255), 0);

	memset(buf, 0x7C, sizeof(buf));
	ATF_REQUIRE_EQ(getrandom(buf, 4095, 0), 4095);
	ATF_REQUIRE_EQ(memcmp(&buf[4095], reference, sizeof(reference) - 4095), 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getrandom_count);
	ATF_TP_ADD_TC(tp, getrandom_fault);
	ATF_TP_ADD_TC(tp, getrandom_randomness);
	return (atf_no_error());
}
