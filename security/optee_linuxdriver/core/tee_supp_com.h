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
#ifndef TEE_SUPP_COMM_H
#define TEE_SUPP_COMM_H

#define TEE_RPC_ICMD_ALLOCATE 0x1001
#define TEE_RPC_ICMD_FREE     0x1002
#define TEE_RPC_ICMD_INVOKE   0x1003

#define TEE_RPC_NBR_BUFF 1
#define TEE_RPC_DATA_SIZE 64
#define TEE_RPC_BUFFER_NUMBER 5

#define TEE_RPC_STATE_IDLE    0x00
#define TEE_RPC_STATE_ACTIVE  0x01

/* Keep aligned with optee_client (user space) */
#define TEE_RPC_BUFFER		0x00000001
#define TEE_RPC_VALUE		0x00000002
#define TEE_RPC_LOAD_TA		0x10000001
/*
 * Handled within the driver only
 * Keep aligned with optee_os (secure space)
 */
#define TEE_RPC_MUTEX_WAIT		0x20000000
#define TEE_RPC_WAIT_QUEUE_SLEEP	0x20000001
#define TEE_RPC_WAIT_QUEUE_WAKEUP	0x20000002
#define TEE_RPC_WAIT			0x30000000

/* Parameters for TEE_RPC_WAIT_MUTEX above */
#define TEE_MUTEX_WAIT_SLEEP	0
#define TEE_MUTEX_WAIT_WAKEUP	1
#define TEE_MUTEX_WAIT_DELETE	2

#include <linux/semaphore.h>

/**
 * struct tee_rpc_bf - Contains definition of the tee com buffer
 * @state: Buffer state
 * @data: Command data
 */
struct tee_rpc_bf {
	uint32_t state;
	uint8_t data[TEE_RPC_DATA_SIZE];
};

struct tee_rpc_alloc {
	uint32_t size;	/* size of block */
	void *data;	/* pointer to data */
	void *shm;	/* pointer to an opaque data, being shm structure */
};

struct tee_rpc_free {
	void *shm;	/* pointer to an opaque data, being shm structure */
};

struct tee_rpc_cmd {
	union {
		void    *buffer;
		uint64_t padding_buf;
	};
	uint32_t size;
	uint32_t type;
	int fd;
	int reserved;
};

struct tee_rpc_invoke {
	uint32_t cmd;
	uint32_t res;
	uint32_t nbr_bf;
	uint32_t reserved;
	struct tee_rpc_cmd cmds[TEE_RPC_BUFFER_NUMBER];
};

struct tee_rpc {
	struct tee_rpc_invoke commToUser;
	struct tee_rpc_invoke commFromUser;
	struct semaphore datatouser;
	struct semaphore datafromuser;
	struct mutex thrd_mutex; /* Block the thread to wait for supp answer */
	struct mutex outsync; /* Out sync mutex */
	struct mutex insync; /* In sync mutex */
	struct mutex reqsync; /* Request sync mutex */
	atomic_t  used;
};

enum teec_rpc_result {
	TEEC_RPC_OK,
	TEEC_RPC_FAIL
};

struct tee;

int tee_supp_init(struct tee *tee);
void tee_supp_deinit(struct tee *tee);

enum teec_rpc_result tee_supp_cmd(struct tee *tee,
				  uint32_t id, void *data, size_t datalen);

ssize_t tee_supp_read(struct file *filp, char __user *buffer,
		  size_t length, loff_t *offset);

ssize_t tee_supp_write(struct file *filp, const char __user *buffer,
		   size_t length, loff_t *offset);

#endif
