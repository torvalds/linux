// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Spreadtrum Communications Inc.

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "sprd-mcdt.h"

/* MCDT registers definition */
#define MCDT_CH0_TXD		0x0
#define MCDT_CH0_RXD		0x28
#define MCDT_DAC0_WTMK		0x60
#define MCDT_ADC0_WTMK		0x88
#define MCDT_DMA_EN		0xb0

#define MCDT_INT_EN0		0xb4
#define MCDT_INT_EN1		0xb8
#define MCDT_INT_EN2		0xbc

#define MCDT_INT_CLR0		0xc0
#define MCDT_INT_CLR1		0xc4
#define MCDT_INT_CLR2		0xc8

#define MCDT_INT_RAW1		0xcc
#define MCDT_INT_RAW2		0xd0
#define MCDT_INT_RAW3		0xd4

#define MCDT_INT_MSK1		0xd8
#define MCDT_INT_MSK2		0xdc
#define MCDT_INT_MSK3		0xe0

#define MCDT_DAC0_FIFO_ADDR_ST	0xe4
#define MCDT_ADC0_FIFO_ADDR_ST	0xe8

#define MCDT_CH_FIFO_ST0	0x134
#define MCDT_CH_FIFO_ST1	0x138
#define MCDT_CH_FIFO_ST2	0x13c

#define MCDT_INT_MSK_CFG0	0x140
#define MCDT_INT_MSK_CFG1	0x144

#define MCDT_DMA_CFG0		0x148
#define MCDT_FIFO_CLR		0x14c
#define MCDT_DMA_CFG1		0x150
#define MCDT_DMA_CFG2		0x154
#define MCDT_DMA_CFG3		0x158
#define MCDT_DMA_CFG4		0x15c
#define MCDT_DMA_CFG5		0x160

/* Channel water mark definition */
#define MCDT_CH_FIFO_AE_SHIFT	16
#define MCDT_CH_FIFO_AE_MASK	GENMASK(24, 16)
#define MCDT_CH_FIFO_AF_MASK	GENMASK(8, 0)

/* DMA channel select definition */
#define MCDT_DMA_CH0_SEL_MASK	GENMASK(3, 0)
#define MCDT_DMA_CH0_SEL_SHIFT	0
#define MCDT_DMA_CH1_SEL_MASK	GENMASK(7, 4)
#define MCDT_DMA_CH1_SEL_SHIFT	4
#define MCDT_DMA_CH2_SEL_MASK	GENMASK(11, 8)
#define MCDT_DMA_CH2_SEL_SHIFT	8
#define MCDT_DMA_CH3_SEL_MASK	GENMASK(15, 12)
#define MCDT_DMA_CH3_SEL_SHIFT	12
#define MCDT_DMA_CH4_SEL_MASK	GENMASK(19, 16)
#define MCDT_DMA_CH4_SEL_SHIFT	16
#define MCDT_DAC_DMA_SHIFT	16

/* DMA channel ACK select definition */
#define MCDT_DMA_ACK_SEL_MASK	GENMASK(3, 0)

/* Channel FIFO definition */
#define MCDT_CH_FIFO_ADDR_SHIFT	16
#define MCDT_CH_FIFO_ADDR_MASK	GENMASK(9, 0)
#define MCDT_ADC_FIFO_SHIFT	16
#define MCDT_FIFO_LENGTH	512

#define MCDT_ADC_CHANNEL_NUM	10
#define MCDT_DAC_CHANNEL_NUM	10
#define MCDT_CHANNEL_NUM	(MCDT_ADC_CHANNEL_NUM + MCDT_DAC_CHANNEL_NUM)

enum sprd_mcdt_fifo_int {
	MCDT_ADC_FIFO_AE_INT,
	MCDT_ADC_FIFO_AF_INT,
	MCDT_DAC_FIFO_AE_INT,
	MCDT_DAC_FIFO_AF_INT,
	MCDT_ADC_FIFO_OV_INT,
	MCDT_DAC_FIFO_OV_INT
};

