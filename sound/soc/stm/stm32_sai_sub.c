// SPDX-License-Identifier: GPL-2.0-only
/*
 * STM32 ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "stm32_sai.h"

#define SAI_FREE_PROTOCOL	0x0
#define SAI_SPDIF_PROTOCOL	0x1

#define SAI_SLOT_SIZE_AUTO	0x0
#define SAI_SLOT_SIZE_16	0x1
#define SAI_SLOT_SIZE_32	0x2

#define SAI_DATASIZE_8		0x2
#define SAI_DATASIZE_10		0x3
#define SAI_DATASIZE_16		0x4
#define SAI_DATASIZE_20		0x5
#define SAI_DATASIZE_24		0x6
#define SAI_DATASIZE_32		0x7

#define STM_SAI_DAI_NAME_SIZE	15

#define STM_SAI_IS_PLAYBACK(ip)	((ip)->dir == SNDRV_PCM_STREAM_PLAYBACK)
#define STM_SAI_IS_CAPTURE(ip)	((ip)->dir == SNDRV_PCM_STREAM_CAPTURE)

#define STM_SAI_A_ID		0x0
#define STM_SAI_B_ID		0x1

#define STM_SAI_IS_SUB_A(x)	((x)->id == STM_SAI_A_ID)

#define SAI_SYNC_NONE		0x0
#define SAI_SYNC_INTERNAL	0x1
#define SAI_SYNC_EXTERNAL	0x2

#define STM_SAI_PROTOCOL_IS_SPDIF(ip)	((ip)->spdif)
#define STM_SAI_HAS_SPDIF(x)	((x)->pdata->conf.has_spdif_pdm)
#define STM_SAI_HAS_PDM(x)	((x)->pdata->conf.has_spdif_pdm)
#define STM_SAI_HAS_EXT_SYNC(x) (!STM_SAI_IS_F4((x)->pdata))

#define SAI_IEC60958_BLOCK_FRAMES	192
#define SAI_IEC60958_STATUS_BYTES	24

#define SAI_MCLK_NAME_LEN		32
#define SAI_RATE_11K			11025
#define SAI_MAX_SAMPLE_RATE_8K		192000
#define SAI_MAX_SAMPLE_RATE_11K		176400
#define SAI_CK_RATE_TOLERANCE		1000 /* ppm */

/**
 * struct stm32_sai_sub_data - private data of SAI sub block (block A or B)
 * @pdev: device data pointer
 * @regmap: SAI register map pointer
 * @regmap_config: SAI sub block register map configuration pointer
 * @dma_params: dma configuration data for rx or tx channel
 * @cpu_dai_drv: DAI driver data pointer
 * @cpu_dai: DAI runtime data pointer
 * @substream: PCM substream data pointer
 * @pdata: SAI block parent data pointer
 * @np_sync_provider: synchronization provider node
 * @sai_ck: kernel clock feeding the SAI clock generator
 * @sai_mclk: master clock from SAI mclk provider
 * @phys_addr: SAI registers physical base address
 * @mclk_rate: SAI block master clock frequency (Hz). set at init
 * @id: SAI sub block id corresponding to sub-block A or B
 * @dir: SAI block direction (playback or capture). set at init
 * @master: SAI block mode flag. (true=master, false=slave) set at init
 * @spdif: SAI S/PDIF iec60958 mode flag. set at init
 * @sai_ck_used: flag set while exclusivity on SAI kernel clock is active
 * @fmt: SAI block format. relevant only for custom protocols. set at init
 * @sync: SAI block synchronization mode. (none, internal or external)
 * @synco: SAI block ext sync source (provider setting). (none, sub-block A/B)
 * @synci: SAI block ext sync source (client setting). (SAI sync provider index)
 * @fs_length: frame synchronization length. depends on protocol settings
 * @slots: rx or tx slot number
 * @slot_width: rx or tx slot width in bits
 * @slot_mask: rx or tx active slots mask. set at init or at runtime
 * @data_size: PCM data width. corresponds to PCM substream width.
 * @spdif_frm_cnt: S/PDIF playback frame counter
 * @iec958: iec958 data
 * @ctrl_lock: control lock
 * @irq_lock: prevent race condition with IRQ
 * @set_sai_ck_rate: set SAI kernel clock rate
 * @put_sai_ck_rate: put SAI kernel clock rate
 */
struct stm32_sai_sub_data {
	struct platform_device *pdev;
	struct regmap *regmap;
	const struct regmap_config *regmap_config;
	struct snd_dmaengine_dai_dma_data dma_params;
	struct snd_soc_dai_driver cpu_dai_drv;
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	struct stm32_sai_data *pdata;
	struct device_node *np_sync_provider;
	struct clk *sai_ck;
	struct clk *sai_mclk;
	dma_addr_t phys_addr;
	unsigned int mclk_rate;
	unsigned int id;
	int dir;
	bool master;
	bool spdif;
	bool sai_ck_used;
	int fmt;
	int sync;
	int synco;
	int synci;
	int fs_length;
	int slots;
	int slot_width;
	int slot_mask;
	int data_size;
	unsigned int spdif_frm_cnt;
	struct snd_aes_iec958 iec958;
	struct mutex ctrl_lock; /* protect resources accessed by controls */
	spinlock_t irq_lock; /* used to prevent race condition with IRQ */
	int (*set_sai_ck_rate)(struct stm32_sai_sub_data *sai, unsigned int rate);
	void (*put_sai_ck_rate)(struct stm32_sai_sub_data *sai);
};

enum stm32_sai_fifo_th {
	STM_SAI_FIFO_TH_EMPTY,
	STM_SAI_FIFO_TH_QUARTER,
	STM_SAI_FIFO_TH_HALF,
	STM_SAI_FIFO_TH_3_QUARTER,
	STM_SAI_FIFO_TH_FULL,
};

static bool stm32_sai_sub_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_CR1_REGX:
	case STM_SAI_CR2_REGX:
	case STM_SAI_FRCR_REGX:
	case STM_SAI_SLOTR_REGX:
	case STM_SAI_IMR_REGX:
	case STM_SAI_SR_REGX:
	case STM_SAI_CLRFR_REGX:
	case STM_SAI_DR_REGX:
	case STM_SAI_PDMCR_REGX:
	case STM_SAI_PDMLY_REGX:
		return true;
	default:
		return false;
	}
}

static bool stm32_sai_sub_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_DR_REGX:
	case STM_SAI_SR_REGX:
		return true;
	default:
		return false;
	}
}

static bool stm32_sai_sub_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_CR1_REGX:
	case STM_SAI_CR2_REGX:
	case STM_SAI_FRCR_REGX:
	case STM_SAI_SLOTR_REGX:
	case STM_SAI_IMR_REGX:
	case STM_SAI_CLRFR_REGX:
	case STM_SAI_DR_REGX:
	case STM_SAI_PDMCR_REGX:
	case STM_SAI_PDMLY_REGX:
		return true;
	default:
		return false;
	}
}

