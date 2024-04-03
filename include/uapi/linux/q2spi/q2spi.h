/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _UAPI_LINUX_Q2SPI_H
#define _UAPI_LINUX_Q2SPI_H
#include <linux/types.h>
#include "asm-generic/errno-base.h"
/**
 * enum cmd_type - q2spi request command type
 * @LOCAL_REG_READ:  this command is used to read Q2SPI slave local register space.
 * @LOCAL_REG_WRITE: this command is used to write Q2SPI slave local register space.
 * @DATA_READ:       this command is used to read bulk data from Q2SPI slave.
 * @DATA_WRITE:      this command is used to write bulk data to Q2SPI slave.
 * @HRF_READ:        this command is used to read data from Q2SPI slave HRF.
 * @HRF_WRITE:       this command is used to write data to Q2SPI slave HRF.
 * @SOFT_RESET:      this command is used to reset Q2SPI slave.
 * @ABORT:           this command is used to abort the CR Q2SPI slave.
 */
enum cmd_type {
	LOCAL_REG_READ = 0,
	LOCAL_REG_WRITE = 1,
	DATA_READ = 2,
	DATA_WRITE = 3,
	HRF_READ = 4,
	HRF_WRITE = 5,
	SOFT_RESET = 6,
	ABORT = 7,
	INVALID_CMD = -EINVAL,
};

/**
 * enum priority_type - priority of Q2SPI transfer request
 * @NORMAL: user space client specifies this for normal priority transfer.
 * @HIGH:   user space client specifies this for high priority transfer.
 * @LOW:    same as NORMAL. Reserved for future use.
 */
enum priority_type {
	NORMAL = 0,
	HIGH = 1,
	LOW = NORMAL,
	INVALID_TYPE = -EINVAL,
};

/**
 * enum xfer_status - indicate status of the transfer
 * @SUCCESS:        indicates success
 * @FAILURE:        indicates failure
 * @OVERFLOW:       indicates TX buffer overflow
 * @UNDERFLOW:      indicates RX buffer underflow
 * @RESPONSE_ERROR: indicates AHB response error
 * @CHECKSUM_ERROR: indicates checksum error
 * @TIMEOUT:        timeout for a transfer
 * @OTHER:          reserved for future purpose
 */
enum xfer_status {
	SUCCESS = 0,
	FAILURE = 1,
	OVERFLOW = 2,
	UNDERFLOW = 3,
	RESPONSE_ERROR = 4,
	CHECKSUM_ERROR = 5,
	TIMEOUT = 6,
	OTHER = 7,
	INVALID_STATUS = -EINVAL,
};

/**
 * struct q2spi_request - structure to pass Q2SPI transfer request(read/write) to driver
 * @data_buff:    stores data buffer pointer passed from user space client. First byte filled
 *                by user space client to specify Q2SPI slave(Ganges) configuration(HCI/UCI).
 * @cmd:          represents command type of a transfer request.
 * @addr:         user space client will use this field to indicate any specific address
 *                used for transfer request.
 * @end_point:    user space client specifies source endpoint information using this field.
 * @proto_ind:    user space client specifies protocol indicators BT or UWB using this field.
 * @data_len:     represents transfer length of the transaction.
 * @priority:     priority of Q2SPI transfer request, Valid only in async mode.
 * @sync:         by default synchronous transfer are used.
 *                user space client can use this to specify synchronous or asynchronous
 *                transfers are used.
 * @flow_id:      unique flow id of the transfer assigned by q2spi interface driver. In
 *                asynchronous mode write api returns this flow id to userspace.
 * @reserved[20]: reserved for future purpose.
 *
 * This structure is to send information to q2spi driver from user space.
 */

struct q2spi_request {
	void *data_buff;
	enum cmd_type cmd;
	__u32 addr;
	__u8 end_point;
	__u8 proto_ind;
	__u32 data_len;
	enum priority_type priority;
	__u8 flow_id;
	_Bool sync;
	__u32 reserved[20];
};

/**
 * struct q2spi_client_request - structure to retrieve Q2SPI client request information.
 * @data_buff:    points to data buffer pointer passed from user space client
 * @data_len:     represents transfer length
 * @end_point:    q2spi driver copy CR data arg2 information in this field.
 * @proto_ind:    q2spi driver copy CR data arg3 information in this field.
 * @cmd:          represents command type of a transfer request.
 * @status:       response status for the request
 * @flow_id:      unique flow id of the transfer assigned by q2spi interface driver.
 *                In asynchronous mode write api returns this flow id to userspace,
 *                application can use this flow_id to match the response it
 *                received asynchronously from q2spi driver.
 * @reserved[20]: reserved for future purpose
 *
 * q2spi driver will copy this to user space client as a part of read API which contains previous
 * request response from Q2SPI slave or new client request from Q2SPI slave.
 */

struct q2spi_client_request {
	void *data_buff;
	__u32 data_len;
	__u8 end_point;
	__u8 proto_ind;
	enum cmd_type cmd;
	enum xfer_status status;
	__u8 flow_id;
	__u32 reserved[20];
};
#endif /* _UAPI_LINUX_Q2SPI_H */
