/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2023-2026 Intel Corporation
 * Intel Silicon Security Engine Interface (ISSEI) Linux driver:
 * ISSEI Interface Header
 */
#ifndef _LINUX_ISSEI_H
#define _LINUX_ISSEI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * This ioctl is used to associate the current file descriptor with a
 * FW Client (given by UUID). This opens a communication channel
 * between a host client and a FW client. From this point every read and write
 * will communicate with the associated FW client.
 * The communication between the clients can be terminated by
 * IOCTL_ISSEI_DISCONNECT_CLIENT IOCTL or by
 * closing the file descriptor (file_operation release()).
 *
 * The ioctl argument is a struct with a union that contains
 * the input parameter and the output parameter for this ioctl.
 *
 * The input parameter is UUID of the FW Client.
 * The output parameter is the properties of the FW client
 * (FW protocol version, max message size and client flags).
 */
#define IOCTL_ISSEI_CONNECT_CLIENT \
	_IOWR('H', 0x01, struct issei_connect_client_data)

/**
 * struct issei_client - ISSEI client information structure
 * @max_msg_length: maximum message length supported by the firmware client (in bytes)
 * @protocol_version: protocol version reported by the firmware client
 * @reserved1: reserved
 * @flags: flag bitmask reported by the firmware client
 * @reserved2: reserved
 */
struct issei_client {
	__u32 max_msg_length;
	__u8 protocol_version;
	__u8 reserved1[3];
	__u32 flags;
	__u32 reserved2;
};

#define ISSEI_IOCTL_UUID_LEN 16

/**
 * struct issei_connect_client_data - ioctl Connect Client Data structure
 * @in_client_uuid: unique id of the firmware client to connect to (from user space to kernel)
 * @out_client_properties: connected firmware client properties (from kernel to user space)
 */
struct issei_connect_client_data {
	union {
		__u8 in_client_uuid[ISSEI_IOCTL_UUID_LEN];
		struct issei_client out_client_properties;
	};
};

/*
 * This ioctl is used to terminate association between
 * the host client and the FW client.
 */
#define IOCTL_ISSEI_DISCONNECT_CLIENT \
	_IO('H', 0x02)

#endif /* _LINUX_ISSEI_H */
