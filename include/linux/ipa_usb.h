/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IPA_USB_H_
#define _IPA_USB_H_

#include <linux/if_ether.h>
#include <linux/types.h>

enum ipa_usb_teth_prot {
	IPA_USB_RNDIS = 0,
	IPA_USB_ECM = 1,
	IPA_USB_RMNET = 2,
	IPA_USB_MBIM = 3,
	IPA_USB_DIAG = 4,
	IPA_USB_RMNET_CV2X = 5,
	IPA_USB_MAX_TETH_PROT_SIZE
};

enum teth_bridge_params {
	IPA_TETH_BRIDGE_1 = 0,
	IPA_TETH_BRIDGE_2 = 1,
	IPA_TETH_BRIDGE_MAX
};

/**
 * ipa_usb_teth_params - parameters for RDNIS/ECM initialization API
 *
 * @host_ethaddr:        host Ethernet address in network order
 * @device_ethaddr:      device Ethernet address in network order
 */
struct ipa_usb_teth_params {
	u8 host_ethaddr[ETH_ALEN];
	u8 device_ethaddr[ETH_ALEN];
};

enum ipa_usb_notify_event {
	IPA_USB_DEVICE_READY,
	IPA_USB_REMOTE_WAKEUP,
	IPA_USB_SUSPEND_COMPLETED
};

enum ipa_usb_max_usb_packet_size {
	IPA_USB_FULL_SPEED_64B = 64,
	IPA_USB_HIGH_SPEED_512B = 512,
	IPA_USB_SUPER_SPEED_1024B = 1024
};

enum ipa_usb_gsi_chan_dir {
	CHAN_DIR_FROM_GSI = 0x0,
	CHAN_DIR_TO_GSI = 0x1
};

/**
 * ipa_usb_teth_prot_params - parameters for connecting RNDIS
 *
 * @max_xfer_size_bytes_to_dev:   max size of UL packets in bytes
 * @max_packet_number_to_dev:     max number of UL aggregated packets
 * @max_xfer_size_bytes_to_host:  max size of DL packets in bytes
 *
 */
struct ipa_usb_teth_prot_params {
	u32 max_xfer_size_bytes_to_dev;
	u32 max_packet_number_to_dev;
	u32 max_xfer_size_bytes_to_host;
};

/**
 * ipa_usb_xdci_connect_params - parameters required to start IN, OUT
 * channels, and connect RNDIS/ECM/teth_bridge
 *
 * @max_pkt_size:          USB speed (full/high/super/super-speed plus)
 * @ipa_to_usb_xferrscidx: Transfer Resource Index (XferRscIdx) for IN channel.
 *                         The hardware-assigned transfer resource index for the
 *                         transfer, which was returned in response to the
 *                         Start Transfer command. This field is used for
 *                         "Update Transfer" command.
 *                         Should be 0 =< ipa_to_usb_xferrscidx <= 127.
 * @ipa_to_usb_xferrscidx_valid: true if xferRscIdx should be updated for IN
 *                         channel
 * @usb_to_ipa_xferrscidx: Transfer Resource Index (XferRscIdx) for OUT channel
 *                         Should be 0 =< usb_to_ipa_xferrscidx <= 127.
 * @usb_to_ipa_xferrscidx_valid: true if xferRscIdx should be updated for OUT
 *                         channel
 * @teth_prot:             tethering protocol
 * @teth_prot_params:      parameters for connecting the tethering protocol.
 * @max_supported_bandwidth_mbps: maximum bandwidth need of the client in Mbps
 */
struct ipa_usb_xdci_connect_params {
	enum ipa_usb_max_usb_packet_size max_pkt_size;
	u8 ipa_to_usb_xferrscidx;
	bool ipa_to_usb_xferrscidx_valid;
	u8 usb_to_ipa_xferrscidx;
	bool usb_to_ipa_xferrscidx_valid;
	enum ipa_usb_teth_prot teth_prot;
	struct ipa_usb_teth_prot_params teth_prot_params;
	u32 max_supported_bandwidth_mbps;
};

/**
 * ipa_usb_xdci_chan_scratch - xDCI protocol SW config area of
 * channel scratch
 *
 * @last_trb_addr_iova:  Address (iova LSB - based on alignment restrictions) of
 *                       last TRB in queue. Used to identify roll over case
 * @const_buffer_size:   TRB buffer size in KB (similar to IPA aggregation
 *                       configuration). Must be aligned to max USB Packet Size.
 *                       Should be 1 <= const_buffer_size <= 31.
 * @depcmd_low_addr:     Used to generate "Update Transfer" command
 * @depcmd_hi_addr:      Used to generate "Update Transfer" command.
 */
