/*
 * Copyright (c) 2014, Linaro Limited
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
#ifndef TEESMC_H
#define TEESMC_H

#ifndef ASM
/*
 * This section depends on uint64_t, uint32_t uint8_t already being
 * defined. Since this file is used in several different environments
 * (secure world OS and normal world Linux kernel to start with) where
 * stdint.h may not be available it's the responsibility of the one
 * including this file to provide those types.
 */

/*
 * Trusted OS SMC interface.
 *
 * The SMC interface follows SMC Calling Convention
 * (ARM_DEN0028A_SMC_Calling_Convention).
 *
 * The primary objective of this API is to provide a transport layer on
 * which a Global Platform compliant TEE interfaces can be deployed. But the
 * interface can also be used for other implementations.
 *
 * This file is divided in two parts.
 * Part 1 deals with passing parameters to Trusted Applications running in
 * a trusted OS in secure world.
 * Part 2 deals with the lower level handling of the SMC.
 */

/*
 *******************************************************************************
 * Part 1 - passing parameters to Trusted Applications
 *******************************************************************************
 */

/*
 * Same values as TEE_PARAM_* from TEE Internal API
 */
#define TEESMC_ATTR_TYPE_NONE		0
#define TEESMC_ATTR_TYPE_VALUE_INPUT	1
#define TEESMC_ATTR_TYPE_VALUE_OUTPUT	2
#define TEESMC_ATTR_TYPE_VALUE_INOUT	3
#define TEESMC_ATTR_TYPE_MEMREF_INPUT	5
#define TEESMC_ATTR_TYPE_MEMREF_OUTPUT	6
#define TEESMC_ATTR_TYPE_MEMREF_INOUT	7

#define TEESMC_ATTR_TYPE_MASK		0x7

/*
 * Meta parameter to be absorbed by the Secure OS and not passed
 * to the Trusted Application.
 *
 * One example of this is a struct teesmc_meta_open_session which
 * is added to TEESMC{32,64}_CMD_OPEN_SESSION.
 */
#define TEESMC_ATTR_META		0x8

/*
 * Used as an indication from normal world of compatible cache usage.
 * 'I' stands for inner cache and 'O' for outer cache.
 */
#define TEESMC_ATTR_CACHE_I_NONCACHE	0x0
#define TEESMC_ATTR_CACHE_I_WRITE_THR	0x1
#define TEESMC_ATTR_CACHE_I_WRITE_BACK	0x2
#define TEESMC_ATTR_CACHE_O_NONCACHE	0x0
#define TEESMC_ATTR_CACHE_O_WRITE_THR	0x4
#define TEESMC_ATTR_CACHE_O_WRITE_BACK	0x8

#define TEESMC_ATTR_CACHE_NONCACHE	(TEESMC_ATTR_CACHE_I_NONCACHE | \
					 TEESMC_ATTR_CACHE_O_NONCACHE)
#define TEESMC_ATTR_CACHE_DEFAULT	(TEESMC_ATTR_CACHE_I_WRITE_BACK | \
					 TEESMC_ATTR_CACHE_O_WRITE_BACK)

#define TEESMC_ATTR_CACHE_SHIFT		4
#define TEESMC_ATTR_CACHE_MASK		0xf

#define TEESMC_CMD_OPEN_SESSION		0
#define TEESMC_CMD_INVOKE_COMMAND	1
#define TEESMC_CMD_CLOSE_SESSION	2
#define TEESMC_CMD_CANCEL		3

/**
 * struct teesmc32_param_memref - memory reference
 * @buf_ptr: Address of the buffer
 * @size: Size of the buffer
 *
 * Secure and normal world communicates pointer via physical address instead of
 * the virtual address with is usually used for pointers. This is because
 * Secure and normal world has completely independant memory mapping. Normal
 * world can even have a hypervisor which need to translate the guest
 * physical address (AKA IPA in ARM lingo) to a real physical address
 * before passing the structure to secure world.
 */
struct teesmc32_param_memref {
	uint32_t buf_ptr;
	uint32_t size;
};

/**
 * struct teesmc32_param_memref - memory reference
 * @buf_ptr: Address of the buffer
 * @size: Size of the buffer
 *
 * See description of struct teesmc32_param_memref.
 */
struct teesmc64_param_memref {
	uint64_t buf_ptr;
	uint64_t size;
};

/**
 * struct teesmc32_param_value - values
 * @a: first value
 * @b: second value
 */
