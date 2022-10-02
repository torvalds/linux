/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2018, Intel Corporation
 */

#ifndef __STRATIX10_SMC_H
#define __STRATIX10_SMC_H

#include <linux/arm-smccc.h>
#include <linux/bitops.h>

/**
 * This file defines the Secure Monitor Call (SMC) message protocol used for
 * service layer driver in normal world (EL1) to communicate with secure
 * monitor software in Secure Monitor Exception Level 3 (EL3).
 *
 * This file is shared with secure firmware (FW) which is out of kernel tree.
 *
 * An ARM SMC instruction takes a function identifier and up to 6 64-bit
 * register values as arguments, and can return up to 4 64-bit register
 * value. The operation of the secure monitor is determined by the parameter
 * values passed in through registers.
 *
 * EL1 and EL3 communicates pointer as physical address rather than the
 * virtual address.
 *
 * Functions specified by ARM SMC Calling convention:
 *
 * FAST call executes atomic operations, returns when the requested operation
 * has completed.
 * STD call starts a operation which can be preempted by a non-secure
 * interrupt. The call can return before the requested operation has
 * completed.
 *
 * a0..a7 is used as register names in the descriptions below, on arm32
 * that translates to r0..r7 and on arm64 to w0..w7.
 */

/**
 * @func_num: function ID
 */
#define INTEL_SIP_SMC_STD_CALL_VAL(func_num) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_64, \
	ARM_SMCCC_OWNER_SIP, (func_num))

#define INTEL_SIP_SMC_FAST_CALL_VAL(func_num) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
	ARM_SMCCC_OWNER_SIP, (func_num))

/**
 * Return values in INTEL_SIP_SMC_* call
 *
 * INTEL_SIP_SMC_RETURN_UNKNOWN_FUNCTION:
 * Secure monitor software doesn't recognize the request.
 *
 * INTEL_SIP_SMC_STATUS_OK:
 * Secure monitor software accepts the service client's request.
 *
 * INTEL_SIP_SMC_STATUS_BUSY:
 * Secure monitor software is still processing service client's request.
 *
 * INTEL_SIP_SMC_STATUS_REJECTED:
 * Secure monitor software reject the service client's request.
 *
 * INTEL_SIP_SMC_STATUS_ERROR:
 * There is error during the process of service request.
 *
 * INTEL_SIP_SMC_RSU_ERROR:
 * There is error during the process of remote status update request.
 */
#define INTEL_SIP_SMC_RETURN_UNKNOWN_FUNCTION		0xFFFFFFFF
#define INTEL_SIP_SMC_STATUS_OK				0x0
#define INTEL_SIP_SMC_STATUS_BUSY			0x1
#define INTEL_SIP_SMC_STATUS_REJECTED			0x2
#define INTEL_SIP_SMC_STATUS_ERROR			0x4
#define INTEL_SIP_SMC_RSU_ERROR				0x7

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_START
 *
 * Sync call used by service driver at EL1 to request the FPGA in EL3 to
 * be prepare to receive a new configuration.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_FPGA_CONFIG_START.
 * a1: flag for full or partial configuration. 0 for full and 1 for partial
 * configuration.
 * a2-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK, or INTEL_SIP_SMC_STATUS_ERROR.
 * a1-3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_START 1
#define INTEL_SIP_SMC_FPGA_CONFIG_START \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_START)

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_WRITE
 *
 * Async call used by service driver at EL1 to provide FPGA configuration data
 * to secure world.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_FPGA_CONFIG_WRITE.
 * a1: 64bit physical address of the configuration data memory block
 * a2: Size of configuration data block.
 * a3-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_STATUS_BUSY or
 * INTEL_SIP_SMC_STATUS_ERROR.
 * a1: 64bit physical address of 1st completed memory block if any completed
 * block, otherwise zero value.
 * a2: 64bit physical address of 2nd completed memory block if any completed
 * block, otherwise zero value.
 * a3: 64bit physical address of 3rd completed memory block if any completed
 * block, otherwise zero value.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_WRITE 2
#define INTEL_SIP_SMC_FPGA_CONFIG_WRITE \
	INTEL_SIP_SMC_STD_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_WRITE)

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_COMPLETED_WRITE
 *
 * Sync call used by service driver at EL1 to track the completed write
 * transactions. This request is called after INTEL_SIP_SMC_FPGA_CONFIG_WRITE
 * call returns INTEL_SIP_SMC_STATUS_BUSY.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_FPGA_CONFIG_COMPLETED_WRITE.
 * a1-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_FPGA_BUSY or
 * INTEL_SIP_SMC_STATUS_ERROR.
 * a1: 64bit physical address of 1st completed memory block.
 * a2: 64bit physical address of 2nd completed memory block if
 * any completed block, otherwise zero value.
 * a3: 64bit physical address of 3rd completed memory block if
 * any completed block, otherwise zero value.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_COMPLETED_WRITE 3