enum sprd_mcdt_fifo_sts {
	MCDT_ADC_FIFO_REAL_FULL,
	MCDT_ADC_FIFO_REAL_EMPTY,
	MCDT_ADC_FIFO_AF,
	MCDT_ADC_FIFO_AE,
	MCDT_DAC_FIFO_REAL_FULL,
	MCDT_DAC_FIFO_REAL_EMPTY,
	MCDT_DAC_FIFO_AF,
	MCDT_DAC_FIFO_AE
};

struct sprd_mcdt_dev {
	struct device *dev;
	void __iomem *base;
	spinlock_t lock;
	struct sprd_mcdt_chan chan[MCDT_CHANNEL_NUM];
};

static LIST_HEAD(sprd_mcdt_chan_list);
static DEFINE_MUTEX(sprd_mcdt_list_mutex);

static void sprd_mcdt_update(struct sprd_mcdt_dev *mcdt, u32 reg, u32 val,
			     u32 mask)
{
	u32 orig = readl_relaxed(mcdt->base + reg);
	u32 tmp;

	tmp = (orig & ~mask) | val;
	writel_relaxed(tmp, mcdt->base + reg);
}

static void sprd_mcdt_dac_set_watermark(struct sprd_mcdt_dev *mcdt, u8 channel,
					u32 full, u32 empty)
{
	u32 reg = MCDT_DAC0_WTMK + channel * 4;
	u32 water_mark =
		(empty << MCDT_CH_FIFO_AE_SHIFT) & MCDT_CH_FIFO_AE_MASK;

	water_mark |= full & MCDT_CH_FIFO_AF_MASK;
	sprd_mcdt_update(mcdt, reg, water_mark,
			 MCDT_CH_FIFO_AE_MASK | MCDT_CH_FIFO_AF_MASK);
}

static void sprd_mcdt_adc_set_watermark(struct sprd_mcdt_dev *mcdt, u8 channel,
					u32 full, u32 empty)
{
	u32 reg = MCDT_ADC0_WTMK + channel * 4;
	u32 water_mark =
		(empty << MCDT_CH_FIFO_AE_SHIFT) & MCDT_CH_FIFO_AE_MASK;

	water_mark |= full & MCDT_CH_FIFO_AF_MASK;
	sprd_mcdt_update(mcdt, reg, water_mark,
			 MCDT_CH_FIFO_AE_MASK | MCDT_CH_FIFO_AF_MASK);
}

static void sprd_mcdt_dac_dma_enable(struct sprd_mcdt_dev *mcdt, u8 channel,
				     bool enable)
{
	u32 shift = MCDT_DAC_DMA_SHIFT + channel;

	if (enable)
		sprd_mcdt_update(mcdt, MCDT_DMA_EN, BIT(shift), BIT(shift));
	else
		sprd_mcdt_update(mcdt, MCDT_DMA_EN, 0, BIT(shift));
}

static void sprd_mcdt_adc_dma_enable(struct sprd_mcdt_dev *mcdt, u8 channel,
				     bool enable)
{
	if (enable)
		sprd_mcdt_update(mcdt, MCDT_DMA_EN, BIT(channel), BIT(channel));
	else
		sprd_mcdt_update(mcdt, MCDT_DMA_EN, 0, BIT(channel));
}

static void sprd_mcdt_ap_int_enable(struct sprd_mcdt_dev *mcdt, u8 channel,
				    bool enable)
{
	if (enable)
		sprd_mcdt_update(mcdt, MCDT_INT_MSK_CFG0, BIT(channel),
				 BIT(channel));
	else
		sprd_mcdt_update(mcdt, MCDT_INT_MSK_CFG0, 0, BIT(channel));
}

static void sprd_mcdt_dac_write_fifo(struct sprd_mcdt_dev *mcdt, u8 channel,
				     u32 val)
{
	u32 reg = MCDT_CH0_TXD + channel * 4;

	writel_relaxed(val, mcdt->base + reg);
}

static void sprd_mcdt_adc_read_fifo(struct sprd_mcdt_dev *mcdt, u8 channel,
				    u32 *val)
{
	u32 reg = MCDT_CH0_RXD + channel * 4;

	*val = readl_relaxed(mcdt->base + reg);
}

