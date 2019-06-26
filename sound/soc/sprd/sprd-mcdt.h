// SPDX-License-Identifier: GPL-2.0

#ifndef __SPRD_MCDT_H
#define __SPRD_MCDT_H

enum sprd_mcdt_channel_type {
	SPRD_MCDT_DAC_CHAN,
	SPRD_MCDT_ADC_CHAN,
	SPRD_MCDT_UNKNOWN_CHAN,
};

enum sprd_mcdt_dma_chan {
	SPRD_MCDT_DMA_CH0,
	SPRD_MCDT_DMA_CH1,
	SPRD_MCDT_DMA_CH2,
	SPRD_MCDT_DMA_CH3,
	SPRD_MCDT_DMA_CH4,
};

struct sprd_mcdt_chan_callback {
	void (*notify)(void *data);
	void *data;
};

/**
 * struct sprd_mcdt_chan - this struct represents a single channel instance
 * @mcdt: the mcdt controller
 * @id: channel id
 * @fifo_phys: channel fifo physical address which is used for DMA transfer
 * @type: channel type
 * @cb: channel fifo interrupt's callback interface to notify the fifo events
 * @dma_enable: indicate if use DMA mode to transfer data
 * @int_enable: indicate if use interrupt mode to notify users to read or
 * write data manually
 * @list: used to link into the global list
 *
 * Note: users should not modify any members of this structure.
 */
struct sprd_mcdt_chan {
	struct sprd_mcdt_dev *mcdt;
	u8 id;
	unsigned long fifo_phys;
	enum sprd_mcdt_channel_type type;
	enum sprd_mcdt_dma_chan dma_chan;
	struct sprd_mcdt_chan_callback *cb;
	bool dma_enable;
	bool int_enable;
	struct list_head list;
};

#ifdef CONFIG_SND_SOC_SPRD_MCDT
struct sprd_mcdt_chan *sprd_mcdt_request_chan(u8 channel,
					      enum sprd_mcdt_channel_type type);
void sprd_mcdt_free_chan(struct sprd_mcdt_chan *chan);

int sprd_mcdt_chan_write(struct sprd_mcdt_chan *chan, char *tx_buf, u32 size);
int sprd_mcdt_chan_read(struct sprd_mcdt_chan *chan, char *rx_buf, u32 size);
int sprd_mcdt_chan_int_enable(struct sprd_mcdt_chan *chan, u32 water_mark,
			      struct sprd_mcdt_chan_callback *cb);
void sprd_mcdt_chan_int_disable(struct sprd_mcdt_chan *chan);

int sprd_mcdt_chan_dma_enable(struct sprd_mcdt_chan *chan,
			      enum sprd_mcdt_dma_chan dma_chan, u32 water_mark);
void sprd_mcdt_chan_dma_disable(struct sprd_mcdt_chan *chan);

#else

struct sprd_mcdt_chan *sprd_mcdt_request_chan(u8 channel,
					      enum sprd_mcdt_channel_type type)
{
	return NULL;
}

void sprd_mcdt_free_chan(struct sprd_mcdt_chan *chan)
{ }

int sprd_mcdt_chan_write(struct sprd_mcdt_chan *chan, char *tx_buf, u32 size)
{
	return -EINVAL;
}

int sprd_mcdt_chan_read(struct sprd_mcdt_chan *chan, char *rx_buf, u32 size)
{
	return 0;
}

int sprd_mcdt_chan_int_enable(struct sprd_mcdt_chan *chan, u32 water_mark,
			      struct sprd_mcdt_chan_callback *cb)
{
	return -EINVAL;
}

void sprd_mcdt_chan_int_disable(struct sprd_mcdt_chan *chan)
{ }

int sprd_mcdt_chan_dma_enable(struct sprd_mcdt_chan *chan,
			      enum sprd_mcdt_dma_chan dma_chan, u32 water_mark)
{
	return -EINVAL;
}

void sprd_mcdt_chan_dma_disable(struct sprd_mcdt_chan *chan)
{ }

#endif

#endif /* __SPRD_MCDT_H */
