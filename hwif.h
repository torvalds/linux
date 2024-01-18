/*
 * TC956X ethernet driver.
 *
 * hwif.h
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 *  04 Nov 2021 : 1. Added separate control functons for MAC TX and RX start/stop
 *  VERSION     : 01-00-20
 *  26 Dec 2023 : 1. Added the support for TC commands taprio and flower
 *  VERSION     : 01-03-59
 */

#ifndef __TC956XMAC_HWIF_H__
#define __TC956XMAC_HWIF_H__

#include <linux/netdevice.h>
#include "tc956xmac_inc.h"
#include <linux/version.h>

#define tc956xmac_do_void_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) { \
		(__priv)->hw->__module->__cname(__priv, (__arg0), ##__args); \
		__result = 0; \
	} \
	__result; \
})
#define tc956xmac_do_callback(__priv, __module, __cname,  __arg0, __args...) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) \
		__result = (__priv)->hw->__module->__cname(__priv, (__arg0), ##__args); \
	__result; \
})
#define tc956xmac_do_void_no_param_callback(__priv, __module, __cname) \
({ \
	int __result = -EINVAL; \
	if ((__priv)->hw->__module && (__priv)->hw->__module->__cname) { \
		(__priv)->hw->__module->__cname(__priv); \
		__result = 0; \
	} \
	__result; \
})

struct tc956xmac_priv;
struct tc956xmac_extra_stats;
struct tc956xmac_safety_stats;
struct dma_desc;
struct dma_extended_desc;
struct dma_edesc;

#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
struct fn_id;
enum mbx_msg_fns;
#endif

/* Descriptors helpers */
struct tc956xmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc)(struct tc956xmac_priv *priv, struct dma_desc *p, int disable_rx_ic, int mode,
			int end, int bfsize);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc)(struct tc956xmac_priv *priv, struct dma_desc *p, int mode, int end);
	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc)(struct tc956xmac_priv *priv, struct dma_desc *p, int is_fs, int len,
			bool csum_flag, u32 crc_pad, int mode, bool tx_own,
			bool ls, unsigned int tot_pkt_len);
	void (*prepare_tso_tx_desc)(struct tc956xmac_priv *priv, struct dma_desc *p, int is_fs, int len1,
			int len2, bool tx_own, bool ls, unsigned int tcphdrlen,
			unsigned int tcppayloadlen);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner)(struct tc956xmac_priv *priv, struct dma_desc *p);
	int (*get_tx_owner)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc)(struct tc956xmac_priv *priv, struct dma_desc *p, int mode);
	/*
	 * Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted
	 */
	void (*set_tx_ic)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* Last tx segment reports the transmit status */
	int (*get_tx_ls)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* Return the transmit status looking at the TDES1 */
	int (*tx_status)(struct tc956xmac_priv *priv, void *data, struct tc956xmac_extra_stats *x,
			struct dma_desc *p, void __iomem *ioaddr);
	/* Get the buffer size from the descriptor */
	int (*get_tx_len)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* Handle extra events on specific interrupts hw dependent */
	void (*set_rx_owner)(struct tc956xmac_priv *priv, struct dma_desc *p, int disable_rx_ic);
	/* Get the receive frame size */
	int (*get_rx_frame_len)(struct tc956xmac_priv *priv, struct dma_desc *p, int rx_coe_type);
	/* Return the reception status looking at the RDES1 */
	int (*rx_status)(struct tc956xmac_priv *priv, void *data, struct tc956xmac_extra_stats *x,
			struct dma_desc *p);
	void (*rx_extended_status)(struct tc956xmac_priv *priv, void *data, struct tc956xmac_extra_stats *x,
			struct dma_extended_desc *p);
	/* Set tx timestamp enable bit */
	void (*enable_tx_timestamp)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* get tx timestamp status */
	int (*get_tx_timestamp_status)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* get timestamp value */
	void (*get_timestamp)(struct tc956xmac_priv *priv, void *desc, u32 ats, u64 *ts);
	/* get rx timestamp status */
	int (*get_rx_timestamp_status)(struct tc956xmac_priv *priv, void *desc, void *next_desc, u32 ats);
	/* Display ring */
	void (*display_ring)(struct tc956xmac_priv *priv, void *head, unsigned int size, bool rx);
	/* set MSS via context descriptor */
	void (*set_mss)(struct tc956xmac_priv *priv, struct dma_desc *p, unsigned int mss);
	/* get descriptor skbuff address */
	void (*get_addr)(struct tc956xmac_priv *priv, struct dma_desc *p, unsigned int *addr);
	/* set descriptor skbuff address */
	void (*set_addr)(struct tc956xmac_priv *priv, struct dma_desc *p, dma_addr_t addr);
	/* clear descriptor */
	void (*clear)(struct tc956xmac_priv *priv, struct dma_desc *p);
	/* RSS */
	int (*get_rx_hash)(struct tc956xmac_priv *priv, struct dma_desc *p, u32 *hash,
			   enum pkt_hash_types *type);
	int (*get_rx_header_len)(struct tc956xmac_priv *priv, struct dma_desc *p, unsigned int *len);
	void (*set_sec_addr)(struct tc956xmac_priv *priv, struct dma_desc *p, dma_addr_t addr);
	void (*set_sarc)(struct tc956xmac_priv *priv, struct dma_desc *p, u32 sarc_type);
	void (*set_vlan_tag)(struct tc956xmac_priv *priv, struct dma_desc *p, u16 tag, u16 inner_tag,
			     u32 inner_type);
	void (*set_vlan)(struct tc956xmac_priv *priv, struct dma_desc *p, u32 type);
	void (*set_tbs)(struct tc956xmac_priv *priv, struct dma_edesc *p, u32 sec, u32 nsec, bool lt_valid);
	void (*set_ostc)(struct tc956xmac_priv *priv, struct dma_desc *p, u32 ttsh, u32 ttsl);
};

