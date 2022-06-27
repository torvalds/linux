/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_SPCOM_H_
#define _UAPI_SPCOM_H_

#include <linux/types.h>	/* __u32, bool */
#ifndef BIT
	#define BIT(x) (1 << x)
#endif

#ifndef PAGE_SIZE
	#define PAGE_SIZE 4096
#endif

/**
 * @brief - Secure Processor Communication interface to user space spcomlib.
 *
 * Sending data and control commands by write() file operation.
 * Receiving data by read() file operation.
 * Getting the next request size by read() file operation,
 * with special size SPCOM_GET_NEXT_REQUEST_SIZE.
 */

/*
 * Maximum number of channel between Secure Processor and HLOS.
 * including predefined channels, like "sp_kernel".
 */
#define SPCOM_MAX_CHANNELS	0x20

/* Maximum size (including null) for channel names */
#define SPCOM_CHANNEL_NAME_SIZE	32
/*
 * file read(fd, buf, size) with this size,
 * hints the kernel that user space wants to read the next-req-size.
 * This size is bigger than both SPCOM_MAX_REQUEST_SIZE and
 * SPCOM_MAX_RESPONSE_SIZE , so it is not a valid data size.
 */
#define SPCOM_GET_NEXT_REQUEST_SIZE	(PAGE_SIZE-1)

/* Command Id between spcomlib and spcom driver, on write() */
enum spcom_cmd_id {
	SPCOM_CMD_LOAD_APP        = 0x4C4F4144, /* "LOAD" = 0x4C4F4144 */
	SPCOM_CMD_RESET_SP        = 0x52455354, /* "REST" = 0x52455354 */
	SPCOM_CMD_SEND            = 0x53454E44, /* "SEND" = 0x53454E44 */
	SPCOM_CMD_SEND_MODIFIED   = 0x534E444D, /* "SNDM" = 0x534E444D */
	SPCOM_CMD_LOCK_ION_BUF    = 0x4C4F434B, /* "LOCK" = 0x4C4F434B */
	SPCOM_CMD_UNLOCK_ION_BUF  = 0x554C434B, /* "ULCK" = 0x4C4F434B */
	SPCOM_CMD_FSSR            = 0x46535352, /* "FSSR" = 0x46535352 */
	SPCOM_CMD_CREATE_CHANNEL  = 0x43524554, /* "CRET" = 0x43524554 */

#define SPCOM_CMD_ENABLE_SSR \
	SPCOM_CMD_ENABLE_SSR
	SPCOM_CMD_ENABLE_SSR      = 0x45535352, /* "ESSR" =0x45535352*/

#define SPCOM_CMD_RESTART_SP \
	SPCOM_CMD_RESTART_SP
	SPCOM_CMD_RESTART_SP      = 0x52535452, /* "RSTR" = 0x52535452 */
};

/*
 * @note: Event types that are always implicitly polled:
 * POLLERR=0x08 | POLLHUP=0x10 | POLLNVAL=0x20
 * so bits 3,4,5 can't be used
 */
enum spcom_poll_events {
	SPCOM_POLL_LINK_STATE = BIT(1),
	SPCOM_POLL_CH_CONNECT = BIT(2),
	SPCOM_POLL_READY_FLAG = BIT(14), /* output */
	SPCOM_POLL_WAIT_FLAG  = BIT(15), /* if set , wait for the event */
};

/* Common Command structure between User Space and spcom driver, on write() */
struct spcom_user_command {
	enum spcom_cmd_id cmd_id;
	__u32 arg;
} __packed;

/* Command structure between User Space and spcom driver, on write() */
struct spcom_send_command {
	enum spcom_cmd_id cmd_id;
	__u32 timeout_msec;
	__u32 buf_size;
	char buf[0]; /* Variable buffer size - must be last field */
} __packed;

/* Command structure between userspace spcomlib and spcom driver, on write() */
struct spcom_user_create_channel_command {
	enum spcom_cmd_id cmd_id;
	char ch_name[SPCOM_CHANNEL_NAME_SIZE];
#define SPCOM_IS_SHARABLE_SUPPORTED
	_Bool is_sharable;
} __packed;

/* Command structure between userspace spcomlib and spcom driver, on write() */
#define SPCOM_USER_RESTART_SP_CMD
struct spcom_user_restart_sp_command {
	enum spcom_cmd_id cmd_id;
	__u32 arg;
#define SPCOM_IS_UPDATED_SUPPORETED
	__u32 is_updated;
} __packed;

/* maximum ION buf for send-modfied-command */
#define SPCOM_MAX_ION_BUF 4

struct spcom_ion_info {
	__s32 fd; /* ION buffer File Descriptor, set -1 for invalid fd */
	__u32 buf_offset; /* virtual address offset in request/response */
};

/* Pass this FD to unlock all ION buffer for the specific channel */
#define SPCOM_ION_FD_UNLOCK_ALL	0xFFFF

