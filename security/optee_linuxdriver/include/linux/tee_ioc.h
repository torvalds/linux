/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _TEE_IOC_H
#define _TEE_IOC_H

#include <linux/tee_client_api.h>

#ifndef __KERNEL__
#define __user
#endif

/**
 * struct tee_cmd_io - The command sent to an open tee device.
 * @err: Error code (as in Global Platform TEE Client API spec)
 * @origin: Origin for the error code (also from spec).
 * @cmd: The command to be executed in the trusted application.
 * @uuid: The uuid for the trusted application.
 * @data: The trusted application or memory block.
 * @data_size: The size of the trusted application or memory block.
 * @op: The cmd payload operation for the trusted application.
 *
 * This structure is mainly used in the Linux kernel for communication
 * with the user space.
 */
struct tee_cmd_io {
	TEEC_Result err;
	uint32_t origin;
	uint32_t cmd;
	TEEC_UUID __user *uuid;
	void __user *data;
	uint32_t data_size;
	TEEC_Operation __user *op;
	int fd_sess;
};

struct tee_shm_io {
	void __user *buffer;
	size_t size;
	uint32_t flags;
	union {
		int fd_shm;
		void *ptr;
	};
	uint8_t registered;
};

#define TEE_OPEN_SESSION_IOC		_IOWR('t', 161, struct tee_cmd_io)
#define TEE_INVOKE_COMMAND_IOC		_IOWR('t', 163, struct tee_cmd_io)
#define TEE_REQUEST_CANCELLATION_IOC	_IOWR('t', 164, struct tee_cmd_io)
#define TEE_ALLOC_SHM_IOC		_IOWR('t', 165, struct tee_shm_io)
#define TEE_GET_FD_FOR_RPC_SHM_IOC	_IOWR('t', 167, struct tee_shm_io)

#endif /* _TEE_IOC_H */
