/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cannonlake SST DSP Support
 *
 * Copyright (C) 2016-17, Intel Corporation.
 */

#ifndef __CNL_SST_DSP_H__
#define __CNL_SST_DSP_H__

struct sst_dsp;
struct skl_sst;
struct sst_dsp_device;
struct sst_generic_ipc;

/* Intel HD Audio General DSP Registers */
#define CNL_ADSP_GEN_BASE		0x0
#define CNL_ADSP_REG_ADSPCS		(CNL_ADSP_GEN_BASE + 0x04)
#define CNL_ADSP_REG_ADSPIC		(CNL_ADSP_GEN_BASE + 0x08)
#define CNL_ADSP_REG_ADSPIS		(CNL_ADSP_GEN_BASE + 0x0c)

/* Intel HD Audio Inter-Processor Communication Registers */
#define CNL_ADSP_IPC_BASE               0xc0
#define CNL_ADSP_REG_HIPCTDR            (CNL_ADSP_IPC_BASE + 0x00)
#define CNL_ADSP_REG_HIPCTDA            (CNL_ADSP_IPC_BASE + 0x04)
#define CNL_ADSP_REG_HIPCTDD            (CNL_ADSP_IPC_BASE + 0x08)
#define CNL_ADSP_REG_HIPCIDR            (CNL_ADSP_IPC_BASE + 0x10)
#define CNL_ADSP_REG_HIPCIDA            (CNL_ADSP_IPC_BASE + 0x14)
#define CNL_ADSP_REG_HIPCIDD            (CNL_ADSP_IPC_BASE + 0x18)
#define CNL_ADSP_REG_HIPCCTL            (CNL_ADSP_IPC_BASE + 0x28)

/* HIPCTDR */
#define CNL_ADSP_REG_HIPCTDR_BUSY	BIT(31)

/* HIPCTDA */
#define CNL_ADSP_REG_HIPCTDA_DONE	BIT(31)

/* HIPCIDR */
#define CNL_ADSP_REG_HIPCIDR_BUSY	BIT(31)

/* HIPCIDA */
#define CNL_ADSP_REG_HIPCIDA_DONE	BIT(31)

/* CNL HIPCCTL */
#define CNL_ADSP_REG_HIPCCTL_DONE	BIT(1)
#define CNL_ADSP_REG_HIPCCTL_BUSY	BIT(0)

/* CNL HIPCT */
#define CNL_ADSP_REG_HIPCT_BUSY		BIT(31)

/* Intel HD Audio SRAM Window 1 */
#define CNL_ADSP_SRAM1_BASE		0xa0000

#define CNL_ADSP_MMIO_LEN		0x10000

#define CNL_ADSP_W0_STAT_SZ		0x1000

#define CNL_ADSP_W0_UP_SZ		0x1000

#define CNL_ADSP_W1_SZ			0x1000

#define CNL_FW_STS_MASK			0xf

#define CNL_ADSPIC_IPC			0x1
#define CNL_ADSPIS_IPC			0x1

#define CNL_DSP_CORES		4
#define CNL_DSP_CORES_MASK	((1 << CNL_DSP_CORES) - 1)

/* core reset - asserted high */
#define CNL_ADSPCS_CRST_SHIFT	0
#define CNL_ADSPCS_CRST(x)	(x << CNL_ADSPCS_CRST_SHIFT)

/* core run/stall - when set to 1 core is stalled */
#define CNL_ADSPCS_CSTALL_SHIFT	8
#define CNL_ADSPCS_CSTALL(x)	(x << CNL_ADSPCS_CSTALL_SHIFT)

/* set power active - when set to 1 turn core on */
#define CNL_ADSPCS_SPA_SHIFT	16
#define CNL_ADSPCS_SPA(x)	(x << CNL_ADSPCS_SPA_SHIFT)

/* current power active - power status of cores, set by hardware */
#define CNL_ADSPCS_CPA_SHIFT	24
#define CNL_ADSPCS_CPA(x)	(x << CNL_ADSPCS_CPA_SHIFT)

int cnl_dsp_enable_core(struct sst_dsp *ctx, unsigned int core);
int cnl_dsp_disable_core(struct sst_dsp *ctx, unsigned int core);
irqreturn_t cnl_dsp_sst_interrupt(int irq, void *dev_id);
void cnl_dsp_free(struct sst_dsp *dsp);

void cnl_ipc_int_enable(struct sst_dsp *ctx);
void cnl_ipc_int_disable(struct sst_dsp *ctx);
void cnl_ipc_op_int_enable(struct sst_dsp *ctx);
void cnl_ipc_op_int_disable(struct sst_dsp *ctx);
bool cnl_ipc_int_status(struct sst_dsp *ctx);
void cnl_ipc_free(struct sst_generic_ipc *ipc);

int cnl_sst_dsp_init(struct device *dev, void __iomem *mmio_base, int irq,
		     const char *fw_name, struct skl_dsp_loader_ops dsp_ops,
		     struct skl_sst **dsp);
int cnl_sst_init_fw(struct device *dev, struct skl_sst *ctx);
void cnl_sst_dsp_cleanup(struct device *dev, struct skl_sst *ctx);

#endif /*__CNL_SST_DSP_H__*/
