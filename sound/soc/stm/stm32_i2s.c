// SPDX-License-Identifier: GPL-2.0-only
/*
 *  STM32 ALSA SoC Digital Audio Interface (I2S) driver.
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#define STM32_I2S_CR1_REG	0x0
#define STM32_I2S_CFG1_REG	0x08
#define STM32_I2S_CFG2_REG	0x0C
#define STM32_I2S_IER_REG	0x10
#define STM32_I2S_SR_REG	0x14
#define STM32_I2S_IFCR_REG	0x18
#define STM32_I2S_TXDR_REG	0X20
#define STM32_I2S_RXDR_REG	0x30
#define STM32_I2S_CGFR_REG	0X50
#define STM32_I2S_HWCFGR_REG	0x3F0
#define STM32_I2S_VERR_REG	0x3F4
#define STM32_I2S_IPIDR_REG	0x3F8
#define STM32_I2S_SIDR_REG	0x3FC

/* Bit definition for SPI2S_CR1 register */
#define I2S_CR1_SPE		BIT(0)
#define I2S_CR1_CSTART		BIT(9)
#define I2S_CR1_CSUSP		BIT(10)
#define I2S_CR1_HDDIR		BIT(11)
#define I2S_CR1_SSI		BIT(12)
#define I2S_CR1_CRC33_17	BIT(13)
#define I2S_CR1_RCRCI		BIT(14)
#define I2S_CR1_TCRCI		BIT(15)

/* Bit definition for SPI_CFG2 register */
#define I2S_CFG2_IOSWP_SHIFT	15
#define I2S_CFG2_IOSWP		BIT(I2S_CFG2_IOSWP_SHIFT)
#define I2S_CFG2_LSBFRST	BIT(23)
#define I2S_CFG2_AFCNTR		BIT(31)

/* Bit definition for SPI_CFG1 register */
#define I2S_CFG1_FTHVL_SHIFT	5
#define I2S_CFG1_FTHVL_MASK	GENMASK(8, I2S_CFG1_FTHVL_SHIFT)
#define I2S_CFG1_FTHVL_SET(x)	((x) << I2S_CFG1_FTHVL_SHIFT)

#define I2S_CFG1_TXDMAEN	BIT(15)
#define I2S_CFG1_RXDMAEN	BIT(14)

/* Bit definition for SPI2S_IER register */
#define I2S_IER_RXPIE		BIT(0)
#define I2S_IER_TXPIE		BIT(1)
#define I2S_IER_DPXPIE		BIT(2)
#define I2S_IER_EOTIE		BIT(3)
#define I2S_IER_TXTFIE		BIT(4)
#define I2S_IER_UDRIE		BIT(5)
#define I2S_IER_OVRIE		BIT(6)
#define I2S_IER_CRCEIE		BIT(7)
#define I2S_IER_TIFREIE		BIT(8)
#define I2S_IER_MODFIE		BIT(9)
#define I2S_IER_TSERFIE		BIT(10)

/* Bit definition for SPI2S_SR register */
#define I2S_SR_RXP		BIT(0)
#define I2S_SR_TXP		BIT(1)
#define I2S_SR_DPXP		BIT(2)
#define I2S_SR_EOT		BIT(3)
#define I2S_SR_TXTF		BIT(4)
#define I2S_SR_UDR		BIT(5)
#define I2S_SR_OVR		BIT(6)
#define I2S_SR_CRCERR		BIT(7)
#define I2S_SR_TIFRE		BIT(8)
#define I2S_SR_MODF		BIT(9)
#define I2S_SR_TSERF		BIT(10)
#define I2S_SR_SUSP		BIT(11)
#define I2S_SR_TXC		BIT(12)
#define I2S_SR_RXPLVL		GENMASK(14, 13)
#define I2S_SR_RXWNE		BIT(15)

#define I2S_SR_MASK		GENMASK(15, 0)

/* Bit definition for SPI_IFCR register */
#define I2S_IFCR_EOTC		BIT(3)
#define I2S_IFCR_TXTFC		BIT(4)
#define I2S_IFCR_UDRC		BIT(5)
#define I2S_IFCR_OVRC		BIT(6)
#define I2S_IFCR_CRCEC		BIT(7)
#define I2S_IFCR_TIFREC		BIT(8)
#define I2S_IFCR_MODFC		BIT(9)
#define I2S_IFCR_TSERFC		BIT(10)
#define I2S_IFCR_SUSPC		BIT(11)

#define I2S_IFCR_MASK		GENMASK(11, 3)

/* Bit definition for SPI_I2SCGFR register */
#define I2S_CGFR_I2SMOD		BIT(0)

#define I2S_CGFR_I2SCFG_SHIFT	1
#define I2S_CGFR_I2SCFG_MASK	GENMASK(3, I2S_CGFR_I2SCFG_SHIFT)
#define I2S_CGFR_I2SCFG_SET(x)	((x) << I2S_CGFR_I2SCFG_SHIFT)

#define I2S_CGFR_I2SSTD_SHIFT	4
#define I2S_CGFR_I2SSTD_MASK	GENMASK(5, I2S_CGFR_I2SSTD_SHIFT)
#define I2S_CGFR_I2SSTD_SET(x)	((x) << I2S_CGFR_I2SSTD_SHIFT)