struct spcom_ion_handle {
	__s32 fd; /* File Descriptor associated with the buffer */
};

struct spcom_rmb_error_info {
	__u32 rmb_error;
	__u32 padding;
} __packed;

/* Command structure between User Space and spcom driver, on write() */
struct spcom_user_send_modified_command {
	enum spcom_cmd_id cmd_id;
	struct spcom_ion_info ion_info[SPCOM_MAX_ION_BUF];
	__u32 timeout_msec;
	__u32 buf_size;
	char buf[0]; /* Variable buffer size - must be last field */
} __packed;

enum {
	SPCOM_IONFD_CMD,
	SPCOM_POLL_CMD,
	SPCOM_GET_RMB_CMD,
};

enum spcom_poll_cmd_id {
	SPCOM_LINK_STATE_REQ,
	SPCOM_CH_CONN_STATE_REQ,
};

struct spcom_poll_param {
	/* input parameters */
	_Bool wait;
	enum spcom_poll_cmd_id cmd_id;
	/* output parameter */
	int retval;
} __packed;

#define SPCOM_IOCTL_MAGIC 'S'

#define SPCOM_GET_IONFD _IOR(SPCOM_IOCTL_MAGIC, SPCOM_IONFD_CMD, \
			     struct spcom_ion_handle)
#define SPCOM_SET_IONFD _IOW(SPCOM_IOCTL_MAGIC, SPCOM_IONFD_CMD, \
			     struct spcom_ion_handle)
#define SPCOM_POLL_STATE _IOWR(SPCOM_IOCTL_MAGIC, SPCOM_POLL_CMD, \
			       struct spcom_poll_param)
#define SPCOM_GET_RMB_ERROR _IOR(SPCOM_IOCTL_MAGIC, SPCOM_GET_RMB_CMD, \
			     struct spcom_rmb_error_info)

/* Maximum number of DMA buffer for sending modified message */
#define SPCOM_MAX_DMA_BUF SPCOM_MAX_ION_BUF

/* Pass this FD to unlock all DMA buffer for the specific channel */
#define SPCOM_DMABUF_FD_UNLOCK_ALL	0xFFFF

/* SPCOM events enum */
enum spcom_event_id {
	SPCOM_EVENT_LINK_STATE = 100
};

/* SPCOM IOCTL commands */
enum spcom_ioctl_enum {
	SPCOM_IOCTL_STATE_POLL_CMD = 1000,
	SPCOM_IOCTL_SHARED_CH_CREATE_CMD,
	SPCOM_IOCTL_CH_REGISTER_CMD,
	SPCOM_IOCTL_CH_UNREGISTER_CMD,
	SPCOM_IOCTL_CH_IS_CONNECTED_CMD,
	SPCOM_IOCTL_SEND_MSG_CMD,
	SPCOM_IOCTL_SEND_MOD_MSG_CMD,
	SPCOM_IOCTL_GET_NEXT_REQ_SZ_CMD,
	SPCOM_IOCTL_GET_MSG_CMD,
	SPCOM_IOCTL_DMABUF_LOCK_CMD,
	SPCOM_IOCTL_DMABUF_UNLOCK_CMD,
	SPCOM_IOCTL_RESTART_SPU_CMD,
	SPCOM_IOCTL_ENABLE_SSR_CMD
};

/* SPCOM dma buffer info struct */
struct spcom_dma_buf_info {
	__s32 fd;     /* DMA buffer File Descriptor */
	__u32 offset; /* Address offset in request or response buffer*/
}  __packed;

/* SPCOM dma buffers info table */
struct spcom_dma_buf_info_table {
	struct spcom_dma_buf_info info[SPCOM_MAX_DMA_BUF];
}  __packed;

/* SPCOM poll on event cmd struct */
struct spcom_ioctl_poll_event {
	__u32 event_id;  /* spcom_ioctl_enum */
	__u32 wait;      /* wait for event, zero for no wait, positive number otherwise */
	__s32 retval;    /* updated by spcom driver  */
	__u32 padding;   /* 64-bit alignment, unused */
} __packed;

/* SPCOM register/unregister channel cmd struct */
struct spcom_ioctl_ch {
	char ch_name[SPCOM_CHANNEL_NAME_SIZE]; /* 4 * 64-bit */
}  __packed;

/* SPCOM message cmd struct */
struct spcom_ioctl_message {
	char ch_name[SPCOM_CHANNEL_NAME_SIZE]; /* 4 * 64-bit */
	__u32 timeout_msec;
	__u32 buffer_size;
	char buffer[0]; /* Variable buffer size - must be last field */
}  __packed;

/* SPCOM modified message cmd struct */
struct spcom_ioctl_modified_message {
	char ch_name[SPCOM_CHANNEL_NAME_SIZE];  /* 4 * 64-bit */
	__u32 timeout_msec;
	__u32 buffer_size;
	struct spcom_dma_buf_info info[SPCOM_MAX_DMA_BUF];
	char buffer[0]; /* Variable buffer size - must be last field */
}  __packed;

