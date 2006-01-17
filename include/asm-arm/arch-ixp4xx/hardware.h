/*
 * include/asm-arm/arch-ixp4xx/hardware.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * Hardware definitions for IXP4xx based systems
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#define PCIBIOS_MIN_IO			0x00001000
#define PCIBIOS_MIN_MEM			0x48000000

/*
 * We override the standard dma-mask routines for bouncing.
 */
#define	HAVE_ARCH_PCI_SET_DMA_MASK

#define pcibios_assign_all_busses()	1

#if defined(CONFIG_CPU_IXP46X) && !defined(__ASSEMBLY__)
extern unsigned int processor_id;
#define cpu_is_ixp465() ((processor_id & 0xffffffc0) == 0x69054200)
#else
#define	cpu_is_ixp465()	(0)
#endif

/* Register locations and bits */
#include "ixp4xx-regs.h"

/* Platform helper functions and definitions */
#include "platform.h"

/* Platform specific details */
#include "ixdp425.h"
#include "coyote.h"
#include "prpmc1100.h"
#include "nslu2.h"
#include "nas100d.h"

#endif  /* _ASM_ARCH_HARDWARE_H */
