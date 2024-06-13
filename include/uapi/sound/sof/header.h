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

/*
 * Header for all non IPC ABI data.
 *
 * Identifies data type, size and ABI.
 * Used by any bespoke component data structures or binary blobs.
 */
struct sof_abi_hdr {
	__u32 magic;		/**< 'S', 'O', 'F', '\0' */
	__u32 type;		/**< component specific type */
	__u32 size;		/**< size in bytes of data excl. this struct */
	__u32 abi;		/**< SOF ABI version */
	__u32 reserved[4];	/**< reserved for future use */
	__u32 data[];		/**< Component data - opaque to core */
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
