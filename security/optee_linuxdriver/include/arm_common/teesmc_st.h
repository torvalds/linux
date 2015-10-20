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
#ifndef TEESMC_ST_H
#define TEESMC_ST_H

#define TEESMC_ST_RETURN_NOTAVAIL	0x5700

/*
 * Get Shared Memory Config
 *
 * Returns the Secure/Non-secure shared memory config.
 *
 * Call register usage:
 * r0	SMC Function ID, TEESMC32_ST_FASTCALL_GET_SHM_CONFIG
 * r1-6	Not used
 * r7	Hypervisor Client ID register
 *
 * Have config return register usage:
 * r0	TEESMC_RETURN_OK
 * r1	Physical address of start of SHM
 * r2	Size of of SHM
 * r3	1 if SHM is cached, 0 if uncached.
 * r4-7	Preserved
 *
 * Not available register usage:
 * r0	TEESMC_ST_RETURN_NOTAVAIL
 * r1-3 Not used
 * r4-7	Preserved
 */
#define TEESMC_ST_FUNCID_GET_SHM_CONFIG	0x5700
#define TEESMC32_ST_FASTCALL_GET_SHM_CONFIG \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_ST_FUNCID_GET_SHM_CONFIG)

/*
 * Configures TZ/NS shared mutex for outer cache maintenance
 *
 * Disables, enables usage of outercache mutex.
 * Returns or sets physical address of outercache mutex.
 *
 * Call register usage:
 * r0	SMC Function ID, TEESMC32_ST_FASTCALL_L2CC_MUTEX
 * r1	TEESMC_ST_L2CC_MUTEX_GET_ADDR	Get physical address of mutex
 *	TEESMC_ST_L2CC_MUTEX_SET_ADDR	Set physical address of mutex
 *	TEESMC_ST_L2CC_MUTEX_ENABLE	Enable usage of mutex
 *	TEESMC_ST_L2CC_MUTEX_DISABLE	Disable usage of mutex
 * r2	if r1 == TEESMC_ST_L2CC_MUTEX_SET_ADDR, physical address of mutex
 * r3-6	Not used
 * r7	Hypervisor Client ID register
 *
 * Have config return register usage:
 * r0	TEESMC_RETURN_OK
 * r1	Preserved
 * r2	if r1 == 0, physical address of L2CC mutex
 * r3-7	Preserved
 *
 * Error return register usage:
 * r0	TEESMC_ST_RETURN_NOTAVAIL	Physical address not available
 *	TEESMC_RETURN_EBADADDR		Bad supplied physical address
 *	TEESMC_RETURN_EBADCMD		Unsupported value in r1
 * r1-7	Preserved
 */
#define TEESMC_ST_L2CC_MUTEX_GET_ADDR	0
#define TEESMC_ST_L2CC_MUTEX_SET_ADDR	1
#define TEESMC_ST_L2CC_MUTEX_ENABLE	2
#define TEESMC_ST_L2CC_MUTEX_DISABLE	3
#define TEESMC_ST_FUNCID_L2CC_MUTEX	0x5701
#define TEESMC32_ST_FASTCALL_L2CC_MUTEX \
	TEESMC_CALL_VAL(TEESMC_32, TEESMC_FAST_CALL, TEESMC_OWNER_TRUSTED_OS, \
			TEESMC_ST_FUNCID_L2CC_MUTEX)

/*
 * Allocate payload memory for RPC parameter passing.
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_ST_RPC_ALLOC_PAYLOAD
 * r1/x1	Size in bytes of required payload memory
 * r2/x2	Not used
 * r3-7/x3-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r1/x1	Physical pointer to allocated payload memory, 0 if size
 *		was 0 or if memory can't be allocated
 * r2/x2	Shared memory cookie used when freeing the memory
 * r3-7/x3-7	Preserved
 */
#define TEESMC_ST_RPC_FUNC_ALLOC_PAYLOAD	0x5700
#define TEESMC_RETURN_ST_RPC_ALLOC_PAYLOAD	\
		TEESMC_RPC_VAL(TEESMC_ST_RPC_FUNC_ALLOC_PAYLOAD)


/*
 * Free memory previously allocated by TEESMC_RETURN_ST_RPC_ALLOC_PAYLOAD
 *
 * "Call" register usage:
 * r0/x0	This value, TEESMC_RETURN_ST_RPC_FREE_PAYLOAD
 * r1/x1	Shared memory cookie belonging to this payload memory
 * r2-7/x2-7	Resume information, must be preserved
 *
 * "Return" register usage:
 * r0/x0	SMC Function ID, TEESMC32_CALL_RETURN_FROM_RPC if it was an
 *		AArch32 SMC return or TEESMC64_CALL_RETURN_FROM_RPC for
 *		AArch64 SMC return
 * r2-7/x2-7	Preserved
 */
#define TEESMC_ST_RPC_FUNC_FREE_PAYLOAD		0x5701
#define TEESMC_RETURN_ST_RPC_FREE_PAYLOAD	\
		TEESMC_RPC_VAL(TEESMC_ST_RPC_FUNC_FREE_PAYLOAD)

/* Overriding default UID since the interface is extended
 * 384fb3e0-e7f8-11e3-af63-0002a5d5c51b
 */
#define TEESMC_ST_UID_R0		0x384fb3e0
#define TEESMC_ST_UID_R1		0xe7f811e3
#define TEESMC_ST_UID_R2		0xaf630002
#define TEESMC_ST_UID32_R3		0xa5d5c51b
#define TEESMC_ST_UID64_R3		0xa5d5c51c

#define TEESMC_ST_REVISION_MAJOR	1
#define TEESMC_ST_REVISION_MINOR	0

/*
 * UUID for OP-TEE
 * 486178e0-e7f8-11e3-bc5e-0002a5d5c51b
 */
#define TEESMC_OS_OPTEE_UUID_R0		0x486178e0
#define TEESMC_OS_OPTEE_UUID_R1		0xe7f811e3
#define TEESMC_OS_OPTEE_UUID_R2		0xbc5e0002
#define TEESMC_OS_OPTEE_UUID_R3		0xa5d5c51b

#define TEESMC_OS_OPTEE_REVISION_MAJOR	1
#define TEESMC_OS_OPTEE_REVISION_MINOR	0

#endif /*TEESMC_ST_H*/