static int stm32_sai_sub_reg_up(struct stm32_sai_sub_data *sai,
				unsigned int reg, unsigned int mask,
				unsigned int val)
{
	int ret;

	ret = clk_enable(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(sai->regmap, reg, mask, val);

	clk_disable(sai->pdata->pclk);

	return ret;
}

static int stm32_sai_sub_reg_wr(struct stm32_sai_sub_data *sai,
				unsigned int reg, unsigned int mask,
				unsigned int val)
{
	int ret;

	ret = clk_enable(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	ret = regmap_write_bits(sai->regmap, reg, mask, val);

	clk_disable(sai->pdata->pclk);

	return ret;
}

static int stm32_sai_sub_reg_rd(struct stm32_sai_sub_data *sai,
				unsigned int reg, unsigned int *val)
{
	int ret;

	ret = clk_enable(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	ret = regmap_read(sai->regmap, reg, val);

	clk_disable(sai->pdata->pclk);

	return ret;
}

static const struct regmap_config stm32_sai_sub_regmap_config_f4 = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM_SAI_DR_REGX,
	.readable_reg = stm32_sai_sub_readable_reg,
	.volatile_reg = stm32_sai_sub_volatile_reg,
	.writeable_reg = stm32_sai_sub_writeable_reg,
	.fast_io = true,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config stm32_sai_sub_regmap_config_h7 = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM_SAI_PDMLY_REGX,
	.readable_reg = stm32_sai_sub_readable_reg,
	.volatile_reg = stm32_sai_sub_volatile_reg,
	.writeable_reg = stm32_sai_sub_writeable_reg,
	.fast_io = true,
	.cache_type = REGCACHE_FLAT,
};

static int snd_pcm_iec958_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int snd_pcm_iec958_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *uctl)
{
	struct stm32_sai_sub_data *sai = snd_kcontrol_chip(kcontrol);

	mutex_lock(&sai->ctrl_lock);
	memcpy(uctl->value.iec958.status, sai->iec958.status, 4);
	mutex_unlock(&sai->ctrl_lock);

	return 0;
}

static int snd_pcm_iec958_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *uctl)
{
	struct stm32_sai_sub_data *sai = snd_kcontrol_chip(kcontrol);

	mutex_lock(&sai->ctrl_lock);
	memcpy(sai->iec958.status, uctl->value.iec958.status, 4);
	mutex_unlock(&sai->ctrl_lock);

	return 0;
}

static const struct snd_kcontrol_new iec958_ctls = {
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE),
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info = snd_pcm_iec958_info,
	.get = snd_pcm_iec958_get,
	.put = snd_pcm_iec958_put,
};

struct stm32_sai_mclk_data {
	struct clk_hw hw;
	unsigned long freq;
	struct stm32_sai_sub_data *sai_data;
};

#define to_mclk_data(_hw) container_of(_hw, struct stm32_sai_mclk_data, hw)
#define STM32_SAI_MAX_CLKS 1

static int stm32_sai_get_clk_div(struct stm32_sai_sub_data *sai,
				 unsigned long input_rate,
				 unsigned long output_rate)
{
	int version = sai->pdata->conf.version;
	int div;

	div = DIV_ROUND_CLOSEST(input_rate, output_rate);
	if (div > SAI_XCR1_MCKDIV_MAX(version) || div <= 0) {
		dev_err(&sai->pdev->dev, "Divider %d out of range\n", div);
		return -EINVAL;
	}
	dev_dbg(&sai->pdev->dev, "SAI divider %d\n", div);

	if (input_rate % div)
		dev_dbg(&sai->pdev->dev,
			"Rate not accurate. requested (%ld), actual (%ld)\n",
			output_rate, input_rate / div);

	return div;
}

static int stm32_sai_set_clk_div(struct stm32_sai_sub_data *sai,
				 unsigned int div)
{
	int version = sai->pdata->conf.version;
	int ret, cr1, mask;

	if (div > SAI_XCR1_MCKDIV_MAX(version)) {
		dev_err(&sai->pdev->dev, "Divider %d out of range\n", div);
		return -EINVAL;
	}

	mask = SAI_XCR1_MCKDIV_MASK(SAI_XCR1_MCKDIV_WIDTH(version));
	cr1 = SAI_XCR1_MCKDIV_SET(div);
	ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX, mask, cr1);
	if (ret < 0)
		dev_err(&sai->pdev->dev, "Failed to update CR1 register\n");

	return ret;
}

static bool stm32_sai_rate_accurate(unsigned int max_rate, unsigned int rate)
{
	u64 delta, dividend;
	int ratio;

	ratio = DIV_ROUND_CLOSEST(max_rate, rate);
	if (!ratio)
		return false;

	dividend = mul_u32_u32(1000000, abs(max_rate - (ratio * rate)));
	delta = div_u64(dividend, max_rate);

	if (delta <= SAI_CK_RATE_TOLERANCE)
		return true;

	return false;
}

static int stm32_sai_set_parent_clk(struct stm32_sai_sub_data *sai,
				    unsigned int rate)
{
	struct platform_device *pdev = sai->pdev;
	struct clk *parent_clk = sai->pdata->clk_x8k;
	int ret;

	if (!(rate % SAI_RATE_11K))
		parent_clk = sai->pdata->clk_x11k;

	ret = clk_set_parent(sai->sai_ck, parent_clk);
	if (ret)
		dev_err(&pdev->dev, " Error %d setting sai_ck parent clock. %s",
			ret, ret == -EBUSY ?
			"Active stream rates conflict\n" : "\n");

	return ret;
}

static void stm32_sai_put_parent_rate(struct stm32_sai_sub_data *sai)
{
	if (sai->sai_ck_used) {
		sai->sai_ck_used = false;
		clk_rate_exclusive_put(sai->sai_ck);
	}
}