#define tc956xmac_init_rx_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, init_rx_desc, __args)
#define tc956xmac_init_tx_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, init_tx_desc, __args)
#define tc956xmac_prepare_tx_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, prepare_tx_desc, __args)
#define tc956xmac_prepare_tso_tx_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, prepare_tso_tx_desc, __args)
#define tc956xmac_set_tx_owner(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_tx_owner, __args)
#define tc956xmac_get_tx_owner(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_tx_owner, __args)
#define tc956xmac_release_tx_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, release_tx_desc, __args)
#define tc956xmac_set_tx_ic(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_tx_ic, __args)
#define tc956xmac_get_tx_ls(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_tx_ls, __args)
#define tc956xmac_tx_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, tx_status, __args)
#define tc956xmac_get_tx_len(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_tx_len, __args)
#define tc956xmac_set_rx_owner(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_rx_owner, __args)
#define tc956xmac_get_rx_frame_len(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_rx_frame_len, __args)
#define tc956xmac_rx_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, rx_status, __args)
#define tc956xmac_rx_extended_status(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, rx_extended_status, __args)
#define tc956xmac_enable_tx_timestamp(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, enable_tx_timestamp, __args)
#define tc956xmac_get_tx_timestamp_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_tx_timestamp_status, __args)
#define tc956xmac_get_timestamp(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, get_timestamp, __args)
#define tc956xmac_get_rx_timestamp_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_rx_timestamp_status, __args)
#define tc956xmac_display_ring(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, display_ring, __args)
#define tc956xmac_set_mss(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_mss, __args)
#define tc956xmac_get_desc_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, get_addr, __args)
#define tc956xmac_set_desc_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_addr, __args)
#define tc956xmac_clear_desc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, clear, __args)
#define tc956xmac_get_rx_hash(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_rx_hash, __args)
#define tc956xmac_get_rx_header_len(__priv, __args...) \
	tc956xmac_do_callback(__priv, desc, get_rx_header_len, __args)
#define tc956xmac_set_desc_sec_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_sec_addr, __args)
#define tc956xmac_set_desc_sarc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_sarc, __args)
#define tc956xmac_set_desc_vlan_tag(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_vlan_tag, __args)
#define tc956xmac_set_desc_vlan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_vlan, __args)
#define tc956xmac_set_desc_tbs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_tbs, __args)
#define tc956xmac_set_desc_ostc(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, desc, set_ostc, __args)

struct tc956xmac_dma_cfg;
struct dma_features;

/* Specific DMA helpers */
struct tc956xmac_dma_ops {
	/* DMA core initialization */
	int (*reset)(struct tc956xmac_priv *priv, void __iomem *ioaddr);
	void (*init)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_dma_cfg *dma_cfg,
		     int atds);
	void (*init_chan)(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			  struct tc956xmac_dma_cfg *dma_cfg, u32 chan);
	void (*init_rx_chan)(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			     struct tc956xmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	void (*init_tx_chan)(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			     struct tc956xmac_dma_cfg *dma_cfg,
			     dma_addr_t phy, u32 chan);
	/* Configure the AXI Bus Mode Register */
	void (*axi)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_axi *axi);
	/* Dump DMA registers */
	void (*dump_regs)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 *reg_space);
	void (*dma_rx_mode)(struct tc956xmac_priv *priv, void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*dma_tx_mode)(struct tc956xmac_priv *priv, void __iomem *ioaddr, int mode, u32 channel,
			    int fifosz, u8 qmode);
	/* To track extra statistic (if supported) */
	void (*dma_diagnostic_fr)(struct tc956xmac_priv *priv, void *data, struct tc956xmac_extra_stats *x,
				   void __iomem *ioaddr);
	void (*enable_dma_transmission)(struct tc956xmac_priv *priv, void __iomem *ioaddr);
	void (*enable_dma_irq)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan,
			       bool rx, bool tx);
	void (*disable_dma_irq)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan,
				bool rx, bool tx);
	void (*start_tx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan);
	void (*stop_tx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan);
	void (*start_rx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan);
	void (*stop_rx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 chan);
	int (*dma_interrupt)(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			      struct tc956xmac_extra_stats *x, u32 chan);
	/* If supported then get the optional core features */
	void (*get_hw_feature)(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			       struct dma_features *dma_cap);
	/* Program the HW RX Watchdog */
	void (*rx_watchdog)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 riwt, u32 number_chan);
	void (*set_tx_ring_len)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_ring_len)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 len, u32 chan);
	void (*set_rx_tail_ptr)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*set_tx_tail_ptr)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 tail_ptr, u32 chan);
	void (*enable_tso)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool en, u32 chan);
	void (*qmode)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 channel, u8 qmode);
	void (*set_bfsize)(struct tc956xmac_priv *priv, void __iomem *ioaddr, int bfsize, u32 chan);
	void (*enable_sph)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool en, u32 chan);
	int (*enable_tbs)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool en, u32 chan);
	void (*desc_stats)(struct tc956xmac_priv *priv, void __iomem *ioaddr);
};

#define tc956xmac_reset(__priv, __args...) \
	tc956xmac_do_callback(__priv, dma, reset, __args)