#define INTEL_SIP_SMC_FPGA_CONFIG_COMPLETED_WRITE \
INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_COMPLETED_WRITE)

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_ISDONE
 *
 * Sync call used by service driver at EL1 to inform secure world that all
 * data are sent, to check whether or not the secure world had completed
 * the FPGA configuration process.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_FPGA_CONFIG_ISDONE.
 * a1-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_STATUS_BUSY or
 * INTEL_SIP_SMC_STATUS_ERROR.
 * a1-3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_ISDONE 4
#define INTEL_SIP_SMC_FPGA_CONFIG_ISDONE \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_ISDONE)

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_GET_MEM
 *
 * Sync call used by service driver at EL1 to query the physical address of
 * memory block reserved by secure monitor software.
 *
 * Call register usage:
 * a0:INTEL_SIP_SMC_FPGA_CONFIG_GET_MEM.
 * a1-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_STATUS_ERROR.
 * a1: start of physical address of reserved memory block.
 * a2: size of reserved memory block.
 * a3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_GET_MEM 5
#define INTEL_SIP_SMC_FPGA_CONFIG_GET_MEM \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_GET_MEM)

/**
 * Request INTEL_SIP_SMC_FPGA_CONFIG_LOOPBACK
 *
 * For SMC loop-back mode only, used for internal integration, debugging
 * or troubleshooting.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_FPGA_CONFIG_LOOPBACK.
 * a1-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_STATUS_ERROR.
 * a1-3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_LOOPBACK 6
#define INTEL_SIP_SMC_FPGA_CONFIG_LOOPBACK \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FPGA_CONFIG_LOOPBACK)

/**
 * Request INTEL_SIP_SMC_REG_READ
 *
 * Read a protected register at EL3
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_REG_READ.
 * a1: register address.
 * a2-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_REG_ERROR.
 * a1: value in the register
 * a2-3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_REG_READ 7
#define INTEL_SIP_SMC_REG_READ \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_REG_READ)

/**
 * Request INTEL_SIP_SMC_REG_WRITE
 *
 * Write a protected register at EL3
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_REG_WRITE.
 * a1: register address
 * a2: value to program into register.
 * a3-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_REG_ERROR.
 * a1-3: not used.
 */
#define INTEL_SIP_SMC_FUNCID_REG_WRITE 8
#define INTEL_SIP_SMC_REG_WRITE \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_REG_WRITE)

/**
 * Request INTEL_SIP_SMC_FUNCID_REG_UPDATE
 *
 * Update one or more bits in a protected register at EL3 using a
 * read-modify-write operation.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_REG_UPDATE.
 * a1: register address
 * a2: write Mask.
 * a3: value to write.
 * a4-7: not used.
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_REG_ERROR.
 * a1-3: Not used.
 */
#define INTEL_SIP_SMC_FUNCID_REG_UPDATE 9
#define INTEL_SIP_SMC_REG_UPDATE \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_REG_UPDATE)

/**
 * Request INTEL_SIP_SMC_RSU_STATUS
 *
 * Request remote status update boot log, call is synchronous.
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_STATUS
 * a1-7 not used
 *
 * Return status
 * a0: Current Image
 * a1: Last Failing Image
 * a2: Version | State
 * a3: Error details | Error location
 *
 * Or
 *
 * a0: INTEL_SIP_SMC_RSU_ERROR
 */
#define INTEL_SIP_SMC_FUNCID_RSU_STATUS 11
#define INTEL_SIP_SMC_RSU_STATUS \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_STATUS)

/**
 * Request INTEL_SIP_SMC_RSU_UPDATE
 *
 * Request to set the offset of the bitstream to boot after reboot, call
 * is synchronous.
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_UPDATE
 * a1 64bit physical address of the configuration data memory in flash
 * a2-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 */
#define INTEL_SIP_SMC_FUNCID_RSU_UPDATE 12
#define INTEL_SIP_SMC_RSU_UPDATE \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_UPDATE)

/**
 * Request INTEL_SIP_SMC_ECC_DBE
 *
 * Sync call used by service driver at EL1 to alert EL3 that a Double
 * Bit ECC error has occurred.
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_ECC_DBE
 * a1 SysManager Double Bit Error value
 * a2-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 */
