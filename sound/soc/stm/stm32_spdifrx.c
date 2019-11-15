// SPDX-License-Identifier: GPL-2.0-only
/*
 * STM32 ALSA SoC Digital Audio Interface (SPDIF-rx) driver.
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

/* SPDIF-rx Register Map */
#define STM32_SPDIFRX_CR	0x00
#define STM32_SPDIFRX_IMR	0x04
#define STM32_SPDIFRX_SR	0x08
#define STM32_SPDIFRX_IFCR	0x0C
#define STM32_SPDIFRX_DR	0x10
#define STM32_SPDIFRX_CSR	0x14
#define STM32_SPDIFRX_DIR	0x18
#define STM32_SPDIFRX_VERR	0x3F4
#define STM32_SPDIFRX_IDR	0x3F8
#define STM32_SPDIFRX_SIDR	0x3FC

/* Bit definition for SPDIF_CR register */
#define SPDIFRX_CR_SPDIFEN_SHIFT	0
#define SPDIFRX_CR_SPDIFEN_MASK	GENMASK(1, SPDIFRX_CR_SPDIFEN_SHIFT)
#define SPDIFRX_CR_SPDIFENSET(x)	((x) << SPDIFRX_CR_SPDIFEN_SHIFT)

#define SPDIFRX_CR_RXDMAEN	BIT(2)
#define SPDIFRX_CR_RXSTEO	BIT(3)

#define SPDIFRX_CR_DRFMT_SHIFT	4
#define SPDIFRX_CR_DRFMT_MASK	GENMASK(5, SPDIFRX_CR_DRFMT_SHIFT)
#define SPDIFRX_CR_DRFMTSET(x)	((x) << SPDIFRX_CR_DRFMT_SHIFT)

#define SPDIFRX_CR_PMSK		BIT(6)
#define SPDIFRX_CR_VMSK		BIT(7)
#define SPDIFRX_CR_CUMSK	BIT(8)
#define SPDIFRX_CR_PTMSK	BIT(9)
#define SPDIFRX_CR_CBDMAEN	BIT(10)
#define SPDIFRX_CR_CHSEL_SHIFT	11
#define SPDIFRX_CR_CHSEL	BIT(SPDIFRX_CR_CHSEL_SHIFT)

#define SPDIFRX_CR_NBTR_SHIFT	12
#define SPDIFRX_CR_NBTR_MASK	GENMASK(13, SPDIFRX_CR_NBTR_SHIFT)
#define SPDIFRX_CR_NBTRSET(x)	((x) << SPDIFRX_CR_NBTR_SHIFT)

#define SPDIFRX_CR_WFA		BIT(14)

#define SPDIFRX_CR_INSEL_SHIFT	16
#define SPDIFRX_CR_INSEL_MASK	GENMASK(18, PDIFRX_CR_INSEL_SHIFT)
#define SPDIFRX_CR_INSELSET(x)	((x) << SPDIFRX_CR_INSEL_SHIFT)

#define SPDIFRX_CR_CKSEN_SHIFT	20
#define SPDIFRX_CR_CKSEN	BIT(20)
#define SPDIFRX_CR_CKSBKPEN	BIT(21)

/* Bit definition for SPDIFRX_IMR register */
#define SPDIFRX_IMR_RXNEI	BIT(0)
#define SPDIFRX_IMR_CSRNEIE	BIT(1)
#define SPDIFRX_IMR_PERRIE	BIT(2)
#define SPDIFRX_IMR_OVRIE	BIT(3)
#define SPDIFRX_IMR_SBLKIE	BIT(4)
#define SPDIFRX_IMR_SYNCDIE	BIT(5)
#define SPDIFRX_IMR_IFEIE	BIT(6)

#define SPDIFRX_XIMR_MASK	GENMASK(6, 0)

/* Bit definition for SPDIFRX_SR register */
#define SPDIFRX_SR_RXNE		BIT(0)
#define SPDIFRX_SR_CSRNE	BIT(1)
#define SPDIFRX_SR_PERR		BIT(2)
#define SPDIFRX_SR_OVR		BIT(3)
#define SPDIFRX_SR_SBD		BIT(4)
#define SPDIFRX_SR_SYNCD	BIT(5)
#define SPDIFRX_SR_FERR		BIT(6)
#define SPDIFRX_SR_SERR		BIT(7)
#define SPDIFRX_SR_TERR		BIT(8)

#define SPDIFRX_SR_WIDTH5_SHIFT	16
#define SPDIFRX_SR_WIDTH5_MASK	GENMASK(30, PDIFRX_SR_WIDTH5_SHIFT)
#define SPDIFRX_SR_WIDTH5SET(x)	((x) << SPDIFRX_SR_WIDTH5_SHIFT)