#define I2S_CGFR_PCMSYNC	BIT(7)

#define I2S_CGFR_DATLEN_SHIFT	8
#define I2S_CGFR_DATLEN_MASK	GENMASK(9, I2S_CGFR_DATLEN_SHIFT)
#define I2S_CGFR_DATLEN_SET(x)	((x) << I2S_CGFR_DATLEN_SHIFT)

#define I2S_CGFR_CHLEN_SHIFT	10
#define I2S_CGFR_CHLEN		BIT(I2S_CGFR_CHLEN_SHIFT)
#define I2S_CGFR_CKPOL		BIT(11)
#define I2S_CGFR_FIXCH		BIT(12)
#define I2S_CGFR_WSINV		BIT(13)
#define I2S_CGFR_DATFMT		BIT(14)

#define I2S_CGFR_I2SDIV_SHIFT	16
#define I2S_CGFR_I2SDIV_BIT_H	23
#define I2S_CGFR_I2SDIV_MASK	GENMASK(I2S_CGFR_I2SDIV_BIT_H,\
				I2S_CGFR_I2SDIV_SHIFT)
#define I2S_CGFR_I2SDIV_SET(x)	((x) << I2S_CGFR_I2SDIV_SHIFT)
#define	I2S_CGFR_I2SDIV_MAX	((1 << (I2S_CGFR_I2SDIV_BIT_H -\
				I2S_CGFR_I2SDIV_SHIFT)) - 1)

#define I2S_CGFR_ODD_SHIFT	24
#define I2S_CGFR_ODD		BIT(I2S_CGFR_ODD_SHIFT)
#define I2S_CGFR_MCKOE		BIT(25)

/* Registers below apply to I2S version 1.1 and more */

/* Bit definition for SPI_HWCFGR register */
#define I2S_HWCFGR_I2S_SUPPORT_MASK	GENMASK(15, 12)

/* Bit definition for SPI_VERR register */
#define I2S_VERR_MIN_MASK	GENMASK(3, 0)
#define I2S_VERR_MAJ_MASK	GENMASK(7, 4)

/* Bit definition for SPI_IPIDR register */
#define I2S_IPIDR_ID_MASK	GENMASK(31, 0)

/* Bit definition for SPI_SIDR register */
#define I2S_SIDR_ID_MASK	GENMASK(31, 0)

#define I2S_IPIDR_NUMBER	0x00130022

enum i2s_master_mode {
	I2S_MS_NOT_SET,
	I2S_MS_MASTER,
	I2S_MS_SLAVE,
};

enum i2s_mode {
	I2S_I2SMOD_TX_SLAVE,
	I2S_I2SMOD_RX_SLAVE,
	I2S_I2SMOD_TX_MASTER,
	I2S_I2SMOD_RX_MASTER,
	I2S_I2SMOD_FD_SLAVE,
	I2S_I2SMOD_FD_MASTER,
};

enum i2s_fifo_th {
	I2S_FIFO_TH_NONE,
	I2S_FIFO_TH_ONE_QUARTER,
	I2S_FIFO_TH_HALF,
	I2S_FIFO_TH_THREE_QUARTER,
	I2S_FIFO_TH_FULL,
};

enum i2s_std {
	I2S_STD_I2S,
	I2S_STD_LEFT_J,
	I2S_STD_RIGHT_J,
	I2S_STD_DSP,
};

enum i2s_datlen {
	I2S_I2SMOD_DATLEN_16,
	I2S_I2SMOD_DATLEN_24,
	I2S_I2SMOD_DATLEN_32,
};

#define STM32_I2S_FIFO_SIZE		16

#define STM32_I2S_IS_MASTER(x)		((x)->ms_flg == I2S_MS_MASTER)
#define STM32_I2S_IS_SLAVE(x)		((x)->ms_flg == I2S_MS_SLAVE)

#define STM32_I2S_NAME_LEN		32
#define STM32_I2S_RATE_11K		11025

/**
 * struct stm32_i2s_data - private data of I2S
 * @regmap_conf: I2S register map configuration pointer
 * @regmap: I2S register map pointer
 * @pdev: device data pointer
 * @dai_drv: DAI driver pointer
 * @dma_data_tx: dma configuration data for tx channel
 * @dma_data_rx: dma configuration data for tx channel
 * @substream: PCM substream data pointer
 * @i2sclk: kernel clock feeding the I2S clock generator
 * @i2smclk: master clock from I2S mclk provider
 * @pclk: peripheral clock driving bus interface
 * @x8kclk: I2S parent clock for sampling frequencies multiple of 8kHz
 * @x11kclk: I2S parent clock for sampling frequencies multiple of 11kHz
 * @base:  mmio register base virtual address
 * @phys_addr: I2S registers physical base address
 * @lock_fd: lock to manage race conditions in full duplex mode
 * @irq_lock: prevent race condition with IRQ
 * @mclk_rate: master clock frequency (Hz)
 * @fmt: DAI protocol
 * @divider: prescaler division ratio
 * @div: prescaler div field
 * @odd: prescaler odd field
 * @refcount: keep count of opened streams on I2S
 * @ms_flg: master mode flag.
 */