struct teesmc32_param_value {
	uint32_t a;
	uint32_t b;
};

/**
 * struct teesmc64_param_value - values
 * @a: first value
 * @b: second value
 */
struct teesmc64_param_value {
	uint64_t a;
	uint64_t b;
};

/**
 * struct teesmc32_param - parameter
 * @attr: attributes
 * @memref: a memory reference
 * @value: a value
 *
 * attr & TEESMC_ATTR_TYPE_MASK indicates if memref or value is used in the
 * union. TEESMC_ATTR_TYPE_VALUE_* indicates value and
 * TEESMC_ATTR_TYPE_MEMREF_* indicates memref. TEESMC_ATTR_TYPE_NONE
 * indicates that none of the members are used.
 */
struct teesmc32_param {
	uint32_t attr;
	union {
		struct teesmc32_param_memref memref;
		struct teesmc32_param_value value;
	} u;
};

/**
 * struct teesmc64_param - parameter
 * @attr: attributes
 * @memref: a memory reference
 * @value: a value
 *
 * See description of union teesmc32_param.
 */
struct teesmc64_param {
	uint64_t attr;
	union {
		struct teesmc64_param_memref memref;
		struct teesmc64_param_value value;
	} u;
};

/**
 * struct teesmc32_arg - SMC argument for Trusted OS
 * @cmd: Command, one of TEESMC_CMD_*
 * @ta_func: Trusted Application function, specific to the Trusted Application,
 *	     used if cmd == TEESMC_CMD_INVOKE_COMMAND
 * @session: In parameter for all TEESMC_CMD_* except
 *	     TEESMC_CMD_OPEN_SESSION where it's an output paramter instead
 * @ret: return value
 * @ret_origin: origin of the return value
 * @num_params: number of parameters supplied to the OS Command
 * @params: the parameters supplied to the OS Command
 *
 * All normal SMC calls to Trusted OS uses this struct. If cmd requires
 * further information than what these field holds it can be passed as a
 * parameter tagged as meta (setting the TEESMC_ATTR_META bit in
 * corresponding param_attrs). This is used for TEESMC_CMD_OPEN_SESSION
 * to pass a struct teesmc32_meta_open_session which is needed find the
 * Trusted Application and to indicate the credentials of the client.
 */
struct teesmc32_arg {
	uint32_t cmd;
	uint32_t ta_func;
	uint32_t session;
	uint32_t ret;
	uint32_t ret_origin;
	uint32_t num_params;
	/*
	 * Commented out elements used to visualize the layout dynamic part
	 * of the struct. Note that these fields are not available at all
	 * if num_params == 0.
	 *
	 * params is accessed through the macro TEESMC32_GET_PARAMS
	 */

	/* struct teesmc32_param params[num_params]; */
};

/**
 * TEESMC32_GET_PARAMS - return pointer to union teesmc32_param *
 *
 * @x: Pointer to a struct teesmc32_arg
 *
 * Returns a pointer to the params[] inside a struct teesmc32_arg.
 */
#define TEESMC32_GET_PARAMS(x) \
	(struct teesmc32_param *)(((struct teesmc32_arg *)(x)) + 1)

/**
 * TEESMC32_GET_ARG_SIZE - return size of struct teesmc32_arg
 *
 * @num_params: Number of parameters embedded in the struct teesmc32_arg
 *
 * Returns the size of the struct teesmc32_arg together with the number
 * of embedded paramters.
 */
#define TEESMC32_GET_ARG_SIZE(num_params) \
	(sizeof(struct teesmc32_arg) + \
	 sizeof(struct teesmc32_param) * (num_params))

/**
 * struct teesmc64_arg - SMC argument for Trusted OS
 * @cmd: OS Command, one of TEESMC_CMD_*
 * @ta_func: Trusted Application function, specific to the Trusted Application
 * @session: In parameter for all TEESMC_CMD_* but
 *	     TEESMC_CMD_OPEN_SESSION
 * @ret: return value
 * @ret_origin: origin of the return value
 * @num_params: number of parameters supplied to the OS Command
 * @params: the parameters supplied to the OS Command
 *
 * See description of struct teesmc32_arg.
 */
struct teesmc64_arg {
	uint64_t cmd;
	uint64_t ta_func;
	uint64_t session;
	uint64_t ret;
	uint64_t ret_origin;
	uint64_t num_params;
	/*
	 * Commented out elements used to visualize the layout dynamic part
	 * of the struct. Note that these fields are not available at all
	 * if num_params == 0.
	 *
	 * params is accessed through the macro TEESMC64_GET_PARAMS
	 */

