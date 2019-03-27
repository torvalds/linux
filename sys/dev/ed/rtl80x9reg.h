/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, David Madole
 * All rights reserved.
 * Copyright (c) 2005, M. Warner Losh.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * Based on patches subitted by: David Madole, edited by M. Warner Losh.
 *
 * $FreeBSD$
 */

/*
 * RTL8019/8029 Specific Registers
 */

#define ED_RTL80X9_CONFIG0	0x03
#define ED_RTL80X9_CONFIG2	0x05
#define ED_RTL80X9_CONFIG3	0x06
#define ED_RTL80X9_80X9ID0	0x0a
#define ED_RTL80X9_ID0			0x50
#define ED_RTL80X9_80X9ID1	0x0b
#define ED_RTL8019_ID1			0x70
#define ED_RTL8029_ID1			0x43

#define	ED_RTL80X9_CF0_BNC	0x04
#define ED_RTL80X9_CF0_AUI	0x20

#define ED_RTL80X9_CF2_MEDIA	0xc0
#define ED_RTL80X9_CF2_AUTO	0x00
#define ED_RTL80X9_CF2_10_T	0x40
#define ED_RTL80X9_CF2_10_5	0x80
#define ED_RTL80X9_CF2_10_2	0xc0

#define ED_RTL80X9_CF3_FUDUP	0x40

#define ED_RTL8029_PCI_ID	0x802910ec