struct stm32_i2s_data {
	const struct regmap_config *regmap_conf;
	struct regmap *regmap;
	struct platform_device *pdev;
	struct snd_soc_dai_driver *dai_drv;
	struct snd_dmaengine_dai_dma_data dma_data_tx;
	struct snd_dmaengine_dai_dma_data dma_data_rx;
	struct snd_pcm_substream *substream;
	struct clk *i2sclk;
	struct clk *i2smclk;
	struct clk *pclk;
	struct clk *x8kclk;
	struct clk *x11kclk;
	void __iomem *base;
	dma_addr_t phys_addr;
	spinlock_t lock_fd; /* Manage race conditions for full duplex */
	spinlock_t irq_lock; /* used to prevent race condition with IRQ */
	unsigned int mclk_rate;
	unsigned int fmt;
	unsigned int divider;
	unsigned int div;
	bool odd;
	int refcount;
	int ms_flg;
};

struct stm32_i2smclk_data {
	struct clk_hw hw;
	unsigned long freq;
	struct stm32_i2s_data *i2s_data;
};

#define to_mclk_data(_hw) container_of(_hw, struct stm32_i2smclk_data, hw)

static int stm32_i2s_calc_clk_div(struct stm32_i2s_data *i2s,
				  unsigned long input_rate,
				  unsigned long output_rate)
{
	unsigned int ratio, div, divider = 1;
	bool odd;

	ratio = DIV_ROUND_CLOSEST(input_rate, output_rate);

	/* Check the parity of the divider */
	odd = ratio & 0x1;

	/* Compute the div prescaler */
	div = ratio >> 1;

	/* If div is 0 actual divider is 1 */
	if (div) {
		divider = ((2 * div) + odd);
		dev_dbg(&i2s->pdev->dev, "Divider: 2*%d(div)+%d(odd) = %d\n",
			div, odd, divider);
	}

	/* Division by three is not allowed by I2S prescaler */
	if ((div == 1 && odd) || div > I2S_CGFR_I2SDIV_MAX) {
		dev_err(&i2s->pdev->dev, "Wrong divider setting\n");
		return -EINVAL;
	}

	if (input_rate % divider)
		dev_dbg(&i2s->pdev->dev,
			"Rate not accurate. requested (%ld), actual (%ld)\n",
			output_rate, input_rate / divider);

	i2s->div = div;
	i2s->odd = odd;
	i2s->divider = divider;

	return 0;
}

static int stm32_i2s_set_clk_div(struct stm32_i2s_data *i2s)
{
	u32 cgfr, cgfr_mask;

	cgfr = I2S_CGFR_I2SDIV_SET(i2s->div) | (i2s->odd << I2S_CGFR_ODD_SHIFT);
	cgfr_mask = I2S_CGFR_I2SDIV_MASK | I2S_CGFR_ODD;

	return regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
				  cgfr_mask, cgfr);
}

static int stm32_i2s_set_parent_clock(struct stm32_i2s_data *i2s,
				      unsigned int rate)
{
	struct platform_device *pdev = i2s->pdev;
	struct clk *parent_clk;
	int ret;

	if (!(rate % STM32_I2S_RATE_11K))
		parent_clk = i2s->x11kclk;
	else
		parent_clk = i2s->x8kclk;

	ret = clk_set_parent(i2s->i2sclk, parent_clk);
	if (ret)
		dev_err(&pdev->dev,
			"Error %d setting i2sclk parent clock\n", ret);

	return ret;
}

static long stm32_i2smclk_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	struct stm32_i2smclk_data *mclk = to_mclk_data(hw);
	struct stm32_i2s_data *i2s = mclk->i2s_data;
	int ret;

	ret = stm32_i2s_calc_clk_div(i2s, *prate, rate);
	if (ret)
		return ret;

	mclk->freq = *prate / i2s->divider;

	return mclk->freq;
}

static unsigned long stm32_i2smclk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct stm32_i2smclk_data *mclk = to_mclk_data(hw);

	return mclk->freq;
}

static int stm32_i2smclk_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct stm32_i2smclk_data *mclk = to_mclk_data(hw);
	struct stm32_i2s_data *i2s = mclk->i2s_data;
	int ret;

	ret = stm32_i2s_calc_clk_div(i2s, parent_rate, rate);
	if (ret)
		return ret;

	ret = stm32_i2s_set_clk_div(i2s);
	if (ret)
		return ret;

	mclk->freq = rate;

	return 0;
}

static int stm32_i2smclk_enable(struct clk_hw *hw)
{
	struct stm32_i2smclk_data *mclk = to_mclk_data(hw);
	struct stm32_i2s_data *i2s = mclk->i2s_data;

	dev_dbg(&i2s->pdev->dev, "Enable master clock\n");

	return regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
				    I2S_CGFR_MCKOE, I2S_CGFR_MCKOE);
}

static void stm32_i2smclk_disable(struct clk_hw *hw)
{
	struct stm32_i2smclk_data *mclk = to_mclk_data(hw);
	struct stm32_i2s_data *i2s = mclk->i2s_data;

	dev_dbg(&i2s->pdev->dev, "Disable master clock\n");

	regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG, I2S_CGFR_MCKOE, 0);
}