#define INTEL_SIP_SMC_FUNCID_ECC_DBE 13
#define INTEL_SIP_SMC_ECC_DBE \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_ECC_DBE)

/**
 * Request INTEL_SIP_SMC_RSU_NOTIFY
 *
 * Sync call used by service driver at EL1 to report hard processor
 * system execution stage to firmware
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_NOTIFY
 * a1 32bit value representing hard processor system execution stage
 * a2-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 */
#define INTEL_SIP_SMC_FUNCID_RSU_NOTIFY 14
#define INTEL_SIP_SMC_RSU_NOTIFY \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_NOTIFY)

/**
 * Request INTEL_SIP_SMC_RSU_RETRY_COUNTER
 *
 * Sync call used by service driver at EL1 to query RSU retry counter
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_RETRY_COUNTER
 * a1-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 * a1 the retry counter
 *
 * Or
 *
 * a0 INTEL_SIP_SMC_RSU_ERROR
 */
#define INTEL_SIP_SMC_FUNCID_RSU_RETRY_COUNTER 15
#define INTEL_SIP_SMC_RSU_RETRY_COUNTER \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_RETRY_COUNTER)

/**
 * Request INTEL_SIP_SMC_RSU_DCMF_VERSION
 *
 * Sync call used by service driver at EL1 to query DCMF (Decision
 * Configuration Management Firmware) version from FW
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_DCMF_VERSION
 * a1-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 * a1 dcmf1 | dcmf0
 * a2 dcmf3 | dcmf2
 *
 * Or
 *
 * a0 INTEL_SIP_SMC_RSU_ERROR
 */
#define INTEL_SIP_SMC_FUNCID_RSU_DCMF_VERSION 16
#define INTEL_SIP_SMC_RSU_DCMF_VERSION \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_DCMF_VERSION)

/**
 * Request INTEL_SIP_SMC_RSU_MAX_RETRY
 *
 * Sync call used by service driver at EL1 to query max retry value from FW
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_MAX_RETRY
 * a1-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 * a1 max retry value
 *
 * Or
 * a0 INTEL_SIP_SMC_RSU_ERROR
 */
#define INTEL_SIP_SMC_FUNCID_RSU_MAX_RETRY 18
#define INTEL_SIP_SMC_RSU_MAX_RETRY \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_MAX_RETRY)

/**
 * Request INTEL_SIP_SMC_RSU_DCMF_STATUS
 *
 * Sync call used by service driver at EL1 to query DCMF status from FW
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_RSU_DCMF_STATUS
 * a1-7 not used
 *
 * Return status
 * a0 INTEL_SIP_SMC_STATUS_OK
 * a1 dcmf3 | dcmf2 | dcmf1 | dcmf0
 *
 * Or
 *
 * a0 INTEL_SIP_SMC_RSU_ERROR
 */
#define INTEL_SIP_SMC_FUNCID_RSU_DCMF_STATUS 20
#define INTEL_SIP_SMC_RSU_DCMF_STATUS \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_RSU_DCMF_STATUS)

/**
 * Request INTEL_SIP_SMC_SERVICE_COMPLETED
 * Sync call to check if the secure world have completed service request
 * or not.
 *
 * Call register usage:
 * a0: INTEL_SIP_SMC_SERVICE_COMPLETED
 * a1: this register is optional. If used, it is the physical address for
 *     secure firmware to put output data
 * a2: this register is optional. If used, it is the size of output data
 * a3-a7: not used
 *
 * Return status:
 * a0: INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_STATUS_ERROR,
 *     INTEL_SIP_SMC_REJECTED or INTEL_SIP_SMC_STATUS_BUSY
 * a1: mailbox error if a0 is INTEL_SIP_SMC_STATUS_ERROR
 * a2: physical address containing the process info
 *     for FCS certificate -- the data contains the certificate status
 *     for FCS cryption -- the data contains the actual data size FW processes
 * a3: output data size
 */
#define INTEL_SIP_SMC_FUNCID_SERVICE_COMPLETED 30
#define INTEL_SIP_SMC_SERVICE_COMPLETED \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_SERVICE_COMPLETED)

/**
 * Request INTEL_SIP_SMC_FIRMWARE_VERSION
 *
 * Sync call used to query the version of running firmware
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FIRMWARE_VERSION
 * a1-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_STATUS_ERROR
 * a1 running firmware version
 */
#define INTEL_SIP_SMC_FUNCID_FIRMWARE_VERSION 31
#define INTEL_SIP_SMC_FIRMWARE_VERSION \
        INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FIRMWARE_VERSION)