static int stm32_sai_set_parent_rate(struct stm32_sai_sub_data *sai,
				     unsigned int rate)
{
	struct platform_device *pdev = sai->pdev;
	unsigned int sai_ck_rate, sai_ck_max_rate, sai_ck_min_rate, sai_curr_rate, sai_new_rate;
	int div, ret;

	/*
	 * Set minimum and maximum expected kernel clock frequency
	 * - mclk on or spdif:
	 *   f_sai_ck = MCKDIV * mclk-fs * fs
	 *   Here typical 256 ratio is assumed for mclk-fs
	 * - mclk off:
	 *   f_sai_ck = MCKDIV * FRL * fs
	 *   Where FRL=[8..256], MCKDIV=[1..n] (n depends on SAI version)
	 *   Set constraint MCKDIV * FRL <= 256, to ensure MCKDIV is in available range
	 *   f_sai_ck = sai_ck_max_rate * pow_of_two(FRL) / 256
	 */
	sai_ck_min_rate = rate * 256;
	if (!(rate % SAI_RATE_11K))
		sai_ck_max_rate = SAI_MAX_SAMPLE_RATE_11K * 256;
	else
		sai_ck_max_rate = SAI_MAX_SAMPLE_RATE_8K * 256;

	if (!sai->sai_mclk && !STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		sai_ck_min_rate = rate * sai->fs_length;
		sai_ck_max_rate /= DIV_ROUND_CLOSEST(256, roundup_pow_of_two(sai->fs_length));
	}

	/*
	 * Request exclusivity, as the clock is shared by SAI sub-blocks and by
	 * some SAI instances. This allows to ensure that the rate cannot be
	 * changed while one or more SAIs are using the clock.
	 */
	clk_rate_exclusive_get(sai->sai_ck);
	sai->sai_ck_used = true;

	/*
	 * Check current kernel clock rate. If it gives the expected accuracy
	 * return immediately.
	 */
	sai_curr_rate = clk_get_rate(sai->sai_ck);
	dev_dbg(&pdev->dev, "kernel clock rate: min [%u], max [%u], current [%u]",
		sai_ck_min_rate, sai_ck_max_rate, sai_curr_rate);
	if (stm32_sai_rate_accurate(sai_ck_max_rate, sai_curr_rate) &&
	    sai_curr_rate >= sai_ck_min_rate)
		return 0;

	/*
	 * Otherwise try to set the maximum rate and check the new actual rate.
	 * If the new rate does not give the expected accuracy, try to set
	 * lower rates for the kernel clock.
	 */
	sai_ck_rate = sai_ck_max_rate;
	div = 1;
	do {
		/* Check new rate accuracy. Return if ok */
		sai_new_rate = clk_round_rate(sai->sai_ck, sai_ck_rate);
		if (stm32_sai_rate_accurate(sai_ck_rate, sai_new_rate)) {
			ret = clk_set_rate(sai->sai_ck, sai_ck_rate);
			if (ret) {
				dev_err(&pdev->dev, "Error %d setting sai_ck rate. %s",
					ret, ret == -EBUSY ?
					"Active stream rates may be in conflict\n" : "\n");
				goto err;
			}

			return 0;
		}

		/* Try a lower frequency */
		div++;
		sai_ck_rate = sai_ck_max_rate / div;
	} while (sai_ck_rate >= sai_ck_min_rate);

	/* No accurate rate found */
	dev_err(&pdev->dev, "Failed to find an accurate rate");

err:
	stm32_sai_put_parent_rate(sai);

	return -EINVAL;
}

static int stm32_sai_mclk_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct stm32_sai_mclk_data *mclk = to_mclk_data(hw);
	struct stm32_sai_sub_data *sai = mclk->sai_data;
	int div;

	div = stm32_sai_get_clk_div(sai, req->best_parent_rate, req->rate);
	if (div <= 0)
		return -EINVAL;

	mclk->freq = req->best_parent_rate / div;

	req->rate = mclk->freq;

	return 0;
}

static unsigned long stm32_sai_mclk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct stm32_sai_mclk_data *mclk = to_mclk_data(hw);

	return mclk->freq;
}

static int stm32_sai_mclk_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct stm32_sai_mclk_data *mclk = to_mclk_data(hw);
	struct stm32_sai_sub_data *sai = mclk->sai_data;
	int div, ret;

	div = stm32_sai_get_clk_div(sai, parent_rate, rate);
	if (div < 0)
		return div;

	ret = stm32_sai_set_clk_div(sai, div);
	if (ret)
		return ret;

	mclk->freq = rate;

	return 0;
}

static int stm32_sai_mclk_enable(struct clk_hw *hw)
{
	struct stm32_sai_mclk_data *mclk = to_mclk_data(hw);
	struct stm32_sai_sub_data *sai = mclk->sai_data;

	dev_dbg(&sai->pdev->dev, "Enable master clock\n");

	return stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
				    SAI_XCR1_MCKEN, SAI_XCR1_MCKEN);
}

static void stm32_sai_mclk_disable(struct clk_hw *hw)
{
	struct stm32_sai_mclk_data *mclk = to_mclk_data(hw);
	struct stm32_sai_sub_data *sai = mclk->sai_data;

	dev_dbg(&sai->pdev->dev, "Disable master clock\n");

	stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX, SAI_XCR1_MCKEN, 0);
}

static const struct clk_ops mclk_ops = {
	.enable = stm32_sai_mclk_enable,
	.disable = stm32_sai_mclk_disable,
	.recalc_rate = stm32_sai_mclk_recalc_rate,
	.determine_rate = stm32_sai_mclk_determine_rate,
	.set_rate = stm32_sai_mclk_set_rate,
};