static const struct clk_ops mclk_ops = {
	.enable = stm32_i2smclk_enable,
	.disable = stm32_i2smclk_disable,
	.recalc_rate = stm32_i2smclk_recalc_rate,
	.round_rate = stm32_i2smclk_round_rate,
	.set_rate = stm32_i2smclk_set_rate,
};

static int stm32_i2s_add_mclk_provider(struct stm32_i2s_data *i2s)
{
	struct clk_hw *hw;
	struct stm32_i2smclk_data *mclk;
	struct device *dev = &i2s->pdev->dev;
	const char *pname = __clk_get_name(i2s->i2sclk);
	char *mclk_name, *p, *s = (char *)pname;
	int ret, i = 0;

	mclk = devm_kzalloc(dev, sizeof(*mclk), GFP_KERNEL);
	if (!mclk)
		return -ENOMEM;

	mclk_name = devm_kcalloc(dev, sizeof(char),
				 STM32_I2S_NAME_LEN, GFP_KERNEL);
	if (!mclk_name)
		return -ENOMEM;

	/*
	 * Forge mclk clock name from parent clock name and suffix.
	 * String after "_" char is stripped in parent name.
	 */
	p = mclk_name;
	while (*s && *s != '_' && (i < (STM32_I2S_NAME_LEN - 7))) {
		*p++ = *s++;
		i++;
	}
	strcat(p, "_mclk");

	mclk->hw.init = CLK_HW_INIT(mclk_name, pname, &mclk_ops, 0);
	mclk->i2s_data = i2s;
	hw = &mclk->hw;

	dev_dbg(dev, "Register master clock %s\n", mclk_name);
	ret = devm_clk_hw_register(&i2s->pdev->dev, hw);
	if (ret) {
		dev_err(dev, "mclk register fails with error %d\n", ret);
		return ret;
	}
	i2s->i2smclk = hw->clk;

	/* register mclk provider */
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static irqreturn_t stm32_i2s_isr(int irq, void *devid)
{
	struct stm32_i2s_data *i2s = (struct stm32_i2s_data *)devid;
	struct platform_device *pdev = i2s->pdev;
	u32 sr, ier;
	unsigned long flags;
	int err = 0;

	regmap_read(i2s->regmap, STM32_I2S_SR_REG, &sr);
	regmap_read(i2s->regmap, STM32_I2S_IER_REG, &ier);

	flags = sr & ier;
	if (!flags) {
		dev_dbg(&pdev->dev, "Spurious IRQ sr=0x%08x, ier=0x%08x\n",
			sr, ier);
		return IRQ_NONE;
	}

	regmap_write_bits(i2s->regmap, STM32_I2S_IFCR_REG,
			  I2S_IFCR_MASK, flags);

	if (flags & I2S_SR_OVR) {
		dev_dbg(&pdev->dev, "Overrun\n");
		err = 1;
	}

	if (flags & I2S_SR_UDR) {
		dev_dbg(&pdev->dev, "Underrun\n");
		err = 1;
	}

	if (flags & I2S_SR_TIFRE)
		dev_dbg(&pdev->dev, "Frame error\n");

	spin_lock(&i2s->irq_lock);
	if (err && i2s->substream)
		snd_pcm_stop_xrun(i2s->substream);
	spin_unlock(&i2s->irq_lock);

	return IRQ_HANDLED;
}

static bool stm32_i2s_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_I2S_CR1_REG:
	case STM32_I2S_CFG1_REG:
	case STM32_I2S_CFG2_REG:
	case STM32_I2S_IER_REG:
	case STM32_I2S_SR_REG:
	case STM32_I2S_RXDR_REG:
	case STM32_I2S_CGFR_REG:
	case STM32_I2S_HWCFGR_REG:
	case STM32_I2S_VERR_REG:
	case STM32_I2S_IPIDR_REG:
	case STM32_I2S_SIDR_REG:
		return true;
	default:
		return false;
	}
}

static bool stm32_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_I2S_SR_REG:
	case STM32_I2S_RXDR_REG:
		return true;
	default:
		return false;
	}
}

static bool stm32_i2s_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM32_I2S_CR1_REG:
	case STM32_I2S_CFG1_REG:
	case STM32_I2S_CFG2_REG:
	case STM32_I2S_IER_REG:
	case STM32_I2S_IFCR_REG:
	case STM32_I2S_TXDR_REG:
	case STM32_I2S_CGFR_REG:
		return true;
	default:
		return false;
	}
}