/* Bit definition for SPDIFRX_IFCR register */
#define SPDIFRX_IFCR_PERRCF	BIT(2)
#define SPDIFRX_IFCR_OVRCF	BIT(3)
#define SPDIFRX_IFCR_SBDCF	BIT(4)
#define SPDIFRX_IFCR_SYNCDCF	BIT(5)

#define SPDIFRX_XIFCR_MASK	GENMASK(5, 2)

/* Bit definition for SPDIFRX_DR register (DRFMT = 0b00) */
#define SPDIFRX_DR0_DR_SHIFT	0
#define SPDIFRX_DR0_DR_MASK	GENMASK(23, SPDIFRX_DR0_DR_SHIFT)
#define SPDIFRX_DR0_DRSET(x)	((x) << SPDIFRX_DR0_DR_SHIFT)

#define SPDIFRX_DR0_PE		BIT(24)

#define SPDIFRX_DR0_V		BIT(25)
#define SPDIFRX_DR0_U		BIT(26)
#define SPDIFRX_DR0_C		BIT(27)

#define SPDIFRX_DR0_PT_SHIFT	28
#define SPDIFRX_DR0_PT_MASK	GENMASK(29, SPDIFRX_DR0_PT_SHIFT)
#define SPDIFRX_DR0_PTSET(x)	((x) << SPDIFRX_DR0_PT_SHIFT)

/* Bit definition for SPDIFRX_DR register (DRFMT = 0b01) */
#define  SPDIFRX_DR1_PE		BIT(0)
#define  SPDIFRX_DR1_V		BIT(1)
#define  SPDIFRX_DR1_U		BIT(2)
#define  SPDIFRX_DR1_C		BIT(3)

#define  SPDIFRX_DR1_PT_SHIFT	4
#define  SPDIFRX_DR1_PT_MASK	GENMASK(5, SPDIFRX_DR1_PT_SHIFT)
#define  SPDIFRX_DR1_PTSET(x)	((x) << SPDIFRX_DR1_PT_SHIFT)

#define SPDIFRX_DR1_DR_SHIFT	8
#define SPDIFRX_DR1_DR_MASK	GENMASK(31, SPDIFRX_DR1_DR_SHIFT)
#define SPDIFRX_DR1_DRSET(x)	((x) << SPDIFRX_DR1_DR_SHIFT)

/* Bit definition for SPDIFRX_DR register (DRFMT = 0b10) */
#define SPDIFRX_DR1_DRNL1_SHIFT	0
#define SPDIFRX_DR1_DRNL1_MASK	GENMASK(15, SPDIFRX_DR1_DRNL1_SHIFT)
#define SPDIFRX_DR1_DRNL1SET(x)	((x) << SPDIFRX_DR1_DRNL1_SHIFT)

#define SPDIFRX_DR1_DRNL2_SHIFT	16
#define SPDIFRX_DR1_DRNL2_MASK	GENMASK(31, SPDIFRX_DR1_DRNL2_SHIFT)
#define SPDIFRX_DR1_DRNL2SET(x)	((x) << SPDIFRX_DR1_DRNL2_SHIFT)

/* Bit definition for SPDIFRX_CSR register */
#define SPDIFRX_CSR_USR_SHIFT	0
#define SPDIFRX_CSR_USR_MASK	GENMASK(15, SPDIFRX_CSR_USR_SHIFT)
#define SPDIFRX_CSR_USRGET(x)	(((x) & SPDIFRX_CSR_USR_MASK)\
				>> SPDIFRX_CSR_USR_SHIFT)

#define SPDIFRX_CSR_CS_SHIFT	16
#define SPDIFRX_CSR_CS_MASK	GENMASK(23, SPDIFRX_CSR_CS_SHIFT)
#define SPDIFRX_CSR_CSGET(x)	(((x) & SPDIFRX_CSR_CS_MASK)\
				>> SPDIFRX_CSR_CS_SHIFT)

#define SPDIFRX_CSR_SOB		BIT(24)

/* Bit definition for SPDIFRX_DIR register */
#define SPDIFRX_DIR_THI_SHIFT	0
#define SPDIFRX_DIR_THI_MASK	GENMASK(12, SPDIFRX_DIR_THI_SHIFT)
#define SPDIFRX_DIR_THI_SET(x)	((x) << SPDIFRX_DIR_THI_SHIFT)

#define SPDIFRX_DIR_TLO_SHIFT	16
#define SPDIFRX_DIR_TLO_MASK	GENMASK(28, SPDIFRX_DIR_TLO_SHIFT)
#define SPDIFRX_DIR_TLO_SET(x)	((x) << SPDIFRX_DIR_TLO_SHIFT)

#define SPDIFRX_SPDIFEN_DISABLE	0x0
#define SPDIFRX_SPDIFEN_SYNC	0x1
#define SPDIFRX_SPDIFEN_ENABLE	0x3

