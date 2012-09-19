/*
 * linux/amba/pl08x.h - ARM PrimeCell DMA Controller driver
 *
 * Copyright (C) 2005 ARM Ltd
 * Copyright (C) 2010 ST-Ericsson SA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * pl08x information required by platform code
 *
 * Please credit ARM.com
 * Documentation: ARM DDI 0196D
 */

#ifndef AMBA_PL08X_H
#define AMBA_PL08X_H

/* We need sizes of structs from this header */
#include <linux/dmaengine.h>
#include <linux/interrupt.h>

struct pl08x_driver_data;
struct pl08x_phy_chan;
struct pl08x_txd;

/* Bitmasks for selecting AHB ports for DMA transfers */
enum {
	PL08X_AHB1 = (1 << 0),
	PL08X_AHB2 = (1 << 1)
};

/**
 * struct pl08x_channel_data - data structure to pass info between
 * platform and PL08x driver regarding channel configuration
 * @bus_id: name of this device channel, not just a device name since
 * devices may have more than one channel e.g. "foo_tx"
 * @min_signal: the minimum DMA signal number to be muxed in for this
 * channel (for platforms supporting muxed signals). If you have
 * static assignments, make sure this is set to the assigned signal
 * number, PL08x have 16 possible signals in number 0 thru 15 so
 * when these are not enough they often get muxed (in hardware)
 * disabling simultaneous use of the same channel for two devices.
 * @max_signal: the maximum DMA signal number to be muxed in for
 * the channel. Set to the same as min_signal for
 * devices with static assignments
 * @muxval: a number usually used to poke into some mux regiser to
 * mux in the signal to this channel
 * @cctl_memcpy: options for the channel control register for memcpy
 *  *** not used for slave channels ***
 * @addr: source/target address in physical memory for this DMA channel,
 * can be the address of a FIFO register for burst requests for example.
 * This can be left undefined if the PrimeCell API is used for configuring
 * this.
 * @single: the device connected to this channel will request single DMA
 * transfers, not bursts. (Bursts are default.)
 * @periph_buses: the device connected to this channel is accessible via
 * these buses (use PL08X_AHB1 | PL08X_AHB2).
 */
struct pl08x_channel_data {
	const char *bus_id;
	int min_signal;
	int max_signal;
	u32 muxval;
	u32 cctl_memcpy;
	dma_addr_t addr;
	bool single;
	u8 periph_buses;
};

/**
 * struct pl08x_platform_data - the platform configuration for the PL08x
 * PrimeCells.
 * @slave_channels: the channels defined for the different devices on the
 * platform, all inclusive, including multiplexed channels. The available
 * physical channels will be multiplexed around these signals as they are
 * requested, just enumerate all possible channels.
 * @get_signal: request a physical signal to be used for a DMA transfer
 * immediately: if there is some multiplexing or similar blocking the use
 * of the channel the transfer can be denied by returning less than zero,
 * else it returns the allocated signal number
 * @put_signal: indicate to the platform that this physical signal is not
 * running any DMA transfer and multiplexing can be recycled
 * @lli_buses: buses which LLIs can be fetched from: PL08X_AHB1 | PL08X_AHB2
 * @mem_buses: buses which memory can be accessed from: PL08X_AHB1 | PL08X_AHB2
 */
struct pl08x_platform_data {
	const struct pl08x_channel_data *slave_channels;
	unsigned int num_slave_channels;
	struct pl08x_channel_data memcpy_channel;
	int (*get_signal)(const struct pl08x_channel_data *);
	void (*put_signal)(const struct pl08x_channel_data *, int);
	u8 lli_buses;
	u8 mem_buses;
};

#ifdef CONFIG_AMBA_PL08X
bool pl08x_filter_id(struct dma_chan *chan, void *chan_id);
#else
static inline bool pl08x_filter_id(struct dma_chan *chan, void *chan_id)
{
	return false;
}
#endif

#endif	/* AMBA_PL08X_H */
