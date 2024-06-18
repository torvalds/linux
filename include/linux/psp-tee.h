/* SPDX-License-Identifier: MIT */
/*
 * AMD Trusted Execution Environment (TEE) interface
 *
 * Author: Rijo Thomas <Rijo-john.Thomas@amd.com>
 *
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 */

#ifndef __PSP_TEE_H_
#define __PSP_TEE_H_

#include <linux/types.h>
#include <linux/errno.h>

/* This file defines the Trusted Execution Environment (TEE) interface commands
 * and the API exported by AMD Secure Processor driver to communicate with
 * AMD-TEE Trusted OS.
 */

/**
 * enum tee_cmd_id - TEE Interface Command IDs
 * @TEE_CMD_ID_LOAD_TA:          Load Trusted Application (TA) binary into
 *                               TEE environment
 * @TEE_CMD_ID_UNLOAD_TA:        Unload TA binary from TEE environment
 * @TEE_CMD_ID_OPEN_SESSION:     Open session with loaded TA
 * @TEE_CMD_ID_CLOSE_SESSION:    Close session with loaded TA
 * @TEE_CMD_ID_INVOKE_CMD:       Invoke a command with loaded TA
 * @TEE_CMD_ID_MAP_SHARED_MEM:   Map shared memory
 * @TEE_CMD_ID_UNMAP_SHARED_MEM: Unmap shared memory
 */
enum tee_cmd_id {
	TEE_CMD_ID_LOAD_TA = 1,
	TEE_CMD_ID_UNLOAD_TA,
	TEE_CMD_ID_OPEN_SESSION,
	TEE_CMD_ID_CLOSE_SESSION,
	TEE_CMD_ID_INVOKE_CMD,
	TEE_CMD_ID_MAP_SHARED_MEM,
	TEE_CMD_ID_UNMAP_SHARED_MEM,
};

#ifdef CONFIG_CRYPTO_DEV_SP_PSP
/**
 * psp_tee_process_cmd() - Process command in Trusted Execution Environment
 * @cmd_id:     TEE command ID (&enum tee_cmd_id)
 * @buf:        Command buffer for TEE processing. On success, is updated
 *              with the response
 * @len:        Length of command buffer in bytes
 * @status:     On success, holds the TEE command execution status
 *
 * This function submits a command to the Trusted OS for processing in the
 * TEE environment and waits for a response or until the command times out.
 *
 * Returns:
 * 0 if TEE successfully processed the command
 * -%ENODEV    if PSP device not available
 * -%EINVAL    if invalid input
 * -%ETIMEDOUT if TEE command timed out
 * -%EBUSY     if PSP device is not responsive
 */
int psp_tee_process_cmd(enum tee_cmd_id cmd_id, void *buf, size_t len,
			u32 *status);

/**
 * psp_check_tee_status() - Checks whether there is a TEE which a driver can
 * talk to.
 *
 * This function can be used by AMD-TEE driver to query if there is TEE with
 * which it can communicate.
 *
 * Returns:
 * 0          if the device has TEE
 * -%ENODEV   if there is no TEE available
 */
int psp_check_tee_status(void);

#else /* !CONFIG_CRYPTO_DEV_SP_PSP */

static inline int psp_tee_process_cmd(enum tee_cmd_id cmd_id, void *buf,
				      size_t len, u32 *status)
{
	return -ENODEV;
}

static inline int psp_check_tee_status(void)
{
	return -ENODEV;
}
#endif /* CONFIG_CRYPTO_DEV_SP_PSP */
#endif /* __PSP_TEE_H_ */