struct ipa_usb_xdci_chan_scratch {
	u16 last_trb_addr_iova;
	u8 const_buffer_size;
	u32 depcmd_low_addr;
	u8 depcmd_hi_addr;
};

/**
 * ipa_usb_xdci_chan_params - xDCI channel related properties
 *
 * @keep_ipa_awake:      when true, IPA will not be clock gated
 * @teth_prot:           tethering protocol for which the channel is created
 * @gevntcount_low_addr: GEVNCOUNT low address for event scratch
 * @gevntcount_hi_addr:  GEVNCOUNT high address for event scratch
 * @dir:                 channel direction
 * @xfer_ring_len:       length of transfer ring in bytes (must be integral
 *                       multiple of transfer element size - 16B for xDCI)
 * @xfer_scratch:        parameters for xDCI channel scratch
 * @xfer_ring_base_addr_iova: IO virtual address mapped to pysical base address
 * @data_buff_base_len:  length of data buffer allocated by USB driver
 * @data_buff_base_addr_iova:  IO virtual address mapped to pysical base address
 * @sgt_xfer_rings:      Scatter table for Xfer rings,contains valid non NULL
 *			 value
 *                       when USB S1-SMMU enabed, else NULL.
 * @sgt_data_buff:       Scatter table for data buffs,contains valid non NULL
 *			 value
 *                       when USB S1-SMMU enabed, else NULL.
 *
 */
struct ipa_usb_xdci_chan_params {
	/* IPA EP params */
	bool keep_ipa_awake;
	enum ipa_usb_teth_prot teth_prot;
	/* event ring params */
	u32 gevntcount_low_addr;
	u8 gevntcount_hi_addr;
	/* transfer ring params */
	enum ipa_usb_gsi_chan_dir dir;
	u16 xfer_ring_len;
	struct ipa_usb_xdci_chan_scratch xfer_scratch;
	u64 xfer_ring_base_addr_iova;
	u32 data_buff_base_len;
	u64 data_buff_base_addr_iova;
	struct sg_table *sgt_xfer_rings;
	struct sg_table *sgt_data_buff;
};

/**
 * ipa_usb_chan_out_params - out parameters for channel request
 *
 * @clnt_hdl:            opaque client handle assigned by IPA to client
 * @db_reg_phs_addr_lsb: Physical address of doorbell register where the 32
 *                       LSBs of the doorbell value should be written
 * @db_reg_phs_addr_msb: Physical address of doorbell register where the 32
 *                       MSBs of the doorbell value should be written
 *
 */
struct ipa_req_chan_out_params {
	u32 clnt_hdl;
	u32 db_reg_phs_addr_lsb;
	u32 db_reg_phs_addr_msb;
};

struct ipa_usb_ops {
	int (*init_teth_prot)(enum ipa_usb_teth_prot teth_prot,
		struct ipa_usb_teth_params *teth_params,
		int (*ipa_usb_notify_cb)(enum ipa_usb_notify_event, void *),
		void *user_data);
	int (*xdci_connect)
		(struct ipa_usb_xdci_chan_params *ul_chan_params,
		struct ipa_usb_xdci_chan_params *dl_chan_params,
		struct ipa_req_chan_out_params *ul_out_params,
		struct ipa_req_chan_out_params *dl_out_params,
		struct ipa_usb_xdci_connect_params *connect_params);
	int (*xdci_disconnect)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot);
	int (*deinit_teth_prot)(enum ipa_usb_teth_prot teth_prot);
	int (*xdci_suspend)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot,
		bool with_remote_wakeup);
	int (*xdci_resume)(u32 ul_clnt_hdl, u32 dl_clnt_hdl,
		enum ipa_usb_teth_prot teth_prot);
	bool (*ipa_usb_is_teth_prot_connected)
		(enum ipa_usb_teth_prot usb_teth_prot);
};

#if IS_ENABLED(CONFIG_USB_F_GSI)
void ipa_ready_callback(void *ops);
#else
static inline void ipa_ready_callback(void *ops)
{ }
#endif

#endif /* _IPA_USB_H_ */