/* Bit definition for SPDIFRX_VERR register */
#define SPDIFRX_VERR_MIN_MASK	GENMASK(3, 0)
#define SPDIFRX_VERR_MAJ_MASK	GENMASK(7, 4)

/* Bit definition for SPDIFRX_IDR register */
#define SPDIFRX_IDR_ID_MASK	GENMASK(31, 0)

/* Bit definition for SPDIFRX_SIDR register */
#define SPDIFRX_SIDR_SID_MASK	GENMASK(31, 0)

#define SPDIFRX_IPIDR_NUMBER	0x00130041

#define SPDIFRX_IN1		0x1
#define SPDIFRX_IN2		0x2
#define SPDIFRX_IN3		0x3
#define SPDIFRX_IN4		0x4
#define SPDIFRX_IN5		0x5
#define SPDIFRX_IN6		0x6
#define SPDIFRX_IN7		0x7
#define SPDIFRX_IN8		0x8

#define SPDIFRX_NBTR_NONE	0x0
#define SPDIFRX_NBTR_3		0x1
#define SPDIFRX_NBTR_15		0x2
#define SPDIFRX_NBTR_63		0x3

#define SPDIFRX_DRFMT_RIGHT	0x0
#define SPDIFRX_DRFMT_LEFT	0x1
#define SPDIFRX_DRFMT_PACKED	0x2

/* 192 CS bits in S/PDIF frame. i.e 24 CS bytes */
#define SPDIFRX_CS_BYTES_NB	24
#define SPDIFRX_UB_BYTES_NB	48

/*
 * CSR register is retrieved as a 32 bits word
 * It contains 1 channel status byte and 2 user data bytes
 * 2 S/PDIF frames are acquired to get all CS/UB bits
 */
#define SPDIFRX_CSR_BUF_LENGTH	(SPDIFRX_CS_BYTES_NB * 4 * 2)

/**
 * struct stm32_spdifrx_data - private data of SPDIFRX
 * @pdev: device data pointer
 * @base: mmio register base virtual address
 * @regmap: SPDIFRX register map pointer
 * @regmap_conf: SPDIFRX register map configuration pointer
 * @cs_completion: channel status retrieving completion
 * @kclk: kernel clock feeding the SPDIFRX clock generator
 * @dma_params: dma configuration data for rx channel
 * @substream: PCM substream data pointer
 * @dmab: dma buffer info pointer
 * @ctrl_chan: dma channel for S/PDIF control bits
 * @desc:dma async transaction descriptor
 * @slave_config: dma slave channel runtime config pointer
 * @phys_addr: SPDIFRX registers physical base address
 * @lock: synchronization enabling lock
 * @cs: channel status buffer
 * @ub: user data buffer
 * @irq: SPDIFRX interrupt line
 * @refcount: keep count of opened DMA channels
 */
struct stm32_spdifrx_data {
	struct platform_device *pdev;
	void __iomem *base;
	struct regmap *regmap;
	const struct regmap_config *regmap_conf;
	struct completion cs_completion;
	struct clk *kclk;
	struct snd_dmaengine_dai_dma_data dma_params;
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *dmab;
	struct dma_chan *ctrl_chan;
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config slave_config;
	dma_addr_t phys_addr;
	spinlock_t lock;  /* Sync enabling lock */
	unsigned char cs[SPDIFRX_CS_BYTES_NB];
	unsigned char ub[SPDIFRX_UB_BYTES_NB];
	int irq;
	int refcount;
};

static void stm32_spdifrx_dma_complete(void *data)
{
	struct stm32_spdifrx_data *spdifrx = (struct stm32_spdifrx_data *)data;
	struct platform_device *pdev = spdifrx->pdev;
	u32 *p_start = (u32 *)spdifrx->dmab->area;
	u32 *p_end = p_start + (2 * SPDIFRX_CS_BYTES_NB) - 1;
	u32 *ptr = p_start;
	u16 *ub_ptr = (short *)spdifrx->ub;
	int i = 0;

	regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
			   SPDIFRX_CR_CBDMAEN,
			   (unsigned int)~SPDIFRX_CR_CBDMAEN);

	if (!spdifrx->dmab->area)
		return;

	while (ptr <= p_end) {
		if (*ptr & SPDIFRX_CSR_SOB)
			break;
		ptr++;
	}

	if (ptr > p_end) {
		dev_err(&pdev->dev, "Start of S/PDIF block not found\n");
		return;
	}

	while (i < SPDIFRX_CS_BYTES_NB) {
		spdifrx->cs[i] = (unsigned char)SPDIFRX_CSR_CSGET(*ptr);
		*ub_ptr++ = SPDIFRX_CSR_USRGET(*ptr++);
		if (ptr > p_end) {
			dev_err(&pdev->dev, "Failed to get channel status\n");
			return;
		}
		i++;
	}

	complete(&spdifrx->cs_completion);
}

