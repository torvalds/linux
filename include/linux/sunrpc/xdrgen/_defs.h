/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Oracle and/or its affiliates.
 *
 * This header defines XDR data type primitives specified in
 * Section 4 of RFC 4506, used by RPC programs implemented
 * in the Linux kernel.
 */

#ifndef _SUNRPC_XDRGEN__DEFS_H_
#define _SUNRPC_XDRGEN__DEFS_H_

#define TRUE	(true)
#define FALSE	(false)

typedef struct {
	u32 len;
	unsigned char *data;
} string;

typedef struct {
	u32 len;
	u8 *data;
} opaque;

#endif /* _SUNRPC_XDRGEN__DEFS_H_ */