static int stm32_sai_add_mclk_provider(struct stm32_sai_sub_data *sai)
{
	struct clk_hw *hw;
	struct stm32_sai_mclk_data *mclk;
	struct device *dev = &sai->pdev->dev;
	const char *pname = __clk_get_name(sai->sai_ck);
	char *mclk_name, *p, *s = (char *)pname;
	int ret, i = 0;

	mclk = devm_kzalloc(dev, sizeof(*mclk), GFP_KERNEL);
	if (!mclk)
		return -ENOMEM;

	mclk_name = devm_kcalloc(dev, sizeof(char),
				 SAI_MCLK_NAME_LEN, GFP_KERNEL);
	if (!mclk_name)
		return -ENOMEM;

	/*
	 * Forge mclk clock name from parent clock name and suffix.
	 * String after "_" char is stripped in parent name.
	 */
	p = mclk_name;
	while (*s && *s != '_' && (i < (SAI_MCLK_NAME_LEN - 7))) {
		*p++ = *s++;
		i++;
	}
	STM_SAI_IS_SUB_A(sai) ? strcat(p, "a_mclk") : strcat(p, "b_mclk");

	mclk->hw.init = CLK_HW_INIT(mclk_name, pname, &mclk_ops, 0);
	mclk->sai_data = sai;
	hw = &mclk->hw;

	dev_dbg(dev, "Register master clock %s\n", mclk_name);
	ret = devm_clk_hw_register(&sai->pdev->dev, hw);
	if (ret) {
		dev_err(dev, "mclk register returned %d\n", ret);
		return ret;
	}
	sai->sai_mclk = hw->clk;

	/* register mclk provider */
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static irqreturn_t stm32_sai_isr(int irq, void *devid)
{
	struct stm32_sai_sub_data *sai = (struct stm32_sai_sub_data *)devid;
	struct platform_device *pdev = sai->pdev;
	unsigned int sr, imr, flags;
	snd_pcm_state_t status = SNDRV_PCM_STATE_RUNNING;

	stm32_sai_sub_reg_rd(sai, STM_SAI_IMR_REGX, &imr);
	stm32_sai_sub_reg_rd(sai, STM_SAI_SR_REGX, &sr);

	flags = sr & imr;
	if (!flags)
		return IRQ_NONE;

	stm32_sai_sub_reg_wr(sai, STM_SAI_CLRFR_REGX, SAI_XCLRFR_MASK,
			     SAI_XCLRFR_MASK);

	if (!sai->substream) {
		dev_err(&pdev->dev, "Device stopped. Spurious IRQ 0x%x\n", sr);
		return IRQ_NONE;
	}

	if (flags & SAI_XIMR_OVRUDRIE) {
		dev_err(&pdev->dev, "IRQ %s\n",
			STM_SAI_IS_PLAYBACK(sai) ? "underrun" : "overrun");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_MUTEDETIE)
		dev_dbg(&pdev->dev, "IRQ mute detected\n");

	if (flags & SAI_XIMR_WCKCFGIE) {
		dev_err(&pdev->dev, "IRQ wrong clock configuration\n");
		status = SNDRV_PCM_STATE_DISCONNECTED;
	}

	if (flags & SAI_XIMR_CNRDYIE)
		dev_err(&pdev->dev, "IRQ Codec not ready\n");

	if (flags & SAI_XIMR_AFSDETIE) {
		dev_err(&pdev->dev, "IRQ Anticipated frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_LFSDETIE) {
		dev_err(&pdev->dev, "IRQ Late frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	spin_lock(&sai->irq_lock);
	if (status != SNDRV_PCM_STATE_RUNNING && sai->substream)
		snd_pcm_stop_xrun(sai->substream);
	spin_unlock(&sai->irq_lock);

	return IRQ_HANDLED;
}

static int stm32_sai_set_sysclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	/*
	 * The mclk rate is determined at runtime from the audio stream rate.
	 * Skip calls to the set_sysclk callback that are not relevant during the
	 * initialization phase.
	 */
	if (!snd_soc_card_is_instantiated(cpu_dai->component->card))
		return 0;

	if (dir == SND_SOC_CLOCK_OUT && sai->sai_mclk) {
		ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
					   SAI_XCR1_NODIV,
					 freq ? 0 : SAI_XCR1_NODIV);
		if (ret < 0)
			return ret;

		/* Assume shutdown if requested frequency is 0Hz */
		if (!freq) {
			/* Release mclk rate only if rate was actually set */
			if (sai->mclk_rate) {
				clk_rate_exclusive_put(sai->sai_mclk);
				sai->mclk_rate = 0;
			}

			if (sai->put_sai_ck_rate)
				sai->put_sai_ck_rate(sai);

			return 0;
		}

		/* If master clock is used, configure SAI kernel clock now */
		ret = sai->set_sai_ck_rate(sai, freq);
		if (ret)
			return ret;

		ret = clk_set_rate_exclusive(sai->sai_mclk, freq);
		if (ret) {
			dev_err(cpu_dai->dev,
				ret == -EBUSY ?
				"Active streams have incompatible rates" :
				"Could not set mclk rate\n");
			return ret;
		}

		dev_dbg(cpu_dai->dev, "SAI MCLK frequency is %uHz\n", freq);
		sai->mclk_rate = freq;
	}

	return 0;
}

static int stm32_sai_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai, u32 tx_mask,
				      u32 rx_mask, int slots, int slot_width)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int slotr, slotr_mask, slot_size;

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		dev_warn(cpu_dai->dev, "Slot setting relevant only for TDM\n");
		return 0;
	}

	dev_dbg(cpu_dai->dev, "Masks tx/rx:%#x/%#x, slots:%d, width:%d\n",
		tx_mask, rx_mask, slots, slot_width);

	switch (slot_width) {
	case 16:
		slot_size = SAI_SLOT_SIZE_16;
		break;
	case 32:
		slot_size = SAI_SLOT_SIZE_32;
		break;
	default:
		slot_size = SAI_SLOT_SIZE_AUTO;
		break;
	}

	slotr = SAI_XSLOTR_SLOTSZ_SET(slot_size) |
		SAI_XSLOTR_NBSLOT_SET(slots - 1);
	slotr_mask = SAI_XSLOTR_SLOTSZ_MASK | SAI_XSLOTR_NBSLOT_MASK;

	/* tx/rx mask set in machine init, if slot number defined in DT */
	if (STM_SAI_IS_PLAYBACK(sai)) {
		sai->slot_mask = tx_mask;
		slotr |= SAI_XSLOTR_SLOTEN_SET(tx_mask);
	}

	if (STM_SAI_IS_CAPTURE(sai)) {
		sai->slot_mask = rx_mask;
		slotr |= SAI_XSLOTR_SLOTEN_SET(rx_mask);
	}

	slotr_mask |= SAI_XSLOTR_SLOTEN_MASK;

	stm32_sai_sub_reg_up(sai, STM_SAI_SLOTR_REGX, slotr_mask, slotr);

	sai->slot_width = slot_width;
	sai->slots = slots;

	return 0;
}

static int stm32_sai_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1, frcr = 0;
	int cr1_mask, frcr_mask = 0;
	int ret;

	dev_dbg(cpu_dai->dev, "fmt %x\n", fmt);

	/* Do not generate master by default */
	cr1 = SAI_XCR1_NODIV;
	cr1_mask = SAI_XCR1_NODIV;

	cr1_mask |= SAI_XCR1_PRTCFG_MASK;
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		cr1 |= SAI_XCR1_PRTCFG_SET(SAI_SPDIF_PROTOCOL);
		goto conf_update;
	}

	cr1 |= SAI_XCR1_PRTCFG_SET(SAI_FREE_PROTOCOL);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	/* SCK active high for all protocols */
	case SND_SOC_DAIFMT_I2S:
		cr1 |= SAI_XCR1_CKSTR;
		frcr |= SAI_XFRCR_FSOFF | SAI_XFRCR_FSDEF;
		break;
	/* Left justified */
	case SND_SOC_DAIFMT_MSB:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSDEF;
		break;
	/* Right justified */
	case SND_SOC_DAIFMT_LSB:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSDEF;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSOFF;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		frcr |= SAI_XFRCR_FSPOL;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported protocol %#x\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	cr1_mask |= SAI_XCR1_CKSTR;
	frcr_mask |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSOFF |
		     SAI_XFRCR_FSDEF;

	/* DAI clock strobing. Invert setting previously set */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		cr1 ^= SAI_XCR1_CKSTR;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		frcr ^= SAI_XFRCR_FSPOL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert fs & sck */
		cr1 ^= SAI_XCR1_CKSTR;
		frcr ^= SAI_XFRCR_FSPOL;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported strobing %#x\n",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}
	cr1_mask |= SAI_XCR1_CKSTR;
	frcr_mask |= SAI_XFRCR_FSPOL;

	stm32_sai_sub_reg_up(sai, STM_SAI_FRCR_REGX, frcr_mask, frcr);

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		/* codec is master */
		cr1 |= SAI_XCR1_SLAVE;
		sai->master = false;
		break;
	case SND_SOC_DAIFMT_BP_FP:
		sai->master = true;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported mode %#x\n",
			fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK);
		return -EINVAL;
	}

	/* Set slave mode if sub-block is synchronized with another SAI */
	if (sai->sync) {
		dev_dbg(cpu_dai->dev, "Synchronized SAI configured as slave\n");
		cr1 |= SAI_XCR1_SLAVE;
		sai->master = false;
	}

	cr1_mask |= SAI_XCR1_SLAVE;