static int stm32_spdifrx_dma_ctrl_start(struct stm32_spdifrx_data *spdifrx)
{
	dma_cookie_t cookie;
	int err;

	spdifrx->desc = dmaengine_prep_slave_single(spdifrx->ctrl_chan,
						    spdifrx->dmab->addr,
						    SPDIFRX_CSR_BUF_LENGTH,
						    DMA_DEV_TO_MEM,
						    DMA_CTRL_ACK);
	if (!spdifrx->desc)
		return -EINVAL;

	spdifrx->desc->callback = stm32_spdifrx_dma_complete;
	spdifrx->desc->callback_param = spdifrx;
	cookie = dmaengine_submit(spdifrx->desc);
	err = dma_submit_error(cookie);
	if (err)
		return -EINVAL;

	dma_async_issue_pending(spdifrx->ctrl_chan);

	return 0;
}

static void stm32_spdifrx_dma_ctrl_stop(struct stm32_spdifrx_data *spdifrx)
{
	dmaengine_terminate_async(spdifrx->ctrl_chan);
}

static int stm32_spdifrx_start_sync(struct stm32_spdifrx_data *spdifrx)
{
	int cr, cr_mask, imr, ret;

	/* Enable IRQs */
	imr = SPDIFRX_IMR_IFEIE | SPDIFRX_IMR_SYNCDIE | SPDIFRX_IMR_PERRIE;
	ret = regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_IMR, imr, imr);
	if (ret)
		return ret;

	spin_lock(&spdifrx->lock);

	spdifrx->refcount++;

	regmap_read(spdifrx->regmap, STM32_SPDIFRX_CR, &cr);

	if (!(cr & SPDIFRX_CR_SPDIFEN_MASK)) {
		/*
		 * Start sync if SPDIFRX is still in idle state.
		 * SPDIFRX reception enabled when sync done
		 */
		dev_dbg(&spdifrx->pdev->dev, "start synchronization\n");

		/*
		 * SPDIFRX configuration:
		 * Wait for activity before starting sync process. This avoid
		 * to issue sync errors when spdif signal is missing on input.
		 * Preamble, CS, user, validity and parity error bits not copied
		 * to DR register.
		 */
		cr = SPDIFRX_CR_WFA | SPDIFRX_CR_PMSK | SPDIFRX_CR_VMSK |
		     SPDIFRX_CR_CUMSK | SPDIFRX_CR_PTMSK | SPDIFRX_CR_RXSTEO;
		cr_mask = cr;

		cr |= SPDIFRX_CR_SPDIFENSET(SPDIFRX_SPDIFEN_SYNC);
		cr_mask |= SPDIFRX_CR_SPDIFEN_MASK;
		ret = regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
					 cr_mask, cr);
		if (ret < 0)
			dev_err(&spdifrx->pdev->dev,
				"Failed to start synchronization\n");
	}

	spin_unlock(&spdifrx->lock);

	return ret;
}

static void stm32_spdifrx_stop(struct stm32_spdifrx_data *spdifrx)
{
	int cr, cr_mask, reg;

	spin_lock(&spdifrx->lock);

	if (--spdifrx->refcount) {
		spin_unlock(&spdifrx->lock);
		return;
	}

	cr = SPDIFRX_CR_SPDIFENSET(SPDIFRX_SPDIFEN_DISABLE);
	cr_mask = SPDIFRX_CR_SPDIFEN_MASK | SPDIFRX_CR_RXDMAEN;

	regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR, cr_mask, cr);

	regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_IMR,
			   SPDIFRX_XIMR_MASK, 0);

	regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_IFCR,
			   SPDIFRX_XIFCR_MASK, SPDIFRX_XIFCR_MASK);

	/* dummy read to clear CSRNE and RXNE in status register */
	regmap_read(spdifrx->regmap, STM32_SPDIFRX_DR, &reg);
	regmap_read(spdifrx->regmap, STM32_SPDIFRX_CSR, &reg);

	spin_unlock(&spdifrx->lock);
}

static int stm32_spdifrx_dma_ctrl_register(struct device *dev,
					   struct stm32_spdifrx_data *spdifrx)
{
	int ret;

	spdifrx->ctrl_chan = dma_request_chan(dev, "rx-ctrl");
	if (IS_ERR(spdifrx->ctrl_chan)) {
		dev_err(dev, "dma_request_slave_channel failed\n");
		return PTR_ERR(spdifrx->ctrl_chan);
	}

	spdifrx->dmab = devm_kzalloc(dev, sizeof(struct snd_dma_buffer),
				     GFP_KERNEL);
	if (!spdifrx->dmab)
		return -ENOMEM;