#define tc956xmac_dma_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, init, __args)
#define tc956xmac_init_chan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, init_chan, __args)
#define tc956xmac_init_rx_chan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, init_rx_chan, __args)
#define tc956xmac_init_tx_chan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, init_tx_chan, __args)
#define tc956xmac_axi(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, axi, __args)
#define tc956xmac_dump_dma_regs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, dump_regs, __args)
#define tc956xmac_dma_rx_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, dma_rx_mode, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_dma_tx_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, dma_tx_mode, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_dma_tx_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, dma_tx_mode, __args)
#endif
#define tc956xmac_dma_diagnostic_fr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, dma_diagnostic_fr, __args)
#define tc956xmac_enable_dma_transmission(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, enable_dma_transmission, __args)
#define tc956xmac_enable_dma_irq(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, enable_dma_irq, __args)
#define tc956xmac_disable_dma_irq(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, disable_dma_irq, __args)
#define tc956xmac_start_tx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, start_tx, __args)
#define tc956xmac_stop_tx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, stop_tx, __args)
#define tc956xmac_start_rx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, start_rx, __args)
#define tc956xmac_stop_rx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, stop_rx, __args)
#define tc956xmac_dma_interrupt_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, dma, dma_interrupt, __args)
#define tc956xmac_get_hw_feature(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, get_hw_feature, __args)
#define tc956xmac_rx_watchdog(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, rx_watchdog, __args)
#define tc956xmac_set_tx_ring_len(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, set_tx_ring_len, __args)
#define tc956xmac_set_rx_ring_len(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, set_rx_ring_len, __args)
#define tc956xmac_set_rx_tail_ptr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, set_rx_tail_ptr, __args)
#define tc956xmac_set_tx_tail_ptr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, set_tx_tail_ptr, __args)
#define tc956xmac_enable_tso(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, enable_tso, __args)
#define tc956xmac_dma_qmode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, qmode, __args)
#define tc956xmac_set_dma_bfsize(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, set_bfsize, __args)
#define tc956xmac_enable_sph(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, enable_sph, __args)
#define tc956xmac_enable_tbs(__priv, __args...) \
	tc956xmac_do_callback(__priv, dma, enable_tbs, __args)
#define tc956xmac_dma_desc_stats(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, dma, desc_stats, __args)

struct mac_device_info;
struct net_device;
struct rgmii_adv;
struct tc956xmac_safety_stats;
struct tc956xmac_tc_entry;
struct tc956xmac_pps_cfg;
struct tc956xmac_rss;
struct tc956xmac_est;
struct tc956xmac_rx_parser_cfg;

/* Helpers to program the MAC core */
struct tc956xmac_ops {
	/* MAC core initialization */
	void (*core_init)(struct tc956xmac_priv *priv, struct mac_device_info *hw, struct net_device *dev);
	/* Enable the MAC RX/TX */
	void (*set_mac)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
	/* Start/Stop the MAC TX */
	void (*set_mac_tx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
	/* Start/Stop the MAC RX */
	void (*set_mac_rx)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
	/* Enable and verify that the IPC module is supported */
	int (*rx_ipc)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	/* Enable RX Queues */
	void (*rx_queue_enable)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u8 mode, u32 queue);
	/* RX Queues Priority */
	void (*rx_queue_prio)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 prio, u32 queue);
	/* TX Queues Priority */
	void (*tx_queue_prio)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 prio, u32 queue);
	/* RX Queues Routing */
	void (*rx_queue_routing)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u8 packet,
				 u32 queue);
	/* Program RX Algorithms */
	void (*prog_mtl_rx_algorithms)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 rx_alg);
	/* Program TX Algorithms */
	void (*prog_mtl_tx_algorithms)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 tx_alg);
	/* Set MTL TX queues weight */
	void (*set_mtl_tx_queue_weight)(struct tc956xmac_priv *priv, struct mac_device_info *hw,
					u32 weight, u32 tc);
	/* RX MTL queue to RX dma mapping */
	void (*map_mtl_to_dma)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 queue, u32 chan);
	/* Configure AV Algorithm */
	void (*config_cbs)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 send_slope,
			   u32 idle_slope, u32 high_credit, u32 low_credit,
			   u32 queue);
	/* Dump MAC registers */
	void (*dump_regs)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 *reg_space);
	/* Handle extra events on specific interrupts hw dependent */
	int (*host_irq_status)(struct tc956xmac_priv *priv, struct mac_device_info *hw,
			       struct tc956xmac_extra_stats *x);
	/* Handle MTL interrupts */
	int (*host_mtl_irq_status)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 chan);
	/* Multicast filter setting */
	void (*set_filter)(struct tc956xmac_priv *priv, struct mac_device_info *hw, struct net_device *dev);
	/* Flow control setting */
	void (*flow_ctrl)(struct tc956xmac_priv *priv, struct mac_device_info *hw, unsigned int duplex,
			  unsigned int fc, unsigned int pause_time, u32 tx_cnt);
	/* Set power management mode (e.g. magic frame) */
	void (*pmt)(struct tc956xmac_priv *priv, struct mac_device_info *hw, unsigned long mode);
	/* Set/Get Unicast MAC addresses */
	void (*set_umac_addr)(struct tc956xmac_priv *priv, struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n, unsigned int vf);
	void (*get_umac_addr)(struct tc956xmac_priv *priv, struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n);
	void (*set_eee_mode)(struct tc956xmac_priv *priv, struct mac_device_info *hw,
			     bool en_tx_lpi_clockgating);
	void (*reset_eee_mode)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*set_eee_timer)(struct tc956xmac_priv *priv, struct mac_device_info *hw, int ls, int tw);
	void (*set_eee_pls)(struct tc956xmac_priv *priv, struct mac_device_info *hw, int link);
	void (*debug)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_extra_stats *x,
		      u32 rx_queues, u32 tx_queues);
	/* PCS calls */
	void (*pcs_ctrl_ane)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool ane, bool srgmi_ral,
			     bool loopback);
	void (*pcs_rane)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool restart);
	void (*pcs_get_adv_lp)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct rgmii_adv *adv);
#ifdef TC956X
	int (*xpcs_init)(struct tc956xmac_priv *priv, void __iomem *xpcsaddr);
	void (*xpcs_ctrl_ane)(struct tc956xmac_priv *priv, bool ane);