	/* struct teesmc64_param params[num_params]; */
};

/**
 * TEESMC64_GET_PARAMS - return pointer to union teesmc64_param *
 *
 * @x: Pointer to a struct teesmc64_arg
 *
 * Returns a pointer to the params[] inside a struct teesmc64_arg.
 */
#define TEESMC64_GET_PARAMS(x) \
	(struct teesmc64_param *)(((struct teesmc64_arg *)(x)) + 1)

/**
 * TEESMC64_GET_ARG_SIZE - return size of struct teesmc64_arg
 *
 * @num_params: Number of parameters embedded in the struct teesmc64_arg
 *
 * Returns the size of the struct teesmc64_arg together with the number
 * of embedded paramters.
 */
#define TEESMC64_GET_ARG_SIZE(num_params) \
	(sizeof(struct teesmc64_arg) + \
	 sizeof(union teesmc64_param) * (num_params))

#define TEESMC_UUID_LEN	16

/**
 * struct teesmc_meta_open_session - additional parameters for
 *				     TEESMC32_CMD_OPEN_SESSION and
 *				     TEESMC64_CMD_OPEN_SESSION
 * @uuid: UUID of the Trusted Application
 * @clnt_uuid: UUID of client
 * @clnt_login: Login class of client, TEE_LOGIN_* if being Global Platform
 *		compliant
 *
 * This struct is passed in the first parameter as an input memref tagged
 * as meta on an TEESMC{32,64}_CMD_OPEN_SESSION cmd. It's important
 * that it really is the first parameter to make it easy for an eventual
 * hypervisor to inspect and possibly update clnt_* values.
 */
struct teesmc_meta_open_session {
	uint8_t uuid[TEESMC_UUID_LEN];
	uint8_t clnt_uuid[TEESMC_UUID_LEN];
	uint32_t clnt_login;
};


#endif /*!ASM*/

/*
 *******************************************************************************
 * Part 2 - low level SMC interaction
 *******************************************************************************
 */

#define TEESMC_32			0
#define TEESMC_64			0x40000000
#define TEESMC_FAST_CALL		0x80000000
#define TEESMC_STD_CALL			0

#define TEESMC_OWNER_MASK		0x3F
#define TEESMC_OWNER_SHIFT		24

#define TEESMC_FUNC_MASK		0xFFFF

#define TEESMC_IS_FAST_CALL(smc_val)	((smc_val) & TEESMC_FAST_CALL)
#define TEESMC_IS_64(smc_val)		((smc_val) & TEESMC_64)
#define TEESMC_FUNC_NUM(smc_val)	((smc_val) & TEESMC_FUNC_MASK)
#define TEESMC_OWNER_NUM(smc_val)	(((smc_val) >> TEESMC_OWNER_SHIFT) & \
					 TEESMC_OWNER_MASK)

#define TEESMC_CALL_VAL(type, calling_convention, owner, func_num) \
			((type) | (calling_convention) | \
			(((owner) & TEESMC_OWNER_MASK) << TEESMC_OWNER_SHIFT) |\
			((func_num) & TEESMC_FUNC_MASK))

#define TEESMC_OWNER_ARCH		0
#define TEESMC_OWNER_CPU		1
#define TEESMC_OWNER_SIP		2
#define TEESMC_OWNER_OEM		3
#define TEESMC_OWNER_STANDARD		4
#define TEESMC_OWNER_TRUSTED_APP	48
#define TEESMC_OWNER_TRUSTED_OS		50

/* Rockchip define trusted os call */
#define TEESMC_OWNER_TRUSTED_OS_ROCKCHIP	55

#define TEESMC_OWNER_TRUSTED_OS_API	63

/*
 * Function specified by SMC Calling convention.
 */
#define TEESMC32_FUNCID_CALLS_COUNT	0xFF00
#define TEESMC32_CALLS_COUNT \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, \
			TEESMC_OWNER_TRUSTED_OS_API, \
			TEESMC32_FUNCID_CALLS_COUNT)

/*
 * Function specified by SMC Calling convention
 *
 * Return one of the following UIDs if using API specified in this file
 * without further extentions:
 * 65cb6b93-af0c-4617-8ed6-644a8d1140f8 : Only 32 bit calls are supported
 * 65cb6b93-af0c-4617-8ed6-644a8d1140f9 : Both 32 and 64 bit calls are supported
 */