	spdifrx->dmab->dev.type = SNDRV_DMA_TYPE_DEV_IRAM;
	spdifrx->dmab->dev.dev = dev;
	ret = snd_dma_alloc_pages(spdifrx->dmab->dev.type, dev,
				  SPDIFRX_CSR_BUF_LENGTH, spdifrx->dmab);
	if (ret < 0) {
		dev_err(dev, "snd_dma_alloc_pages returned error %d\n", ret);
		return ret;
	}

	spdifrx->slave_config.direction = DMA_DEV_TO_MEM;
	spdifrx->slave_config.src_addr = (dma_addr_t)(spdifrx->phys_addr +
					 STM32_SPDIFRX_CSR);
	spdifrx->slave_config.dst_addr = spdifrx->dmab->addr;
	spdifrx->slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdifrx->slave_config.src_maxburst = 1;

	ret = dmaengine_slave_config(spdifrx->ctrl_chan,
				     &spdifrx->slave_config);
	if (ret < 0) {
		dev_err(dev, "dmaengine_slave_config returned error %d\n", ret);
		spdifrx->ctrl_chan = NULL;
	}

	return ret;
};

static const char * const spdifrx_enum_input[] = {
	"in0", "in1", "in2", "in3"
};

/*  By default CS bits are retrieved from channel A */
static const char * const spdifrx_enum_cs_channel[] = {
	"A", "B"
};

static SOC_ENUM_SINGLE_DECL(ctrl_enum_input,
			    STM32_SPDIFRX_CR, SPDIFRX_CR_INSEL_SHIFT,
			    spdifrx_enum_input);

static SOC_ENUM_SINGLE_DECL(ctrl_enum_cs_channel,
			    STM32_SPDIFRX_CR, SPDIFRX_CR_CHSEL_SHIFT,
			    spdifrx_enum_cs_channel);

static int stm32_spdifrx_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int stm32_spdifrx_ub_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int stm32_spdifrx_get_ctrl_data(struct stm32_spdifrx_data *spdifrx)
{
	int ret = 0;

	memset(spdifrx->cs, 0, SPDIFRX_CS_BYTES_NB);
	memset(spdifrx->ub, 0, SPDIFRX_UB_BYTES_NB);

	pinctrl_pm_select_default_state(&spdifrx->pdev->dev);

	ret = stm32_spdifrx_dma_ctrl_start(spdifrx);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(spdifrx->kclk);
	if (ret) {
		dev_err(&spdifrx->pdev->dev, "Enable kclk failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
				 SPDIFRX_CR_CBDMAEN, SPDIFRX_CR_CBDMAEN);
	if (ret < 0)
		goto end;

	ret = stm32_spdifrx_start_sync(spdifrx);
	if (ret < 0)
		goto end;

	if (wait_for_completion_interruptible_timeout(&spdifrx->cs_completion,
						      msecs_to_jiffies(100))
						      <= 0) {
		dev_dbg(&spdifrx->pdev->dev, "Failed to get control data\n");
		ret = -EAGAIN;
	}

	stm32_spdifrx_stop(spdifrx);
	stm32_spdifrx_dma_ctrl_stop(spdifrx);

end:
	clk_disable_unprepare(spdifrx->kclk);
	pinctrl_pm_select_sleep_state(&spdifrx->pdev->dev);

	return ret;
}

static int stm32_spdifrx_capture_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);

	stm32_spdifrx_get_ctrl_data(spdifrx);

	ucontrol->value.iec958.status[0] = spdifrx->cs[0];
	ucontrol->value.iec958.status[1] = spdifrx->cs[1];
	ucontrol->value.iec958.status[2] = spdifrx->cs[2];
	ucontrol->value.iec958.status[3] = spdifrx->cs[3];
	ucontrol->value.iec958.status[4] = spdifrx->cs[4];

	return 0;
}

static int stm32_spdif_user_bits_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);

	stm32_spdifrx_get_ctrl_data(spdifrx);

	ucontrol->value.iec958.status[0] = spdifrx->ub[0];
	ucontrol->value.iec958.status[1] = spdifrx->ub[1];
	ucontrol->value.iec958.status[2] = spdifrx->ub[2];
	ucontrol->value.iec958.status[3] = spdifrx->ub[3];
	ucontrol->value.iec958.status[4] = spdifrx->ub[4];

	return 0;
}

static struct snd_kcontrol_new stm32_spdifrx_iec_ctrls[] = {
	/* Channel status control */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = stm32_spdifrx_info,
		.get = stm32_spdifrx_capture_get,
	},
	/* User bits control */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "IEC958 User Bit Capture Default",
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = stm32_spdifrx_ub_info,
		.get = stm32_spdif_user_bits_get,
	},
};

static struct snd_kcontrol_new stm32_spdifrx_ctrls[] = {
	SOC_ENUM("SPDIFRX input", ctrl_enum_input),
	SOC_ENUM("SPDIFRX CS channel", ctrl_enum_cs_channel),
};