#endif
	/* Safety Features */
	int (*safety_feat_config)(struct tc956xmac_priv *priv, void __iomem *ioaddr, unsigned int asp);
	int (*safety_feat_irq_status)(struct tc956xmac_priv *priv, struct net_device *ndev,
			void __iomem *ioaddr, unsigned int asp,
			struct tc956xmac_safety_stats *stats);
	int (*safety_feat_dump)(struct tc956xmac_priv *priv, struct tc956xmac_safety_stats *stats,
			int index, unsigned long *count, const char **desc);
	/* Flexible RX Parser */
	int (*rxp_config)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_tc_entry *entries,
			  unsigned int count);
	int (*rx_parser_init)(struct tc956xmac_priv *priv, struct net_device *ndev,
			struct mac_device_info *hw, unsigned int spram,
			unsigned int frpsel, unsigned int frpes,
			struct tc956xmac_rx_parser_cfg *cnf);
	/* Flexible PPS */
	int (*flex_pps_config)(struct tc956xmac_priv *priv, void __iomem *ioaddr, int index,
			       struct tc956xmac_pps_cfg *cfg, bool enable,
			       u32 sub_second_inc, u32 systime_flags);
	/* Loopback for selftests */
	void (*set_mac_loopback)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
	/* RSS */
	int (*rss_configure)(struct tc956xmac_priv *priv, struct mac_device_info *hw,
			     struct tc956xmac_rss *cfg, u32 num_rxq);
	/* VLAN */
	void (*update_vlan_hash)(struct tc956xmac_priv *priv, struct net_device *dev, bool is_double, u16 vid,
				u16 vf);

	void (*delete_vlan)(struct tc956xmac_priv *priv, struct net_device *dev, u16 vid,
				u16 vf);
	void (*enable_vlan)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 type);
#ifdef TC956X
	void (*disable_tx_vlan)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*enable_rx_vlan_stripping)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*disable_rx_vlan_stripping)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*enable_rx_vlan_filtering)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*disable_rx_vlan_filtering)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
#endif
	/* TX Timestamp */
	int (*get_mac_tx_timestamp)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u64 *ts);
	/* Source Address Insertion / Replacement */
	void (*sarc_configure)(struct tc956xmac_priv *priv, void __iomem *ioaddr, int val);
	/* Filtering */
	int (*config_l3_filter)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 filter_no,
				bool en, bool ipv6, bool sa, bool inv,
				u32 match);
	int (*config_l4_filter)(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 filter_no,
				bool en, bool udp, bool sa, bool inv,
				u32 match);
	void (*set_arp_offload)(struct tc956xmac_priv *priv, struct mac_device_info *hw, bool en, u32 addr);
	int (*est_configure)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_est *cfg,
			     unsigned int ptp_rate);
	void (*fpe_configure)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 num_txq, u32 num_rxq,
			      bool enable);
	void (*set_ptp_offload)(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool en);
	void (*jumbo_en)(struct tc956xmac_priv *priv, struct net_device *dev, u32 en);
};

#define tc956xmac_core_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, core_init, __args)
#define tc956xmac_mac_set(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_mac, __args)
#define tc956xmac_mac_set_tx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_mac_tx, __args)
#define tc956xmac_mac_set_rx(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_mac_rx, __args)
#define tc956xmac_rx_ipc(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, rx_ipc, __args)
#define tc956xmac_rx_queue_enable(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, rx_queue_enable, __args)
#define tc956xmac_rx_queue_prio(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, rx_queue_prio, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_tx_queue_prio(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, tx_queue_prio, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_tx_queue_prio(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, tx_queue_prio, __args)
#endif
#define tc956xmac_rx_queue_routing(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, rx_queue_routing, __args)
#define tc956xmac_prog_mtl_rx_algorithms(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, prog_mtl_rx_algorithms, __args)
#define tc956xmac_prog_mtl_tx_algorithms(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, prog_mtl_tx_algorithms, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_set_mtl_tx_queue_weight(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_mtl_tx_queue_weight, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_set_mtl_tx_queue_weight(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, set_mtl_tx_queue_weight, __args)
#endif

#define tc956xmac_map_mtl_to_dma(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, map_mtl_to_dma, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_config_cbs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, config_cbs, __args)
#elif defined TC956X_SRIOV_VF
#define tc956xmac_config_cbs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, config_cbs, __args)
#endif

#ifdef TC956X_SRIOV_VF
#define tc956x_phy_link(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, phy_link, __args)
#define tc956x_setup_mbx_etf(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, setup_mbx_etf, __args)
#define tc956x_get_drv_cap(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, get_drv_cap, __args)
#define tc956x_rx_crc(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_crc, __args)
#define tc956x_rx_dma_ch_tlptr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_dma_ch_tlptr, __args)
#define tc956x_rx_dma_err(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_dma_err, __args)
#define tc956x_rx_csum(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_csum, __args)
#define tc956x_mbx_flr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, pf_flr, __args)
#endif

#define tc956xmac_dump_mac_regs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, dump_regs, __args)
#define tc956xmac_host_irq_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, host_irq_status, __args)
#define tc956xmac_host_mtl_irq_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, host_mtl_irq_status, __args)
#define tc956xmac_set_filter(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_filter, __args)
#define tc956xmac_flow_ctrl(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, flow_ctrl, __args)
#define tc956xmac_pmt(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, pmt, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_set_umac_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_umac_addr, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_set_umac_addr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, set_umac_addr, __args)
#endif
#ifdef TC956X_SRIOV_PF
#define tc956xmac_get_umac_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, get_umac_addr, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_get_umac_addr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, get_umac_addr, __args)
#endif
#define tc956xmac_set_eee_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_eee_mode, __args)
#ifdef TC956X_SRIOV_VF
#define tc956xmac_reset_eee_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, reset_eee_mode, __args)
#else
#define tc956xmac_reset_eee_mode(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, reset_eee_mode, __args)
#endif
#define tc956xmac_set_eee_timer(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_eee_timer, __args)
#define tc956xmac_set_eee_pls(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_eee_pls, __args)
#define tc956xmac_mac_debug(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, debug, __args)
#ifdef TC956X
#define tc956xmac_xpcs_init(__priv, __args...) \
		tc956xmac_do_callback(__priv, mac, xpcs_init, __args)
#define tc956xmac_xpcs_ctrl_ane(__priv, __args...) \
		tc956xmac_do_void_callback(__priv, mac, xpcs_ctrl_ane, __args)
