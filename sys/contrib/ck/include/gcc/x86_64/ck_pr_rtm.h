/*
 * Copyright 2013-2015 Samy Al Bahra.
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
 * Copyright (c) 2012,2013 Intel Corporation
 * Author: Andi Kleen
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef CK_PR_X86_64_RTM_H
#define CK_PR_X86_64_RTM_H

#ifndef CK_PR_X86_64_H
#error Do not include this file directly, use ck_pr.h
#endif

#define CK_F_PR_RTM

#include <ck_cc.h>
#include <ck_stdbool.h>

#define CK_PR_RTM_STARTED	(~0U)
#define CK_PR_RTM_EXPLICIT	(1 << 0)
#define CK_PR_RTM_RETRY		(1 << 1)
#define CK_PR_RTM_CONFLICT	(1 << 2)
#define CK_PR_RTM_CAPACITY	(1 << 3)
#define CK_PR_RTM_DEBUG		(1 << 4)
#define CK_PR_RTM_NESTED	(1 << 5)
#define CK_PR_RTM_CODE(x)	(((x) >> 24) & 0xFF)

CK_CC_INLINE static unsigned int
ck_pr_rtm_begin(void)
{
	unsigned int r = CK_PR_RTM_STARTED;

	__asm__ __volatile__(".byte 0xc7,0xf8;"
			     ".long 0;"
				: "+a" (r)
				:
				: "memory");

	return r;
}

CK_CC_INLINE static void
ck_pr_rtm_end(void)
{

	__asm__ __volatile__(".byte 0x0f,0x01,0xd5" ::: "memory");
	return;
}

CK_CC_INLINE static void
ck_pr_rtm_abort(const unsigned int status)
{

	__asm__ __volatile__(".byte 0xc6,0xf8,%P0" :: "i" (status) : "memory");
	return;
}

CK_CC_INLINE static bool
ck_pr_rtm_test(void)
{
	bool r;

	__asm__ __volatile__(".byte 0x0f,0x01,0xd6;"
			     "setnz %0"
				: "=r" (r)
				:
				: "memory");

	return r;
}

#endif /* CK_PR_X86_64_RTM_H */

