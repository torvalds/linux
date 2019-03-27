/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _ACPI_H_
#define _ACPI_H_

#define	SCI_INT			9

#define	SMI_CMD			0xb2
#define	BHYVE_ACPI_ENABLE	0xa0
#define	BHYVE_ACPI_DISABLE	0xa1

#define	PM1A_EVT_ADDR		0x400
#define	PM1A_CNT_ADDR		0x404

#define	IO_PMTMR		0x408	/* 4-byte i/o port for the timer */

struct vmctx;

int	acpi_build(struct vmctx *ctx, int ncpu);
void	dsdt_line(const char *fmt, ...);
void	dsdt_fixed_ioport(uint16_t iobase, uint16_t length);
void	dsdt_fixed_irq(uint8_t irq);
void	dsdt_fixed_mem32(uint32_t base, uint32_t length);
void	dsdt_indent(int levels);
void	dsdt_unindent(int levels);
void	sci_init(struct vmctx *ctx);

#endif /* _ACPI_H_ */