#endif
#define tc956xmac_pcs_ctrl_ane(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, pcs_ctrl_ane, __args)
#define tc956xmac_pcs_rane(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, pcs_rane, __args)
#define tc956xmac_pcs_get_adv_lp(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, pcs_get_adv_lp, __args)
#define tc956xmac_safety_feat_config(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, safety_feat_config, __args)
#define tc956xmac_safety_feat_irq_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, safety_feat_irq_status, __args)
#define tc956xmac_safety_feat_dump(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, safety_feat_dump, __args)
#define tc956xmac_rxp_config(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, rxp_config, __args)
#define tc956xmac_rx_parser_init(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, rx_parser_init, __args)
#define tc956xmac_flex_pps_config(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, flex_pps_config, __args)
#define tc956xmac_set_mac_loopback(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_mac_loopback, __args)
#define tc956xmac_rss_configure(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, rss_configure, __args)
#define tc956xmac_update_vlan_hash(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, update_vlan_hash, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_delete_vlan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, delete_vlan, __args)
#else //TC956X_SRIOV_PF
#define tc956xmac_delete_vlan(__priv, __args...) \
			tc956xmac_do_void_callback(__priv, mbx_wrapper, delete_vlan, __args)
#endif
#define tc956xmac_enable_vlan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, enable_vlan, __args)
#ifdef TC956X
#define tc956xmac_disable_tx_vlan(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, disable_tx_vlan, __args)
#define tc956xmac_enable_rx_vlan_stripping(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, enable_rx_vlan_stripping, __args)
#define tc956xmac_disable_rx_vlan_stripping(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, disable_rx_vlan_stripping, __args)
#define tc956xmac_enable_rx_vlan_filtering(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, enable_rx_vlan_filtering, __args)
#define tc956xmac_disable_rx_vlan_filtering(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, disable_rx_vlan_filtering, __args)
#endif
#define tc956xmac_get_mac_tx_timestamp(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, get_mac_tx_timestamp, __args)
#define tc956xmac_sarc_configure(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, sarc_configure, __args)
#define tc956xmac_config_l3_filter(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, config_l3_filter, __args)
#define tc956xmac_config_l4_filter(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, config_l4_filter, __args)
#define tc956xmac_set_arp_offload(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_arp_offload, __args)
#define tc956xmac_est_configure(__priv, __args...) \
	tc956xmac_do_callback(__priv, mac, est_configure, __args)
#define tc956xmac_fpe_configure(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, fpe_configure, __args)
#define tc956xmac_set_ptp_offload(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, set_ptp_offload, __args)
#define tc956xmac_jumbo_en(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mac, jumbo_en, __args)
#ifdef TC956X_SRIOV_VF
#define tc956xmac_get_speed(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, get_speed, __args)
#define tc956xmac_get_est(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, get_est, __args)
#define tc956xmac_set_est(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, set_est, __args)
#define tc956xmac_get_rxp(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, get_rxp, __args)
#define tc956xmac_set_rxp(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, set_rxp, __args)
#define tc956xmac_get_fpe(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, get_fpe, __args)
#define tc956xmac_set_fpe(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, set_fpe, __args)
#define tc956xmac_reg_wr(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, reg_wr, __args)

#define tc956xmac_get_cbs(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, get_cbs, __args)

#define tc956xmac_set_cbs(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, set_cbs, __args)

#define tc956xmac_ethtool_get_pauseparam(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, get_pause_param, __args)

#define tc956xmac_ethtool_get_eee(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, get_eee, __args)


#define tc956xmac_add_mac(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, add_mac, __args)

#define tc956xmac_delete_mac(__priv, __args...) \
			tc956xmac_do_void_callback(__priv, mbx_wrapper, delete_mac, __args)

#define tc956xmac_add_vlan(__priv, __args...) \
			tc956xmac_do_void_callback(__priv, mbx_wrapper, add_vlan, __args)
#endif
#ifdef TC956X_SRIOV_VF
#define tc956xmac_get_link_status(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, get_link_status, __args)

#define tc956xmac_vf_reset(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, vf_reset, __args)
#endif
/* PTP and HW Timer helpers */
struct tc956xmac_hwtimestamp {
	void (*config_hw_tstamping)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 data);
	void (*config_sub_second_increment)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 ptp_clock,
					   int gmac4, u32 *ssinc);
	int (*init_systime)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 sec, u32 nsec);
	int (*config_addend)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 addend);
	int (*adjust_systime)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 sec, u32 nsec,
			       int add_sub, int gmac4);
	void (*get_systime)(struct tc956xmac_priv *priv, void __iomem *ioaddr, u64 *systime);
};

#define tc956xmac_config_hw_tstamping(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, ptp, config_hw_tstamping, __args)
#define tc956xmac_config_sub_second_increment(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, ptp, config_sub_second_increment, __args)
#define tc956xmac_init_systime(__priv, __args...) \
	tc956xmac_do_callback(__priv, ptp, init_systime, __args)
#define tc956xmac_config_addend(__priv, __args...) \
	tc956xmac_do_callback(__priv, ptp, config_addend, __args)
#define tc956xmac_adjust_systime(__priv, __args...) \
	tc956xmac_do_callback(__priv, ptp, adjust_systime, __args)
#define tc956xmac_get_systime(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, ptp, get_systime, __args)

/* Helpers to manage the descriptors for chain and ring modes */
struct tc956xmac_mode_ops {
	void (*init)(struct tc956xmac_priv *priv, void *des, dma_addr_t phy_addr, unsigned int size,
		      unsigned int extend_desc);
	unsigned int (*is_jumbo_frm)(struct tc956xmac_priv *priv, int len, int ehn_desc);
	int (*jumbo_frm)(struct tc956xmac_priv *priv, void *priv_ptr, struct sk_buff *skb, int csum);
	int (*set_16kib_bfsize)(struct tc956xmac_priv *priv, int mtu);
	void (*init_desc3)(struct tc956xmac_priv *priv, struct dma_desc *p);
	void (*refill_desc3)(struct tc956xmac_priv *priv, void *priv_ptr, struct dma_desc *p);
	void (*clean_desc3)(struct tc956xmac_priv *priv, void *priv_ptr, struct dma_desc *p);
};