static int stm32_spdifrx_dai_register_ctrls(struct snd_soc_dai *cpu_dai)
{
	int ret;

	ret = snd_soc_add_dai_controls(cpu_dai, stm32_spdifrx_iec_ctrls,
				       ARRAY_SIZE(stm32_spdifrx_iec_ctrls));
	if (ret < 0)
		return ret;

	return snd_soc_add_component_controls(cpu_dai->component,
					      stm32_spdifrx_ctrls,
					      ARRAY_SIZE(stm32_spdifrx_ctrls));
}

static int stm32_spdifrx_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct stm32_spdifrx_data *spdifrx = dev_get_drvdata(cpu_dai->dev);

	spdifrx->dma_params.addr = (dma_addr_t)(spdifrx->phys_addr +
				   STM32_SPDIFRX_DR);
	spdifrx->dma_params.maxburst = 1;

	snd_soc_dai_init_dma_data(cpu_dai, NULL, &spdifrx->dma_params);

	return stm32_spdifrx_dai_register_ctrls(cpu_dai);
}

static bool stm32_spdifrx_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_SPDIFRX_CR:
	case STM32_SPDIFRX_IMR:
	case STM32_SPDIFRX_SR:
	case STM32_SPDIFRX_IFCR:
	case STM32_SPDIFRX_DR:
	case STM32_SPDIFRX_CSR:
	case STM32_SPDIFRX_DIR:
	case STM32_SPDIFRX_VERR:
	case STM32_SPDIFRX_IDR:
	case STM32_SPDIFRX_SIDR:
		return true;
	default:
		return false;
	}
}

static bool stm32_spdifrx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_SPDIFRX_DR:
	case STM32_SPDIFRX_CSR:
	case STM32_SPDIFRX_SR:
	case STM32_SPDIFRX_DIR:
		return true;
	default:
		return false;
	}
}

static bool stm32_spdifrx_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_SPDIFRX_CR:
	case STM32_SPDIFRX_IMR:
	case STM32_SPDIFRX_IFCR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config stm32_h7_spdifrx_regmap_conf = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM32_SPDIFRX_SIDR,
	.readable_reg = stm32_spdifrx_readable_reg,
	.volatile_reg = stm32_spdifrx_volatile_reg,
	.writeable_reg = stm32_spdifrx_writeable_reg,
	.num_reg_defaults_raw = STM32_SPDIFRX_SIDR / sizeof(u32) + 1,
	.fast_io = true,
	.cache_type = REGCACHE_FLAT,
};

static irqreturn_t stm32_spdifrx_isr(int irq, void *devid)
{
	struct stm32_spdifrx_data *spdifrx = (struct stm32_spdifrx_data *)devid;
	struct snd_pcm_substream *substream = spdifrx->substream;
	struct platform_device *pdev = spdifrx->pdev;
	unsigned int cr, mask, sr, imr;
	unsigned int flags;
	int err = 0, err_xrun = 0;

	regmap_read(spdifrx->regmap, STM32_SPDIFRX_SR, &sr);
	regmap_read(spdifrx->regmap, STM32_SPDIFRX_IMR, &imr);

	mask = imr & SPDIFRX_XIMR_MASK;
	/* SERR, TERR, FERR IRQs are generated if IFEIE is set */
	if (mask & SPDIFRX_IMR_IFEIE)
		mask |= (SPDIFRX_IMR_IFEIE << 1) | (SPDIFRX_IMR_IFEIE << 2);

	flags = sr & mask;
	if (!flags) {
		dev_err(&pdev->dev, "Unexpected IRQ. rflags=%#x, imr=%#x\n",
			sr, imr);
		return IRQ_NONE;
	}

	/* Clear IRQs */
	regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_IFCR,
			   SPDIFRX_XIFCR_MASK, flags);

	if (flags & SPDIFRX_SR_PERR) {
		dev_dbg(&pdev->dev, "Parity error\n");
		err_xrun = 1;
	}

	if (flags & SPDIFRX_SR_OVR) {
		dev_dbg(&pdev->dev, "Overrun error\n");
		err_xrun = 1;
	}

	if (flags & SPDIFRX_SR_SBD)
		dev_dbg(&pdev->dev, "Synchronization block detected\n");

	if (flags & SPDIFRX_SR_SYNCD) {
		dev_dbg(&pdev->dev, "Synchronization done\n");

		/* Enable spdifrx */
		cr = SPDIFRX_CR_SPDIFENSET(SPDIFRX_SPDIFEN_ENABLE);
		regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
				   SPDIFRX_CR_SPDIFEN_MASK, cr);
	}

	if (flags & SPDIFRX_SR_FERR) {
		dev_dbg(&pdev->dev, "Frame error\n");
		err = 1;
	}

	if (flags & SPDIFRX_SR_SERR) {
		dev_dbg(&pdev->dev, "Synchronization error\n");
		err = 1;
	}

	if (flags & SPDIFRX_SR_TERR) {
		dev_dbg(&pdev->dev, "Timeout error\n");
		err = 1;
	}

	if (err) {
		/* SPDIFRX in STATE_STOP. Disable SPDIFRX to clear errors */
		cr = SPDIFRX_CR_SPDIFENSET(SPDIFRX_SPDIFEN_DISABLE);
		regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
				   SPDIFRX_CR_SPDIFEN_MASK, cr);

		if (substream)
			snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);

		return IRQ_HANDLED;
	}

	if (err_xrun && substream)
		snd_pcm_stop_xrun(substream);

	return IRQ_HANDLED;
}