conf_update:
	ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX, cr1_mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

	sai->fmt = fmt;

	return 0;
}

static int stm32_sai_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int imr, cr2, ret;
	unsigned long flags;

	spin_lock_irqsave(&sai->irq_lock, flags);
	sai->substream = substream;
	spin_unlock_irqrestore(&sai->irq_lock, flags);

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S32_LE);
		snd_pcm_hw_constraint_single(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}

	ret = clk_prepare_enable(sai->sai_ck);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to enable clock: %d\n", ret);
		return ret;
	}

	/* Enable ITs */
	stm32_sai_sub_reg_wr(sai, STM_SAI_CLRFR_REGX,
			     SAI_XCLRFR_MASK, SAI_XCLRFR_MASK);

	imr = SAI_XIMR_OVRUDRIE;
	if (STM_SAI_IS_CAPTURE(sai)) {
		stm32_sai_sub_reg_rd(sai, STM_SAI_CR2_REGX, &cr2);
		if (cr2 & SAI_XCR2_MUTECNT_MASK)
			imr |= SAI_XIMR_MUTEDETIE;
	}

	if (sai->master)
		imr |= SAI_XIMR_WCKCFGIE;
	else
		imr |= SAI_XIMR_AFSDETIE | SAI_XIMR_LFSDETIE;

	stm32_sai_sub_reg_up(sai, STM_SAI_IMR_REGX,
			     SAI_XIMR_MASK, imr);

	return 0;
}

static int stm32_sai_set_config(struct snd_soc_dai *cpu_dai,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1, cr1_mask, ret;

	/*
	 * DMA bursts increment is set to 4 words.
	 * SAI fifo threshold is set to half fifo, to keep enough space
	 * for DMA incoming bursts.
	 */
	stm32_sai_sub_reg_wr(sai, STM_SAI_CR2_REGX,
			     SAI_XCR2_FFLUSH | SAI_XCR2_FTH_MASK,
			     SAI_XCR2_FFLUSH |
			     SAI_XCR2_FTH_SET(STM_SAI_FIFO_TH_HALF));

	/* DS bits in CR1 not set for SPDIF (size forced to 24 bits).*/
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		sai->spdif_frm_cnt = 0;
		return 0;
	}

	/* Mode, data format and channel config */
	cr1_mask = SAI_XCR1_DS_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_16);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_32);
		break;
	default:
		dev_err(cpu_dai->dev, "Data format not supported\n");
		return -EINVAL;
	}

	cr1_mask |= SAI_XCR1_MONO;
	if ((sai->slots == 2) && (params_channels(params) == 1))
		cr1 |= SAI_XCR1_MONO;

	ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX, cr1_mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

	return 0;
}

static int stm32_sai_set_slots(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int slotr, slot_sz;

	stm32_sai_sub_reg_rd(sai, STM_SAI_SLOTR_REGX, &slotr);

	/*
	 * If SLOTSZ is set to auto in SLOTR, align slot width on data size
	 * By default slot width = data size, if not forced from DT
	 */
	slot_sz = slotr & SAI_XSLOTR_SLOTSZ_MASK;
	if (slot_sz == SAI_XSLOTR_SLOTSZ_SET(SAI_SLOT_SIZE_AUTO))
		sai->slot_width = sai->data_size;

	if (sai->slot_width < sai->data_size) {
		dev_err(cpu_dai->dev,
			"Data size %d larger than slot width\n",
			sai->data_size);
		return -EINVAL;
	}

	/* Slot number is set to 2, if not specified in DT */
	if (!sai->slots)
		sai->slots = 2;

	/* The number of slots in the audio frame is equal to NBSLOT[3:0] + 1*/
	stm32_sai_sub_reg_up(sai, STM_SAI_SLOTR_REGX,
			     SAI_XSLOTR_NBSLOT_MASK,
			     SAI_XSLOTR_NBSLOT_SET((sai->slots - 1)));

	/* Set default slots mask if not already set from DT */
	if (!(slotr & SAI_XSLOTR_SLOTEN_MASK)) {
		sai->slot_mask = (1 << sai->slots) - 1;
		stm32_sai_sub_reg_up(sai,
				     STM_SAI_SLOTR_REGX, SAI_XSLOTR_SLOTEN_MASK,
				     SAI_XSLOTR_SLOTEN_SET(sai->slot_mask));
	}

	dev_dbg(cpu_dai->dev, "Slots %d, slot width %d\n",
		sai->slots, sai->slot_width);

	return 0;
}