static void sprd_mcdt_dac_dma_chn_select(struct sprd_mcdt_dev *mcdt, u8 channel,
					 enum sprd_mcdt_dma_chan dma_chan)
{
	switch (dma_chan) {
	case SPRD_MCDT_DMA_CH0:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG0,
				 channel << MCDT_DMA_CH0_SEL_SHIFT,
				 MCDT_DMA_CH0_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH1:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG0,
				 channel << MCDT_DMA_CH1_SEL_SHIFT,
				 MCDT_DMA_CH1_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH2:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG0,
				 channel << MCDT_DMA_CH2_SEL_SHIFT,
				 MCDT_DMA_CH2_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH3:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG0,
				 channel << MCDT_DMA_CH3_SEL_SHIFT,
				 MCDT_DMA_CH3_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH4:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG0,
				 channel << MCDT_DMA_CH4_SEL_SHIFT,
				 MCDT_DMA_CH4_SEL_MASK);
		break;
	}
}

static void sprd_mcdt_adc_dma_chn_select(struct sprd_mcdt_dev *mcdt, u8 channel,
					 enum sprd_mcdt_dma_chan dma_chan)
{
	switch (dma_chan) {
	case SPRD_MCDT_DMA_CH0:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG1,
				 channel << MCDT_DMA_CH0_SEL_SHIFT,
				 MCDT_DMA_CH0_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH1:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG1,
				 channel << MCDT_DMA_CH1_SEL_SHIFT,
				 MCDT_DMA_CH1_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH2:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG1,
				 channel << MCDT_DMA_CH2_SEL_SHIFT,
				 MCDT_DMA_CH2_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH3:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG1,
				 channel << MCDT_DMA_CH3_SEL_SHIFT,
				 MCDT_DMA_CH3_SEL_MASK);
		break;

	case SPRD_MCDT_DMA_CH4:
		sprd_mcdt_update(mcdt, MCDT_DMA_CFG1,
				 channel << MCDT_DMA_CH4_SEL_SHIFT,
				 MCDT_DMA_CH4_SEL_MASK);
		break;
	}
}

static u32 sprd_mcdt_dma_ack_shift(u8 channel)
{
	switch (channel) {
	default:
	case 0:
	case 8:
		return 0;
	case 1:
	case 9:
		return 4;
	case 2:
		return 8;
	case 3:
		return 12;
	case 4:
		return 16;
	case 5:
		return 20;
	case 6:
		return 24;
	case 7:
		return 28;
	}
}

static void sprd_mcdt_dac_dma_ack_select(struct sprd_mcdt_dev *mcdt, u8 channel,
					 enum sprd_mcdt_dma_chan dma_chan)
{
	u32 reg, shift = sprd_mcdt_dma_ack_shift(channel), ack = dma_chan;

	switch (channel) {
	case 0 ... 7:
		reg = MCDT_DMA_CFG2;
		break;

	case 8 ... 9:
		reg = MCDT_DMA_CFG3;
		break;

	default:
		return;
	}

	sprd_mcdt_update(mcdt, reg, ack << shift,
			 MCDT_DMA_ACK_SEL_MASK << shift);
}

static void sprd_mcdt_adc_dma_ack_select(struct sprd_mcdt_dev *mcdt, u8 channel,
					 enum sprd_mcdt_dma_chan dma_chan)
{
	u32 reg, shift = sprd_mcdt_dma_ack_shift(channel), ack = dma_chan;

	switch (channel) {
	case 0 ... 7:
		reg = MCDT_DMA_CFG4;
		break;

	case 8 ... 9:
		reg = MCDT_DMA_CFG5;
		break;

	default:
		return;
	}

	sprd_mcdt_update(mcdt, reg, ack << shift,
			 MCDT_DMA_ACK_SEL_MASK << shift);
}

static bool sprd_mcdt_chan_fifo_sts(struct sprd_mcdt_dev *mcdt, u8 channel,
				    enum sprd_mcdt_fifo_sts fifo_sts)
{
	u32 reg, shift;

	switch (channel) {
	case 0 ... 3:
		reg = MCDT_CH_FIFO_ST0;
		break;
	case 4 ... 7:
		reg = MCDT_CH_FIFO_ST1;
		break;
	case 8 ... 9:
		reg = MCDT_CH_FIFO_ST2;
		break;
	default:
		return false;
	}