static int stm32_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	u32 cgfr;
	u32 cgfr_mask =  I2S_CGFR_I2SSTD_MASK | I2S_CGFR_CKPOL |
			 I2S_CGFR_WSINV | I2S_CGFR_I2SCFG_MASK;

	dev_dbg(cpu_dai->dev, "fmt %x\n", fmt);

	/*
	 * winv = 0 : default behavior (high/low) for all standards
	 * ckpol = 0 for all standards.
	 */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cgfr = I2S_CGFR_I2SSTD_SET(I2S_STD_I2S);
		break;
	case SND_SOC_DAIFMT_MSB:
		cgfr = I2S_CGFR_I2SSTD_SET(I2S_STD_LEFT_J);
		break;
	case SND_SOC_DAIFMT_LSB:
		cgfr = I2S_CGFR_I2SSTD_SET(I2S_STD_RIGHT_J);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		cgfr = I2S_CGFR_I2SSTD_SET(I2S_STD_DSP);
		break;
	/* DSP_B not mapped on I2S PCM long format. 1 bit offset does not fit */
	default:
		dev_err(cpu_dai->dev, "Unsupported protocol %#x\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	/* DAI clock strobing */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		cgfr |= I2S_CGFR_CKPOL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		cgfr |= I2S_CGFR_WSINV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		cgfr |= I2S_CGFR_CKPOL;
		cgfr |= I2S_CGFR_WSINV;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported strobing %#x\n",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		i2s->ms_flg = I2S_MS_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s->ms_flg = I2S_MS_MASTER;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported mode %#x\n",
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	i2s->fmt = fmt;
	return regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
				  cgfr_mask, cgfr);
}

static int stm32_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	dev_dbg(cpu_dai->dev, "I2S MCLK frequency is %uHz. mode: %s, dir: %s\n",
		freq, STM32_I2S_IS_MASTER(i2s) ? "master" : "slave",
		dir ? "output" : "input");

	/* MCLK generation is available only in master mode */
	if (dir == SND_SOC_CLOCK_OUT && STM32_I2S_IS_MASTER(i2s)) {
		if (!i2s->i2smclk) {
			dev_dbg(cpu_dai->dev, "No MCLK registered\n");
			return 0;
		}

		/* Assume shutdown if requested frequency is 0Hz */
		if (!freq) {
			/* Release mclk rate only if rate was actually set */
			if (i2s->mclk_rate) {
				clk_rate_exclusive_put(i2s->i2smclk);
				i2s->mclk_rate = 0;
			}
			return regmap_update_bits(i2s->regmap,
						  STM32_I2S_CGFR_REG,
						  I2S_CGFR_MCKOE, 0);
		}
		/* If master clock is used, set parent clock now */
		ret = stm32_i2s_set_parent_clock(i2s, freq);
		if (ret)
			return ret;
		ret = clk_set_rate_exclusive(i2s->i2smclk, freq);
		if (ret) {
			dev_err(cpu_dai->dev, "Could not set mclk rate\n");
			return ret;
		}
		ret = regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
					 I2S_CGFR_MCKOE, I2S_CGFR_MCKOE);
		if (!ret)
			i2s->mclk_rate = freq;
	}

	return ret;
}

static int stm32_i2s_configure_clock(struct snd_soc_dai *cpu_dai,
				     struct snd_pcm_hw_params *params)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long i2s_clock_rate;
	unsigned int nb_bits, frame_len;
	unsigned int rate = params_rate(params);
	u32 cgfr;
	int ret;

	if (!(rate % 11025))
		clk_set_parent(i2s->i2sclk, i2s->x11kclk);
	else
		clk_set_parent(i2s->i2sclk, i2s->x8kclk);
	i2s_clock_rate = clk_get_rate(i2s->i2sclk);

	/*
	 * mckl = mclk_ratio x ws
	 *   i2s mode : mclk_ratio = 256
	 *   dsp mode : mclk_ratio = 128
	 *
	 * mclk on
	 *   i2s mode : div = i2s_clk / (mclk_ratio * ws)
	 *   dsp mode : div = i2s_clk / (mclk_ratio * ws)
	 * mclk off
	 *   i2s mode : div = i2s_clk / (nb_bits x ws)
	 *   dsp mode : div = i2s_clk / (nb_bits x ws)
	 */
	if (i2s->mclk_rate) {
		ret = stm32_i2s_calc_clk_div(i2s, i2s_clock_rate,
					     i2s->mclk_rate);
		if (ret)
			return ret;
	} else {
		frame_len = 32;
		if ((i2s->fmt & SND_SOC_DAIFMT_FORMAT_MASK) ==
		    SND_SOC_DAIFMT_DSP_A)
			frame_len = 16;

		/* master clock not enabled */
		ret = regmap_read(i2s->regmap, STM32_I2S_CGFR_REG, &cgfr);
		if (ret < 0)
			return ret;

		nb_bits = frame_len * (FIELD_GET(I2S_CGFR_CHLEN, cgfr) + 1);
		ret = stm32_i2s_calc_clk_div(i2s, i2s_clock_rate,
					     (nb_bits * rate));
		if (ret)
			return ret;
	}

	ret = stm32_i2s_set_clk_div(i2s);
	if (ret < 0)
		return ret;

	/* Set bitclock and frameclock to their inactive state */
	return regmap_update_bits(i2s->regmap, STM32_I2S_CFG2_REG,
				  I2S_CFG2_AFCNTR, I2S_CFG2_AFCNTR);
}

