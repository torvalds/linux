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
#ifndef __TEE_ARMV7_OP_H__
#define __TEE_ARMV7_OP_H__

enum t_issw_service_id {
	/*
	 * ("SSAPI_PRE_INIT_SERV")
	 */
	SSAPI_PRE_INIT_SERV = 1,

	/*
	 * ("SSAPI_POST_SPEEDUP_INIT_SERV")
	 * Reserved, Not used
	 */
	SSAPI_POST_SPEEDUP_INIT_SERV = 2,

	/*
	 * ("SSAPI_ISSW_IMPORT_SERV")
	 */
	SSAPI_ISSW_IMPORT_SERV = 3,

	/*
	 * ("SSAPI_RET_FROM_INT_SERV")
	 */
	SSAPI_RET_FROM_INT_SERV = 4,

	/*
	 * ("SSAPI_RET_FROM_RPC_SERV")
	 */
	SSAPI_RET_FROM_RPC_SERV = 5,

	/*
	 * "ISSWAPI_ISSW_EXECUTE_SERV" is linked to ROM code
	 * ("SSAPI_ISSW_EXECUTE_SERV")
	 */
	ISSWAPI_ISSW_EXECUTE_SERV = 6,
	ISSWAPI_PROT_APPL_MSG_SEND = 0x10000000,
	ISSWAPI_EXTERNAL_CODE_CHECK = 0x10000001,
	ISSWAPI_SECURE_LOAD = 0x10000002,
	ISSWAPI_ISSW_REIMPORT_PUB_KEYS = 0x10000003,

	/* Accessible only on request */
	ISSWAPI_WRITE_L2CC = 0x10000004,
	ISSWAPI_WRITE_CP15_SCTLR = 0x10000005,
	ISSWAPI_READ_CP15_SCTLR = 0x10000006,
	ISSWAPI_WRITE_CP15_ACTLR = 0x10000007,
	ISSWAPI_READ_CP15_ACTLR = 0x10000008,
	ISSWAPI_WRITE_CP15_DIAGR = 0x10000009,
	ISSWAPI_READ_CP15_DIAGR = 0x1000000A,

	ISSWAPI_EXECUTE_TA = 0x11000001,
	ISSWAPI_CLOSE_TA = 0x11000002,
	ISSWAPI_FLUSH_BOOT_CODE = 0x11000003,
	/* Generic, restricted to be used by u-boot */
	ISSWAPI_VERIFY_SIGNED_HEADER = 0x11000005,
	ISSWAPI_VERIFY_HASH = 0x11000006,
	/* 8500 only, restricted to be used by u-boot */
	ISSWAPI_GET_RT_FLAGS = 0x11000007,

	/* For TEE Client API 1.0 */
	ISSWAPI_TEEC_OPEN_SESSION = 0x11000008,
	ISSWAPI_TEEC_CLOSE_SESSION = 0x11000009,
	ISSWAPI_TEEC_INVOKE_COMMAND = 0x1100000a,
	ISSWAPI_REGISTER_RPC = 0x1100000b,	/* this is NOT a GP TEE API ! */
	ISSWAPI_SET_SEC_DDR = 0x1100000c,	/* this is NOT a GP TEE API ! */
	ISSWAPI_TEEC_CANCEL_COMMAND = 0x1100000d,
	ISSWAPI_TEEC_REGISTER_MEMORY = 0x1100000e,
	ISSWAPI_TEEC_UNREGISTER_MEMORY = 0x1100000f,

	/* Internal command */
	ISSWAPI_TEE_DEINIT_CPU = 0x11000010,
	ISSWAPI_TEE_CRASH_CPU = 0x11000011,
	ISSWAPI_TEE_SET_CORE_TRACE_LEVEL = 0x11000012,
	ISSWAPI_TEE_GET_CORE_TRACE_LEVEL = 0x11000013,
	ISSWAPI_TEE_SET_TA_TRACE_LEVEL = 0x11000014,
	ISSWAPI_TEE_GET_TA_TRACE_LEVEL = 0x11000015,
	ISSWAPI_TEE_GET_CORE_STATUS = 0x11000016,
	ISSWAPI_TEE_FLUSH_CACHE = 0x11000017,

	ISSWAPI_REGISTER_DEF_SHM = 0x11000020,
	ISSWAPI_UNREGISTER_DEF_SHM = 0x11000021,
	ISSWAPI_REGISTER_IRQFWD = 0x11000022,
	ISSWAPI_UNREGISTER_IRQFWD = 0x11000023,
	ISSWAPI_GET_SHM_START = 0x11000024,
	ISSWAPI_GET_SHM_SIZE = 0x11000025,
	ISSWAPI_GET_SHM_CACHED = 0x11000026,

