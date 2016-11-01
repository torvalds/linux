/*
 *  skl.h - HD Audio skylake defintions.
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#ifndef __SOUND_SOC_SKL_H
#define __SOUND_SOC_SKL_H

#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>
#include "skl-nhlt.h"

#define SKL_SUSPEND_DELAY 2000

/* Vendor Specific Registers */
#define AZX_REG_VS_EM1			0x1000
#define AZX_REG_VS_INRC			0x1004
#define AZX_REG_VS_OUTRC		0x1008
#define AZX_REG_VS_FIFOTRK		0x100C
#define AZX_REG_VS_FIFOTRK2		0x1010
#define AZX_REG_VS_EM2			0x1030
#define AZX_REG_VS_EM3L			0x1038
#define AZX_REG_VS_EM3U			0x103C
#define AZX_REG_VS_EM4L			0x1040
#define AZX_REG_VS_EM4U			0x1044
#define AZX_REG_VS_LTRC			0x1048
#define AZX_REG_VS_D0I3C		0x104A
#define AZX_REG_VS_PCE			0x104B
#define AZX_REG_VS_L2MAGC		0x1050
#define AZX_REG_VS_L2LAHPT		0x1054
#define AZX_REG_VS_SDXDPIB_XBASE	0x1084
#define AZX_REG_VS_SDXDPIB_XINTERVAL	0x20
#define AZX_REG_VS_SDXEFIFOS_XBASE	0x1094
#define AZX_REG_VS_SDXEFIFOS_XINTERVAL	0x20

#define AZX_PCIREG_PGCTL		0x44
#define AZX_PGCTL_LSRMD_MASK		(1 << 4)
#define AZX_PCIREG_CGCTL		0x48
#define AZX_CGCTL_MISCBDCGE_MASK	(1 << 6)

struct skl_dsp_resource {
	u32 max_mcps;
	u32 max_mem;
	u32 mcps;
	u32 mem;
};

struct skl {
	struct hdac_ext_bus ebus;
	struct pci_dev *pci;

	unsigned int init_failed:1; /* delayed init failed */
	struct platform_device *dmic_dev;
	struct platform_device *i2s_dev;
	struct snd_soc_platform *platform;

	struct nhlt_acpi_table *nhlt; /* nhlt ptr */
	struct skl_sst *skl_sst; /* sst skl ctx */

	struct skl_dsp_resource resource;
	struct list_head ppl_list;

	const char *fw_name;
	char tplg_name[64];
	unsigned short pci_id;
	const struct firmware *tplg;

	int supend_active;
};

#define skl_to_ebus(s)	(&(s)->ebus)
#define ebus_to_skl(sbus) \
	container_of(sbus, struct skl, sbus)

/* to pass dai dma data */
struct skl_dma_params {
	u32 format;
	u8 stream_tag;
};

/* to pass dmic data */
struct skl_machine_pdata {
	u32 dmic_num;
};

struct skl_dsp_ops {
	int id;
	struct skl_dsp_loader_ops (*loader_ops)(void);
	int (*init)(struct device *dev, void __iomem *mmio_base,
			int irq, const char *fw_name,
			struct skl_dsp_loader_ops loader_ops,
			struct skl_sst **skl_sst);
	void (*cleanup)(struct device *dev, struct skl_sst *ctx);
};

int skl_platform_unregister(struct device *dev);
int skl_platform_register(struct device *dev);

struct nhlt_acpi_table *skl_nhlt_init(struct device *dev);
void skl_nhlt_free(struct nhlt_acpi_table *addr);
struct nhlt_specific_cfg *skl_get_ep_blob(struct skl *skl, u32 instance,
			u8 link_type, u8 s_fmt, u8 no_ch, u32 s_rate, u8 dirn);

int skl_get_dmic_geo(struct skl *skl);
int skl_nhlt_update_topology_bin(struct skl *skl);
int skl_init_dsp(struct skl *skl);
int skl_free_dsp(struct skl *skl);
int skl_suspend_dsp(struct skl *skl);
int skl_resume_dsp(struct skl *skl);
void skl_cleanup_resources(struct skl *skl);
#endif /* __SOUND_SOC_SKL_H */
