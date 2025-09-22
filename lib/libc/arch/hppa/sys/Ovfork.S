/*	$OpenBSD: Ovfork.S,v 1.16 2023/12/10 16:45:51 deraadt Exp $	*/

/*
 * Copyright (c) 1999,2002 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "SYS.h"

SYSENTRY_HIDDEN(vfork)
	.import errno, data
	copy	rp, t4
	ldil	L%SYSCALLGATE, r1
99:	ble	4(sr7, r1)
	PINSYSCALL(__CONCAT(SYS_,vfork), 99b)
	 ldi	__CONCAT(SYS_,vfork), t1
	comb,=	r0, t1, 1f
	copy	t4, rp
	/* set errno */
	mfctl	cr27, r1
	stw	t1, TCB_OFFSET_ERRNO(r1)
	ldi	-1, ret0
1:	bv	r0(rp)
	 nop
SYSEXIT_HIDDEN(vfork)

	.end
