/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 */

#ifndef __KSMBD_TRANSPORT_RDMA_H__
#define __KSMBD_TRANSPORT_RDMA_H__

#define SMB_DIRECT_PORT	5445

/* SMB DIRECT negotiation request packet [MS-KSMBD] 2.2.1 */
struct smb_direct_negotiate_req {
	__le16 min_version;
	__le16 max_version;
	__le16 reserved;
	__le16 credits_requested;
	__le32 preferred_send_size;
	__le32 max_receive_size;
	__le32 max_fragmented_size;
} __packed;

/* SMB DIRECT negotiation response packet [MS-KSMBD] 2.2.2 */
struct smb_direct_negotiate_resp {
	__le16 min_version;
	__le16 max_version;
	__le16 negotiated_version;
	__le16 reserved;
	__le16 credits_requested;
	__le16 credits_granted;
	__le32 status;
	__le32 max_readwrite_size;
	__le32 preferred_send_size;
	__le32 max_receive_size;
	__le32 max_fragmented_size;
} __packed;

#define SMB_DIRECT_RESPONSE_REQUESTED 0x0001

/* SMB DIRECT data transfer packet with payload [MS-KSMBD] 2.2.3 */
struct smb_direct_data_transfer {
	__le16 credits_requested;
	__le16 credits_granted;
	__le16 flags;
	__le16 reserved;
	__le32 remaining_data_length;
	__le32 data_offset;
	__le32 data_length;
	__le32 padding;
	__u8 buffer[];
} __packed;

#ifdef CONFIG_SMB_SERVER_SMBDIRECT
int ksmbd_rdma_init(void);
int ksmbd_rdma_destroy(void);
bool ksmbd_rdma_capable_netdev(struct net_device *netdev);
#else
static inline int ksmbd_rdma_init(void) { return 0; }
static inline int ksmbd_rdma_destroy(void) { return 0; }
static inline bool ksmbd_rdma_capable_netdev(struct net_device *netdev) { return false; }
#endif

#endif /* __KSMBD_TRANSPORT_RDMA_H__ */