static void stm32_sai_set_frame(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int fs_active, offset, format;
	int frcr, frcr_mask;

	format = sai->fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	sai->fs_length = sai->slot_width * sai->slots;

	fs_active = sai->fs_length / 2;
	if ((format == SND_SOC_DAIFMT_DSP_A) ||
	    (format == SND_SOC_DAIFMT_DSP_B))
		fs_active = 1;

	frcr = SAI_XFRCR_FRL_SET((sai->fs_length - 1));
	frcr |= SAI_XFRCR_FSALL_SET((fs_active - 1));
	frcr_mask = SAI_XFRCR_FRL_MASK | SAI_XFRCR_FSALL_MASK;

	dev_dbg(cpu_dai->dev, "Frame length %d, frame active %d\n",
		sai->fs_length, fs_active);

	stm32_sai_sub_reg_up(sai, STM_SAI_FRCR_REGX, frcr_mask, frcr);

	if ((sai->fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_LSB) {
		offset = sai->slot_width - sai->data_size;

		stm32_sai_sub_reg_up(sai, STM_SAI_SLOTR_REGX,
				     SAI_XSLOTR_FBOFF_MASK,
				     SAI_XSLOTR_FBOFF_SET(offset));
	}
}

static void stm32_sai_init_iec958_status(struct stm32_sai_sub_data *sai)
{
	unsigned char *cs = sai->iec958.status;

	cs[0] = IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS_NONE;
	cs[1] = IEC958_AES1_CON_GENERAL;
	cs[2] = IEC958_AES2_CON_SOURCE_UNSPEC | IEC958_AES2_CON_CHANNEL_UNSPEC;
	cs[3] = IEC958_AES3_CON_CLOCK_1000PPM | IEC958_AES3_CON_FS_NOTID;
}

static void stm32_sai_set_iec958_status(struct stm32_sai_sub_data *sai,
					struct snd_pcm_runtime *runtime)
{
	if (!runtime)
		return;

	/* Force the sample rate according to runtime rate */
	mutex_lock(&sai->ctrl_lock);
	switch (runtime->rate) {
	case 22050:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_22050;
		break;
	case 44100:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_44100;
		break;
	case 88200:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_88200;
		break;
	case 176400:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_176400;
		break;
	case 24000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_24000;
		break;
	case 48000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_48000;
		break;
	case 96000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_96000;
		break;
	case 192000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_192000;
		break;
	case 32000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_32000;
		break;
	default:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_NOTID;
		break;
	}
	mutex_unlock(&sai->ctrl_lock);
}

static int stm32_sai_configure_clock(struct snd_soc_dai *cpu_dai,
				     struct snd_pcm_hw_params *params)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int div = 0, cr1 = 0;
	int sai_clk_rate, mclk_ratio, den;
	unsigned int rate = params_rate(params);
	int ret;

	if (!sai->sai_mclk) {
		ret = sai->set_sai_ck_rate(sai, rate);
		if (ret)
			return ret;
	}
	sai_clk_rate = clk_get_rate(sai->sai_ck);

	if (STM_SAI_IS_F4(sai->pdata)) {
		/* mclk on (NODIV=0)
		 *   mclk_rate = 256 * fs
		 *   MCKDIV = 0 if sai_ck < 3/2 * mclk_rate
		 *   MCKDIV = sai_ck / (2 * mclk_rate) otherwise
		 * mclk off (NODIV=1)
		 *   MCKDIV ignored. sck = sai_ck
		 */
		if (!sai->mclk_rate)
			return 0;

		if (2 * sai_clk_rate >= 3 * sai->mclk_rate) {
			div = stm32_sai_get_clk_div(sai, sai_clk_rate,
						    2 * sai->mclk_rate);
			if (div < 0)
				return div;
		}
	} else {
		/*
		 * TDM mode :
		 *   mclk on
		 *      MCKDIV = sai_ck / (ws x 256)	(NOMCK=0. OSR=0)
		 *      MCKDIV = sai_ck / (ws x 512)	(NOMCK=0. OSR=1)
		 *   mclk off
		 *      MCKDIV = sai_ck / (frl x ws)	(NOMCK=1)
		 * Note: NOMCK/NODIV correspond to same bit.
		 */
		if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
			div = stm32_sai_get_clk_div(sai, sai_clk_rate,
						    rate * 128);
			if (div < 0)
				return div;
		} else {
			if (sai->mclk_rate) {
				mclk_ratio = sai->mclk_rate / rate;
				if (mclk_ratio == 512) {
					cr1 = SAI_XCR1_OSR;
				} else if (mclk_ratio != 256) {
					dev_err(cpu_dai->dev,
						"Wrong mclk ratio %d\n",
						mclk_ratio);
					return -EINVAL;
				}

				stm32_sai_sub_reg_up(sai,
						     STM_SAI_CR1_REGX,
						     SAI_XCR1_OSR, cr1);

				div = stm32_sai_get_clk_div(sai, sai_clk_rate,
							    sai->mclk_rate);
				if (div < 0)
					return div;
			} else {
				/* mclk-fs not set, master clock not active */
				den = sai->fs_length * params_rate(params);
				div = stm32_sai_get_clk_div(sai, sai_clk_rate,
							    den);
				if (div < 0)
					return div;
			}
		}
	}

	return stm32_sai_set_clk_div(sai, div);
}

static int stm32_sai_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	sai->data_size = params_width(params);

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		/* Rate not already set in runtime structure */
		substream->runtime->rate = params_rate(params);
		stm32_sai_set_iec958_status(sai, substream->runtime);
	} else {
		ret = stm32_sai_set_slots(cpu_dai);
		if (ret < 0)
			return ret;
		stm32_sai_set_frame(cpu_dai);
	}

	ret = stm32_sai_set_config(cpu_dai, substream, params);
	if (ret)
		return ret;

	if (sai->master)
		ret = stm32_sai_configure_clock(cpu_dai, params);

	return ret;
}

static int stm32_sai_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(cpu_dai->dev, "Enable DMA and SAI\n");

		stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
				     SAI_XCR1_DMAEN, SAI_XCR1_DMAEN);

		/* Enable SAI */
		ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
					   SAI_XCR1_SAIEN, SAI_XCR1_SAIEN);
		if (ret < 0)
			dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(cpu_dai->dev, "Disable DMA and SAI\n");

		stm32_sai_sub_reg_up(sai, STM_SAI_IMR_REGX,
				     SAI_XIMR_MASK, 0);

		stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
				     SAI_XCR1_SAIEN,
				     (unsigned int)~SAI_XCR1_SAIEN);

		ret = stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX,
					   SAI_XCR1_DMAEN,
					   (unsigned int)~SAI_XCR1_DMAEN);
		if (ret < 0)
			dev_err(cpu_dai->dev, "Failed to update CR1 register\n");

		if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
			sai->spdif_frm_cnt = 0;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void stm32_sai_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;

	stm32_sai_sub_reg_up(sai, STM_SAI_IMR_REGX, SAI_XIMR_MASK, 0);

	clk_disable_unprepare(sai->sai_ck);

	/*
	 * Release kernel clock if following conditions are fulfilled
	 * - Master clock is not used. Kernel clock won't be released trough sysclk
	 * - Put handler is defined. Involve that clock is managed exclusively
	 */
	if (!sai->sai_mclk && sai->put_sai_ck_rate)
		sai->put_sai_ck_rate(sai);

	spin_lock_irqsave(&sai->irq_lock, flags);
	sai->substream = NULL;
	spin_unlock_irqrestore(&sai->irq_lock, flags);
}

static int stm32_sai_pcm_new(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	struct snd_kcontrol_new knew = iec958_ctls;

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		dev_dbg(&sai->pdev->dev, "%s: register iec controls", __func__);
		knew.device = rtd->pcm->device;
		return snd_ctl_add(rtd->pcm->card, snd_ctl_new1(&knew, sai));
	}

	return 0;
}

