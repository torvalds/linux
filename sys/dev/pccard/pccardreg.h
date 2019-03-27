/*	$NetBSD: pcmciareg.h,v 1.7 1998/10/29 09:45:52 enami Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
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

/*
 * Card Configuration Registers
 */

#define	PCCARD_CCR_OPTION			0x00
#define	PCCARD_CCR_OPTION_SRESET			0x80
#define	PCCARD_CCR_OPTION_LEVIREQ			0x40
#define	PCCARD_CCR_OPTION_CFINDEX			0x3F
#define	PCCARD_CCR_OPTION_IREQ_ENABLE			0x04
#define	PCCARD_CCR_OPTION_ADDR_DECODE			0x02
#define	PCCARD_CCR_OPTION_FUNC_ENABLE			0x01
#define	PCCARD_CCR_STATUS			0x02
#define	PCCARD_CCR_STATUS_PINCHANGED			0x80
#define	PCCARD_CCR_STATUS_SIGCHG			0x40
#define	PCCARD_CCR_STATUS_IOIS8				0x20
#define	PCCARD_CCR_STATUS_RESERVED1			0x10
#define	PCCARD_CCR_STATUS_AUDIO				0x08
#define	PCCARD_CCR_STATUS_PWRDWN			0x04
#define	PCCARD_CCR_STATUS_INTR				0x02
#define	PCCARD_CCR_STATUS_INTRACK			0x01
#define	PCCARD_CCR_PIN				0x04
#define	PCCARD_CCR_PIN_CBVD1				0x80
#define	PCCARD_CCR_PIN_CBVD2				0x40
#define	PCCARD_CCR_PIN_CRDYBSY				0x20
#define	PCCARD_CCR_PIN_CWPROT				0x10
#define	PCCARD_CCR_PIN_RBVD1				0x08
#define	PCCARD_CCR_PIN_RBVD2				0x04
#define	PCCARD_CCR_PIN_RRDYBSY				0x02
#define	PCCARD_CCR_PIN_RWPROT				0x01
#define	PCCARD_CCR_SOCKETCOPY			0x06
#define	PCCARD_CCR_SOCKETCOPY_RESERVED			0x80
#define	PCCARD_CCR_SOCKETCOPY_COPY_MASK			0x70
#define	PCCARD_CCR_SOCKETCOPY_COPY_SHIFT		4
#define	PCCARD_CCR_SOCKETCOPY_SOCKET_MASK		0x0F
#define PCCARD_CCR_EXTSTATUS			0x08
#define	PCCARD_CCR_IOBASE0			0x0A
#define	PCCARD_CCR_IOBASE1			0x0C
#define	PCCARD_CCR_IOBASE2			0x0E
#define	PCCARD_CCR_IOBASE3			0x10
#define	PCCARD_CCR_IOSIZE			0x12

#define	PCCARD_CCR_SIZE				0x14
