/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_H__
#define __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_H__

/* SMB-DIRECT buffer descriptor V1 structure [MS-SMBD] 2.2.3.1 */
struct smbdirect_buffer_descriptor_v1 {
	__le64 offset;
	__le32 token;
	__le32 length;
} __packed;

/*
 * Connection parameters mostly from [MS-SMBD] 3.1.1.1
 *
 * These are setup and negotiated at the beginning of a
 * connection and remain constant unless explicitly changed.
 *
 * Some values are important for the upper layer.
 */
struct smbdirect_socket_parameters {
	__u16 recv_credit_max;
	__u16 send_credit_target;
	__u32 max_send_size;
	__u32 max_fragmented_send_size;
	__u32 max_recv_size;
	__u32 max_fragmented_recv_size;
	__u32 max_read_write_size;
	__u32 keepalive_interval_msec;
	__u32 keepalive_timeout_msec;
} __packed;

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_H__ */