#define tc956xmac_mode_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mode, init, __args)
#define tc956xmac_is_jumbo_frm(__priv, __args...) \
	tc956xmac_do_callback(__priv, mode, is_jumbo_frm, __args)
#define tc956xmac_jumbo_frm(__priv, __args...) \
	tc956xmac_do_callback(__priv, mode, jumbo_frm, __args)
#define tc956xmac_set_16kib_bfsize(__priv, __args...) \
	tc956xmac_do_callback(__priv, mode, set_16kib_bfsize, __args)
#define tc956xmac_init_desc3(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mode, init_desc3, __args)
#define tc956xmac_refill_desc3(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mode, refill_desc3, __args)
#define tc956xmac_clean_desc3(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mode, clean_desc3, __args)

#ifdef TC956X_SRIOV_PF
struct tc956xmac_priv;
#endif
struct tc_cls_u32_offload;
struct tc_cbs_qopt_offload;
struct flow_cls_offload;
struct tc_taprio_qopt_offload;
struct tc_etf_qopt_offload;
struct tc_query_caps_base;

struct tc956xmac_tc_ops {
	int (*init)(struct tc956xmac_priv *priv, void *data);
	int (*setup_cls_u32)(struct tc956xmac_priv *priv,
			     struct tc_cls_u32_offload *cls);
	int (*setup_cbs)(struct tc956xmac_priv *priv,
			 struct tc_cbs_qopt_offload *qopt);
	int (*setup_cls)(struct tc956xmac_priv *priv,
			 struct flow_cls_offload *cls);
	int (*setup_taprio)(struct tc956xmac_priv *priv,
			    struct tc_taprio_qopt_offload *qopt);
	int (*setup_etf)(struct tc956xmac_priv *priv,
			 struct tc_etf_qopt_offload *qopt);
	int (*query_caps)(struct tc956xmac_priv *priv,
			 struct tc_query_caps_base *base);
};

#define tc956xmac_tc_init(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, init, __args)
#define tc956xmac_tc_setup_cls_u32(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, setup_cls_u32, __args)
#ifdef TC956X_SRIOV_PF
#define tc956xmac_tc_setup_cbs(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, setup_cbs, __args)
#elif (defined TC956X_SRIOV_VF)
#define tc956xmac_tc_setup_cbs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx_wrapper, setup_cbs, __args)
#endif
#define tc956xmac_tc_setup_cls(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, setup_cls, __args)
#define tc956xmac_tc_setup_taprio(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, setup_taprio, __args)
#define tc956xmac_tc_setup_etf(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, setup_etf, __args)
#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 2, 16))
#define tc956xmac_tc_setup_query_cap(__priv, __args...) \
	tc956xmac_do_callback(__priv, tc, query_caps, __args)
#endif

struct tc956xmac_counters;

struct tc956xmac_mmc_ops {
	void (*ctrl)(struct tc956xmac_priv *priv, void __iomem *ioaddr, unsigned int mode);
	void (*intr_all_mask)(struct tc956xmac_priv *priv, void __iomem *ioaddr);
	void (*read)(struct tc956xmac_priv *priv, void __iomem *ioaddr, struct tc956xmac_counters *mmc);
};

#define tc956xmac_mmc_ctrl(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mmc, ctrl, __args)
#define tc956xmac_mmc_intr_all_mask(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mmc, intr_all_mask, __args)
#define tc956xmac_mmc_read(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mmc, read, __args)

#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
struct mac_rsc_mng_ops {
#ifdef TC956X_SRIOV_VF
	int (*init)(struct tc956xmac_priv *priv, struct net_device *dev);
#endif
	int (*get_fn_id)(struct tc956xmac_priv *priv, void __iomem *reg_pci_bridge_config_addr, struct fn_id *fn_id_info);
	/*rscs[4] = {PFx_DMA_bit_pattern, VF0_DMA_bit_pattern, VF1_DMA_bit_pattern, VF2_DMA_bit_pattern}*/
	void (*set_rscs)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *rscs);
	/*resource allocated rscs - bit pattern [DMA7 bit-7] ... ... ... [DMA0 bit-0]*/
	void (*get_rscs)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *rscs);
};
#endif

#ifdef TC956X_SRIOV_VF
#define tc956xmac_rsc_mng_init(__priv, __args...) \
	tc956xmac_do_callback(__priv, rsc, init, __args)
#endif
#define tc956xmac_rsc_mng_get_fn_id(__priv, __args...) \
	tc956xmac_do_callback(__priv, rsc, get_fn_id, __args)
#define tc956xmac_rsc_mng_set_rscs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, rsc, set_rscs, __args)
#define tc956xmac_rsc_mng_get_rscs(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, rsc, get_rscs, __args)