/**
 * Request INTEL_SIP_SMC_SVC_VERSION
 *
 * Sync call used to query the SIP SMC API Version
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_SVC_VERSION
 * a1-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK
 * a1 Major
 * a2 Minor
 */
#define INTEL_SIP_SMC_SVC_FUNCID_VERSION 512
#define INTEL_SIP_SMC_SVC_VERSION \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_SVC_FUNCID_VERSION)

/**
 * SMC call protocol for FPGA Crypto Service (FCS)
 * FUNCID starts from 90
 */

/**
 * Request INTEL_SIP_SMC_FCS_RANDOM_NUMBER
 *
 * Sync call used to query the random number generated by the firmware
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FCS_RANDOM_NUMBER
 * a1 the physical address for firmware to write generated random data
 * a2-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_FCS_ERROR or
 *      INTEL_SIP_SMC_FCS_REJECTED
 * a1 mailbox error
 * a2 the physical address of generated random number
 * a3 size
 */
#define INTEL_SIP_SMC_FUNCID_FCS_RANDOM_NUMBER 90
#define INTEL_SIP_SMC_FCS_RANDOM_NUMBER \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FCS_RANDOM_NUMBER)

/**
 * Request INTEL_SIP_SMC_FCS_CRYPTION
 * Async call for data encryption and HMAC signature generation, or for
 * data decryption and HMAC verification.
 *
 * Call INTEL_SIP_SMC_SERVICE_COMPLETED to get the output encrypted or
 * decrypted data
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FCS_CRYPTION
 * a1 cryption mode (1 for encryption and 0 for decryption)
 * a2 physical address which stores to be encrypted or decrypted data
 * a3 input data size
 * a4 physical address which will hold the encrypted or decrypted output data
 * a5 output data size
 * a6-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_STATUS_ERROR or
 *      INTEL_SIP_SMC_STATUS_REJECTED
 * a1-3 not used
 */
#define INTEL_SIP_SMC_FUNCID_FCS_CRYPTION 91
#define INTEL_SIP_SMC_FCS_CRYPTION \
	INTEL_SIP_SMC_STD_CALL_VAL(INTEL_SIP_SMC_FUNCID_FCS_CRYPTION)

/**
 * Request INTEL_SIP_SMC_FCS_SERVICE_REQUEST
 * Async call for authentication service of HPS software
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FCS_SERVICE_REQUEST
 * a1 the physical address of data block
 * a2 size of data block
 * a3-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_ERROR or
 *      INTEL_SIP_SMC_REJECTED
 * a1-a3 not used
 */
#define INTEL_SIP_SMC_FUNCID_FCS_SERVICE_REQUEST 92
#define INTEL_SIP_SMC_FCS_SERVICE_REQUEST \
	INTEL_SIP_SMC_STD_CALL_VAL(INTEL_SIP_SMC_FUNCID_FCS_SERVICE_REQUEST)

/**
 * Request INTEL_SIP_SMC_FUNCID_FCS_SEND_CERTIFICATE
 * Sync call to send a signed certificate
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FCS_SEND_CERTIFICATE
 * a1 the physical address of CERTIFICATE block
 * a2 size of data block
 * a3-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK or INTEL_SIP_SMC_FCS_REJECTED
 * a1-a3 not used
 */
#define INTEL_SIP_SMC_FUNCID_FCS_SEND_CERTIFICATE 93
#define INTEL_SIP_SMC_FCS_SEND_CERTIFICATE \
	INTEL_SIP_SMC_STD_CALL_VAL(INTEL_SIP_SMC_FUNCID_FCS_SEND_CERTIFICATE)

/**
 * Request INTEL_SIP_SMC_FCS_GET_PROVISION_DATA
 * Sync call to dump all the fuses and key hashes
 *
 * Call register usage:
 * a0 INTEL_SIP_SMC_FCS_GET_PROVISION_DATA
 * a1 the physical address for firmware to write structure of fuse and
 *    key hashes
 * a2-a7 not used
 *
 * Return status:
 * a0 INTEL_SIP_SMC_STATUS_OK, INTEL_SIP_SMC_FCS_ERROR or
 *      INTEL_SIP_SMC_FCS_REJECTED
 * a1 mailbox error
 * a2 physical address for the structure of fuse and key hashes
 * a3 the size of structure
 *
 */
#define INTEL_SIP_SMC_FUNCID_FCS_GET_PROVISION_DATA 94
#define INTEL_SIP_SMC_FCS_GET_PROVISION_DATA \
	INTEL_SIP_SMC_FAST_CALL_VAL(INTEL_SIP_SMC_FUNCID_FCS_GET_PROVISION_DATA)

#endif