static int stm32_i2s_configure(struct snd_soc_dai *cpu_dai,
			       struct snd_pcm_hw_params *params,
			       struct snd_pcm_substream *substream)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	int format = params_width(params);
	u32 cfgr, cfgr_mask, cfg1;
	unsigned int fthlv;
	int ret;

	switch (format) {
	case 16:
		cfgr = I2S_CGFR_DATLEN_SET(I2S_I2SMOD_DATLEN_16);
		cfgr_mask = I2S_CGFR_DATLEN_MASK | I2S_CGFR_CHLEN;
		break;
	case 32:
		cfgr = I2S_CGFR_DATLEN_SET(I2S_I2SMOD_DATLEN_32) |
					   I2S_CGFR_CHLEN;
		cfgr_mask = I2S_CGFR_DATLEN_MASK | I2S_CGFR_CHLEN;
		break;
	default:
		dev_err(cpu_dai->dev, "Unexpected format %d", format);
		return -EINVAL;
	}

	if (STM32_I2S_IS_SLAVE(i2s)) {
		cfgr |= I2S_CGFR_I2SCFG_SET(I2S_I2SMOD_FD_SLAVE);

		/* As data length is either 16 or 32 bits, fixch always set */
		cfgr |= I2S_CGFR_FIXCH;
		cfgr_mask |= I2S_CGFR_FIXCH;
	} else {
		cfgr |= I2S_CGFR_I2SCFG_SET(I2S_I2SMOD_FD_MASTER);
	}
	cfgr_mask |= I2S_CGFR_I2SCFG_MASK;

	ret = regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
				 cfgr_mask, cfgr);
	if (ret < 0)
		return ret;

	fthlv = STM32_I2S_FIFO_SIZE * I2S_FIFO_TH_ONE_QUARTER / 4;
	cfg1 = I2S_CFG1_FTHVL_SET(fthlv - 1);

	return regmap_update_bits(i2s->regmap, STM32_I2S_CFG1_REG,
				  I2S_CFG1_FTHVL_MASK, cfg1);
}

static int stm32_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&i2s->irq_lock, flags);
	i2s->substream = substream;
	spin_unlock_irqrestore(&i2s->irq_lock, flags);

	if ((i2s->fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_DSP_A)
		snd_pcm_hw_constraint_single(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS, 2);

	ret = clk_prepare_enable(i2s->i2sclk);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to enable clock: %d\n", ret);
		return ret;
	}

	return regmap_write_bits(i2s->regmap, STM32_I2S_IFCR_REG,
				 I2S_IFCR_MASK, I2S_IFCR_MASK);
}

static int stm32_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	ret = stm32_i2s_configure(cpu_dai, params, substream);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Configuration returned error %d\n", ret);
		return ret;
	}

	if (STM32_I2S_IS_MASTER(i2s))
		ret = stm32_i2s_configure_clock(cpu_dai, params);

	return ret;
}

static int stm32_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	bool playback_flg = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u32 cfg1_mask, ier;
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* Enable i2s */
		dev_dbg(cpu_dai->dev, "start I2S %s\n",
			playback_flg ? "playback" : "capture");

		cfg1_mask = I2S_CFG1_RXDMAEN | I2S_CFG1_TXDMAEN;
		regmap_update_bits(i2s->regmap, STM32_I2S_CFG1_REG,
				   cfg1_mask, cfg1_mask);

		ret = regmap_update_bits(i2s->regmap, STM32_I2S_CR1_REG,
					 I2S_CR1_SPE, I2S_CR1_SPE);
		if (ret < 0) {
			dev_err(cpu_dai->dev, "Error %d enabling I2S\n", ret);
			return ret;
		}

		ret = regmap_write_bits(i2s->regmap, STM32_I2S_CR1_REG,
					I2S_CR1_CSTART, I2S_CR1_CSTART);
		if (ret < 0) {
			dev_err(cpu_dai->dev, "Error %d starting I2S\n", ret);
			return ret;
		}

		regmap_write_bits(i2s->regmap, STM32_I2S_IFCR_REG,
				  I2S_IFCR_MASK, I2S_IFCR_MASK);

		spin_lock(&i2s->lock_fd);
		i2s->refcount++;
		if (playback_flg) {
			ier = I2S_IER_UDRIE;
		} else {
			ier = I2S_IER_OVRIE;

			if (STM32_I2S_IS_MASTER(i2s) && i2s->refcount == 1)
				/* dummy write to gate bus clocks */
				regmap_write(i2s->regmap,
					     STM32_I2S_TXDR_REG, 0);
		}
		spin_unlock(&i2s->lock_fd);

		if (STM32_I2S_IS_SLAVE(i2s))
			ier |= I2S_IER_TIFREIE;

		regmap_update_bits(i2s->regmap, STM32_I2S_IER_REG, ier, ier);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev_dbg(cpu_dai->dev, "stop I2S %s\n",
			playback_flg ? "playback" : "capture");

		if (playback_flg)
			regmap_update_bits(i2s->regmap, STM32_I2S_IER_REG,
					   I2S_IER_UDRIE,
					   (unsigned int)~I2S_IER_UDRIE);
		else
			regmap_update_bits(i2s->regmap, STM32_I2S_IER_REG,
					   I2S_IER_OVRIE,
					   (unsigned int)~I2S_IER_OVRIE);

		spin_lock(&i2s->lock_fd);
		i2s->refcount--;
		if (i2s->refcount) {
			spin_unlock(&i2s->lock_fd);
			break;
		}

		ret = regmap_update_bits(i2s->regmap, STM32_I2S_CR1_REG,
					 I2S_CR1_SPE, 0);
		if (ret < 0) {
			dev_err(cpu_dai->dev, "Error %d disabling I2S\n", ret);
			spin_unlock(&i2s->lock_fd);
			return ret;
		}
		spin_unlock(&i2s->lock_fd);

		cfg1_mask = I2S_CFG1_RXDMAEN | I2S_CFG1_TXDMAEN;
		regmap_update_bits(i2s->regmap, STM32_I2S_CFG1_REG,
				   cfg1_mask, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void stm32_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_i2s_data *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned long flags;

	clk_disable_unprepare(i2s->i2sclk);

	spin_lock_irqsave(&i2s->irq_lock, flags);
	i2s->substream = NULL;
	spin_unlock_irqrestore(&i2s->irq_lock, flags);
}

