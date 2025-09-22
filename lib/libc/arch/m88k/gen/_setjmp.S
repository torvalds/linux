/* $OpenBSD: _setjmp.S,v 1.14 2016/09/22 18:23:58 guenther Exp $ */
/*-
 * Copyright (c) 2002 Steve Murphree, Jr.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Steve Murphree, Jr.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "SYS.h"

/*
 * C library -- _setjmp, _longjmp
 *
 *	_longjmp(a,v)
 * will generate a "return(v)" from the last call to
 *	_setjmp(a)
 * by restoring registers from the stack.  The previous signal state is
 * neither saved nor restored.
 *
 * For m88k, we define our jmp_buf length to be the size of 21 (_JBLEN) longs.
 * The buffer layout is as follows:
 *
 * jmp_buf[0]		return address
 * jmp_buf[1]		unused
 * jmp_buf[2 to 19]	r14 to r31
 * jmp_buf[20]		setjmp type
 */

#define	U_SETJMP_SIG	0x1764

/*
int _setjmp(jmp_buf env);
 */
ENTRY(_setjmp)
	st	%r1, %r2,0	/* save registers to the environment buffer */
	st	%r14,%r2,8
	st	%r15,%r2,12
	st	%r16,%r2,16
	st	%r17,%r2,20
	st	%r18,%r2,24
	st	%r19,%r2,28
	st	%r20,%r2,32
	st	%r21,%r2,36
	st	%r22,%r2,40
	st	%r23,%r2,44
	st	%r24,%r2,48
	st	%r25,%r2,52
	st	%r26,%r2,56
	st	%r27,%r2,60
	st	%r28,%r2,64
	st	%r29,%r2,68
	st	%r30,%r2,72
	st	%r31,%r2,76
	or	%r4,%r0,U_SETJMP_SIG	/* setjmp type to _setjmp */
	st	%r4,%r2,80
	jmp.n	%r1
	 or	%r2,%r0,0		/* return 0 */
END(_setjmp)

/*
void _longjmp(jmp_buf env, int val);
 */
ENTRY(_longjmp)
	bcnd	eq0,%r2,2f	/* check for bad environment buffer address. */
	ld	%r4,%r2,80	/* check setjmp type. */
	cmp	%r4,%r4,U_SETJMP_SIG	/* should be U_SETJMP_SIG */
	bb1	ne,%r4,2f	/* if != U_SETJMP_SIG, abort. */
	
	ld	%r14,%r2,8	/* restore registers from the environment buffer */
	ld	%r15,%r2,12
	ld	%r16,%r2,16
	ld	%r17,%r2,20
	ld	%r18,%r2,24
	ld	%r19,%r2,28
	ld	%r20,%r2,32
	ld	%r21,%r2,36
	ld	%r22,%r2,40
	ld	%r23,%r2,44
	ld	%r24,%r2,48
	ld	%r25,%r2,52
	ld	%r26,%r2,56
	ld	%r27,%r2,60
	ld	%r28,%r2,64
	ld	%r29,%r2,68
	ld	%r30,%r2,72
	ld	%r31,%r2,76
	ld	%r1,%r2,0		/* restore r1 */
	bcnd.n	ne0,%r3,1f
	 or	%r2,%r3,%r0
	or	%r2,%r0,1		/* never return zero! */
1:	jmp	%r1

2:	br	_HIDDEN(abort)		/* NO RETURN */
END(_longjmp)
