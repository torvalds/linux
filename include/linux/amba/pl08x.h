/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/amba/pl08x.h - ARM PrimeCell DMA Controller driver
 *
 * Copyright (C) 2005 ARM Ltd
 * Copyright (C) 2010 ST-Ericsson SA
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
	dma_addr_t addr;
	bool single;
	u8 periph_buses;
};

enum pl08x_burst_size {
	PL08X_BURST_SZ_1,
	PL08X_BURST_SZ_4,
	PL08X_BURST_SZ_8,
	PL08X_BURST_SZ_16,
	PL08X_BURST_SZ_32,
	PL08X_BURST_SZ_64,
	PL08X_BURST_SZ_128,
	PL08X_BURST_SZ_256,
};

enum pl08x_bus_width {
	PL08X_BUS_WIDTH_8_BITS,
	PL08X_BUS_WIDTH_16_BITS,
	PL08X_BUS_WIDTH_32_BITS,
};

/**
 * struct pl08x_platform_data - the platform configuration for the PL08x
 * PrimeCells.
 * @slave_channels: the channels defined for the different devices on the
 * platform, all inclusive, including multiplexed channels. The available
 * physical channels will be multiplexed around these signals as they are
 * requested, just enumerate all possible channels.
 * @num_slave_channels: number of elements in the slave channel array
 * @memcpy_burst_size: the appropriate burst size for memcpy operations
 * @memcpy_bus_width: memory bus width
 * @memcpy_prot_buff: whether memcpy DMA is bufferable
 * @memcpy_prot_cache: whether memcpy DMA is cacheable
 * @get_xfer_signal: request a physical signal to be used for a DMA transfer
 * immediately: if there is some multiplexing or similar blocking the use
 * of the channel the transfer can be denied by returning less than zero,
 * else it returns the allocated signal number
 * @put_xfer_signal: indicate to the platform that this physical signal is not
 * running any DMA transfer and multiplexing can be recycled
 * @lli_buses: buses which LLIs can be fetched from: PL08X_AHB1 | PL08X_AHB2
 * @mem_buses: buses which memory can be accessed from: PL08X_AHB1 | PL08X_AHB2
 * @slave_map: DMA slave matching table
 * @slave_map_len: number of elements in @slave_map
 */
struct pl08x_platform_data {
	struct pl08x_channel_data *slave_channels;
	unsigned int num_slave_channels;
	enum pl08x_burst_size memcpy_burst_size;
	enum pl08x_bus_width memcpy_bus_width;
	bool memcpy_prot_buff;
	bool memcpy_prot_cache;
	int (*get_xfer_signal)(const struct pl08x_channel_data *);
	void (*put_xfer_signal)(const struct pl08x_channel_data *, int);
	u8 lli_buses;
	u8 mem_buses;
	const struct dma_slave_map *slave_map;
	int slave_map_len;
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
