/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_UAPI_SOUND_SOF_USER_HEADER_H__
#define __INCLUDE_UAPI_SOUND_SOF_USER_HEADER_H__

/*
 * Header for all non IPC ABI data.
 *
 * Identifies data type, size and ABI.
 * Used by any bespoke component data structures or binary blobs.
 */
struct sof_abi_hdr {
	uint32_t magic;		/**< 'S', 'O', 'F', '\0' */
	uint32_t type;		/**< component specific type */
	uint32_t size;		/**< size in bytes of data excl. this struct */
	uint32_t abi;		/**< SOF ABI version */
	uint32_t reserved[4];	/**< reserved for future use */
	uint32_t data[0];	/**< Component data - opaque to core */
}  __packed;

#endif
