/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_INTR_MACHDEP_H_
#define	_MACHINE_INTR_MACHDEP_H_

#define	INTR_VECTORS	256

#define	MAX_PICS		32
#define	MAP_IRQ(node, pin)	powerpc_get_irq(node, pin)

/*
 * Default base address for MSI messages on PowerPC
 */
#define	MSI_INTEL_ADDR_BASE		0xfee00000

extern device_t root_pic;

struct trapframe;

driver_filter_t powerpc_ipi_handler;

void	intrcnt_add(const char *name, u_long **countp);

u_int	powerpc_register_pic(device_t, uint32_t, u_int, u_int, u_int);
u_int	powerpc_get_irq(uint32_t, u_int);

void	powerpc_dispatch_intr(u_int, struct trapframe *);
int	powerpc_enable_intr(void);
int	powerpc_setup_intr(const char *, u_int, driver_filter_t, driver_intr_t,
	    void *, enum intr_type, void **);
int	powerpc_teardown_intr(void *);
int	powerpc_bind_intr(u_int irq, u_char cpu);
int	powerpc_config_intr(int, enum intr_trigger, enum intr_polarity);
int	powerpc_fw_config_intr(int irq, int sense_code);

void	powerpc_intr_mask(u_int irq);
void	powerpc_intr_unmask(u_int irq);

#endif /* _MACHINE_INTR_MACHDEP_H_ */