static int stm32_spdifrx_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *cpu_dai)
{
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	spdifrx->substream = substream;

	ret = clk_prepare_enable(spdifrx->kclk);
	if (ret)
		dev_err(&spdifrx->pdev->dev, "Enable kclk failed: %d\n", ret);

	return ret;
}

static int stm32_spdifrx_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *cpu_dai)
{
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);
	int data_size = params_width(params);
	int fmt;

	switch (data_size) {
	case 16:
		fmt = SPDIFRX_DRFMT_PACKED;
		break;
	case 32:
		fmt = SPDIFRX_DRFMT_LEFT;
		break;
	default:
		dev_err(&spdifrx->pdev->dev, "Unexpected data format\n");
		return -EINVAL;
	}

	/*
	 * Set buswidth to 4 bytes for all data formats.
	 * Packed format: transfer 2 x 2 bytes samples
	 * Left format: transfer 1 x 3 bytes samples + 1 dummy byte
	 */
	spdifrx->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	snd_soc_dai_init_dma_data(cpu_dai, NULL, &spdifrx->dma_params);

	return regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
				  SPDIFRX_CR_DRFMT_MASK,
				  SPDIFRX_CR_DRFMTSET(fmt));
}

static int stm32_spdifrx_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *cpu_dai)
{
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_IMR,
				   SPDIFRX_IMR_OVRIE, SPDIFRX_IMR_OVRIE);

		regmap_update_bits(spdifrx->regmap, STM32_SPDIFRX_CR,
				   SPDIFRX_CR_RXDMAEN, SPDIFRX_CR_RXDMAEN);

		ret = stm32_spdifrx_start_sync(spdifrx);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		stm32_spdifrx_stop(spdifrx);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void stm32_spdifrx_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *cpu_dai)
{
	struct stm32_spdifrx_data *spdifrx = snd_soc_dai_get_drvdata(cpu_dai);

	spdifrx->substream = NULL;
	clk_disable_unprepare(spdifrx->kclk);
}

static const struct snd_soc_dai_ops stm32_spdifrx_pcm_dai_ops = {
	.startup	= stm32_spdifrx_startup,
	.hw_params	= stm32_spdifrx_hw_params,
	.trigger	= stm32_spdifrx_trigger,
	.shutdown	= stm32_spdifrx_shutdown,
};

static struct snd_soc_dai_driver stm32_spdifrx_dai[] = {
	{
		.probe = stm32_spdifrx_dai_probe,
		.capture = {
			.stream_name = "CPU-Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE |
				   SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &stm32_spdifrx_pcm_dai_ops,
	}
};

static const struct snd_pcm_hardware stm32_spdifrx_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024,
	.period_bytes_max = 4 * PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
};

static const struct snd_soc_component_driver stm32_spdifrx_component = {
	.name = "stm32-spdifrx",
};

static const struct snd_dmaengine_pcm_config stm32_spdifrx_pcm_config = {
	.pcm_hardware = &stm32_spdifrx_pcm_hw,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static const struct of_device_id stm32_spdifrx_ids[] = {
	{
		.compatible = "st,stm32h7-spdifrx",
		.data = &stm32_h7_spdifrx_regmap_conf
	},
	{}
};

static int stm32_spdifrx_parse_of(struct platform_device *pdev,
				  struct stm32_spdifrx_data *spdifrx)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct resource *res;

	if (!np)
		return -ENODEV;

	of_id = of_match_device(stm32_spdifrx_ids, &pdev->dev);
	if (of_id)
		spdifrx->regmap_conf =
			(const struct regmap_config *)of_id->data;
	else
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spdifrx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spdifrx->base))
		return PTR_ERR(spdifrx->base);

	spdifrx->phys_addr = res->start;

	spdifrx->kclk = devm_clk_get(&pdev->dev, "kclk");
	if (IS_ERR(spdifrx->kclk)) {
		dev_err(&pdev->dev, "Could not get kclk\n");
		return PTR_ERR(spdifrx->kclk);
	}

	spdifrx->irq = platform_get_irq(pdev, 0);
	if (spdifrx->irq < 0)
		return spdifrx->irq;

	return 0;
}

