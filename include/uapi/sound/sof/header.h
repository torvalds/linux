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
	__u32 data[0];		/**< Component data - opaque to core */
}  __packed;

#endif
