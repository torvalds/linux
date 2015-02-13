/*
 * Copyright (c) 2013-2014 NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __OTE_PROTOCOL_H__
#define __OTE_PROTOCOL_H__

#include "ote_types.h"

#define TE_IOCTL_MAGIC_NUMBER ('t')
#define TE_IOCTL_OPEN_CLIENT_SESSION \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x10, union te_cmd)
#define TE_IOCTL_CLOSE_CLIENT_SESSION \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x11, union te_cmd)
#define TE_IOCTL_LAUNCH_OPERATION \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x14, union te_cmd)

/* ioctls using new structs (eventually to replace current ioctls) */
#define TE_IOCTL_OPEN_CLIENT_SESSION_COMPAT \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x10, union te_cmd_compat)
#define TE_IOCTL_CLOSE_CLIENT_SESSION_COMPAT \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x11, union te_cmd_compat)
#define TE_IOCTL_LAUNCH_OPERATION_COMPAT \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x14, union te_cmd_compat)

#define TE_IOCTL_SS_NEW_REQ_LEGACY \
	_IOR(TE_IOCTL_MAGIC_NUMBER,  0x20, struct te_ss_op_legacy)
#define TE_IOCTL_SS_REQ_COMPLETE_LEGACY \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x21, struct te_ss_op_legacy)

/* ioctls using new SS structs (eventually to replace current SS ioctls) */
#define TE_IOCTL_SS_NEW_REQ \
	_IOR(TE_IOCTL_MAGIC_NUMBER,  0x20, struct te_ss_op)
#define TE_IOCTL_SS_REQ_COMPLETE \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x21, struct te_ss_op)

#define TE_IOCTL_MIN_NR	_IOC_NR(TE_IOCTL_OPEN_CLIENT_SESSION)
#define TE_IOCTL_MAX_NR	_IOC_NR(TE_IOCTL_SS_REQ_COMPLETE)

/* shared buffer is 2 pages: 1st are requests, 2nd are params */
#define TE_CMD_DESC_MAX	(PAGE_SIZE / sizeof(struct te_request))
#define TE_PARAM_MAX	(PAGE_SIZE / sizeof(struct te_oper_param))

#define TE_CMD_DESC_MAX_COMPAT \
	(PAGE_SIZE / sizeof(struct te_request_compat))
#define TE_PARAM_MAX_COMPAT \
	(PAGE_SIZE / sizeof(struct te_oper_param_compat))

#define MAX_EXT_SMC_ARGS	12

extern struct mutex smc_lock;
extern struct tlk_device tlk_dev;

uint32_t _tlk_generic_smc(uint32_t arg0, uintptr_t arg1, uintptr_t arg2);
uint32_t tlk_generic_smc(uint32_t arg0, uintptr_t arg1, uintptr_t arg2);
uint32_t _tlk_extended_smc(uintptr_t *args);
uint32_t tlk_extended_smc(uintptr_t *args);
void tlk_irq_handler(void);

/* errors returned by secure world in reponse to SMC calls */
enum {
	TE_ERROR_PREEMPT_BY_IRQ = 0xFFFFFFFD,
	TE_ERROR_PREEMPT_BY_FS = 0xFFFFFFFE,
};

struct tlk_device {
	struct te_request *req_addr;
	dma_addr_t req_addr_phys;
	struct te_oper_param *param_addr;
	dma_addr_t param_addr_phys;

	struct te_request_compat *req_addr_compat;
	struct te_oper_param_compat *param_addr_compat;

	char *req_param_buf;

	unsigned long *param_bitmap;

	struct list_head used_cmd_list;
	struct list_head free_cmd_list;
};

struct te_cmd_req_desc {
	struct te_request *req_addr;
	struct list_head list;
};

struct te_cmd_req_desc_compat {
	struct te_request_compat *req_addr;
	struct list_head list;
};

struct te_shmem_desc {
	struct list_head list;
	void *buffer;
	size_t size;
	unsigned int mem_type;
};

struct tlk_context {
	struct tlk_device *dev;
	struct list_head shmem_alloc_list;
};

enum {
	/* Trusted Application Calls */
	TE_SMC_OPEN_SESSION		= 0x30000001,
	TE_SMC_CLOSE_SESSION		= 0x30000002,
	TE_SMC_LAUNCH_OPERATION		= 0x30000003,

	/* Trusted OS calls */
	TE_SMC_REGISTER_REQ_BUF		= 0x32000002,
	TE_SMC_REGISTER_IRQ_HANDLER	= 0x32000004,
	TE_SMC_NS_IRQ_DONE		= 0x32000005,
	TE_SMC_INIT_LOGGER		= 0x32000007,
	TE_SMC_SS_REGISTER_HANDLER_LEGACY	= 0x32000008,
	TE_SMC_SS_REQ_COMPLETE		= 0x32000009,
	TE_SMC_SS_REGISTER_HANDLER	= 0x32000010,

