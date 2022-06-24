/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_ESOC_CTRL_H_
#define _UAPI_ESOC_CTRL_H_

#include <linux/types.h>

enum esoc_client_hook_prio {
	ESOC_MHI_HOOK,
	ESOC_MAX_HOOKS
};

struct esoc_link_data {
	enum esoc_client_hook_prio prio;
	__u64 link_id;
};

#define ESOC_CODE		0xCC

#define ESOC_CMD_EXE		_IOW(ESOC_CODE, 1, unsigned int)
#define ESOC_WAIT_FOR_REQ	_IOR(ESOC_CODE, 2, unsigned int)
#define ESOC_NOTIFY		_IOW(ESOC_CODE, 3, unsigned int)
#define ESOC_GET_STATUS		_IOR(ESOC_CODE, 4, unsigned int)
#define ESOC_GET_ERR_FATAL	_IOR(ESOC_CODE, 5, unsigned int)
#define ESOC_WAIT_FOR_CRASH	_IOR(ESOC_CODE, 6, unsigned int)
#define ESOC_REG_REQ_ENG	_IO(ESOC_CODE, 7)
#define ESOC_REG_CMD_ENG	_IO(ESOC_CODE, 8)
#define ESOC_GET_LINK_ID	_IOWR(ESOC_CODE, 9, struct esoc_link_data)
#define ESOC_SET_BOOT_FAIL_ACT	_IOW(ESOC_CODE, 10, unsigned int)
#define ESOC_SET_N_PON_TRIES	_IOW(ESOC_CODE, 11, unsigned int)

#define ESOC_REQ_SEND_SHUTDOWN	ESOC_REQ_SEND_SHUTDOWN
#define ESOC_REQ_CRASH_SHUTDOWN ESOC_REQ_CRASH_SHUTDOWN
#define ESOC_PON_RETRY		ESOC_PON_RETRY
#define ESOC_BOOT_FAIL_ACTION

enum esoc_boot_fail_action {
	BOOT_FAIL_ACTION_RETRY,
	BOOT_FAIL_ACTION_COLD_RESET,
	BOOT_FAIL_ACTION_SHUTDOWN,
	BOOT_FAIL_ACTION_PANIC,
	BOOT_FAIL_ACTION_NOP,
	BOOT_FAIL_ACTION_S3_RESET,
	BOOT_FAIL_ACTION_LAST,
};

enum esoc_evt {
	ESOC_RUN_STATE = 0x1,
	ESOC_UNEXPECTED_RESET,
	ESOC_ERR_FATAL,
	ESOC_IN_DEBUG,
	ESOC_REQ_ENG_ON,
	ESOC_REQ_ENG_OFF,
	ESOC_CMD_ENG_ON,
	ESOC_CMD_ENG_OFF,
	ESOC_INVALID_STATE,
	ESOC_RETRY_PON_EVT,
	ESOC_BOOT_STATE,
};

enum esoc_cmd {
	ESOC_PWR_ON = 1,
	ESOC_PWR_OFF,
	ESOC_FORCE_PWR_OFF,
	ESOC_RESET,
	ESOC_PREPARE_DEBUG,
	ESOC_EXE_DEBUG,
	ESOC_EXIT_DEBUG,
};

enum esoc_notify {
	ESOC_IMG_XFER_DONE = 1,
	ESOC_BOOT_DONE,
	ESOC_BOOT_FAIL,
	ESOC_IMG_XFER_RETRY,
	ESOC_IMG_XFER_FAIL,
	ESOC_UPGRADE_AVAILABLE,
	ESOC_DEBUG_DONE,
	ESOC_DEBUG_FAIL,
	ESOC_PRIMARY_CRASH,
	ESOC_PRIMARY_REBOOT,
	ESOC_PON_RETRY,
};

enum esoc_req {
	ESOC_REQ_IMG = 1,
	ESOC_REQ_DEBUG,
	ESOC_REQ_SHUTDOWN,
	ESOC_REQ_SEND_SHUTDOWN,
	ESOC_REQ_CRASH_SHUTDOWN,
};

#endif