static int stm32_spdifrx_probe(struct platform_device *pdev)
{
	struct stm32_spdifrx_data *spdifrx;
	struct reset_control *rst;
	const struct snd_dmaengine_pcm_config *pcm_config = NULL;
	u32 ver, idr;
	int ret;

	spdifrx = devm_kzalloc(&pdev->dev, sizeof(*spdifrx), GFP_KERNEL);
	if (!spdifrx)
		return -ENOMEM;

	spdifrx->pdev = pdev;
	init_completion(&spdifrx->cs_completion);
	spin_lock_init(&spdifrx->lock);

	platform_set_drvdata(pdev, spdifrx);

	ret = stm32_spdifrx_parse_of(pdev, spdifrx);
	if (ret)
		return ret;

	spdifrx->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "kclk",
						    spdifrx->base,
						    spdifrx->regmap_conf);
	if (IS_ERR(spdifrx->regmap)) {
		dev_err(&pdev->dev, "Regmap init failed\n");
		return PTR_ERR(spdifrx->regmap);
	}

	ret = devm_request_irq(&pdev->dev, spdifrx->irq, stm32_spdifrx_isr, 0,
			       dev_name(&pdev->dev), spdifrx);
	if (ret) {
		dev_err(&pdev->dev, "IRQ request returned %d\n", ret);
		return ret;
	}

	rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (!IS_ERR(rst)) {
		reset_control_assert(rst);
		udelay(2);
		reset_control_deassert(rst);
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &stm32_spdifrx_component,
					      stm32_spdifrx_dai,
					      ARRAY_SIZE(stm32_spdifrx_dai));
	if (ret)
		return ret;

	ret = stm32_spdifrx_dma_ctrl_register(&pdev->dev, spdifrx);
	if (ret)
		goto error;

	pcm_config = &stm32_spdifrx_pcm_config;
	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, pcm_config, 0);
	if (ret) {
		dev_err(&pdev->dev, "PCM DMA register returned %d\n", ret);
		goto error;
	}

	ret = regmap_read(spdifrx->regmap, STM32_SPDIFRX_IDR, &idr);
	if (ret)
		goto error;

	if (idr == SPDIFRX_IPIDR_NUMBER) {
		ret = regmap_read(spdifrx->regmap, STM32_SPDIFRX_VERR, &ver);

		dev_dbg(&pdev->dev, "SPDIFRX version: %lu.%lu registered\n",
			FIELD_GET(SPDIFRX_VERR_MAJ_MASK, ver),
			FIELD_GET(SPDIFRX_VERR_MIN_MASK, ver));
	}

	return ret;

error:
	if (!IS_ERR(spdifrx->ctrl_chan))
		dma_release_channel(spdifrx->ctrl_chan);
	if (spdifrx->dmab)
		snd_dma_free_pages(spdifrx->dmab);

	return ret;
}

static int stm32_spdifrx_remove(struct platform_device *pdev)
{
	struct stm32_spdifrx_data *spdifrx = platform_get_drvdata(pdev);

	if (spdifrx->ctrl_chan)
		dma_release_channel(spdifrx->ctrl_chan);

	if (spdifrx->dmab)
		snd_dma_free_pages(spdifrx->dmab);

	return 0;
}

MODULE_DEVICE_TABLE(of, stm32_spdifrx_ids);

#ifdef CONFIG_PM_SLEEP
static int stm32_spdifrx_suspend(struct device *dev)
{
	struct stm32_spdifrx_data *spdifrx = dev_get_drvdata(dev);

	regcache_cache_only(spdifrx->regmap, true);
	regcache_mark_dirty(spdifrx->regmap);

	return 0;
}

static int stm32_spdifrx_resume(struct device *dev)
{
	struct stm32_spdifrx_data *spdifrx = dev_get_drvdata(dev);

	regcache_cache_only(spdifrx->regmap, false);

	return regcache_sync(spdifrx->regmap);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops stm32_spdifrx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_spdifrx_suspend, stm32_spdifrx_resume)
};

static struct platform_driver stm32_spdifrx_driver = {
	.driver = {
		.name = "st,stm32-spdifrx",
		.of_match_table = stm32_spdifrx_ids,
		.pm = &stm32_spdifrx_pm_ops,
	},
	.probe = stm32_spdifrx_probe,
	.remove = stm32_spdifrx_remove,
};

module_platform_driver(stm32_spdifrx_driver);

MODULE_DESCRIPTION("STM32 Soc spdifrx Interface");
MODULE_AUTHOR("Olivier Moysan, <olivier.moysan@st.com>");
MODULE_ALIAS("platform:stm32-spdifrx");
MODULE_LICENSE("GPL v2");