#ifdef TC956X_SRIOV_VF
/* Specific mailbox helpers */
struct tc956xmac_mbx_wrapper_ops {
	void (*dma_tx_mode)(struct tc956xmac_priv *priv, int mode, u32 channel,
			    int fifosz, u8 qmode);
	void (*get_umac_addr)(struct tc956xmac_priv *priv, unsigned char *addr,
			      unsigned int reg_n);
	int (*set_umac_addr)(struct tc956xmac_priv *priv, unsigned char *addr,
			      unsigned int reg_n);
	/* Set MTL TX queues weight */
	void (*set_mtl_tx_queue_weight)(struct tc956xmac_priv *priv,
					u32 weight, u32 traffic_class);
	/* Configure AV Algorithm */
	void (*config_cbs)(struct tc956xmac_priv *priv, u32 send_slope,
			   u32 idle_slope, u32 high_credit, u32 low_credit,
			   u32 queue);
	int (*setup_cbs)(struct tc956xmac_priv *priv, struct tc_cbs_qopt_offload *qopt);
	/* TX Queues Priority */
	void (*tx_queue_prio)(struct tc956xmac_priv *priv, u32 prio, u32 queue);
	/* Get PF link status */
	void (*get_link_status)(struct tc956xmac_priv *priv, u32 *link_status,
				u32 *speed, u32 *duplex);
	/* PHY Link state change from PF */
	int (*phy_link)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	int (*setup_mbx_etf)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	/* Get driver Capabilities from PF */
	void (*get_drv_cap)(struct tc956xmac_priv *priv, struct tc956xmac_priv *priv1);
	void (*reset_eee_mode)(struct tc956xmac_priv *priv, struct mac_device_info *hw);
	void (*vf_reset)(struct tc956xmac_priv *priv, u8 vf_status);
	/* Rx CRC state update from PF */
	int (*rx_crc)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	int (*rx_dma_ch_tlptr)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	int (*rx_dma_err)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	/* Rx checksum state update from PF */
	int (*rx_csum)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	int (*pf_flr)(struct tc956xmac_priv *priv, u8 *msg_buf, u8 *ack_msg);
	int (*get_cbs)(struct tc956xmac_priv *priv, void __user *data);
	int (*set_cbs)(struct tc956xmac_priv *priv, void __user *data);
	int (*get_est)(struct tc956xmac_priv *priv,	void __user *data);
	int (*set_est)(struct tc956xmac_priv *priv,	void __user *data);
	int (*get_rxp)(struct tc956xmac_priv *priv,	void __user *data);
	int (*set_rxp)(struct tc956xmac_priv *priv,	void __user *data);
	int (*get_fpe)(struct tc956xmac_priv *priv, void __user *data);
	int (*set_fpe)(struct tc956xmac_priv *priv, void __user *data);
	int (*get_speed)(struct tc956xmac_priv *priv, void __user *data);
	int (*reg_wr)(struct tc956xmac_priv *priv, void __user *data);
	int (*get_pause_param)(struct tc956xmac_priv *priv, struct ethtool_pauseparam *pause);
	int (*get_eee)(struct tc956xmac_priv *priv,  struct ethtool_eee *edata);
	int (*get_ts_info)(struct tc956xmac_priv *priv, struct ethtool_ts_info *info);
	int (*add_mac)(struct tc956xmac_priv *priv,  const u8 *mac);
	void (*delete_mac)(struct tc956xmac_priv *priv, const u8 *mac);
	void (*delete_vlan)(struct tc956xmac_priv *priv, u16 vid);
	void (*add_vlan)(struct tc956xmac_priv *priv, u16 vid);
};
#endif

#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
struct mac_mbx_ops {
	void (*init)(struct tc956xmac_priv *priv, void *data);
	int (*read)(struct tc956xmac_priv *priv, u8 *msg_buff,
		enum mbx_msg_fns msg_src, struct fn_id *fn_id_info);
	int (*write)(struct tc956xmac_priv *priv, u8 *msg_buff,
		enum mbx_msg_fns msg_dst, struct fn_id *fn_id_info);
	void (*send_ack)(struct tc956xmac_priv *priv, u8 *msg_buff,
		enum mbx_msg_fns msg_dst, struct fn_id *fn_id_info);
	/* normally interrupt method is used.
	 * But for ACK/NACK checking this can be used.
	 */
	int (*poll_for_ack)(struct tc956xmac_priv *priv, enum mbx_msg_fns msg_src);
};

#define tc956xmac_mbx_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx, init, __args)
#define tc956xmac_mbx_read(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx, read, __args)
#define tc956xmac_mbx_write(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx, write, __args)
#define tc956xmac_mbx_send_ack(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, mbx, send_ack, __args)
#define tc956xmac_mbx_poll_for_ack(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx, poll_for_ack, __args)
#endif /* #ifdef TC956X_SRIOV_PF/VF */

#ifdef TC956X_SRIOV_PF

struct tc956x_msi_ops {
	void (*init)(struct tc956xmac_priv *priv, struct net_device *dev);
	void (*interrupt_en)(struct tc956xmac_priv *priv, struct net_device *dev, u32 en);
	void (*interrupt_clr)(struct tc956xmac_priv *priv, struct net_device *dev, u32 vector);
};

#define tc956x_msi_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, init, __args)
#define tc956x_msi_intr_en(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, interrupt_en, __args)
#define tc956x_msi_intr_clr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, interrupt_clr, __args)

/* Specific mailbox helpers */
struct tc956x_mbx_wrapper_ops {
	void (*phy_link)(struct tc956xmac_priv *priv);
	int (*set_dma_tx_mode)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*set_mtl_tx_queue_weight)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*config_cbs)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*setup_cbs)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*setup_mbx_etf)(struct tc956xmac_priv *priv, u32 ch, u8 vf);
	int (*tx_queue_prio)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*vf_get_link_status)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	void (*rx_crc)(struct tc956xmac_priv *priv);
	void (*rx_csum)(struct tc956xmac_priv *priv);
	void (*pf_flr)(struct tc956xmac_priv *priv);
	int (*reset_eee_mode)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*get_umac_addr)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*set_umac_addr)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*vf_reset)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*get_drv_cap)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff);
	int (*vf_ioctl)(struct tc956xmac_priv *priv, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*vf_ethtool)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *mbx_buff, u8 *ack_buff);
	int (*add_mac)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*delete_mac)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*add_vlan)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*delete_vlan)(struct tc956xmac_priv *priv, struct net_device *dev, u8 *mbx_buff, u8 *ack_buff, u8 vf_no);
	int (*rx_dma_ch_tlptr)(struct tc956xmac_priv *priv, u32 ch, u8 vf_no);
	int (*rx_dma_err)(struct tc956xmac_priv *priv, u8 vf_no);
};

#define tc956x_mbx_wrap_phy_link(__priv) \
	tc956xmac_do_void_no_param_callback(__priv, mbx_wrapper, phy_link)
