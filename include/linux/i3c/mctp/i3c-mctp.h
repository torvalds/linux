/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Intel Corporation.*/

#ifndef I3C_MCTP_H
#define I3C_MCTP_H

#define I3C_MCTP_PACKET_SIZE	68
#define I3C_MCTP_PAYLOAD_SIZE	64
#define I3C_MCTP_HDR_SIZE	4

/* PECI MCTP Intel VDM definitions */
#define MCTP_MSG_TYPE_VDM_PCI		0x7E
#define MCTP_VDM_PCI_INTEL_VENDOR_ID	0x8086
#define MCTP_VDM_PCI_INTEL_PECI		0x2

/* MCTP message header offsets */
#define MCTP_MSG_HDR_MSG_TYPE_OFFSET	0
#define MCTP_MSG_HDR_VENDOR_OFFSET	1
#define MCTP_MSG_HDR_OPCODE_OFFSET	4

struct i3c_mctp_client;

struct mctp_protocol_hdr {
	u8 ver;
	u8 dest;
	u8 src;
	u8 flags_seq_tag;
} __packed;

struct i3c_mctp_packet_data {
	u8 protocol_hdr[I3C_MCTP_HDR_SIZE];
	u8 payload[I3C_MCTP_PAYLOAD_SIZE];
};

struct i3c_mctp_packet {
	struct i3c_mctp_packet_data data;
	u32 size;
};

void *i3c_mctp_packet_alloc(gfp_t flags);
void i3c_mctp_packet_free(void *packet);

int i3c_mctp_get_eid(struct i3c_mctp_client *client, u8 domain_id, u8 *eid);
int i3c_mctp_send_packet(struct i3c_device *i3c, struct i3c_mctp_packet *tx_packet);
struct i3c_mctp_packet *i3c_mctp_receive_packet(struct i3c_mctp_client *client,
						unsigned long timeout);
struct i3c_mctp_client *i3c_mctp_add_peci_client(struct i3c_device *i3c);
void i3c_mctp_remove_peci_client(struct i3c_mctp_client *client);

#endif /* I3C_MCTP_H */