	switch (channel) {
	case 0:
	case 4:
	case 8:
		shift = fifo_sts;
		break;

	case 1:
	case 5:
	case 9:
		shift = 8 + fifo_sts;
		break;

	case 2:
	case 6:
		shift = 16 + fifo_sts;
		break;

	case 3:
	case 7:
		shift = 24 + fifo_sts;
		break;

	default:
		return false;
	}

	return !!(readl_relaxed(mcdt->base + reg) & BIT(shift));
}

static void sprd_mcdt_dac_fifo_clear(struct sprd_mcdt_dev *mcdt, u8 channel)
{
	sprd_mcdt_update(mcdt, MCDT_FIFO_CLR, BIT(channel), BIT(channel));
}

static void sprd_mcdt_adc_fifo_clear(struct sprd_mcdt_dev *mcdt, u8 channel)
{
	u32 shift = MCDT_ADC_FIFO_SHIFT + channel;

	sprd_mcdt_update(mcdt, MCDT_FIFO_CLR, BIT(shift), BIT(shift));
}

static u32 sprd_mcdt_dac_fifo_avail(struct sprd_mcdt_dev *mcdt, u8 channel)
{
	u32 reg = MCDT_DAC0_FIFO_ADDR_ST + channel * 8;
	u32 r_addr = (readl_relaxed(mcdt->base + reg) >>
		      MCDT_CH_FIFO_ADDR_SHIFT) & MCDT_CH_FIFO_ADDR_MASK;
	u32 w_addr = readl_relaxed(mcdt->base + reg) & MCDT_CH_FIFO_ADDR_MASK;

	if (w_addr >= r_addr)
		return 4 * (MCDT_FIFO_LENGTH - w_addr + r_addr);
	else
		return 4 * (r_addr - w_addr);
}

static u32 sprd_mcdt_adc_fifo_avail(struct sprd_mcdt_dev *mcdt, u8 channel)
{
	u32 reg = MCDT_ADC0_FIFO_ADDR_ST + channel * 8;
	u32 r_addr = (readl_relaxed(mcdt->base + reg) >>
		      MCDT_CH_FIFO_ADDR_SHIFT) & MCDT_CH_FIFO_ADDR_MASK;
	u32 w_addr = readl_relaxed(mcdt->base + reg) & MCDT_CH_FIFO_ADDR_MASK;

	if (w_addr >= r_addr)
		return 4 * (w_addr - r_addr);
	else
		return 4 * (MCDT_FIFO_LENGTH - r_addr + w_addr);
}

static u32 sprd_mcdt_int_type_shift(u8 channel,
				    enum sprd_mcdt_fifo_int int_type)
{
	switch (channel) {
	case 0:
	case 4:
	case 8:
		return int_type;

	case 1:
	case 5:
	case 9:
		return  8 + int_type;

	case 2:
	case 6:
		return 16 + int_type;

	case 3:
	case 7:
		return 24 + int_type;

	default:
		return 0;
	}
}

static void sprd_mcdt_chan_int_en(struct sprd_mcdt_dev *mcdt, u8 channel,
				  enum sprd_mcdt_fifo_int int_type, bool enable)
{
	u32 reg, shift = sprd_mcdt_int_type_shift(channel, int_type);

	switch (channel) {
	case 0 ... 3:
		reg = MCDT_INT_EN0;
		break;
	case 4 ... 7:
		reg = MCDT_INT_EN1;
		break;
	case 8 ... 9:
		reg = MCDT_INT_EN2;
		break;
	default:
		return;
	}

	if (enable)
		sprd_mcdt_update(mcdt, reg, BIT(shift), BIT(shift));
	else
		sprd_mcdt_update(mcdt, reg, 0, BIT(shift));
}

static void sprd_mcdt_chan_int_clear(struct sprd_mcdt_dev *mcdt, u8 channel,
				     enum sprd_mcdt_fifo_int int_type)
{
	u32 reg, shift = sprd_mcdt_int_type_shift(channel, int_type);

	switch (channel) {
	case 0 ... 3:
		reg = MCDT_INT_CLR0;
		break;
	case 4 ... 7:
		reg = MCDT_INT_CLR1;
		break;
	case 8 ... 9:
		reg = MCDT_INT_CLR2;
		break;
	default:
		return;
	}

