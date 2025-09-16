/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2017 Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_PDU_H__
#define __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_PDU_H__

#define SMBDIRECT_V1 0x0100

/* SMBD negotiation request packet [MS-SMBD] 2.2.1 */
struct smbdirect_negotiate_req {
	__le16 min_version;
	__le16 max_version;
	__le16 reserved;
	__le16 credits_requested;
	__le32 preferred_send_size;
	__le32 max_receive_size;
	__le32 max_fragmented_size;
} __packed;

/* SMBD negotiation response packet [MS-SMBD] 2.2.2 */
struct smbdirect_negotiate_resp {
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

#define SMBDIRECT_DATA_MIN_HDR_SIZE 0x14
#define SMBDIRECT_DATA_OFFSET       0x18

#define SMBDIRECT_FLAG_RESPONSE_REQUESTED 0x0001

/* SMBD data transfer packet with payload [MS-SMBD] 2.2.3 */
struct smbdirect_data_transfer {
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

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_PDU_H__ */
