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

#ifndef	_POWERPC_POWERMAC_UNINORTHVAR_H_
#define	_POWERPC_POWERMAC_UNINORTHVAR_H_

#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofwpci.h>

struct uninorth_softc {
	struct ofw_pci_softc	pci_sc;
	vm_offset_t		sc_addr;
	vm_offset_t		sc_data;
	int			sc_ver;
	int			sc_skipslot;
	struct mtx		sc_cfg_mtx;
};

struct unin_chip_softc {
	uint64_t		sc_physaddr;
	uint64_t		sc_size;
	vm_offset_t		sc_addr;
	struct rman  		sc_mem_rman;
	int			sc_version;
};

/*
 * Format of a unin reg property entry.
 */
struct unin_chip_reg {
        u_int32_t       mr_base;
        u_int32_t       mr_size;
};

/*
 * Per unin device structure.
 */
struct unin_chip_devinfo {
        int        udi_interrupts[6];
        int        udi_ninterrupts;
        int        udi_base;   
        struct ofw_bus_devinfo udi_obdinfo;
        struct resource_list udi_resources;
};

/*
 * Version register
 */
#define UNIN_VERS       0x0

/*
 * Clock-control register
 */
#define UNIN_CLOCKCNTL		0x20
#define UNIN_CLOCKCNTL_GMAC	0x2

/*
 * Power management register
 */
#define UNIN_PWR_MGMT		0x30
#define UNIN_PWR_NORMAL		0x00
#define UNIN_PWR_IDLE2		0x01
#define UNIN_PWR_SLEEP		0x02
#define UNIN_PWR_SAVE		0x03
#define UNIN_PWR_MASK		0x03

/*
 * Hardware initialization state register
 */
#define UNIN_HWINIT_STATE	0x70
#define UNIN_SLEEPING		0x01
#define UNIN_RUNNING		0x02


/*
 * Toggle registers
 */
#define UNIN_TOGGLE_REG		0xe0
#define UNIN_MPIC_RESET		0x2
#define UNIN_MPIC_OUTPUT_ENABLE	0x4

extern int unin_chip_sleep(device_t dev, int idle);
extern int unin_chip_wake(device_t dev);
#endif  /* _POWERPC_POWERMAC_UNINORTHVAR_H_ */
