/*	$OpenBSD: gai_strerror.c,v 1.8 2015/09/14 07:38:38 guenther Exp $	*/

/*
 * Copyright (c) 1997-1999, Craig Metz, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Craig Metz and
 *      by other contributors.
 * 4. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* gai_strerror() v1.38 */

#include <sys/types.h>
#include <netdb.h>
#include <errno.h>

const char *
gai_strerror(int errnum)
{
	switch (errnum) {
	case 0:
		return "no error";
	case EAI_BADFLAGS:
		return "invalid value for ai_flags";
	case EAI_NONAME:
		return "name or service is not known";
	case EAI_AGAIN:
		return "temporary failure in name resolution";
	case EAI_FAIL:
		return "non-recoverable failure in name resolution";
	case EAI_NODATA:
		return "no address associated with name";
	case EAI_FAMILY:
		return "ai_family not supported";
	case EAI_SOCKTYPE:
		return "ai_socktype not supported";
	case EAI_SERVICE:
		return "service not supported for ai_socktype";
	case EAI_ADDRFAMILY:
		return "address family for name not supported";
	case EAI_MEMORY:
		return "memory allocation failure";
	case EAI_SYSTEM:
		return "system error";
	case EAI_BADHINTS:
		return "invalid value for hints";
	case EAI_PROTOCOL:
		return "resolved protocol is unknown";
	case EAI_OVERFLOW:
		return "argument buffer overflow";
	default:
		return "unknown/invalid error";
	}
}
DEF_WEAK(gai_strerror);