#define tc956x_mbx_wrap_set_dma_tx_mode(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, set_dma_tx_mode, __args)
#define tc956x_mbx_wrap_set_mtl_tx_queue_weight(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, set_mtl_tx_queue_weight, __args)
#define tc956x_mbx_wrap_config_cbs(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, config_cbs, __args)
#define tc956x_mbx_wrap_setup_cbs(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, setup_cbs, __args)
#define tc956x_mbx_wrap_setup_etf(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, setup_mbx_etf, __args)
#define tc956x_mbx_wrap_tx_queue_prior(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, tx_queue_prio, __args)
#define tc956x_mbx_wrap_get_link_status(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, vf_get_link_status, __args)
#define tc956x_mbx_wrap_set_rx_crc(__priv, __args...) \
	tc956xmac_do_void_no_param_callback(__priv, mbx_wrapper, rx_crc)
#define tc956x_mbx_wrap_set_rx_csum(__priv, __args...) \
	tc956xmac_do_void_no_param_callback(__priv, mbx_wrapper, rx_csum)
#define tc956x_mbx_wrap_pf_flr(__priv, __args...) \
	tc956xmac_do_void_no_param_callback(__priv, mbx_wrapper, pf_flr)
#define tc956x_mbx_wrap_rx_dma_ch_tlptr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_dma_ch_tlptr, __args)
#define tc956x_mbx_wrap_rx_dma_err(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, rx_dma_err, __args)
#define tc956x_mbx_wrap_reset_eee_mode(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, reset_eee_mode, __args)
#define tc956x_mbx_wrap_get_umac_addr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, get_umac_addr, __args)
#define tc956x_mbx_wrap_set_umac_addr(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, set_umac_addr, __args)
#define tc956x_mbx_wrap_get_drv_cap(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, get_drv_cap, __args)
#define tc956x_mbx_wrap_vf_reset(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, vf_reset, __args)
#define tc956xmac_mbx_ioctl_interface(__priv, __args...) \
	tc956xmac_do_callback(__priv, mbx_wrapper, vf_ioctl, __args)
#define tc956xmac_mbx_ethtool_interface(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, vf_ethtool, __args)

#define tc956xmac_mbx_add_mac(__priv, __args...) \
		tc956xmac_do_callback(__priv, mbx_wrapper, add_mac, __args)
#define tc956xmac_mbx_delete_mac(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, delete_mac, __args)
#define tc956xmac_mbx_add_vlan(__priv, __args...) \
			tc956xmac_do_callback(__priv, mbx_wrapper, add_vlan, __args)
#define tc956xmac_mbx_delete_vlan(__priv, __args...) \
				tc956xmac_do_callback(__priv, mbx_wrapper, delete_vlan, __args)

#endif /* TC956X_SRIOV_PF */

#ifdef TC956X
/*PMA module*/
struct tc956xmac_pma_ops {
	int (*init)(struct tc956xmac_priv *priv, void __iomem *pmaaddr);
};

#define tc956x_pma_setup(__priv, __args...) \
	tc956xmac_do_callback(__priv, pma, init, __args)

#endif

struct tc956xmac_regs_off {
	u32 ptp_off;
	u32 mmc_off;
#ifdef TC956X
	u32 xpcs_off;
	u32 pma_off;
#endif
};

#ifdef TC956X_SRIOV_VF
struct tc956x_msi_ops {
	void (*init)(struct tc956xmac_priv *priv, struct net_device *dev, struct fn_id *fn_id_info);
	void (*interrupt_en)(struct tc956xmac_priv *priv, struct net_device *dev, u32 en, struct fn_id *fn_id_info);
	void (*interrupt_clr)(struct tc956xmac_priv *priv, struct net_device *dev, u32 vector, struct fn_id *fn_id_info);
};

#define tc956x_msi_init(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, init, __args)
#define tc956x_msi_intr_en(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, interrupt_en, __args)
#define tc956x_msi_intr_clr(__priv, __args...) \
	tc956xmac_do_void_callback(__priv, msi, interrupt_clr, __args)
#endif

extern const struct tc956xmac_ops dwmac100_ops;
extern const struct tc956xmac_dma_ops dwmac100_dma_ops;
extern const struct tc956xmac_ops dwmac1000_ops;
extern const struct tc956xmac_dma_ops dwmac1000_dma_ops;
extern const struct tc956xmac_ops dwmac4_ops;
extern const struct tc956xmac_dma_ops dwmac4_dma_ops;
extern const struct tc956xmac_ops dwmac410_ops;
extern const struct tc956xmac_dma_ops dwmac410_dma_ops;
extern const struct tc956xmac_ops dwmac510_ops;
extern const struct tc956xmac_tc_ops dwmac510_tc_ops;
extern const struct tc956xmac_ops dwxgmac210_ops;
extern const struct tc956xmac_dma_ops dwxgmac210_dma_ops;
extern const struct tc956xmac_desc_ops dwxgmac210_desc_ops;
extern const struct tc956xmac_mmc_ops dwmac_mmc_ops;
extern const struct tc956xmac_mmc_ops dwxgmac_mmc_ops;
#ifdef TC956X
extern const struct tc956xmac_pma_ops tc956x_pma_ops;
#endif
#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
extern const struct tc956x_msi_ops tc956x_msigen_ops;
extern const struct mac_rsc_mng_ops tc956xmac_rsc_mng_ops;
extern const struct mac_mbx_ops tc956xmac_mbx_ops;
#endif

#ifdef TC956X_SRIOV_PF
extern const struct tc956x_mbx_wrapper_ops tc956xmac_mbx_wrapper_ops;
#elif defined TC956X_SRIOV_VF
extern const struct tc956xmac_mbx_wrapper_ops tc956xmac_mbx_wrapper_ops;
#endif

#define GMAC_VERSION		(MAC_OFFSET + 0x00000020)	/* GMAC CORE Version */
#define GMAC4_VERSION		(MAC_OFFSET + 0x00000110)	/* GMAC4+ CORE Version */

int tc956xmac_hwif_init(struct tc956xmac_priv *priv);

#endif /* __TC956XMAC_HWIF_H__ */