	sprd_mcdt_update(mcdt, reg, BIT(shift), BIT(shift));
}

static bool sprd_mcdt_chan_int_sts(struct sprd_mcdt_dev *mcdt, u8 channel,
				   enum sprd_mcdt_fifo_int int_type)
{
	u32 reg, shift = sprd_mcdt_int_type_shift(channel, int_type);

	switch (channel) {
	case 0 ... 3:
		reg = MCDT_INT_MSK1;
		break;
	case 4 ... 7:
		reg = MCDT_INT_MSK2;
		break;
	case 8 ... 9:
		reg = MCDT_INT_MSK3;
		break;
	default:
		return false;
	}

	return !!(readl_relaxed(mcdt->base + reg) & BIT(shift));
}

static irqreturn_t sprd_mcdt_irq_handler(int irq, void *dev_id)
{
	struct sprd_mcdt_dev *mcdt = (struct sprd_mcdt_dev *)dev_id;
	int i;

	spin_lock(&mcdt->lock);

	for (i = 0; i < MCDT_ADC_CHANNEL_NUM; i++) {
		if (sprd_mcdt_chan_int_sts(mcdt, i, MCDT_ADC_FIFO_AF_INT)) {
			struct sprd_mcdt_chan *chan = &mcdt->chan[i];

			sprd_mcdt_chan_int_clear(mcdt, i, MCDT_ADC_FIFO_AF_INT);
			if (chan->cb)
				chan->cb->notify(chan->cb->data);
		}
	}

	for (i = 0; i < MCDT_DAC_CHANNEL_NUM; i++) {
		if (sprd_mcdt_chan_int_sts(mcdt, i, MCDT_DAC_FIFO_AE_INT)) {
			struct sprd_mcdt_chan *chan =
				&mcdt->chan[i + MCDT_ADC_CHANNEL_NUM];

			sprd_mcdt_chan_int_clear(mcdt, i, MCDT_DAC_FIFO_AE_INT);
			if (chan->cb)
				chan->cb->notify(chan->cb->data);
		}
	}

	spin_unlock(&mcdt->lock);

	return IRQ_HANDLED;
}

/**
 * sprd_mcdt_chan_write - write data to the MCDT channel's fifo
 * @chan: the MCDT channel
 * @tx_buf: send buffer
 * @size: data size
 *
 * Note: We can not write data to the channel fifo when enabling the DMA mode,
 * otherwise the channel fifo data will be invalid.
 *
 * If there are not enough space of the channel fifo, it will return errors
 * to users.
 *
 * Returns 0 on success, or an appropriate error code on failure.
 */
