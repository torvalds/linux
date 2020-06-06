/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef K3_UDMA_GLUE_H_
#define K3_UDMA_GLUE_H_

#include <linux/types.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <linux/dma/ti-cppi5.h>

struct k3_udma_glue_tx_channel_cfg {
	struct k3_ring_cfg tx_cfg;
	struct k3_ring_cfg txcq_cfg;

	bool tx_pause_on_err;
	bool tx_filt_einfo;
	bool tx_filt_pswords;
	bool tx_supr_tdpkt;
	u32  swdata_size;
};

struct k3_udma_glue_tx_channel;

struct k3_udma_glue_tx_channel *k3_udma_glue_request_tx_chn(struct device *dev,
		const char *name, struct k3_udma_glue_tx_channel_cfg *cfg);

void k3_udma_glue_release_tx_chn(struct k3_udma_glue_tx_channel *tx_chn);
int k3_udma_glue_push_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			     struct cppi5_host_desc_t *desc_tx,
			     dma_addr_t desc_dma);
int k3_udma_glue_pop_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			    dma_addr_t *desc_dma);
int k3_udma_glue_enable_tx_chn(struct k3_udma_glue_tx_channel *tx_chn);
void k3_udma_glue_disable_tx_chn(struct k3_udma_glue_tx_channel *tx_chn);
void k3_udma_glue_tdown_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			       bool sync);
void k3_udma_glue_reset_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
		void *data, void (*cleanup)(void *data, dma_addr_t desc_dma));
u32 k3_udma_glue_tx_get_hdesc_size(struct k3_udma_glue_tx_channel *tx_chn);
u32 k3_udma_glue_tx_get_txcq_id(struct k3_udma_glue_tx_channel *tx_chn);
int k3_udma_glue_tx_get_irq(struct k3_udma_glue_tx_channel *tx_chn);

enum {
	K3_UDMA_GLUE_SRC_TAG_LO_KEEP = 0,
	K3_UDMA_GLUE_SRC_TAG_LO_USE_FLOW_REG = 1,
	K3_UDMA_GLUE_SRC_TAG_LO_USE_REMOTE_FLOW_ID = 2,
	K3_UDMA_GLUE_SRC_TAG_LO_USE_REMOTE_SRC_TAG = 4,
};

/**
 * k3_udma_glue_rx_flow_cfg - UDMA RX flow cfg
 *
 * @rx_cfg:		RX ring configuration
 * @rxfdq_cfg:		RX free Host PD ring configuration
 * @ring_rxq_id:	RX ring id (or -1 for any)
 * @ring_rxfdq0_id:	RX free Host PD ring (FDQ) if (or -1 for any)
 * @rx_error_handling:	Rx Error Handling Mode (0 - drop, 1 - re-try)
 * @src_tag_lo_sel:	Rx Source Tag Low Byte Selector in Host PD
 */
struct k3_udma_glue_rx_flow_cfg {
	struct k3_ring_cfg rx_cfg;
	struct k3_ring_cfg rxfdq_cfg;
	int ring_rxq_id;
	int ring_rxfdq0_id;
	bool rx_error_handling;
	int src_tag_lo_sel;
};

/**
 * k3_udma_glue_rx_channel_cfg - UDMA RX channel cfg
 *
 * @psdata_size:	SW Data is present in Host PD of @swdata_size bytes
 * @flow_id_base:	first flow_id used by channel.
 *			if @flow_id_base = -1 - range of GP rflows will be
 *			allocated dynamically.
 * @flow_id_num:	number of RX flows used by channel
 * @flow_id_use_rxchan_id:	use RX channel id as flow id,
 *				used only if @flow_id_num = 1
 * @remote		indication that RX channel is remote - some remote CPU
 *			core owns and control the RX channel. Linux Host only
 *			allowed to attach and configure RX Flow within RX
 *			channel. if set - not RX channel operation will be
 *			performed by K3 NAVSS DMA glue interface.
 * @def_flow_cfg	default RX flow configuration,
 *			used only if @flow_id_num = 1
 */
struct k3_udma_glue_rx_channel_cfg {
	u32  swdata_size;
	int  flow_id_base;
	int  flow_id_num;
	bool flow_id_use_rxchan_id;
	bool remote;

	struct k3_udma_glue_rx_flow_cfg *def_flow_cfg;
};

struct k3_udma_glue_rx_channel;

struct k3_udma_glue_rx_channel *k3_udma_glue_request_rx_chn(
		struct device *dev,
		const char *name,
		struct k3_udma_glue_rx_channel_cfg *cfg);

void k3_udma_glue_release_rx_chn(struct k3_udma_glue_rx_channel *rx_chn);
int k3_udma_glue_enable_rx_chn(struct k3_udma_glue_rx_channel *rx_chn);
void k3_udma_glue_disable_rx_chn(struct k3_udma_glue_rx_channel *rx_chn);
void k3_udma_glue_tdown_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
			       bool sync);
int k3_udma_glue_push_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
		u32 flow_num, struct cppi5_host_desc_t *desc_tx,
		dma_addr_t desc_dma);
int k3_udma_glue_pop_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
		u32 flow_num, dma_addr_t *desc_dma);
int k3_udma_glue_rx_flow_init(struct k3_udma_glue_rx_channel *rx_chn,
		u32 flow_idx, struct k3_udma_glue_rx_flow_cfg *flow_cfg);
u32 k3_udma_glue_rx_flow_get_fdq_id(struct k3_udma_glue_rx_channel *rx_chn,
				    u32 flow_idx);
u32 k3_udma_glue_rx_get_flow_id_base(struct k3_udma_glue_rx_channel *rx_chn);
int k3_udma_glue_rx_get_irq(struct k3_udma_glue_rx_channel *rx_chn,
			    u32 flow_num);
void k3_udma_glue_rx_put_irq(struct k3_udma_glue_rx_channel *rx_chn,
			     u32 flow_num);
void k3_udma_glue_reset_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
		u32 flow_num, void *data,
		void (*cleanup)(void *data, dma_addr_t desc_dma),
		bool skip_fdq);
int k3_udma_glue_rx_flow_enable(struct k3_udma_glue_rx_channel *rx_chn,
				u32 flow_idx);
int k3_udma_glue_rx_flow_disable(struct k3_udma_glue_rx_channel *rx_chn,
				 u32 flow_idx);

#endif /* K3_UDMA_GLUE_H_ */
