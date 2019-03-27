/*-
 * Copyright (c) 2016 Jilles Tjoelker <jilles@FreeBSD.org>
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(zero);
ATF_TC_BODY(zero, tc)
{

	assert(memcmp("a", "b", 0) == 0);
	assert(memcmp("", "", 0) == 0);
}

ATF_TC_WITHOUT_HEAD(eq);
ATF_TC_BODY(eq, tc)
{
	unsigned char data1[256], data2[256];
	int i;

	for (i = 0; i < 256; i++)
		data1[i] = data2[i] = i ^ 0x55;
	for (i = 1; i < 256; i++)
		assert(memcmp(data1, data2, i) == 0);
	for (i = 1; i < 256; i++)
		assert(memcmp(data1 + i, data2 + i, 256 - i) == 0);
}

ATF_TC_WITHOUT_HEAD(neq);
ATF_TC_BODY(neq, tc)
{
	unsigned char data1[256], data2[256];
	int i;

	for (i = 0; i < 256; i++) {
		data1[i] = i;
		data2[i] = i ^ 0x55;
	}
	for (i = 1; i < 256; i++)
		assert(memcmp(data1, data2, i) != 0);
	for (i = 1; i < 256; i++)
		assert(memcmp(data1 + i, data2 + i, 256 - i) != 0);
}

ATF_TC_WITHOUT_HEAD(diff);
ATF_TC_BODY(diff, tc)
{
	unsigned char data1[256], data2[256];
	int i;

	memset(data1, 'a', sizeof(data1));
	memset(data2, 'a', sizeof(data2));
	data1[128] = 255;
	data2[128] = 0;
	for (i = 1; i < 66; i++) {
		assert(memcmp(data1 + 128, data2 + 128, i) == 255);
		assert(memcmp(data2 + 128, data1 + 128, i) == -255);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i) == 255);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i) == -255);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i * 2) == 255);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i * 2) == -255);
	}
	data1[128] = 'c';
	data2[128] = 'e';
	for (i = 1; i < 66; i++) {
		assert(memcmp(data1 + 128, data2 + 128, i) == -2);
		assert(memcmp(data2 + 128, data1 + 128, i) == 2);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i) == -2);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i) == 2);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i * 2) == -2);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i * 2) == 2);
	}
	memset(data1 + 129, 'A', sizeof(data1) - 129);
	memset(data2 + 129, 'Z', sizeof(data2) - 129);
	for (i = 1; i < 66; i++) {
		assert(memcmp(data1 + 128, data2 + 128, i) == -2);
		assert(memcmp(data2 + 128, data1 + 128, i) == 2);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i) == -2);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i) == 2);
		assert(memcmp(data1 + 129 - i, data2 + 129 - i, i * 2) == -2);
		assert(memcmp(data2 + 129 - i, data1 + 129 - i, i * 2) == 2);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, zero);
	ATF_TP_ADD_TC(tp, eq);
	ATF_TP_ADD_TC(tp, neq);
	ATF_TP_ADD_TC(tp, diff);

	return (atf_no_error());
}
