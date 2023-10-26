/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Rockchip mpp driver
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_MPP_H
#define _UAPI_RK_MPP_H

#include <linux/types.h>

/* Use 'v' as magic number */
#define MPP_IOC_MAGIC			'v'

#define MPP_IOC_CFG_V1			_IOW(MPP_IOC_MAGIC, 1, unsigned int)
#define MPP_IOC_CFG_V2			_IOW(MPP_IOC_MAGIC, 2, unsigned int)

/**
 * Command type: keep the same as user space
 */
enum MPP_DEV_COMMAND_TYPE {
	MPP_CMD_QUERY_BASE		= 0,
	MPP_CMD_QUERY_HW_SUPPORT	= MPP_CMD_QUERY_BASE + 0,
	MPP_CMD_QUERY_HW_ID		= MPP_CMD_QUERY_BASE + 1,
	MPP_CMD_QUERY_CMD_SUPPORT	= MPP_CMD_QUERY_BASE + 2,
	MPP_CMD_QUERY_BUTT,

	MPP_CMD_INIT_BASE		= 0x100,
	MPP_CMD_INIT_CLIENT_TYPE	= MPP_CMD_INIT_BASE + 0,
	MPP_CMD_INIT_DRIVER_DATA	= MPP_CMD_INIT_BASE + 1,
	MPP_CMD_INIT_TRANS_TABLE	= MPP_CMD_INIT_BASE + 2,
	MPP_CMD_INIT_BUTT,

	MPP_CMD_SEND_BASE		= 0x200,
	MPP_CMD_SET_REG_WRITE		= MPP_CMD_SEND_BASE + 0,
	MPP_CMD_SET_REG_READ		= MPP_CMD_SEND_BASE + 1,
	MPP_CMD_SET_REG_ADDR_OFFSET	= MPP_CMD_SEND_BASE + 2,
	MPP_CMD_SET_RCB_INFO		= MPP_CMD_SEND_BASE + 3,
	MPP_CMD_SET_SESSION_FD		= MPP_CMD_SEND_BASE + 4,
	MPP_CMD_SEND_BUTT,

	MPP_CMD_POLL_BASE		= 0x300,
	MPP_CMD_POLL_HW_FINISH		= MPP_CMD_POLL_BASE + 0,
	MPP_CMD_POLL_HW_IRQ		= MPP_CMD_POLL_BASE + 1,
	MPP_CMD_POLL_BUTT,

	MPP_CMD_CONTROL_BASE		= 0x400,
	MPP_CMD_RESET_SESSION		= MPP_CMD_CONTROL_BASE + 0,
	MPP_CMD_TRANS_FD_TO_IOVA	= MPP_CMD_CONTROL_BASE + 1,
	MPP_CMD_RELEASE_FD		= MPP_CMD_CONTROL_BASE + 2,
	MPP_CMD_SEND_CODEC_INFO		= MPP_CMD_CONTROL_BASE + 3,
	MPP_CMD_CONTROL_BUTT,

	MPP_CMD_BUTT,
};

/* define flags for mpp_request */
#define MPP_FLAGS_MULTI_MSG		(0x00000001)
#define MPP_FLAGS_LAST_MSG		(0x00000002)
#define MPP_FLAGS_REG_FD_NO_TRANS	(0x00000004)
#define MPP_FLAGS_SCL_FD_NO_TRANS	(0x00000008)
#define MPP_FLAGS_REG_NO_OFFSET		(0x00000010)
#define MPP_FLAGS_SECURE_MODE		(0x00010000)

/* data common struct for parse out */
struct mpp_request {
	__u32 cmd;
	__u32 flags;
	__u32 size;
	__u32 offset;
	void __user *data;
};

#define MPP_BAT_MSG_DONE		(0x00000001)

struct mpp_bat_msg {
	__u64 flag;
	__u32 fd;
	__s32 ret;
};

#endif /* _UAPI_RK_MPP_H */
