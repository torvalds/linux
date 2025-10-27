/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *
 */
#ifndef _COMMON_SMB_GLOB_H
#define _COMMON_SMB_GLOB_H

struct smb_version_values {
	char		*version_string;
	__u16		protocol_id;
	__le16		lock_cmd;
	__u32		req_capabilities;
	__u32		max_read_size;
	__u32		max_write_size;
	__u32		max_trans_size;
	__u32		max_credits;
	__u32		large_lock_type;
	__u32		exclusive_lock_type;
	__u32		shared_lock_type;
	__u32		unlock_lock_type;
	size_t		header_preamble_size;
	size_t		header_size;
	size_t		max_header_size;
	size_t		read_rsp_size;
	unsigned int	cap_unix;
	unsigned int	cap_nt_find;
	unsigned int	cap_large_files;
	unsigned int	cap_unicode;
	__u16		signing_enabled;
	__u16		signing_required;
	size_t		create_lease_size;
	size_t		create_durable_size;
	size_t		create_durable_v2_size;
	size_t		create_mxac_size;
	size_t		create_disk_id_size;
	size_t		create_posix_size;
};

static inline void inc_rfc1001_len(void *buf, int count)
{
	be32_add_cpu((__be32 *)buf, count);
}

#define SMB1_VERSION_STRING	"1.0"
#define SMB20_VERSION_STRING    "2.0"
#define SMB21_VERSION_STRING	"2.1"
#define SMBDEFAULT_VERSION_STRING "default"
#define SMB3ANY_VERSION_STRING "3"
#define SMB30_VERSION_STRING	"3.0"
#define SMB302_VERSION_STRING	"3.02"
#define ALT_SMB302_VERSION_STRING "3.0.2"
#define SMB311_VERSION_STRING	"3.1.1"
#define ALT_SMB311_VERSION_STRING "3.11"

#define CIFS_DEFAULT_IOSIZE (1024 * 1024)

#endif	/* _COMMON_SMB_GLOB_H */
