/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021 Intel Corporation */

#ifndef __LINUX_PECI_CPU_H
#define __LINUX_PECI_CPU_H

#include <linux/types.h>

#include "../../arch/x86/include/asm/intel-family.h"

#define PECI_PCS_PKG_ID			0  /* Package Identifier Read */
#define  PECI_PKG_ID_CPU_ID		0x0000  /* CPUID Info */
#define  PECI_PKG_ID_PLATFORM_ID	0x0001  /* Platform ID */
#define  PECI_PKG_ID_DEVICE_ID		0x0002  /* Uncore Device ID */
#define  PECI_PKG_ID_MAX_THREAD_ID	0x0003  /* Max Thread ID */
#define  PECI_PKG_ID_MICROCODE_REV	0x0004  /* CPU Microcode Update Revision */
#define  PECI_PKG_ID_MCA_ERROR_LOG	0x0005  /* Machine Check Status */
#define PECI_PCS_MODULE_TEMP		9  /* Per Core DTS Temperature Read */
#define PECI_PCS_THERMAL_MARGIN		10 /* DTS thermal margin */
#define PECI_PCS_DDR_DIMM_TEMP		14 /* DDR DIMM Temperature */
#define PECI_PCS_TEMP_TARGET		16 /* Temperature Target Read */
#define PECI_PCS_TDP_UNITS		30 /* Units for power/energy registers */

struct peci_device;

int peci_temp_read(struct peci_device *device, s16 *temp_raw);

int peci_pcs_read(struct peci_device *device, u8 index,
		  u16 param, u32 *data);

int peci_pci_local_read(struct peci_device *device, u8 bus, u8 dev,
			u8 func, u16 reg, u32 *data);

int peci_ep_pci_local_read(struct peci_device *device, u8 seg,
			   u8 bus, u8 dev, u8 func, u16 reg, u32 *data);

int peci_mmio_read(struct peci_device *device, u8 bar, u8 seg,
		   u8 bus, u8 dev, u8 func, u64 address, u32 *data);

#endif /* __LINUX_PECI_CPU_H */