#define TEESMC_UID_R0			0x65cb6b93
#define TEESMC_UID_R1			0xaf0c4617
#define TEESMC_UID_R2			0x8ed6644a
#define TEESMC_UID32_R3			0x8d1140f8
#define TEESMC_UID64_R3			0x8d1140f9
#define TEESMC32_FUNCID_CALLS_UID	0xFF01
#define TEESMC32_CALLS_UID \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, \
			TEESMC_OWNER_TRUSTED_OS_API, \
			TEESMC32_FUNCID_CALLS_UID)

/*
 * Function specified by SMC Calling convention
 *
 * Returns 1.0 if using API specified in this file without further extentions.
 */
#define TEESMC_REVISION_MAJOR	1
#define TEESMC_REVISION_MINOR	0
#define TEESMC32_FUNCID_CALLS_REVISION	0xFF03
#define TEESMC32_CALLS_REVISION \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, \
			TEESMC_OWNER_TRUSTED_OS_API, \
			TEESMC32_FUNCID_CALLS_REVISION)

/*
 * Get UUID of Trusted OS.
 *
 * Used by non-secure world to figure out which Trusted OS is installed.
 * Note that returned UUID is the UUID of the Trusted OS, not of the API.
 *
 * Returns UUID in r0-4/w0-4 in the same way as TEESMC32_CALLS_UID
 * described above.
 */
#define TEESMC_FUNCID_GET_OS_UUID	0
#define TEESMC32_CALL_GET_OS_UUID \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_GET_OS_UUID)

/*
 * Get revision of Trusted OS.
 *
 * Used by non-secure world to figure out which version of the Trusted OS
 * is installed. Note that the returned revision is the revision of the
 * Trusted OS, not of the API.
 *
 * Returns revision in r0-1/w0-1 in the same way as TEESMC32_CALLS_REVISION
 * described above.
 */
#define TEESMC_FUNCID_GET_OS_REVISION	1
#define TEESMC32_CALL_GET_OS_REVISION \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_GET_OS_REVISION)



/*
 * Call with struct teesmc32_arg as argument
 *
 * Call register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_WITH_ARG
 * r1/x1	Physical pointer to a struct teesmc32_arg
 * r2-6/x2-6	Not used
 * r7/x7	Hypervisor Client ID register
 *
 * Normal return register usage:
 * r0/x0	Return value, TEESMC_RETURN_*
 * r1-3/x1-3	Not used
 * r4-7/x4-7	Preserved
 *
 * Ebusy return register usage:
 * r0/x0	Return value, TEESMC_RETURN_ETHREAD_LIMIT
 * r1-3/x1-3	Preserved
 * r4-7/x4-7	Preserved
 *
 * RPC return register usage:
 * r0/x0	Return value, TEESMC_RETURN_IS_RPC(val)
 * r1-2/x1-2	RPC parameters
 * r3-7/x3-7	Resume information, must be preserved
 *
 * Possible return values:
 * TEESMC_RETURN_UNKNOWN_FUNCTION	Trusted OS does not recognize this
 *					function.
 * TEESMC_RETURN_OK			Call completed, result updated in
 *					the previously supplied struct
 *					teesmc32_arg.
 * TEESMC_RETURN_ETHREAD_LIMIT		Trusted OS out of threads,
 *					try again later.
 * TEESMC_RETURN_EBADADDR		Bad physcial pointer to struct
 *					teesmc32_arg.
 * TEESMC_RETURN_EBADCMD		Bad/unknown cmd in struct teesmc32_arg
 * TEESMC_RETURN_IS_RPC()		Call suspended by RPC call to normal
 *					world.
 */
#define TEESMC_FUNCID_CALL_WITH_ARG	2
#define TEESMC32_CALL_WITH_ARG \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_STD_CALL, TEESMC_OWNER_TRUSTED_OS, \
	TEESMC_FUNCID_CALL_WITH_ARG)
/* Same as TEESMC32_CALL_WITH_ARG but a "fast call". */
#define TEESMC32_FASTCALL_WITH_ARG \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
	TEESMC_FUNCID_CALL_WITH_ARG)

/*
 * Call with struct teesmc64_arg as argument
 *
 * See description of TEESMC32_CALL_WITH_ARG above, uses struct
 * teesmc64_arg in x1 instead.
 */
