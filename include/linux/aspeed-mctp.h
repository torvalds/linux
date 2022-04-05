/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2020 Intel Corporation */

#ifndef __LINUX_ASPEED_MCTP_H
#define __LINUX_ASPEED_MCTP_H

#include <linux/types.h>

struct mctp_client;
struct aspeed_mctp;

struct pcie_transport_hdr {
	u8 fmt_type;
	u8 mbz;
	u8 mbz_attr_len_hi;
	u8 len_lo;
	u16 requester;
	u8 tag;
	u8 code;
	u16 target;
	u16 vendor;
} __packed;

struct mctp_protocol_hdr {
	u8 ver;
	u8 dest;
	u8 src;
	u8 flags_seq_tag;
} __packed;

#define PCIE_VDM_HDR_SIZE 16
#define MCTP_BTU_SIZE 64
/* The MTU of the ASPEED MCTP can be 64/128/256 */
#define ASPEED_MCTP_MTU MCTP_BTU_SIZE
#define PCIE_VDM_DATA_SIZE_DW (ASPEED_MCTP_MTU / 4)
#define PCIE_VDM_HDR_SIZE_DW (PCIE_VDM_HDR_SIZE / 4)

#define PCIE_MCTP_MIN_PACKET_SIZE (PCIE_VDM_HDR_SIZE + 4)

struct mctp_pcie_packet_data_2500 {
	u32 data[32];
};

struct mctp_pcie_packet_data {
	u32 hdr[PCIE_VDM_HDR_SIZE_DW];
	u32 payload[PCIE_VDM_DATA_SIZE_DW];
};

struct mctp_pcie_packet {
	struct mctp_pcie_packet_data data;
	u32 size;
};

/**
 * aspeed_mctp_add_type_handler() - register for the given MCTP message type
 * @client: pointer to the existing mctp_client context
 * @mctp_type: message type code according to DMTF DSP0239 spec.
 * @pci_vendor_id: vendor ID (non-zero if msg_type is Vendor Defined PCI,
 * otherwise it should be set to 0)
 * @vdm_type: vendor defined message type (it should be set to 0 for non-Vendor
 * Defined PCI message type)
 * @vdm_mask: vendor defined message mask (it should be set to 0 for non-Vendor
 * Defined PCI message type)
 *
 * Return:
 * * 0 - success,
 * * -EINVAL - arguments passed are incorrect,
 * * -ENOMEM - cannot alloc a new handler,
 * * -EBUSY - given message has already registered handler.
 */

int aspeed_mctp_add_type_handler(struct mctp_client *client, u8 mctp_type,
				 u16 pci_vendor_id, u16 vdm_type, u16 vdm_mask);

/**
 * aspeed_mctp_create_client() - create mctp_client context
 * @priv pointer to aspeed-mctp context
 *
 * Returns struct mctp_client or NULL.
 */
struct mctp_client *aspeed_mctp_create_client(struct aspeed_mctp *priv);

/**
 * aspeed_mctp_delete_client()- delete mctp_client context
 * @client: pointer to existing mctp_client context
 */
void aspeed_mctp_delete_client(struct mctp_client *client);

/**
 * aspeed_mctp_send_packet() - send mctp_packet
 * @client: pointer to existing mctp_client context
 * @tx_packet: the allocated packet that needs to be send via aspeed-mctp
 *
 * After the function returns success, the packet is no longer owned by the
 * caller, and as such, the caller should not attempt to free it.
 *
 * Return:
 * * 0 - success,
 * * -ENOSPC - failed to send packet due to lack of available space.
 */
int aspeed_mctp_send_packet(struct mctp_client *client,
			    struct mctp_pcie_packet *tx_packet);

/**
 * aspeed_mctp_receive_packet() - receive mctp_packet
 * @client: pointer to existing mctp_client context
 * @timeout: timeout, in jiffies
 *
 * The function will sleep for up to @timeout if no packet is ready to read.
 *
 * After the function returns valid packet, the caller takes its ownership and
 * is responsible for freeing it.
 *
 * Returns struct mctp_pcie_packet from or ERR_PTR in case of error or the
 * @timeout elapsed.
 */
struct mctp_pcie_packet *aspeed_mctp_receive_packet(struct mctp_client *client,
						    unsigned long timeout);

/**
 * aspeed_mctp_flush_rx_queue() - remove all mctp_packets from rx queue
 * @client: pointer to existing mctp_client context
 */
void aspeed_mctp_flush_rx_queue(struct mctp_client *client);

/**
 * aspeed_mctp_get_eid_bdf() - return PCIe address for requested endpoint ID
 * @client: pointer to existing mctp_client context
 * @eid: requested eid
 * @bdf: pointer to store BDF value
 *
 * Return:
 * * 0 - success,
 * * -ENOENT - there is no record for requested endpoint id.
 */
int aspeed_mctp_get_eid_bdf(struct mctp_client *client, u8 eid, u16 *bdf);

/**
 * aspeed_mctp_get_eid() - return EID for requested BDF and domainId.
 * @client: pointer to existing mctp_client context
 * @bdf: requested BDF value
 * @domain_id: requested domainId
 * @eid: pointer to store EID value
 *
 * Return:
 * * 0 - success,
 * * -ENOENT - there is no record for requested bdf/domainId.
 */
int aspeed_mctp_get_eid(struct mctp_client *client, u16 bdf,
			u8 domain_id, u8 *eid);

void *aspeed_mctp_packet_alloc(gfp_t flags);
void aspeed_mctp_packet_free(void *packet);

#endif /* __LINUX_ASPEED_MCTP_H */
