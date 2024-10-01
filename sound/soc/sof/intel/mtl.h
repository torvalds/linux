/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2020-2022 Intel Corporation
 */

/* DSP Registers */
#define MTL_HFDSSCS			0x1000
#define MTL_HFDSSCS_SPA_MASK		BIT(16)
#define MTL_HFDSSCS_CPA_MASK		BIT(24)
#define MTL_HFSNDWIE			0x114C
#define MTL_HFPWRCTL			0x1D18
#define PTL_HFPWRCTL2			0x1D20
#define MTL_HfPWRCTL_WPIOXPG(x)		BIT((x) + 8)
#define MTL_HFPWRCTL_WPDSPHPXPG		BIT(0)
#define MTL_HFPWRSTS			0x1D1C
#define PTL_HFPWRSTS2			0x1D24
#define MTL_HFPWRSTS_DSPHPXPGS_MASK	BIT(0)
#define MTL_HFINTIPPTR			0x1108
#define MTL_IRQ_INTEN_L_HOST_IPC_MASK	BIT(0)
#define MTL_IRQ_INTEN_L_SOUNDWIRE_MASK	BIT(6)
#define MTL_HFINTIPPTR_PTR_MASK		GENMASK(20, 0)

#define MTL_HDA_VS_D0I3C		0x1D4A

#define MTL_DSP2CXCAP_PRIMARY_CORE	0x178D00
#define MTL_DSP2CXCTL_PRIMARY_CORE	0x178D04
#define MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK BIT(0)
#define MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK BIT(8)
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL GENMASK(25, 24)
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL_SHIFT 24

/* IPC Registers */
#define MTL_DSP_REG_HFIPCXTDR		0x73200
#define MTL_DSP_REG_HFIPCXTDR_BUSY	BIT(31)
#define MTL_DSP_REG_HFIPCXTDR_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXTDA		0x73204
#define MTL_DSP_REG_HFIPCXTDA_BUSY	BIT(31)
#define MTL_DSP_REG_HFIPCXIDR		0x73210
#define MTL_DSP_REG_HFIPCXIDR_BUSY	BIT(31)
#define MTL_DSP_REG_HFIPCXIDR_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXIDA		0x73214
#define MTL_DSP_REG_HFIPCXIDA_DONE	BIT(31)
#define MTL_DSP_REG_HFIPCXIDA_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXCTL		0x73228
#define MTL_DSP_REG_HFIPCXCTL_BUSY	BIT(0)
#define MTL_DSP_REG_HFIPCXCTL_DONE	BIT(1)
#define MTL_DSP_REG_HFIPCXTDDY		0x73300
#define MTL_DSP_REG_HFIPCXIDDY		0x73380
#define MTL_DSP_REG_HfHIPCIE		0x1140
#define MTL_DSP_REG_HfHIPCIE_IE_MASK	BIT(0)
#define MTL_DSP_REG_HfSNDWIE		0x114C
#define MTL_DSP_REG_HfSNDWIE_IE_MASK	GENMASK(3, 0)

#define MTL_DSP_IRQSTS			0x20
#define MTL_DSP_IRQSTS_IPC		BIT(0)
#define MTL_DSP_IRQSTS_SDW		BIT(6)

#define MTL_DSP_REG_POLL_INTERVAL_US	10	/* 10 us */

/* Memory windows */
#define MTL_SRAM_WINDOW_OFFSET(x)	(0x180000 + 0x8000 * (x))

#define MTL_DSP_MBOX_UPLINK_OFFSET	(MTL_SRAM_WINDOW_OFFSET(0) + 0x1000)
#define MTL_DSP_MBOX_UPLINK_SIZE	0x1000
#define MTL_DSP_MBOX_DOWNLINK_OFFSET	MTL_SRAM_WINDOW_OFFSET(1)
#define MTL_DSP_MBOX_DOWNLINK_SIZE	0x1000

/* FW registers */
#define MTL_DSP_ROM_STS			MTL_SRAM_WINDOW_OFFSET(0) /* ROM status */
#define MTL_DSP_ROM_ERROR		(MTL_SRAM_WINDOW_OFFSET(0) + 0x4) /* ROM error code */

#define MTL_DSP_REG_HFFLGPXQWY		0x163200 /* DSP core0 status */
#define MTL_DSP_REG_HFFLGPXQWY_ERROR	0x163204 /* DSP core0 error */