static int stm32_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	int cr1 = 0, cr1_mask, ret;

	sai->cpu_dai = cpu_dai;

	sai->dma_params.addr = (dma_addr_t)(sai->phys_addr + STM_SAI_DR_REGX);
	/*
	 * DMA supports 4, 8 or 16 burst sizes. Burst size 4 is the best choice,
	 * as it allows bytes, half-word and words transfers. (See DMA fifos
	 * constraints).
	 */
	sai->dma_params.maxburst = 4;
	if (sai->pdata->conf.fifo_size < 8 || sai->pdata->conf.no_dma_burst)
		sai->dma_params.maxburst = 1;
	/* Buswidth will be set by framework at runtime */
	sai->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;

	if (STM_SAI_IS_PLAYBACK(sai))
		snd_soc_dai_init_dma_data(cpu_dai, &sai->dma_params, NULL);
	else
		snd_soc_dai_init_dma_data(cpu_dai, NULL, &sai->dma_params);

	/* Next settings are not relevant for spdif mode */
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
		return 0;

	cr1_mask = SAI_XCR1_RX_TX;
	if (STM_SAI_IS_CAPTURE(sai))
		cr1 |= SAI_XCR1_RX_TX;

	/* Configure synchronization */
	if (sai->sync == SAI_SYNC_EXTERNAL) {
		/* Configure synchro client and provider */
		ret = sai->pdata->set_sync(sai->pdata, sai->np_sync_provider,
					   sai->synco, sai->synci);
		if (ret)
			return ret;
	}

	cr1_mask |= SAI_XCR1_SYNCEN_MASK;
	cr1 |= SAI_XCR1_SYNCEN_SET(sai->sync);

	return stm32_sai_sub_reg_up(sai, STM_SAI_CR1_REGX, cr1_mask, cr1);
}

static const struct snd_soc_dai_ops stm32_sai_pcm_dai_ops = {
	.probe		= stm32_sai_dai_probe,
	.set_sysclk	= stm32_sai_set_sysclk,
	.set_fmt	= stm32_sai_set_dai_fmt,
	.set_tdm_slot	= stm32_sai_set_dai_tdm_slot,
	.startup	= stm32_sai_startup,
	.hw_params	= stm32_sai_hw_params,
	.trigger	= stm32_sai_trigger,
	.shutdown	= stm32_sai_shutdown,
	.pcm_new	= stm32_sai_pcm_new,
};

static const struct snd_soc_dai_ops stm32_sai_pcm_dai_ops2 = {
	.probe		= stm32_sai_dai_probe,
	.set_sysclk	= stm32_sai_set_sysclk,
	.set_fmt	= stm32_sai_set_dai_fmt,
	.set_tdm_slot	= stm32_sai_set_dai_tdm_slot,
	.startup	= stm32_sai_startup,
	.hw_params	= stm32_sai_hw_params,
	.trigger	= stm32_sai_trigger,
	.shutdown	= stm32_sai_shutdown,
};

static int stm32_sai_pcm_process_spdif(struct snd_pcm_substream *substream,
				       int channel, unsigned long hwoff,
				       unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	int *ptr = (int *)(runtime->dma_area + hwoff +
			   channel * (runtime->dma_bytes / runtime->channels));
	ssize_t cnt = bytes_to_samples(runtime, bytes);
	unsigned int frm_cnt = sai->spdif_frm_cnt;
	unsigned int byte;
	unsigned int mask;

	do {
		*ptr = ((*ptr >> 8) & 0x00ffffff);

		/* Set channel status bit */
		byte = frm_cnt >> 3;
		mask = 1 << (frm_cnt - (byte << 3));
		if (sai->iec958.status[byte] & mask)
			*ptr |= 0x04000000;
		ptr++;

		if (!(cnt % 2))
			frm_cnt++;

		if (frm_cnt == SAI_IEC60958_BLOCK_FRAMES)
			frm_cnt = 0;
	} while (--cnt);
	sai->spdif_frm_cnt = frm_cnt;

	return 0;
}

/* No support of mmap in S/PDIF mode */
static const struct snd_pcm_hardware stm32_sai_pcm_hw_spdif = {
	.info = SNDRV_PCM_INFO_INTERLEAVED,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024,
	.period_bytes_max = PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
};

static const struct snd_pcm_hardware stm32_sai_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024, /* 5ms at 48kHz */
	.period_bytes_max = PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
};

static struct snd_soc_dai_driver stm32_sai_playback_dai = {
		.id = 1, /* avoid call to fmt_single_name() */
		.playback = {
			.channels_min = 1,
			.channels_max = 16,
			.rate_min = 8000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			/* DMA does not support 24 bits transfers */
			.formats =
				SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &stm32_sai_pcm_dai_ops,
};

static struct snd_soc_dai_driver stm32_sai_capture_dai = {
		.id = 1, /* avoid call to fmt_single_name() */
		.capture = {
			.channels_min = 1,
			.channels_max = 16,
			.rate_min = 8000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			/* DMA does not support 24 bits transfers */
			.formats =
				SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &stm32_sai_pcm_dai_ops2,
};

static const struct snd_dmaengine_pcm_config stm32_sai_pcm_config = {
	.pcm_hardware = &stm32_sai_pcm_hw,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static const struct snd_dmaengine_pcm_config stm32_sai_pcm_config_spdif = {
	.pcm_hardware = &stm32_sai_pcm_hw_spdif,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.process = stm32_sai_pcm_process_spdif,
};

static const struct snd_soc_component_driver stm32_component = {
	.name = "stm32-sai",
	.legacy_dai_naming = 1,
};

static const struct of_device_id stm32_sai_sub_ids[] = {
	{ .compatible = "st,stm32-sai-sub-a",
	  .data = (void *)STM_SAI_A_ID},
	{ .compatible = "st,stm32-sai-sub-b",
	  .data = (void *)STM_SAI_B_ID},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_sai_sub_ids);

static int stm32_sai_sub_parse_of(struct platform_device *pdev,
				  struct stm32_sai_sub_data *sai)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void __iomem *base;
	struct of_phandle_args args;
	int ret;

	if (!np)
		return -ENODEV;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sai->phys_addr = res->start;

	sai->regmap_config = &stm32_sai_sub_regmap_config_f4;
	/* Note: PDM registers not available for sub-block B */
	if (STM_SAI_HAS_PDM(sai) && STM_SAI_IS_SUB_A(sai))
		sai->regmap_config = &stm32_sai_sub_regmap_config_h7;

	/*
	 * Do not manage peripheral clock through regmap framework as this
	 * can lead to circular locking issue with sai master clock provider.
	 * Manage peripheral clock directly in driver instead.
	 */
	sai->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    sai->regmap_config);
	if (IS_ERR(sai->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->regmap),
				     "Regmap init error\n");

