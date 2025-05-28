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

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_H__ */
