/*	$OpenBSD: setjmp.c,v 1.3 2003/07/31 21:48:06 deraadt Exp $	*/
/*
 * Copyright (c) 1993, 1994, 1995, 1996 by Chris Provenzano and contributors, 
 * proven@mit.edu All rights reserved.
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
 *	This product includes software developed by Chris Provenzano,
 *	the University of California, Berkeley, and contributors.
 * 4. Neither the name of Chris Provenzano, the University, nor the names of
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO, THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <setjmp.h>
#include <stdlib.h>
#include "test.h"

int reached;

static void *
_jump(void *arg)
{
	jmp_buf foo;

	reached = 0;
	if (_setjmp(foo)) {
		ASSERT(reached);
		return NULL;
	}
	reached = 1;
	_longjmp(foo, 1);
	PANIC("_longjmp");
}

static void *
jump(void *arg)
{
	jmp_buf foo;

	reached = 0;
	if (setjmp(foo)) {
		ASSERT(reached);
		return NULL;
	}
	reached = 1;
	longjmp(foo, 1);
	PANIC("longjmp");
}

int
main(int argc, char *argv[])
{
	pthread_t child;
	void *res;

	printf("jumping in main thread\n");
	(void)jump(NULL);
	printf("_jumping in main thread\n");
	(void)_jump(NULL);

	printf("jumping in child thread\n");
	CHECKr(pthread_create(&child, NULL, jump, NULL));
	CHECKr(pthread_join(child, &res));

	printf("_jumping in child thread\n");
	CHECKr(pthread_create(&child, NULL, _jump, NULL));
	CHECKr(pthread_join(child, &res));

	SUCCEED;
}