#define TEESMC64_CALL_WITH_ARG \
	TEESMC_CALL_VAL(TEESMC_64, TEESMC_STD_CALL, TEESMC_OWNER_TRUSTED_OS, \
	TEESMC_FUNCID_CALL_WITH_ARG)
/* Same as TEESMC64_CALL_WITH_ARG but a "fast call". */
#define TEESMC64_FASTCALL_WITH_ARG \
	TEESMC_CALL_VAL(TEESMC_64, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
	TEESMC_FUNCID_CALL_WITH_ARG)

/*
 * Resume from RPC (for example after processing an IRQ)
 *
 * Call register usage:
 * r0/x0	SMC Function ID,
 *		TEESMC32_CALL_RETURN_FROM_RPC or
 *		TEESMC32_FASTCALL_RETURN_FROM_RPC
 * r1-3/x1-3	Value of r1-3/x1-3 when TEESMC32_CALL_WITH_ARG returned
 *		TEESMC_RETURN_RPC in r0/x0
 *
 * Return register usage is the same as for TEESMC32_CALL_WITH_ARG above.
 *
 * Possible return values
 * TEESMC_RETURN_UNKNOWN_FUNCTION	Trusted OS does not recognize this
 *					function.
 * TEESMC_RETURN_OK			Original call completed, result
 *					updated in the previously supplied.
 *					struct teesmc32_arg
 * TEESMC_RETURN_RPC			Call suspended by RPC call to normal
 *					world.
 * TEESMC_RETURN_ETHREAD_LIMIT		Trusted OS out of threads,
 *					try again later.
 * TEESMC_RETURN_ERESUME		Resume failed, the opaque resume
 *					information was corrupt.
 */
#define TEESMC_FUNCID_RETURN_FROM_RPC	3
#define TEESMC32_CALL_RETURN_FROM_RPC \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_STD_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_RETURN_FROM_RPC)
/* Same as TEESMC32_CALL_RETURN_FROM_RPC but a "fast call". */
#define TEESMC32_FASTCALL_RETURN_FROM_RPC \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_RETURN_FROM_RPC)

/*
 * Resume from RPC (for example after processing an IRQ)
 *
 * See description of TEESMC32_CALL_RETURN_FROM_RPC above, used when
 * it's a 64bit call that has returned.
 */
#define TEESMC64_CALL_RETURN_FROM_RPC \
	TEESMC_CALL_VAL(TEESMC_64, TEESMC_STD_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_RETURN_FROM_RPC)
/* Same as TEESMC64_CALL_RETURN_FROM_RPC but a "fast call". */
#define TEESMC64_FASTCALL_RETURN_FROM_RPC \
	TEESMC_CALL_VAL(TEESMC_64, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_RETURN_FROM_RPC)

/*
 * From secure monitor to Trusted OS, handle FIQ
 *
 * A virtual call which is injected by the Secure Monitor when an FIQ is
 * raised while in normal world (SCR_NS is set). The monitor restores
 * secure architecture registers and secure EL_SP1 and jumps to previous
 * secure EL3_ELR. Trusted OS should preserve all general purpose
 * registers.
 *
 * Call register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_HANDLE_FIQ
 * r1-7/x1-7	Not used, but must be preserved
 *
 * Return register usage:
 * Note used
 */
#define TEESMC_FUNCID_CALL_HANDLE_FIQ	0xf000
#define TEESMC32_CALL_HANDLE_FIQ \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_FUNCID_CALL_HANDLE_FIQ)

#define TEESMC_RETURN_RPC_PREFIX_MASK	0xFFFF0000
#define TEESMC_RETURN_RPC_PREFIX	0xFFFF0000
#define TEESMC_RETURN_RPC_FUNC_MASK	0x0000FFFF

#define TEESMC_RETURN_GET_RPC_FUNC(ret)	((ret) & TEESMC_RETURN_RPC_FUNC_MASK)

#define TEESMC_RPC_VAL(func)		((func) | TEESMC_RETURN_RPC_PREFIX)

/*
 * Allocate argument memory for RPC parameter passing.
 * Argument memory is used to hold a struct teesmc32_arg.
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_RPC_ALLOC
 * r1/x1	Size in bytes of required argument memory
 * r2-7/x2-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1/x1	Physical pointer to allocated argument memory, 0 if size
 *		was 0 or if memory can't be allocated
 * r2-7/x2-7	Preserved
 */
#define TEESMC_RPC_FUNC_ALLOC_ARG	0
#define TEESMC_RETURN_RPC_ALLOC_ARG	\
	TEESMC_RPC_VAL(TEESMC_RPC_FUNC_ALLOC_ARG)

