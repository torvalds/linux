#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/types.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

/*
 * M2P channels.
 *
 * Note that these values are also directly used for setting the PPALLOC
 * register.
 */
#define EP93XX_DMA_I2S1		0
#define EP93XX_DMA_I2S2		1
#define EP93XX_DMA_AAC1		2
#define EP93XX_DMA_AAC2		3
#define EP93XX_DMA_AAC3		4
#define EP93XX_DMA_I2S3		5
#define EP93XX_DMA_UART1	6
#define EP93XX_DMA_UART2	7
#define EP93XX_DMA_UART3	8
#define EP93XX_DMA_IRDA		9
/* M2M channels */
#define EP93XX_DMA_SSP		10
#define EP93XX_DMA_IDE		11

/**
 * struct ep93xx_dma_data - configuration data for the EP93xx dmaengine
 * @port: peripheral which is requesting the channel
 * @direction: TX/RX channel
 * @name: optional name for the channel, this is displayed in /proc/interrupts
 *
 * This information is passed as private channel parameter in a filter
 * function. Note that this is only needed for slave/cyclic channels.  For
 * memcpy channels %NULL data should be passed.
 */
struct ep93xx_dma_data {
	int				port;
	enum dma_transfer_direction	direction;
	const char			*name;
};

/**
 * struct ep93xx_dma_chan_data - platform specific data for a DMA channel
 * @name: name of the channel, used for getting the right clock for the channel
 * @base: mapped registers
 * @irq: interrupt number used by this channel
 */
struct ep93xx_dma_chan_data {
	const char			*name;
	void __iomem			*base;
	int				irq;
};

/**
 * struct ep93xx_dma_platform_data - platform data for the dmaengine driver
 * @channels: array of channels which are passed to the driver
 * @num_channels: number of channels in the array
 *
 * This structure is passed to the DMA engine driver via platform data. For
 * M2P channels, contract is that even channels are for TX and odd for RX.
 * There is no requirement for the M2M channels.
 */
struct ep93xx_dma_platform_data {
	struct ep93xx_dma_chan_data	*channels;
	size_t				num_channels;
};

static inline bool ep93xx_dma_chan_is_m2p(struct dma_chan *chan)
{
	return !strcmp(dev_name(chan->device->dev), "ep93xx-dma-m2p");
}

/**
 * ep93xx_dma_chan_direction - returns direction the channel can be used
 * @chan: channel
 *
 * This function can be used in filter functions to find out whether the
 * channel supports given DMA direction. Only M2P channels have such
 * limitation, for M2M channels the direction is configurable.
 */
static inline enum dma_transfer_direction
ep93xx_dma_chan_direction(struct dma_chan *chan)
{
	if (!ep93xx_dma_chan_is_m2p(chan))
		return DMA_NONE;

	/* even channels are for TX, odd for RX */
	return (chan->chan_id % 2 == 0) ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
}

#endif /* __ASM_ARCH_DMA_H */