/* SPCOM ioctl get next request size command struct */
struct spcom_ioctl_next_request_size {
	char ch_name[SPCOM_CHANNEL_NAME_SIZE];  /* 4 * 64-bit */
	__u32 size;
	__u32 padding; /* 64-bit alignment, unused */
}  __packed;

/* SPCOM ioctl buffer lock or unlock command struct */
struct spcom_ioctl_dmabuf_lock {
	char ch_name[SPCOM_CHANNEL_NAME_SIZE];  /* 4 * 64-bit */
	__s32 fd;
	__u32 padding; /* 64-bit alignment, unused */
} __packed;

/* SPCOM ioctl command to handle event  poll */
#define SPCOM_IOCTL_STATE_POLL _IOWR(          \
		SPCOM_IOCTL_MAGIC,             \
		SPCOM_IOCTL_STATE_POLL_CMD,    \
		struct spcom_ioctl_poll_event  \
		)

/* SPCOM ioctl command to handle SPCOM shared channel create */
#define SPCOM_IOCTL_SHARED_CH_CREATE _IOW(          \
		SPCOM_IOCTL_MAGIC,                  \
		SPCOM_IOCTL_SHARED_CH_CREATE_CMD,   \
		struct spcom_ioctl_ch               \
		)

/* SPCOM ioctl command to handle SPCOM channel register */
#define SPCOM_IOCTL_CH_REGISTER _IOW(          \
		SPCOM_IOCTL_MAGIC,             \
		SPCOM_IOCTL_CH_REGISTER_CMD,   \
		struct spcom_ioctl_ch          \
		)

/*  IOCTL to handle SPCOM channel unregister */
#define SPCOM_IOCTL_CH_UNREGISTER _IOW(          \
		SPCOM_IOCTL_MAGIC,               \
		SPCOM_IOCTL_CH_UNREGISTER_CMD,   \
		struct spcom_ioctl_ch            \
		)

/* IOCTL to check SPCOM channel connectivity with remote edge */
#define SPCOM_IOCTL_CH_IS_CONNECTED _IOW(          \
		SPCOM_IOCTL_MAGIC,                 \
		SPCOM_IOCTL_CH_IS_CONNECTED_CMD,   \
		struct spcom_ioctl_ch              \
		)

/* IOCTL to handle SPCOM send message */
#define SPCOM_IOCTL_SEND_MSG _IOW(          \
		SPCOM_IOCTL_MAGIC,          \
		SPCOM_IOCTL_SEND_MSG_CMD,   \
		struct spcom_ioctl_message  \
		)

/* IOCTL to handle SPCOM send modified message */
#define SPCOM_IOCTL_SEND_MOD_MSG _IOW(               \
		SPCOM_IOCTL_MAGIC,                   \
		SPCOM_IOCTL_SEND_MOD_MSG_CMD,        \
		struct spcom_ioctl_modified_message  \
		)

/* IOCTL to handle SPCOM get next request message size */
#define SPCOM_IOCTL_GET_NEXT_REQ_SZ _IOWR(            \
		SPCOM_IOCTL_MAGIC,                    \
		SPCOM_IOCTL_GET_NEXT_REQ_SZ_CMD,      \
		struct spcom_ioctl_next_request_size  \
		)

/* IOCTL to handle SPCOM get request */
#define SPCOM_IOCTL_GET_MSG _IOWR(          \
		SPCOM_IOCTL_MAGIC,          \
		SPCOM_IOCTL_GET_MSG_CMD,    \
		struct spcom_ioctl_message  \
		)

/* IOCTL to handle DMA buffer lock/unlock */
#define SPCOM_IOCTL_DMABUF_LOCK _IOW(           \
		SPCOM_IOCTL_MAGIC,              \
		SPCOM_IOCTL_DMABUF_LOCK_CMD,    \
		struct spcom_ioctl_dmabuf_lock  \
		)

/* IOCTL to handle DMA buffer unlock */
#define SPCOM_IOCTL_DMABUF_UNLOCK _IOW(          \
		SPCOM_IOCTL_MAGIC,               \
		SPCOM_IOCTL_DMABUF_UNLOCK_CMD,   \
		struct spcom_ioctl_dmabuf_lock   \
		)

/* IOCTL to handle SPU restart */
#define SPCOM_IOCTL_RESTART_SPU _IO(          \
		SPCOM_IOCTL_MAGIC,            \
		SPCOM_IOCTL_RESTART_SPU_CMD   \
		)

/* IOCTL to enable SSR */
#define SPCOM_IOCTL_ENABLE_SSR _IO(          \
		SPCOM_IOCTL_MAGIC,           \
		SPCOM_IOCTL_ENABLE_SSR_CMD   \
		)

#endif /* _UAPI_SPCOM_H_ */
