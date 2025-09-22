/*	$OpenBSD: eh.S,v 1.10 2014/04/22 22:58:02 aoyama Exp $	*/
/*
 * Copyright (c) 2006, Miodrag Vallat
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/board.h>
#include <machine/m8820x.h>

/*
 * Save Pbus fault status register from data and inst CMMU.
 *
 * PFSR_SAVE is invoked with TMP, TMP2 and TMP3 available, and TMP contains
 * curcpu().
 *
 * On the LUNA-88K we just have to peek at the CMMU PFSR our curcpu() points
 * to.
 */

#define	PFSR_SAVE \
	ld	TMP2, TMP,  CI_PFSR_I0; \
	ld	TMP3, TMP2, %r0; \
	st	TMP3, %r31, EF_IPFSR; \
	ld	TMP2, TMP,  CI_PFSR_D0; \
	ld	TMP3, TMP2, %r0; \
	st	TMP3, %r31, EF_DPFSR

#include <m88k/m88k/eh_common.S>
