/*	$OpenBSD: devreg.h,v 1.3 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: devreg.h,v 1.5 2006/01/21 04:57:07 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SH_DEVREG_H_
#define	_SH_DEVREG_H_
/*
 * SH embedded device register defines.
 */

/*
 * Access method
 */
#define	_reg_read_1(a)		(*(volatile uint8_t *)((vaddr_t)(a)))
#define	_reg_read_2(a)		(*(volatile uint16_t *)((vaddr_t)(a)))
#define	_reg_read_4(a)		(*(volatile uint32_t *)((vaddr_t)(a)))
#define	_reg_write_1(a, v)						\
	(*(volatile uint8_t *)(a)  = (uint8_t)(v))
#define	_reg_write_2(a, v)						\
	(*(volatile uint16_t *)(a) = (uint16_t)(v))
#define	_reg_write_4(a, v)						\
	(*(volatile uint32_t *)(a) = (uint32_t)(v))
#define	_reg_bset_1(a, v)						\
	(*(volatile uint8_t *)(a)  |= (uint8_t)(v))
#define	_reg_bset_2(a, v)						\
	(*(volatile uint16_t *)(a) |= (uint16_t)(v))
#define	_reg_bset_4(a, v)						\
	(*(volatile uint32_t *)(a) |= (uint32_t)(v))
#define	_reg_bclr_1(a, v)						\
	(*(volatile uint8_t *)(a)  &= ~(uint8_t)(v))
#define	_reg_bclr_2(a, v)						\
	(*(volatile uint16_t *)(a) &= ~(uint16_t)(v))
#define	_reg_bclr_4(a, v)						\
	(*(volatile uint32_t *)(a) &= ~(uint32_t)(v))

/*
 * Register address.
 */
#if defined(SH3) && defined(SH4)
#define	SH_(x)		__sh_ ## x
#elif defined(SH3)
#define	SH_(x)		SH3_ ## x
#elif defined(SH4)
#define	SH_(x)		SH4_ ## x
#endif

#ifndef _LOCORE
/* Initialize register address for SH3 && SH4 kernel. */
void sh_devreg_init(void);
#endif
#endif /* !_SH_DEVREG_H_ */
