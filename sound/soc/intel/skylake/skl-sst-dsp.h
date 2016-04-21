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

#include <linux/interrupt.h>
#include <sound/memalloc.h>
#include "skl-sst-cldma.h"

struct sst_dsp;
struct skl_sst;
struct sst_dsp_device;

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

/* FW base IDs */
#define SKL_INSTANCE_ID			0
#define SKL_BASE_FW_MODULE_ID		0

/* Intel HD Audio SRAM Window 1 */
#define SKL_ADSP_SRAM1_BASE		0xA000

#define SKL_ADSP_MMIO_LEN		0x10000

#define SKL_ADSP_W0_STAT_SZ		0x1000

#define SKL_ADSP_W0_UP_SZ		0x1000

#define SKL_ADSP_W1_SZ			0x1000

#define SKL_FW_STS_MASK			0xf

#define SKL_FW_INIT			0x1
#define SKL_FW_RFW_START		0xf

#define SKL_ADSPIC_IPC			1
#define SKL_ADSPIS_IPC			1

/* ADSPCS - Audio DSP Control & Status */
#define SKL_DSP_CORES		1
#define SKL_DSP_CORE0_MASK	1
#define SKL_DSP_CORES_MASK	((1 << SKL_DSP_CORES) - 1)

/* Core Reset - asserted high */
#define SKL_ADSPCS_CRST_SHIFT	0
#define SKL_ADSPCS_CRST_MASK	(SKL_DSP_CORES_MASK << SKL_ADSPCS_CRST_SHIFT)
#define SKL_ADSPCS_CRST(x)	((x << SKL_ADSPCS_CRST_SHIFT) & SKL_ADSPCS_CRST_MASK)

/* Core run/stall - when set to '1' core is stalled */
#define SKL_ADSPCS_CSTALL_SHIFT	8
#define SKL_ADSPCS_CSTALL_MASK	(SKL_DSP_CORES_MASK <<	\
					SKL_ADSPCS_CSTALL_SHIFT)
#define SKL_ADSPCS_CSTALL(x)	((x << SKL_ADSPCS_CSTALL_SHIFT) &	\
				SKL_ADSPCS_CSTALL_MASK)

/* Set Power Active - when set to '1' turn cores on */
#define SKL_ADSPCS_SPA_SHIFT	16
#define SKL_ADSPCS_SPA_MASK	(SKL_DSP_CORES_MASK << SKL_ADSPCS_SPA_SHIFT)
#define SKL_ADSPCS_SPA(x)	((x << SKL_ADSPCS_SPA_SHIFT) & SKL_ADSPCS_SPA_MASK)

/* Current Power Active - power status of cores, set by hardware */
#define SKL_ADSPCS_CPA_SHIFT	24
#define SKL_ADSPCS_CPA_MASK	(SKL_DSP_CORES_MASK << SKL_ADSPCS_CPA_SHIFT)
#define SKL_ADSPCS_CPA(x)	((x << SKL_ADSPCS_CPA_SHIFT) & SKL_ADSPCS_CPA_MASK)

#define SST_DSP_POWER_D0	0x0  /* full On */
#define SST_DSP_POWER_D3	0x3  /* Off */

enum skl_dsp_states {
	SKL_DSP_RUNNING = 1,
	SKL_DSP_RESET,
};

struct skl_dsp_fw_ops {
	int (*load_fw)(struct sst_dsp  *ctx);
	/* FW module parser/loader */
	int (*parse_fw)(struct sst_dsp *ctx);
	int (*set_state_D0)(struct sst_dsp *ctx);
	int (*set_state_D3)(struct sst_dsp *ctx);
	unsigned int (*get_fw_errcode)(struct sst_dsp *ctx);
	int (*load_mod)(struct sst_dsp *ctx, u16 mod_id, u8 *mod_name);
	int (*unload_mod)(struct sst_dsp *ctx, u16 mod_id);

};

struct skl_dsp_loader_ops {
	int stream_tag;

	int (*alloc_dma_buf)(struct device *dev,
		struct snd_dma_buffer *dmab, size_t size);
	int (*free_dma_buf)(struct device *dev,
		struct snd_dma_buffer *dmab);
	int (*prepare)(struct device *dev, unsigned int format,
				unsigned int byte_size,
				struct snd_dma_buffer *bufp);
	int (*trigger)(struct device *dev, bool start, int stream_tag);

	int (*cleanup)(struct device *dev, struct snd_dma_buffer *dmab,
				 int stream_tag);
};

struct skl_load_module_info {
	u16 mod_id;
	const struct firmware *fw;
};

struct skl_module_table {
	struct skl_load_module_info *mod_info;
	unsigned int usage_cnt;
	struct list_head list;
};

void skl_cldma_process_intr(struct sst_dsp *ctx);
void skl_cldma_int_disable(struct sst_dsp *ctx);
int skl_cldma_prepare(struct sst_dsp *ctx);

void skl_dsp_set_state_locked(struct sst_dsp *ctx, int state);
struct sst_dsp *skl_dsp_ctx_init(struct device *dev,
		struct sst_dsp_device *sst_dev, int irq);
int skl_dsp_enable_core(struct sst_dsp *ctx);
int skl_dsp_disable_core(struct sst_dsp *ctx);
bool is_skl_dsp_running(struct sst_dsp *ctx);
irqreturn_t skl_dsp_sst_interrupt(int irq, void *dev_id);
int skl_dsp_wake(struct sst_dsp *ctx);
int skl_dsp_sleep(struct sst_dsp *ctx);
void skl_dsp_free(struct sst_dsp *dsp);

int skl_dsp_boot(struct sst_dsp *ctx);
int skl_sst_dsp_init(struct device *dev, void __iomem *mmio_base, int irq,
		const char *fw_name, struct skl_dsp_loader_ops dsp_ops,
		struct skl_sst **dsp);
int bxt_sst_dsp_init(struct device *dev, void __iomem *mmio_base, int irq,
		const char *fw_name, struct skl_dsp_loader_ops dsp_ops,
		struct skl_sst **dsp);
void skl_sst_dsp_cleanup(struct device *dev, struct skl_sst *ctx);
void bxt_sst_dsp_cleanup(struct device *dev, struct skl_sst *ctx);

#endif /*__SKL_SST_DSP_H__*/
