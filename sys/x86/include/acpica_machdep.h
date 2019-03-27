/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Mitsuru IWASAKI
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
 * $FreeBSD$
 */

/******************************************************************************
 *
 * Name: acpica_machdep.h - arch-specific defines, etc.
 *       $Revision$
 *
 *****************************************************************************/

#ifndef __ACPICA_MACHDEP_H__
#define	__ACPICA_MACHDEP_H__

#ifdef _KERNEL
/*
 * Calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces 
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#define	ACPI_SYSTEM_XFACE
#define	ACPI_EXTERNAL_XFACE
#define	ACPI_INTERNAL_XFACE
#define	ACPI_INTERNAL_VAR_XFACE

/* Asm macros */

#define	ACPI_ASM_MACROS
#define	BREAKPOINT3
#define	ACPI_DISABLE_IRQS() disable_intr()
#define	ACPI_ENABLE_IRQS()  enable_intr()

#define	ACPI_FLUSH_CPU_CACHE()	wbinvd()

/* Section 5.2.10.1: global lock acquire/release functions */
int	acpi_acquire_global_lock(volatile uint32_t *);
int	acpi_release_global_lock(volatile uint32_t *);
#define	ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acq)	do {			\
	(Acq) = acpi_acquire_global_lock(&((GLptr)->GlobalLock));	\
} while (0)
#define	ACPI_RELEASE_GLOBAL_LOCK(GLptr, Acq)	do {			\
	(Acq) = acpi_release_global_lock(&((GLptr)->GlobalLock));	\
} while (0)
 
enum intr_trigger;
enum intr_polarity;

void	acpi_SetDefaultIntrModel(int model);
void	acpi_cpu_c1(void);
void	acpi_cpu_idle_mwait(uint32_t mwait_hint);
void	*acpi_map_table(vm_paddr_t pa, const char *sig);
void	acpi_unmap_table(void *table);
vm_paddr_t acpi_find_table(const char *sig);
void	madt_parse_interrupt_values(void *entry,
	    enum intr_trigger *trig, enum intr_polarity *pol);

extern int madt_found_sci_override;

#endif /* _KERNEL */

#endif /* __ACPICA_MACHDEP_H__ */