	/* SIP (SOC specific) calls.  */
	TE_SMC_PROGRAM_VPR		= 0x82000003,
};

enum {
	TE_PARAM_TYPE_NONE	= 0,
	TE_PARAM_TYPE_INT_RO    = 1,
	TE_PARAM_TYPE_INT_RW    = 2,
	TE_PARAM_TYPE_MEM_RO    = 3,
	TE_PARAM_TYPE_MEM_RW    = 4,
};

struct te_oper_param {
	uint32_t index;
	uint32_t type;
	union {
		struct {
			uint32_t val;
		} Int;
		struct {
			void  *base;
			uint32_t len;
		} Mem;
	} u;
	void *next_ptr_user;
};

struct te_oper_param_compat {
	uint32_t index;
	uint32_t type;
	union {
		struct {
			uint32_t val;
		} Int;
		struct {
			uint64_t base;
			uint32_t len;
		} Mem;
	} u;
	uint64_t next_ptr_user;
};

struct te_operation {
	uint32_t command;
	struct te_oper_param *list_head;
	/* Maintain a pointer to tail of list to easily add new param node */
	struct te_oper_param *list_tail;
	uint32_t list_count;
	uint32_t status;
	uint32_t iterface_side;
};

struct te_service_id {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint8_t clock_seq_and_node[8];
};

/*
 * OpenSession
 */
struct te_opensession {
	struct te_service_id dest_uuid;
	struct te_operation operation;
	uint32_t answer;
};

/*
 * CloseSession
 */
struct te_closesession {
	uint32_t	session_id;
	uint32_t	answer;
};

/*
 * LaunchOperation
 */
struct te_launchop {
	uint32_t		session_id;
	struct te_operation	operation;
	uint32_t		answer;
};

union te_cmd {
	struct te_opensession	opensession;
	struct te_closesession	closesession;
	struct te_launchop	launchop;
};

/*
 * Compat versions of the original structs (eventually to replace
 * the old structs, once the lib/TLK kernel changes are in).
 */
struct te_operation_compat {
	uint32_t	command;
	uint32_t	status;
	uint64_t	list_head;
	uint64_t	list_tail;
	uint32_t	list_count;
	uint32_t	interface_side;
};

/*
 * OpenSession
 */
struct te_opensession_compat {
	struct te_service_id		dest_uuid;
	struct te_operation_compat	operation;
	uint64_t			answer;
};

/*
 * CloseSession
 */
struct te_closesession_compat {
	uint32_t	session_id;
	uint64_t	answer;
};

/*
 * LaunchOperation
 */
struct te_launchop_compat {
	uint32_t			session_id;
	struct te_operation_compat	operation;
	uint64_t			answer;
};

union te_cmd_compat {
	struct te_opensession_compat	opensession;
	struct te_closesession_compat	closesession;
	struct te_launchop_compat	launchop;
};

struct te_request {
	uint32_t		type;
	uint32_t		session_id;
	uint32_t		command_id;
	struct te_oper_param	*params;
	uint32_t		params_size;
	uint32_t		dest_uuid[4];
	uint32_t		result;
	uint32_t		result_origin;
};

struct te_request_compat {
	uint32_t		type;
	uint32_t		session_id;
	uint32_t		command_id;
	uint64_t		params;
	uint32_t		params_size;
	uint32_t		dest_uuid[4];
	uint32_t		result;
	uint32_t		result_origin;
};

struct te_answer {
	uint32_t	result;
	uint32_t	session_id;
	uint32_t	result_origin;
};

void te_open_session(struct te_opensession *cmd,
	struct te_request *request,
	struct tlk_context *context);

void te_close_session(struct te_closesession *cmd,
	struct te_request *request,
	struct tlk_context *context);

void te_launch_operation(struct te_launchop *cmd,
	struct te_request *request,
	struct tlk_context *context);

void te_open_session_compat(struct te_opensession_compat *cmd,
	struct te_request_compat *request,
	struct tlk_context *context);

void te_close_session_compat(struct te_closesession_compat *cmd,
	struct te_request_compat *request,
	struct tlk_context *context);

void te_launch_operation_compat(struct te_launchop_compat *cmd,
	struct te_request_compat *request,
	struct tlk_context *context);

#define SS_OP_MAX_DATA_SIZE	0x1000
struct te_ss_op {
	uint32_t	req_size;
	uint8_t		data[SS_OP_MAX_DATA_SIZE];
};

struct te_ss_op_legacy {
	uint8_t		data[SS_OP_MAX_DATA_SIZE];
};

int te_handle_ss_ioctl_legacy(struct file *file, unsigned int ioctl_num,
		unsigned long ioctl_param);
int te_handle_ss_ioctl(struct file *file, unsigned int ioctl_num,
		unsigned long ioctl_param);
int te_handle_fs_ioctl(struct file *file, unsigned int ioctl_num,
		unsigned long ioctl_param);
void ote_print_logs(void);
void tlk_ss_op(void);

#endif
