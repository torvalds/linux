/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Userspace interface for AMD Dynamic Boost Control (DBC)
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#ifndef __PSP_DBC_USER_H__
#define __PSP_DBC_USER_H__

#include <linux/types.h>

/**
 * DOC: AMD Dynamic Boost Control (DBC) interface
 */

#define DBC_NONCE_SIZE		16
#define DBC_SIG_SIZE		32

/**
 * struct dbc_user_nonce - Nonce exchange structure (input/output).
 * @auth_needed: Whether the PSP should authenticate this request (input).
 *               0: no authentication, PSP will return single use nonce.
 *               1: authentication: PSP will return multi-use nonce.
 * @nonce:       8 byte value used for future authentication (output).
 * @signature:   Optional 32 byte signature created by software using a
 *               previous nonce (input).
 */
struct dbc_user_nonce {
	__u32	auth_needed;
	__u8	nonce[DBC_NONCE_SIZE];
	__u8	signature[DBC_SIG_SIZE];
} __packed;

/**
 * Dynamic Boost Control (DBC) IOC
 *
 * possible return codes for all DBC IOCTLs:
 *  0:          success
 *  -EINVAL:    invalid input
 *  -E2BIG:     excess data passed
 *  -EFAULT:    failed to copy to/from userspace
 *  -EBUSY:     mailbox in recovery or in use
 *  -ENODEV:    driver not bound with PSP device
 *  -EACCES:    request isn't authorized
 *  -EINVAL:    invalid parameter
 *  -ETIMEDOUT: request timed out
 *  -EAGAIN:    invalid request for state machine
 *  -ENOENT:    not implemented
 *  -ENFILE:    overflow
 *  -EPERM:     invalid signature
 *  -EIO:       unknown error
 */
#define DBC_IOC_TYPE	'D'

/**
 * DBCIOCNONCE - Fetch a nonce from the PSP for authenticating commands.
 *               If a nonce is fetched without authentication it can only
 *               be utilized for one command.
 *               If a nonce is fetched with authentication it can be used
 *               for multiple requests.
 */
#define DBCIOCNONCE	_IOWR(DBC_IOC_TYPE, 0x1, struct dbc_user_nonce)

#endif /* __PSP_DBC_USER_H__ */