static int stm32_i2s_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct stm32_i2s_data *i2s = dev_get_drvdata(cpu_dai->dev);
	struct snd_dmaengine_dai_dma_data *dma_data_tx = &i2s->dma_data_tx;
	struct snd_dmaengine_dai_dma_data *dma_data_rx = &i2s->dma_data_rx;

	/* Buswidth will be set by framework */
	dma_data_tx->addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	dma_data_tx->addr = (dma_addr_t)(i2s->phys_addr) + STM32_I2S_TXDR_REG;
	dma_data_tx->maxburst = 1;
	dma_data_rx->addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	dma_data_rx->addr = (dma_addr_t)(i2s->phys_addr) + STM32_I2S_RXDR_REG;
	dma_data_rx->maxburst = 1;

	snd_soc_dai_init_dma_data(cpu_dai, dma_data_tx, dma_data_rx);

	return 0;
}

static const struct regmap_config stm32_h7_i2s_regmap_conf = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM32_I2S_SIDR_REG,
	.readable_reg = stm32_i2s_readable_reg,
	.volatile_reg = stm32_i2s_volatile_reg,
	.writeable_reg = stm32_i2s_writeable_reg,
	.num_reg_defaults_raw = STM32_I2S_SIDR_REG / sizeof(u32) + 1,
	.fast_io = true,
	.cache_type = REGCACHE_FLAT,
};

static const struct snd_soc_dai_ops stm32_i2s_pcm_dai_ops = {
	.set_sysclk	= stm32_i2s_set_sysclk,
	.set_fmt	= stm32_i2s_set_dai_fmt,
	.startup	= stm32_i2s_startup,
	.hw_params	= stm32_i2s_hw_params,
	.trigger	= stm32_i2s_trigger,
	.shutdown	= stm32_i2s_shutdown,
};

static const struct snd_pcm_hardware stm32_i2s_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024,
	.period_bytes_max = 4 * PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
};

static const struct snd_dmaengine_pcm_config stm32_i2s_pcm_config = {
	.pcm_hardware	= &stm32_i2s_pcm_hw,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = PAGE_SIZE * 8,
};

static const struct snd_soc_component_driver stm32_i2s_component = {
	.name = "stm32-i2s",
};

static void stm32_i2s_dai_init(struct snd_soc_pcm_stream *stream,
			       char *stream_name)
{
	stream->stream_name = stream_name;
	stream->channels_min = 1;
	stream->channels_max = 2;
	stream->rates = SNDRV_PCM_RATE_8000_192000;
	stream->formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE;
}

static int stm32_i2s_dais_init(struct platform_device *pdev,
			       struct stm32_i2s_data *i2s)
{
	struct snd_soc_dai_driver *dai_ptr;

	dai_ptr = devm_kzalloc(&pdev->dev, sizeof(struct snd_soc_dai_driver),
			       GFP_KERNEL);
	if (!dai_ptr)
		return -ENOMEM;

	dai_ptr->probe = stm32_i2s_dai_probe;
	dai_ptr->ops = &stm32_i2s_pcm_dai_ops;
	dai_ptr->id = 1;
	stm32_i2s_dai_init(&dai_ptr->playback, "playback");
	stm32_i2s_dai_init(&dai_ptr->capture, "capture");
	i2s->dai_drv = dai_ptr;

	return 0;
}

static const struct of_device_id stm32_i2s_ids[] = {
	{
		.compatible = "st,stm32h7-i2s",
		.data = &stm32_h7_i2s_regmap_conf
	},
	{},
};