/*
 * Allocate payload memory for RPC parameter passing.
 * Payload memory is used to hold the memory referred to by struct
 * teesmc32_param_memref.
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_RPC_ALLOC
 * r1/x1	Size in bytes of required payload memory
 * r2-7/x2-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1/x1	Physical pointer to allocated payload memory, 0 if size
 *		was 0 or if memory can't be allocated
 * r2-7/x2-7	Preserved
 */
#define TEESMC_RPC_FUNC_ALLOC_PAYLOAD	1
#define TEESMC_RETURN_RPC_ALLOC_PAYLOAD	\
	TEESMC_RPC_VAL(TEESMC_RPC_FUNC_ALLOC_PAYLOAD)

/*
 * Free memory previously allocated by TEESMC_RETURN_RPC_ALLOC_ARG.
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_RPC_FREE
 * r1/x1	Physical pointer to previously allocated argument memory
 * r2-7/x2-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1/x1	Not used
 * r2-7/x2-7	Preserved
 */
#define TEESMC_RPC_FUNC_FREE_ARG	2
#define TEESMC_RETURN_RPC_FREE_ARG	TEESMC_RPC_VAL(TEESMC_RPC_FUNC_FREE_ARG)

/*
 * Free memory previously allocated by TEESMC_RETURN_RPC_ALLOC_PAYLOAD.
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_RPC_FREE
 * r1/x1	Physical pointer to previously allocated payload memory
 * r3-7/x3-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1-2/x1-2	Not used
 * r3-7/x3-7	Preserved
 */
#define TEESMC_RPC_FUNC_FREE_PAYLOAD	3
#define TEESMC_RETURN_RPC_FREE_PAYLOAD	\
	TEESMC_RPC_VAL(TEESMC_RPC_FUNC_FREE_PAYLOAD)

/*
 * Deliver an IRQ in normal world.
 *
 * "Call" register usage:
 * r0/x0	TEESMC_RETURN_RPC_IRQ
 * r1-7/x1-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1-7/x1-7	Preserved
 */
#define TEESMC_RPC_FUNC_IRQ		4
#define TEESMC_RETURN_RPC_IRQ		TEESMC_RPC_VAL(TEESMC_RPC_FUNC_IRQ)

/*
 * Do an RPC request. The supplied struct teesmc{32,64}_arg tells which
 * request to do and the paramters for the request. The following fields
 * are used (the rest are unused):
 * - cmd		the Request ID
 * - ret		return value of the request, filled in by normal world
 * - num_params		number of parameters for the request
 * - params		the parameters
 * - param_attrs	attributes of the parameters
 *
 * "Call" register usage:
 * r0/x0	TEESMC_RETURN_RPC_CMD
 * r1/x1	Physical pointer to a struct teesmc32_arg if returning from
 *		a AArch32 SMC or a struct teesmc64_arg if returning from a
 *		AArch64 SMC, must be preserved, only the data should
 *		be updated
 * r2-7/x2-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1-7/x1-7	Preserved
 */
#define TEESMC_RPC_FUNC_CMD		5
#define TEESMC_RETURN_RPC_CMD		TEESMC_RPC_VAL(TEESMC_RPC_FUNC_CMD)


/* Returned in r0 */
#define TEESMC_RETURN_UNKNOWN_FUNCTION	0xFFFFFFFF

/* Returned in r0 only from Trusted OS functions */
#define TEESMC_RETURN_OK		0x0
#define TEESMC_RETURN_ETHREAD_LIMIT	0x1
#define TEESMC_RETURN_ERESUME		0x2
#define TEESMC_RETURN_EBADADDR		0x3
#define TEESMC_RETURN_EBADCMD		0x4
#define TEESMC_RETURN_IS_RPC(ret) \
	(((ret) & TEESMC_RETURN_RPC_PREFIX_MASK) == TEESMC_RETURN_RPC_PREFIX)

/*
 * Returned in r1 by Trusted OS functions if r0 = TEESMC_RETURN_RPC
 */
#define TEESMC_RPC_REQUEST_IRQ		0x0

#define TEESMC_ROCKCHIP_FUNCID_SET_UART_PORT		0x0000
#define TEESMC32_ROCKCHIP_FASTCALL_SET_UART_PORT \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS_ROCKCHIP, \
	TEESMC_ROCKCHIP_FUNCID_SET_UART_PORT)

#endif /* TEESMC_H */