	/* Get direction property */
	if (of_property_match_string(np, "dma-names", "tx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_PLAYBACK;
	} else if (of_property_match_string(np, "dma-names", "rx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_CAPTURE;
	} else {
		dev_err(&pdev->dev, "Unsupported direction\n");
		return -EINVAL;
	}

	/* Get spdif iec60958 property */
	sai->spdif = false;
	if (of_property_present(np, "st,iec60958")) {
		if (!STM_SAI_HAS_SPDIF(sai) ||
		    sai->dir == SNDRV_PCM_STREAM_CAPTURE) {
			dev_err(&pdev->dev, "S/PDIF IEC60958 not supported\n");
			return -EINVAL;
		}
		stm32_sai_init_iec958_status(sai);
		sai->spdif = true;
		sai->master = true;
	}

	/* Get synchronization property */
	args.np = NULL;
	ret = of_parse_phandle_with_fixed_args(np, "st,sync", 1, 0, &args);
	if (ret < 0  && ret != -ENOENT) {
		dev_err(&pdev->dev, "Failed to get st,sync property\n");
		return ret;
	}

	sai->sync = SAI_SYNC_NONE;
	if (args.np) {
		if (args.np == np) {
			dev_err(&pdev->dev, "%pOFn sync own reference\n", np);
			of_node_put(args.np);
			return -EINVAL;
		}

		sai->np_sync_provider  = of_get_parent(args.np);
		if (!sai->np_sync_provider) {
			dev_err(&pdev->dev, "%pOFn parent node not found\n",
				np);
			of_node_put(args.np);
			return -ENODEV;
		}

		sai->sync = SAI_SYNC_INTERNAL;
		if (sai->np_sync_provider != sai->pdata->pdev->dev.of_node) {
			if (!STM_SAI_HAS_EXT_SYNC(sai)) {
				dev_err(&pdev->dev,
					"External synchro not supported\n");
				of_node_put(args.np);
				return -EINVAL;
			}
			sai->sync = SAI_SYNC_EXTERNAL;

			sai->synci = args.args[0];
			if (sai->synci < 1 ||
			    (sai->synci > (SAI_GCR_SYNCIN_MAX + 1))) {
				dev_err(&pdev->dev, "Wrong SAI index\n");
				of_node_put(args.np);
				return -EINVAL;
			}

			if (of_property_match_string(args.np, "compatible",
						     "st,stm32-sai-sub-a") >= 0)
				sai->synco = STM_SAI_SYNC_OUT_A;

			if (of_property_match_string(args.np, "compatible",
						     "st,stm32-sai-sub-b") >= 0)
				sai->synco = STM_SAI_SYNC_OUT_B;

			if (!sai->synco) {
				dev_err(&pdev->dev, "Unknown SAI sub-block\n");
				of_node_put(args.np);
				return -EINVAL;
			}
		}

		dev_dbg(&pdev->dev, "%s synchronized with %s\n",
			pdev->name, args.np->full_name);
	}

	of_node_put(args.np);
	sai->sai_ck = devm_clk_get(&pdev->dev, "sai_ck");
	if (IS_ERR(sai->sai_ck))
		return dev_err_probe(&pdev->dev, PTR_ERR(sai->sai_ck),
				     "Missing kernel clock sai_ck\n");

	ret = clk_prepare(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	if (STM_SAI_IS_F4(sai->pdata))
		return 0;

	/* Register mclk provider if requested */
	if (of_property_present(np, "#clock-cells")) {
		ret = stm32_sai_add_mclk_provider(sai);
		if (ret < 0)
			return ret;
	} else {
		sai->sai_mclk = devm_clk_get_optional(&pdev->dev, "MCLK");
		if (IS_ERR(sai->sai_mclk))
			return PTR_ERR(sai->sai_mclk);
	}

	return 0;
}

static int stm32_sai_sub_probe(struct platform_device *pdev)
{
	struct stm32_sai_sub_data *sai;
	const struct snd_dmaengine_pcm_config *conf = &stm32_sai_pcm_config;
	int ret;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	sai->id = (uintptr_t)device_get_match_data(&pdev->dev);

	sai->pdev = pdev;
	mutex_init(&sai->ctrl_lock);
	spin_lock_init(&sai->irq_lock);
	platform_set_drvdata(pdev, sai);

	sai->pdata = dev_get_drvdata(pdev->dev.parent);
	if (!sai->pdata) {
		dev_err(&pdev->dev, "Parent device data not available\n");
		return -EINVAL;
	}

	if (sai->pdata->conf.get_sai_ck_parent) {
		sai->set_sai_ck_rate = stm32_sai_set_parent_clk;
	} else {
		sai->set_sai_ck_rate = stm32_sai_set_parent_rate;
		sai->put_sai_ck_rate = stm32_sai_put_parent_rate;
	}

	ret = stm32_sai_sub_parse_of(pdev, sai);
	if (ret)
		return ret;

	if (STM_SAI_IS_PLAYBACK(sai))
		sai->cpu_dai_drv = stm32_sai_playback_dai;
	else
		sai->cpu_dai_drv = stm32_sai_capture_dai;
	sai->cpu_dai_drv.name = dev_name(&pdev->dev);

	ret = devm_request_irq(&pdev->dev, sai->pdata->irq, stm32_sai_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), sai);
	if (ret) {
		dev_err(&pdev->dev, "IRQ request returned %d\n", ret);
		return ret;
	}

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
		conf = &stm32_sai_pcm_config_spdif;

	ret = snd_dmaengine_pcm_register(&pdev->dev, conf, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Could not register pcm dma\n");

	ret = snd_soc_register_component(&pdev->dev, &stm32_component,
					 &sai->cpu_dai_drv, 1);
	if (ret) {
		snd_dmaengine_pcm_unregister(&pdev->dev);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void stm32_sai_sub_remove(struct platform_device *pdev)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(&pdev->dev);

	clk_unprepare(sai->pdata->pclk);
	snd_dmaengine_pcm_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int stm32_sai_sub_suspend(struct device *dev)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	regcache_cache_only(sai->regmap, true);
	regcache_mark_dirty(sai->regmap);

	clk_disable(sai->pdata->pclk);

	return 0;
}

static int stm32_sai_sub_resume(struct device *dev)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(sai->pdata->pclk);
	if (ret < 0)
		return ret;

	regcache_cache_only(sai->regmap, false);
	ret = regcache_sync(sai->regmap);

	clk_disable(sai->pdata->pclk);

	return ret;
}

static const struct dev_pm_ops stm32_sai_sub_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(stm32_sai_sub_suspend, stm32_sai_sub_resume)
};

static struct platform_driver stm32_sai_sub_driver = {
	.driver = {
		.name = "st,stm32-sai-sub",
		.of_match_table = stm32_sai_sub_ids,
		.pm = pm_ptr(&stm32_sai_sub_pm_ops),
	},
	.probe = stm32_sai_sub_probe,
	.remove = stm32_sai_sub_remove,
};

module_platform_driver(stm32_sai_sub_driver);

MODULE_DESCRIPTION("STM32 Soc SAI sub-block Interface");
MODULE_AUTHOR("Olivier Moysan <olivier.moysan@st.com>");
MODULE_ALIAS("platform:st,stm32-sai-sub");
MODULE_LICENSE("GPL v2");