int sprd_mcdt_chan_write(struct sprd_mcdt_chan *chan, char *tx_buf, u32 size)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;
	int avail, i = 0, words = size / 4;
	u32 *buf = (u32 *)tx_buf;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (chan->dma_enable) {
		dev_err(mcdt->dev,
			"Can not write data when DMA mode enabled\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EINVAL;
	}

	if (sprd_mcdt_chan_fifo_sts(mcdt, chan->id, MCDT_DAC_FIFO_REAL_FULL)) {
		dev_err(mcdt->dev, "Channel fifo is full now\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EBUSY;
	}

	avail = sprd_mcdt_dac_fifo_avail(mcdt, chan->id);
	if (size > avail) {
		dev_err(mcdt->dev,
			"Data size is larger than the available fifo size\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EBUSY;
	}

	while (i++ < words)
		sprd_mcdt_dac_write_fifo(mcdt, chan->id, *buf++);

	spin_unlock_irqrestore(&mcdt->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_write);

/**
 * sprd_mcdt_chan_read - read data from the MCDT channel's fifo
 * @chan: the MCDT channel
 * @rx_buf: receive buffer
 * @size: data size
 *
 * Note: We can not read data from the channel fifo when enabling the DMA mode,
 * otherwise the reading data will be invalid.
 *
 * Usually user need start to read data once receiving the fifo full interrupt.
 *
 * Returns data size of reading successfully, or an error code on failure.
 */
int sprd_mcdt_chan_read(struct sprd_mcdt_chan *chan, char *rx_buf, u32 size)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;
	int i = 0, avail, words = size / 4;
	u32 *buf = (u32 *)rx_buf;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (chan->dma_enable) {
		dev_err(mcdt->dev, "Can not read data when DMA mode enabled\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EINVAL;
	}

	if (sprd_mcdt_chan_fifo_sts(mcdt, chan->id, MCDT_ADC_FIFO_REAL_EMPTY)) {
		dev_err(mcdt->dev, "Channel fifo is empty\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EBUSY;
	}

	avail = sprd_mcdt_adc_fifo_avail(mcdt, chan->id);
	if (size > avail)
		words = avail / 4;

	while (i++ < words)
		sprd_mcdt_adc_read_fifo(mcdt, chan->id, buf++);

	spin_unlock_irqrestore(&mcdt->lock, flags);
	return words * 4;
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_read);

/**
 * sprd_mcdt_chan_int_enable - enable the interrupt mode for the MCDT channel
 * @chan: the MCDT channel
 * @water_mark: water mark to trigger a interrupt
 * @cb: callback when a interrupt happened
 *
 * Now it only can enable fifo almost full interrupt for ADC channel and fifo
 * almost empty interrupt for DAC channel. Morevoer for interrupt mode, user
 * should use sprd_mcdt_chan_read() or sprd_mcdt_chan_write() to read or write
 * data manually.
 *
 * For ADC channel, user can start to read data once receiving one fifo full
 * interrupt. For DAC channel, user can start to write data once receiving one
 * fifo empty interrupt or just call sprd_mcdt_chan_write() to write data
 * directly.
 *
 * Returns 0 on success, or an error code on failure.
 */
int sprd_mcdt_chan_int_enable(struct sprd_mcdt_chan *chan, u32 water_mark,
			      struct sprd_mcdt_chan_callback *cb)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (chan->dma_enable || chan->int_enable) {
		dev_err(mcdt->dev, "Failed to set interrupt mode.\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EINVAL;
	}

	switch (chan->type) {
	case SPRD_MCDT_ADC_CHAN:
		sprd_mcdt_adc_fifo_clear(mcdt, chan->id);
		sprd_mcdt_adc_set_watermark(mcdt, chan->id, water_mark,
					    MCDT_FIFO_LENGTH - 1);
		sprd_mcdt_chan_int_en(mcdt, chan->id,
				      MCDT_ADC_FIFO_AF_INT, true);
		sprd_mcdt_ap_int_enable(mcdt, chan->id, true);
		break;

	case SPRD_MCDT_DAC_CHAN:
		sprd_mcdt_dac_fifo_clear(mcdt, chan->id);
		sprd_mcdt_dac_set_watermark(mcdt, chan->id,
					    MCDT_FIFO_LENGTH - 1, water_mark);
		sprd_mcdt_chan_int_en(mcdt, chan->id,
				      MCDT_DAC_FIFO_AE_INT, true);
		sprd_mcdt_ap_int_enable(mcdt, chan->id, true);
		break;

	default:
		dev_err(mcdt->dev, "Unsupported channel type\n");
		ret = -EINVAL;
	}

	if (!ret) {
		chan->cb = cb;
		chan->int_enable = true;
	}

	spin_unlock_irqrestore(&mcdt->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_int_enable);

/**
 * sprd_mcdt_chan_int_disable - disable the interrupt mode for the MCDT channel
 * @chan: the MCDT channel
 */
void sprd_mcdt_chan_int_disable(struct sprd_mcdt_chan *chan)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (!chan->int_enable) {
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return;
	}

	switch (chan->type) {
	case SPRD_MCDT_ADC_CHAN:
		sprd_mcdt_chan_int_en(mcdt, chan->id,
				      MCDT_ADC_FIFO_AF_INT, false);
		sprd_mcdt_chan_int_clear(mcdt, chan->id, MCDT_ADC_FIFO_AF_INT);
		sprd_mcdt_ap_int_enable(mcdt, chan->id, false);
		break;

	case SPRD_MCDT_DAC_CHAN:
		sprd_mcdt_chan_int_en(mcdt, chan->id,
				      MCDT_DAC_FIFO_AE_INT, false);
		sprd_mcdt_chan_int_clear(mcdt, chan->id, MCDT_DAC_FIFO_AE_INT);
		sprd_mcdt_ap_int_enable(mcdt, chan->id, false);
		break;

	default:
		break;
	}

	chan->int_enable = false;
	spin_unlock_irqrestore(&mcdt->lock, flags);
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_int_disable);

/**
 * sprd_mcdt_chan_dma_enable - enable the DMA mode for the MCDT channel
 * @chan: the MCDT channel
 * @dma_chan: specify which DMA channel will be used for this MCDT channel
 * @water_mark: water mark to trigger a DMA request
 *
 * Enable the DMA mode for the MCDT channel, that means we can use DMA to
 * transfer data to the channel fifo and do not need reading/writing data
 * manually.
 *
 * Returns 0 on success, or an error code on failure.
 */
int sprd_mcdt_chan_dma_enable(struct sprd_mcdt_chan *chan,
			      enum sprd_mcdt_dma_chan dma_chan,
			      u32 water_mark)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (chan->dma_enable || chan->int_enable ||
	    dma_chan > SPRD_MCDT_DMA_CH4) {
		dev_err(mcdt->dev, "Failed to set DMA mode\n");
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return -EINVAL;
	}

	switch (chan->type) {
	case SPRD_MCDT_ADC_CHAN:
		sprd_mcdt_adc_fifo_clear(mcdt, chan->id);
		sprd_mcdt_adc_set_watermark(mcdt, chan->id,
					    water_mark, MCDT_FIFO_LENGTH - 1);
		sprd_mcdt_adc_dma_enable(mcdt, chan->id, true);
		sprd_mcdt_adc_dma_chn_select(mcdt, chan->id, dma_chan);
		sprd_mcdt_adc_dma_ack_select(mcdt, chan->id, dma_chan);
		break;

	case SPRD_MCDT_DAC_CHAN:
		sprd_mcdt_dac_fifo_clear(mcdt, chan->id);
		sprd_mcdt_dac_set_watermark(mcdt, chan->id,
					    MCDT_FIFO_LENGTH - 1, water_mark);
		sprd_mcdt_dac_dma_enable(mcdt, chan->id, true);
		sprd_mcdt_dac_dma_chn_select(mcdt, chan->id, dma_chan);
		sprd_mcdt_dac_dma_ack_select(mcdt, chan->id, dma_chan);
		break;

	default:
		dev_err(mcdt->dev, "Unsupported channel type\n");
		ret = -EINVAL;
	}

	if (!ret)
		chan->dma_enable = true;

	spin_unlock_irqrestore(&mcdt->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_dma_enable);

/**
 * sprd_mcdt_chan_dma_disable - disable the DMA mode for the MCDT channel
 * @chan: the MCDT channel
 */
void sprd_mcdt_chan_dma_disable(struct sprd_mcdt_chan *chan)
{
	struct sprd_mcdt_dev *mcdt = chan->mcdt;
	unsigned long flags;

	spin_lock_irqsave(&mcdt->lock, flags);

	if (!chan->dma_enable) {
		spin_unlock_irqrestore(&mcdt->lock, flags);
		return;
	}

	switch (chan->type) {
	case SPRD_MCDT_ADC_CHAN:
		sprd_mcdt_adc_dma_enable(mcdt, chan->id, false);
		sprd_mcdt_adc_fifo_clear(mcdt, chan->id);
		break;

	case SPRD_MCDT_DAC_CHAN:
		sprd_mcdt_dac_dma_enable(mcdt, chan->id, false);
		sprd_mcdt_dac_fifo_clear(mcdt, chan->id);
		break;

	default:
		break;
	}

	chan->dma_enable = false;
	spin_unlock_irqrestore(&mcdt->lock, flags);
}
EXPORT_SYMBOL_GPL(sprd_mcdt_chan_dma_disable);

/**
 * sprd_mcdt_request_chan - request one MCDT channel
 * @channel: channel id
 * @type: channel type, it can be one ADC channel or DAC channel
 *
 * Rreturn NULL if no available channel.
 */
struct sprd_mcdt_chan *sprd_mcdt_request_chan(u8 channel,
					      enum sprd_mcdt_channel_type type)
{
	struct sprd_mcdt_chan *temp;

	mutex_lock(&sprd_mcdt_list_mutex);

	list_for_each_entry(temp, &sprd_mcdt_chan_list, list) {
		if (temp->type == type && temp->id == channel) {
			list_del_init(&temp->list);
			break;
		}
	}

	if (list_entry_is_head(temp, &sprd_mcdt_chan_list, list))
		temp = NULL;

	mutex_unlock(&sprd_mcdt_list_mutex);

	return temp;
}
EXPORT_SYMBOL_GPL(sprd_mcdt_request_chan);

/**
 * sprd_mcdt_free_chan - free one MCDT channel
 * @chan: the channel to be freed
 */
void sprd_mcdt_free_chan(struct sprd_mcdt_chan *chan)
{
	struct sprd_mcdt_chan *temp;

	sprd_mcdt_chan_dma_disable(chan);
	sprd_mcdt_chan_int_disable(chan);

	mutex_lock(&sprd_mcdt_list_mutex);

	list_for_each_entry(temp, &sprd_mcdt_chan_list, list) {
		if (temp == chan) {
			mutex_unlock(&sprd_mcdt_list_mutex);
			return;
		}
	}

	list_add_tail(&chan->list, &sprd_mcdt_chan_list);
	mutex_unlock(&sprd_mcdt_list_mutex);
}
EXPORT_SYMBOL_GPL(sprd_mcdt_free_chan);

static void sprd_mcdt_init_chans(struct sprd_mcdt_dev *mcdt,
				 struct resource *res)
{
	int i;

	for (i = 0; i < MCDT_CHANNEL_NUM; i++) {
		struct sprd_mcdt_chan *chan = &mcdt->chan[i];

		if (i < MCDT_ADC_CHANNEL_NUM) {
			chan->id = i;
			chan->type = SPRD_MCDT_ADC_CHAN;
			chan->fifo_phys = res->start + MCDT_CH0_RXD + i * 4;
		} else {
			chan->id = i - MCDT_ADC_CHANNEL_NUM;
			chan->type = SPRD_MCDT_DAC_CHAN;
			chan->fifo_phys = res->start + MCDT_CH0_TXD +
				(i - MCDT_ADC_CHANNEL_NUM) * 4;
		}

		chan->mcdt = mcdt;
		INIT_LIST_HEAD(&chan->list);

		mutex_lock(&sprd_mcdt_list_mutex);
		list_add_tail(&chan->list, &sprd_mcdt_chan_list);
		mutex_unlock(&sprd_mcdt_list_mutex);
	}
}

static int sprd_mcdt_probe(struct platform_device *pdev)
{
	struct sprd_mcdt_dev *mcdt;
	struct resource *res;
	int ret, irq;

	mcdt = devm_kzalloc(&pdev->dev, sizeof(*mcdt), GFP_KERNEL);
	if (!mcdt)
		return -ENOMEM;

	mcdt->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(mcdt->base))
		return PTR_ERR(mcdt->base);

	mcdt->dev = &pdev->dev;
	spin_lock_init(&mcdt->lock);
	platform_set_drvdata(pdev, mcdt);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, sprd_mcdt_irq_handler,
			       0, "sprd-mcdt", mcdt);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request MCDT IRQ\n");
		return ret;
	}

	sprd_mcdt_init_chans(mcdt, res);

	return 0;
}

static int sprd_mcdt_remove(struct platform_device *pdev)
{
	struct sprd_mcdt_chan *chan, *temp;

	mutex_lock(&sprd_mcdt_list_mutex);

	list_for_each_entry_safe(chan, temp, &sprd_mcdt_chan_list, list)
		list_del(&chan->list);

	mutex_unlock(&sprd_mcdt_list_mutex);

	return 0;
}

static const struct of_device_id sprd_mcdt_of_match[] = {
	{ .compatible = "sprd,sc9860-mcdt", },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_mcdt_of_match);

static struct platform_driver sprd_mcdt_driver = {
	.probe = sprd_mcdt_probe,
	.remove = sprd_mcdt_remove,
	.driver = {
		.name = "sprd-mcdt",
		.of_match_table = sprd_mcdt_of_match,
	},
};

module_platform_driver(sprd_mcdt_driver);

MODULE_DESCRIPTION("Spreadtrum Multi-Channel Data Transfer Driver");
MODULE_LICENSE("GPL v2");
