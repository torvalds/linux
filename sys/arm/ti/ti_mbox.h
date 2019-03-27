/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Rui Paulo <rpaulo@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _TI_MBOX_H_
#define _TI_MBOX_H_

#define	TI_MBOX_REVISION		0x00
#define	TI_MBOX_SYSCONFIG		0x10
#define	TI_MBOX_SYSCONFIG_SOFTRST	0x01
#define	TI_MBOX_SYSCONFIG_SMARTIDLE	(0x02 << 2)
#define	TI_MBOX_MESSAGE(n)		(0x40 + (n) * 0x4)
#define	TI_MBOX_FIFOSTATUS(n)		(0x80 + (n) * 0x4)
#define	TI_MBOX_MSGSTATUS(n)		(0xc0 + (n) * 0x4)
#define	TI_MBOX_IRQSTATUS_RAW(n)	(0x100 + (n) * 0x10)
#define	TI_MBOX_IRQSTATUS_CLR(n)	(0x104 + (n) * 0x10)
#define	TI_MBOX_IRQENABLE_SET(n)	(0x108 + (n) * 0x10)
#define	TI_MBOX_IRQENABLE_CLR(n)	(0x10c + (n) * 0x10)

#endif /* _TI_MBOX_H_ */
