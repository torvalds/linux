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
#define DBC_UID_SIZE		16

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
 * struct dbc_user_setuid - UID exchange structure (input).
 * @uid:       16 byte value representing software identity
 * @signature: 32 byte signature created by software using a previous nonce
 */
struct dbc_user_setuid {
	__u8	uid[DBC_UID_SIZE];
	__u8	signature[DBC_SIG_SIZE];
} __packed;

/**
 * struct dbc_user_param - Parameter exchange structure (input/output).
 * @msg_index: Message indicating what parameter to set or get (input)
 * @param:     4 byte parameter, units are message specific. (input/output)
 * @signature: 32 byte signature.
 *             - When sending a message this is to be created by software
 *               using a previous nonce (input)
 *             - For interpreting results, this signature is updated by the
 *               PSP to allow software to validate the authenticity of the
 *               results.
 */
struct dbc_user_param {
	__u32	msg_index;
	__u32	param;
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

/**
 * DBCIOCUID - Set the user ID (UID) of a calling process.
 *             The user ID is 8 bytes long. It must be programmed using a
 *             32 byte signature built using the nonce fetched from
 *             DBCIOCNONCE.
 *             The UID can only be set once until the system is rebooted.
 */
#define DBCIOCUID	_IOW(DBC_IOC_TYPE, 0x2, struct dbc_user_setuid)

/**
 * DBCIOCPARAM - Set or get a parameter from the PSP.
 *               This request will only work after DBCIOCUID has successfully
 *               set the UID of the calling process.
 *               Whether the parameter is set or get is controlled by the
 *               message ID in the request.
 *               This command must be sent using a 32 byte signature built
 *               using the nonce fetched from DBCIOCNONCE.
 *               When the command succeeds, the 32 byte signature will be
 *               updated by the PSP for software to authenticate the results.
 */
#define DBCIOCPARAM	_IOWR(DBC_IOC_TYPE, 0x3, struct dbc_user_param)

/**
 * enum dbc_cmd_msg - Messages utilized by DBCIOCPARAM
 * @PARAM_GET_FMAX_CAP:		Get frequency cap (MHz)
 * @PARAM_SET_FMAX_CAP:		Set frequency cap (MHz)
 * @PARAM_GET_PWR_CAP:		Get socket power cap (mW)
 * @PARAM_SET_PWR_CAP:		Set socket power cap (mW)
 * @PARAM_GET_GFX_MODE:		Get graphics mode (0/1)
 * @PARAM_SET_GFX_MODE:		Set graphics mode (0/1)
 * @PARAM_GET_CURR_TEMP:	Get current temperature (degrees C)
 * @PARAM_GET_FMAX_MAX:		Get maximum allowed value for frequency (MHz)
 * @PARAM_GET_FMAX_MIN:		Get minimum allowed value for frequency (MHz)
 * @PARAM_GET_SOC_PWR_MAX:	Get maximum allowed value for SoC power (mw)
 * @PARAM_GET_SOC_PWR_MIN:	Get minimum allowed value for SoC power (mw)
 * @PARAM_GET_SOC_PWR_CUR:	Get current value for SoC Power (mW)
 */
enum dbc_cmd_msg {
	PARAM_GET_FMAX_CAP	= 0x3,
	PARAM_SET_FMAX_CAP	= 0x4,
	PARAM_GET_PWR_CAP	= 0x5,
	PARAM_SET_PWR_CAP	= 0x6,
	PARAM_GET_GFX_MODE	= 0x7,
	PARAM_SET_GFX_MODE	= 0x8,
	PARAM_GET_CURR_TEMP	= 0x9,
	PARAM_GET_FMAX_MAX	= 0xA,
	PARAM_GET_FMAX_MIN	= 0xB,
	PARAM_GET_SOC_PWR_MAX	= 0xC,
	PARAM_GET_SOC_PWR_MIN	= 0xD,
	PARAM_GET_SOC_PWR_CUR	= 0xE,
};

#endif /* __PSP_DBC_USER_H__ */
