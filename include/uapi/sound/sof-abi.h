/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_UAPI_ABI_H__
#define __INCLUDE_UAPI_ABI_H__

#define SOF_ABI_VERSION		1
#define SOF_ABI_MAGIC		0x00464F53	/* "SOF\0" */

/*
 * Header for all non IPC ABI data. Identifies data type, size and ABI.
 * Used by any bespoke component data structures or binary blobs.
 */

struct sof_abi_hdr {
	uint32_t magic;		/* 'S', 'O', 'F', '\0' */
	uint32_t type;		/* component specific type */
	uint32_t size;		/* size in bytes of data excluding this struct */
	uint32_t abi;		/* SOF ABI version */
	uint32_t comp_abi;	/* component specific ABI version */
	char data[0];
}  __attribute__((packed));

#endif
