/*
 * Platform data for the COH901318 DMA controller
 * Copyright (C) 2007-2013 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef PLAT_COH901318_H
#define PLAT_COH901318_H

#ifdef CONFIG_COH901318

/* We only support the U300 DMA channels */
#define U300_DMA_MSL_TX_0		0
#define U300_DMA_MSL_TX_1		1
#define U300_DMA_MSL_TX_2		2
#define U300_DMA_MSL_TX_3		3
#define U300_DMA_MSL_TX_4		4
#define U300_DMA_MSL_TX_5		5
#define U300_DMA_MSL_TX_6		6
#define U300_DMA_MSL_RX_0		7
#define U300_DMA_MSL_RX_1		8
#define U300_DMA_MSL_RX_2		9
#define U300_DMA_MSL_RX_3		10
#define U300_DMA_MSL_RX_4		11
#define U300_DMA_MSL_RX_5		12
#define U300_DMA_MSL_RX_6		13
#define U300_DMA_MMCSD_RX_TX		14
#define U300_DMA_MSPRO_TX		15
#define U300_DMA_MSPRO_RX		16
#define U300_DMA_UART0_TX		17
#define U300_DMA_UART0_RX		18
#define U300_DMA_APEX_TX		19
#define U300_DMA_APEX_RX		20
#define U300_DMA_PCM_I2S0_TX		21
#define U300_DMA_PCM_I2S0_RX		22
#define U300_DMA_PCM_I2S1_TX		23
#define U300_DMA_PCM_I2S1_RX		24
#define U300_DMA_XGAM_CDI		25
#define U300_DMA_XGAM_PDI		26
#define U300_DMA_SPI_TX			27
#define U300_DMA_SPI_RX			28
#define U300_DMA_GENERAL_PURPOSE_0	29
#define U300_DMA_GENERAL_PURPOSE_1	30
#define U300_DMA_GENERAL_PURPOSE_2	31
#define U300_DMA_GENERAL_PURPOSE_3	32
#define U300_DMA_GENERAL_PURPOSE_4	33
#define U300_DMA_GENERAL_PURPOSE_5	34
#define U300_DMA_GENERAL_PURPOSE_6	35
#define U300_DMA_GENERAL_PURPOSE_7	36
#define U300_DMA_GENERAL_PURPOSE_8	37
#define U300_DMA_UART1_TX		38
#define U300_DMA_UART1_RX		39

#define U300_DMA_DEVICE_CHANNELS	32
#define U300_DMA_CHANNELS		40

/**
 * coh901318_filter_id() - DMA channel filter function
 * @chan: dma channel handle
 * @chan_id: id of dma channel to be filter out
 *
 * In dma_request_channel() it specifies what channel id to be requested
 */
bool coh901318_filter_id(struct dma_chan *chan, void *chan_id);
#else
static inline bool coh901318_filter_id(struct dma_chan *chan, void *chan_id)
{
	return false;
}
#endif

#endif /* PLAT_COH901318_H */
