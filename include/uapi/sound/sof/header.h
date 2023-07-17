/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_UAPI_SOUND_SOF_USER_HEADER_H__
#define __INCLUDE_UAPI_SOUND_SOF_USER_HEADER_H__

#include <linux/types.h>

/**
 * struct sof_abi_hdr - Header for all non IPC ABI data.
 * @magic: Magic number for validation
 *	   for IPC3 data: 0x00464F53 ('S', 'O', 'F', '\0')
 *	   for IPC4 data: 0x34464F53 ('S', 'O', 'F', '4')
 * @type: module specific parameter
 *	  for IPC3: Component specific type
 *	  for IPC4: parameter ID (param_id) of the data
 * @size: The size in bytes of the data, excluding this struct
 * @abi: SOF ABI version. The version is valid in scope of the 'magic', IPC3 and
 *	 IPC4 ABI version numbers have no relationship.
 * @reserved: Reserved for future use
 * @data: Component data - opaque to core
 *
 * Identifies data type, size and ABI.
 * Used by any bespoke component data structures or binary blobs.
 */
struct sof_abi_hdr {
	__u32 magic;
	__u32 type;
	__u32 size;
	__u32 abi;
	__u32 reserved[4];
	__u32 data[];
}  __packed;

#define SOF_MANIFEST_DATA_TYPE_NHLT 1

/**
 * struct sof_manifest_tlv - SOF manifest TLV data
 * @type: type of data
 * @size: data size (not including the size of this struct)
 * @data: payload data
 */
struct sof_manifest_tlv {
	__le32 type;
	__le32 size;
	__u8 data[];
};

/**
 * struct sof_manifest - SOF topology manifest
 * @abi_major: Major ABI version
 * @abi_minor: Minor ABI version
 * @abi_patch: ABI patch
 * @count: count of tlv items
 * @items: consecutive variable size tlv items
 */
struct sof_manifest {
	__le16 abi_major;
	__le16 abi_minor;
	__le16 abi_patch;
	__le16 count;
	struct sof_manifest_tlv items[];
};

#endif