	ISSWAPI_ENABLE_L2CC_MUTEX = 0x20000000,
	ISSWAPI_DISABLE_L2CC_MUTEX = 0x20000001,
	ISSWAPI_GET_L2CC_MUTEX = 0x20000002,
	ISSWAPI_SET_L2CC_MUTEX = 0x20000003,

	ISSWAPI_LOAD_TEE = 0x20000004,

};

/*
 * tee_msg_send - generic part of the msg sent to the TEE
 */
struct tee_msg_send {
	unsigned int service;
};

/*
 * tee_msg_recv - default strcutre of TEE service output message
 */
struct tee_msg_recv {
	int duration;
	uint32_t res;
	uint32_t origin;
};

/*
 * tee_register_irqfwd_xxx - (un)register callback for interrupt forwarding
 */
struct tee_register_irqfwd_send {
	struct tee_msg_send header;
	struct {
		unsigned long cb;
	} data;
};
struct tee_register_irqfwd_recv {
	struct tee_msg_recv header;
};

/*
 * tee_get_l2cc_mutex - input/output argument structures
 */
struct tee_get_l2cc_mutex_send {
	struct tee_msg_send header;
};
struct tee_get_l2cc_mutex_recv {
	struct tee_msg_recv header;
	struct {
		unsigned long paddr;
	} data;
};

/**
 * struct tee_identity - Represents the identity of the client
 * @login: Login id
 * @uuid: UUID as defined above
 */
struct tee_identity {
	uint32_t login;
	TEEC_UUID uuid;
};

/*
 * tee_open_session_data - input arg structure for TEE open session service
 */
struct tee_open_session_data {
	struct ta_signed_header_t *ta;
	TEEC_UUID uuid;
	uint32_t param_types;
	TEEC_Value params[TEEC_CONFIG_PAYLOAD_REF_COUNT];
	struct tee_identity client_id;
	uint32_t params_flags[TEEC_CONFIG_PAYLOAD_REF_COUNT];
};

/*
 * tee_open_session_send - input arg msg for TEE open session service
 */
struct tee_open_session_send {
	struct tee_msg_send header;
	struct tee_open_session_data data;
};

/*
 * tee_open_session_recv - output arg structure for TEE open session service
 */
struct tee_open_session_recv {
	struct tee_msg_recv header;
	uint32_t sess;
	TEEC_Value params[TEEC_CONFIG_PAYLOAD_REF_COUNT];
};

/*
 * tee_invoke_command_data - input arg structure for TEE invoke cmd service
 */
struct tee_invoke_command_data {
	uint32_t sess;
	uint32_t cmd;
	uint32_t param_types;
	TEEC_Value params[TEEC_CONFIG_PAYLOAD_REF_COUNT];
	uint32_t params_flags[TEEC_CONFIG_PAYLOAD_REF_COUNT];
};

struct tee_invoke_command_send {
	struct tee_msg_send header;
	struct tee_invoke_command_data data;
};

/*
 * tee_invoke_command_recv - output arg structure for TEE invoke cmd service
 */
struct tee_invoke_command_recv {
	struct tee_msg_recv header;
	TEEC_Value params[TEEC_CONFIG_PAYLOAD_REF_COUNT];
};

/*
 * tee_cancel_command_data - input arg structure for TEE cancel service
 */
struct tee_cancel_command_data {
	uint32_t sess;
};

/*
 * tee_cancel_command_send - input msg structure for TEE cancel service
 */
struct tee_cancel_command_send {
	struct tee_msg_send header;
	struct tee_cancel_command_data data;
};

/*
 * tee_close_session_data - input arg structure for TEE close session service
 */
struct tee_close_session_data {
	uint32_t sess;
};

/*
 * tee_close_session_send - input arg msg for TEE close session service
 */
struct tee_close_session_send {
	struct tee_msg_send header;
	struct tee_close_session_data data;
};

/*
 * tee_register_rpc_send_data - input arg structure for TEE register rpc service
 */
struct tee_register_rpc_send_data {
	uint32_t fnk;
	uint32_t bf;
	uint32_t nbr_bf;
};

/*
 * tee_register_rpc_send - input msg structure for TEE register rpc service
 */
struct tee_register_rpc_send {
	struct tee_msg_send header;
	struct tee_register_rpc_send_data data;
};

/*
 * tee_core_status_out - output arg structure for TEE status service
 */
#define TEEC_STATUS_MSG_SIZE 80

struct tee_core_status_out {
	struct tee_msg_recv header;
	char raw[TEEC_STATUS_MSG_SIZE];
};

#endif /* __TEE_ARMV7_OP_H__ */