/* FSR status codes */
#define FSR_STATE_ROM_RESET_VECTOR_DONE		0x8
#define FSR_STATE_ROM_PURGE_BOOT		0x9
#define FSR_STATE_ROM_RESTORE_BOOT		0xA
#define FSR_STATE_ROM_FW_ENTRY_POINT		0xB
#define FSR_STATE_ROM_VALIDATE_PUB_KEY		0xC
#define FSR_STATE_ROM_POWER_DOWN_HPSRAM		0xD
#define FSR_STATE_ROM_POWER_DOWN_ULPSRAM	0xE
#define FSR_STATE_ROM_POWER_UP_ULPSRAM_STACK	0xF
#define FSR_STATE_ROM_POWER_UP_HPSRAM_DMA	0x10
#define FSR_STATE_ROM_BEFORE_EP_POINTER_READ	0x11
#define FSR_STATE_ROM_VALIDATE_MANIFEST		0x12
#define FSR_STATE_ROM_VALIDATE_FW_MODULE	0x13
#define FSR_STATE_ROM_PROTECT_IMR_REGION	0x14
#define FSR_STATE_ROM_PUSH_MODEL_ROUTINE	0x15
#define FSR_STATE_ROM_PULL_MODEL_ROUTINE	0x16
#define FSR_STATE_ROM_VALIDATE_PKG_DIR		0x17
#define FSR_STATE_ROM_VALIDATE_CPD		0x18
#define FSR_STATE_ROM_VALIDATE_CSS_MAN_HEADER	0x19
#define FSR_STATE_ROM_VALIDATE_BLOB_SVN		0x1A
#define FSR_STATE_ROM_VERIFY_IFWI_PARTITION	0x1B
#define FSR_STATE_ROM_REMOVE_ACCESS_CONTROL	0x1C
#define FSR_STATE_ROM_AUTH_BYPASS		0x1D
#define FSR_STATE_ROM_AUTH_ENABLED		0x1E
#define FSR_STATE_ROM_INIT_DMA			0x1F
#define FSR_STATE_ROM_PURGE_FW_ENTRY		0x20
#define FSR_STATE_ROM_PURGE_FW_END		0x21
#define FSR_STATE_ROM_CLEAN_UP_BSS_DONE		0x22
#define FSR_STATE_ROM_IMR_RESTORE_ENTRY		0x23
#define FSR_STATE_ROM_IMR_RESTORE_END		0x24
#define FSR_STATE_ROM_FW_MANIFEST_IN_DMA_BUFF	0x25
#define FSR_STATE_ROM_LOAD_CSE_MAN_TO_IMR	0x26
#define FSR_STATE_ROM_LOAD_FW_MAN_TO_IMR	0x27
#define FSR_STATE_ROM_LOAD_FW_CODE_TO_IMR	0x28
#define FSR_STATE_ROM_FW_LOADING_DONE		0x29
#define FSR_STATE_ROM_FW_CODE_LOADED		0x2A
#define FSR_STATE_ROM_VERIFY_IMAGE_TYPE		0x2B
#define FSR_STATE_ROM_AUTH_API_INIT		0x2C
#define FSR_STATE_ROM_AUTH_API_PROC		0x2D
#define FSR_STATE_ROM_AUTH_API_FIRST_BUSY	0x2E
#define FSR_STATE_ROM_AUTH_API_FIRST_RESULT	0x2F
#define FSR_STATE_ROM_AUTH_API_CLEANUP		0x30

#define MTL_DSP_REG_HfIMRIS1		0x162088
#define MTL_DSP_REG_HfIMRIS1_IU_MASK	BIT(0)

bool mtl_dsp_check_ipc_irq(struct snd_sof_dev *sdev);
int mtl_ipc_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg);

void mtl_enable_ipc_interrupts(struct snd_sof_dev *sdev);
void mtl_disable_ipc_interrupts(struct snd_sof_dev *sdev);

int mtl_enable_interrupts(struct snd_sof_dev *sdev, bool enable);

int mtl_dsp_pre_fw_run(struct snd_sof_dev *sdev);
int mtl_dsp_post_fw_run(struct snd_sof_dev *sdev);
void mtl_dsp_dump(struct snd_sof_dev *sdev, u32 flags);

int mtl_power_down_dsp(struct snd_sof_dev *sdev);
int mtl_dsp_cl_init(struct snd_sof_dev *sdev, int stream_tag, bool imr_boot);

irqreturn_t mtl_ipc_irq_thread(int irq, void *context);

int mtl_dsp_ipc_get_mailbox_offset(struct snd_sof_dev *sdev);
int mtl_dsp_ipc_get_window_offset(struct snd_sof_dev *sdev, u32 id);

void mtl_ipc_dump(struct snd_sof_dev *sdev);

int mtl_dsp_core_get(struct snd_sof_dev *sdev, int core);
int mtl_dsp_core_put(struct snd_sof_dev *sdev, int core);
