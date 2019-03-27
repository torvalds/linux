/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
 * $FreeBSD$
 */

#ifndef __X86_INIT_H__
#define __X86_INIT_H__
/*
 * Struct containing pointers to init functions whose
 * implementation is run time selectable.  Selection can be made,
 * for example, based on detection of a BIOS variant or
 * hypervisor environment.
 */
struct init_ops {
	caddr_t	(*parse_preload_data)(u_int64_t);
	void	(*early_clock_source_init)(void);
	void	(*early_delay)(int);
	void	(*parse_memmap)(caddr_t, vm_paddr_t *, int *);
	void	(*mp_bootaddress)(vm_paddr_t *, unsigned int *);
	int	(*start_all_aps)(void);
	void	(*msi_init)(void);
};

extern struct init_ops init_ops;

/* Knob to disable acpi_cpu devices */
extern bool acpi_cpu_disabled;

/* Knob to disable acpi_hpet device */
extern bool acpi_hpet_disabled;

/* Knob to disable acpi_timer device */
extern bool acpi_timer_disabled;

#endif /* __X86_INIT_H__ */
