/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright(c) 2003-2015 Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Intel MEI Interface Header
 */
#ifndef _LINUX_MEI_H
#define _LINUX_MEI_H

#include <linux/mei_uuid.h>

/*
 * This IOCTL is used to associate the current file descriptor with a
 * FW Client (given by UUID). This opens a communication channel
 * between a host client and a FW client. From this point every read and write
 * will communicate with the associated FW client.
 * Only in close() (file_operation release()) is the communication between
 * the clients disconnected.
 *
 * The IOCTL argument is a struct with a union that contains
 * the input parameter and the output parameter for this IOCTL.
 *
 * The input parameter is UUID of the FW Client.
 * The output parameter is the properties of the FW client
 * (FW protocol version and max message size).
 *
 */
#define IOCTL_MEI_CONNECT_CLIENT \
	_IOWR('H' , 0x01, struct mei_connect_client_data)

/*
 * Intel MEI client information struct
 */
struct mei_client {
	__u32 max_msg_length;
	__u8 protocol_version;
	__u8 reserved[3];
};

/*
 * IOCTL Connect Client Data structure
 */
struct mei_connect_client_data {
	union {
		uuid_le in_client_uuid;
		struct mei_client out_client_properties;
	};
};

/**
 * DOC: set and unset event notification for a connected client
 *
 * The IOCTL argument is 1 for enabling event notification and 0 for
 * disabling the service.
 * Return:  -EOPNOTSUPP if the devices doesn't support the feature
 */
#define IOCTL_MEI_NOTIFY_SET _IOW('H', 0x02, __u32)

/**
 * DOC: retrieve notification
 *
 * The IOCTL output argument is 1 if an event was pending and 0 otherwise.
 * The ioctl has to be called in order to acknowledge pending event.
 *
 * Return:  -EOPNOTSUPP if the devices doesn't support the feature
 */
#define IOCTL_MEI_NOTIFY_GET _IOR('H', 0x03, __u32)

/**
 * struct mei_connect_client_vtag - mei client information struct with vtag
 *
 * @in_client_uuid: UUID of client to connect
 * @vtag: virtual tag
 * @reserved: reserved for future use
 */
struct mei_connect_client_vtag {
	uuid_le in_client_uuid;
	__u8 vtag;
	__u8 reserved[3];
};

/**
 * struct mei_connect_client_data_vtag - IOCTL connect data union
 *
 * @connect: input connect data
 * @out_client_properties: output client data
 */
struct mei_connect_client_data_vtag {
	union {
		struct mei_connect_client_vtag connect;
		struct mei_client out_client_properties;
	};
};

/**
 * DOC:
 * This IOCTL is used to associate the current file descriptor with a
 * FW Client (given by UUID), and virtual tag (vtag).
 * The IOCTL opens a communication channel between a host client and
 * a FW client on a tagged channel. From this point on, every read
 * and write will communicate with the associated FW client
 * on the tagged channel.
 * Upone close() the communication is terminated.
 *
 * The IOCTL argument is a struct with a union that contains
 * the input parameter and the output parameter for this IOCTL.
 *
 * The input parameter is UUID of the FW Client, a vtag [0,255].
 * The output parameter is the properties of the FW client
 * (FW protocool version and max message size).
 *
 * Clients that do not support tagged connection
 * will respond with -EOPNOTSUPP.
 */
#define IOCTL_MEI_CONNECT_CLIENT_VTAG \
	_IOWR('H', 0x04, struct mei_connect_client_data_vtag)

#endif /* _LINUX_MEI_H  */