static int stm32_i2s_parse_dt(struct platform_device *pdev,
			      struct stm32_i2s_data *i2s)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id;
	struct reset_control *rst;
	struct resource *res;
	int irq, ret;

	if (!np)
		return -ENODEV;

	of_id = of_match_device(stm32_i2s_ids, &pdev->dev);
	if (of_id)
		i2s->regmap_conf = (const struct regmap_config *)of_id->data;
	else
		return -EINVAL;

	i2s->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2s->base))
		return PTR_ERR(i2s->base);

	i2s->phys_addr = res->start;

	/* Get clocks */
	i2s->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(i2s->pclk)) {
		if (PTR_ERR(i2s->pclk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get pclk: %ld\n",
				PTR_ERR(i2s->pclk));
		return PTR_ERR(i2s->pclk);
	}

	i2s->i2sclk = devm_clk_get(&pdev->dev, "i2sclk");
	if (IS_ERR(i2s->i2sclk)) {
		if (PTR_ERR(i2s->i2sclk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get i2sclk: %ld\n",
				PTR_ERR(i2s->i2sclk));
		return PTR_ERR(i2s->i2sclk);
	}

	i2s->x8kclk = devm_clk_get(&pdev->dev, "x8k");
	if (IS_ERR(i2s->x8kclk)) {
		if (PTR_ERR(i2s->x8kclk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get x8k parent clock: %ld\n",
				PTR_ERR(i2s->x8kclk));
		return PTR_ERR(i2s->x8kclk);
	}

	i2s->x11kclk = devm_clk_get(&pdev->dev, "x11k");
	if (IS_ERR(i2s->x11kclk)) {
		if (PTR_ERR(i2s->x11kclk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get x11k parent clock: %ld\n",
				PTR_ERR(i2s->x11kclk));
		return PTR_ERR(i2s->x11kclk);
	}

	/* Register mclk provider if requested */
	if (of_find_property(np, "#clock-cells", NULL)) {
		ret = stm32_i2s_add_mclk_provider(i2s);
		if (ret < 0)
			return ret;
	}

	/* Get irqs */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, stm32_i2s_isr, IRQF_ONESHOT,
			       dev_name(&pdev->dev), i2s);
	if (ret) {
		dev_err(&pdev->dev, "irq request returned %d\n", ret);
		return ret;
	}

	/* Reset */
	rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		if (PTR_ERR(rst) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Reset controller error %ld\n",
				PTR_ERR(rst));
		return PTR_ERR(rst);
	}
	reset_control_assert(rst);
	udelay(2);
	reset_control_deassert(rst);

	return 0;
}

static int stm32_i2s_remove(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	return 0;
}

static int stm32_i2s_probe(struct platform_device *pdev)
{
	struct stm32_i2s_data *i2s;
	u32 val;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->pdev = pdev;
	i2s->ms_flg = I2S_MS_NOT_SET;
	spin_lock_init(&i2s->lock_fd);
	spin_lock_init(&i2s->irq_lock);
	platform_set_drvdata(pdev, i2s);

	ret = stm32_i2s_parse_dt(pdev, i2s);
	if (ret)
		return ret;

	ret = stm32_i2s_dais_init(pdev, i2s);
	if (ret)
		return ret;

	i2s->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "pclk",
						i2s->base, i2s->regmap_conf);
	if (IS_ERR(i2s->regmap)) {
		if (PTR_ERR(i2s->regmap) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Regmap init error %ld\n",
				PTR_ERR(i2s->regmap));
		return PTR_ERR(i2s->regmap);
	}

	ret = snd_dmaengine_pcm_register(&pdev->dev, &stm32_i2s_pcm_config, 0);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "PCM DMA register error %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_component(&pdev->dev, &stm32_i2s_component,
					 i2s->dai_drv, 1);
	if (ret) {
		snd_dmaengine_pcm_unregister(&pdev->dev);
		return ret;
	}

	/* Set SPI/I2S in i2s mode */
	ret = regmap_update_bits(i2s->regmap, STM32_I2S_CGFR_REG,
				 I2S_CGFR_I2SMOD, I2S_CGFR_I2SMOD);
	if (ret)
		goto error;

	ret = regmap_read(i2s->regmap, STM32_I2S_IPIDR_REG, &val);
	if (ret)
		goto error;

	if (val == I2S_IPIDR_NUMBER) {
		ret = regmap_read(i2s->regmap, STM32_I2S_HWCFGR_REG, &val);
		if (ret)
			goto error;

		if (!FIELD_GET(I2S_HWCFGR_I2S_SUPPORT_MASK, val)) {
			dev_err(&pdev->dev,
				"Device does not support i2s mode\n");
			ret = -EPERM;
			goto error;
		}

		ret = regmap_read(i2s->regmap, STM32_I2S_VERR_REG, &val);
		if (ret)
			goto error;

		dev_dbg(&pdev->dev, "I2S version: %lu.%lu registered\n",
			FIELD_GET(I2S_VERR_MAJ_MASK, val),
			FIELD_GET(I2S_VERR_MIN_MASK, val));
	}

	return ret;

error:
	stm32_i2s_remove(pdev);

	return ret;
}

MODULE_DEVICE_TABLE(of, stm32_i2s_ids);

#ifdef CONFIG_PM_SLEEP
static int stm32_i2s_suspend(struct device *dev)
{
	struct stm32_i2s_data *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);
	regcache_mark_dirty(i2s->regmap);

	return 0;
}

static int stm32_i2s_resume(struct device *dev)
{
	struct stm32_i2s_data *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, false);
	return regcache_sync(i2s->regmap);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops stm32_i2s_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_i2s_suspend, stm32_i2s_resume)
};

static struct platform_driver stm32_i2s_driver = {
	.driver = {
		.name = "st,stm32-i2s",
		.of_match_table = stm32_i2s_ids,
		.pm = &stm32_i2s_pm_ops,
	},
	.probe = stm32_i2s_probe,
	.remove = stm32_i2s_remove,
};

module_platform_driver(stm32_i2s_driver);

MODULE_DESCRIPTION("STM32 Soc i2s Interface");
MODULE_AUTHOR("Olivier Moysan, <olivier.moysan@st.com>");
MODULE_ALIAS("platform:stm32-i2s");
MODULE_LICENSE("GPL v2");
