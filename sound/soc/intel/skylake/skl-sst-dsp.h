/*
 * Skylake SST DSP Support
 *
 * Copyright (C) 2014-15, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __SKL_SST_DSP_H__
#define __SKL_SST_DSP_H__

/* Intel HD Audio General DSP Registers */
#define SKL_ADSP_GEN_BASE		0x0
#define SKL_ADSP_REG_ADSPCS		(SKL_ADSP_GEN_BASE + 0x04)
#define SKL_ADSP_REG_ADSPIC		(SKL_ADSP_GEN_BASE + 0x08)
#define SKL_ADSP_REG_ADSPIS		(SKL_ADSP_GEN_BASE + 0x0C)
#define SKL_ADSP_REG_ADSPIC2		(SKL_ADSP_GEN_BASE + 0x10)
#define SKL_ADSP_REG_ADSPIS2		(SKL_ADSP_GEN_BASE + 0x14)

/* Intel HD Audio Inter-Processor Communication Registers */
#define SKL_ADSP_IPC_BASE		0x40
#define SKL_ADSP_REG_HIPCT		(SKL_ADSP_IPC_BASE + 0x00)
#define SKL_ADSP_REG_HIPCTE		(SKL_ADSP_IPC_BASE + 0x04)
#define SKL_ADSP_REG_HIPCI		(SKL_ADSP_IPC_BASE + 0x08)
#define SKL_ADSP_REG_HIPCIE		(SKL_ADSP_IPC_BASE + 0x0C)
#define SKL_ADSP_REG_HIPCCTL		(SKL_ADSP_IPC_BASE + 0x10)

/*  HIPCI */
#define SKL_ADSP_REG_HIPCI_BUSY		BIT(31)

/* HIPCIE */
#define SKL_ADSP_REG_HIPCIE_DONE	BIT(30)

/* HIPCCTL */
#define SKL_ADSP_REG_HIPCCTL_DONE	BIT(1)
#define SKL_ADSP_REG_HIPCCTL_BUSY	BIT(0)

/* HIPCT */
#define SKL_ADSP_REG_HIPCT_BUSY		BIT(31)

/* Intel HD Audio SRAM Window 1 */
#define SKL_ADSP_SRAM1_BASE		0xA000

#define SKL_ADSP_MMIO_LEN		0x10000

#define SKL_ADSP_W0_STAT_SZ		0x800

#define SKL_ADSP_W0_UP_SZ		0x800

#define SKL_ADSP_W1_SZ			0x1000

#define SKL_ADSPIC_IPC			1
#define SKL_ADSPIS_IPC			1

#endif /*__SKL_SST_DSP_H__*/
