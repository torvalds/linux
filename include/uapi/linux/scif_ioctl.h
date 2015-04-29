/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel SCIF driver.
 *
 */
/*
 * -----------------------------------------
 * SCIF IOCTL interface information
 * -----------------------------------------
 */
#ifndef SCIF_IOCTL_H
#define SCIF_IOCTL_H

#include <linux/types.h>

/**
 * struct scif_port_id - SCIF port information
 * @node:	node on which port resides
 * @port:	local port number
 */
struct scif_port_id {
	__u16 node;
	__u16 port;
};

/**
 * struct scifioctl_connect - used for SCIF_CONNECT IOCTL
 * @self:	used to read back the assigned port_id
 * @peer:	destination node and port to connect to
 */
struct scifioctl_connect {
	struct scif_port_id	self;
	struct scif_port_id	peer;
};

/**
 * struct scifioctl_accept - used for SCIF_ACCEPTREQ IOCTL
 * @flags:	flags
 * @peer:	global id of peer endpoint
 * @endpt:	new connected endpoint descriptor
 */
struct scifioctl_accept {
	__s32			flags;
	struct scif_port_id	peer;
	__u64			endpt;
};

/**
 * struct scifioctl_msg - used for SCIF_SEND/SCIF_RECV IOCTL
 * @msg:	message buffer address
 * @len:	message length
 * @flags:	flags
 * @out_len:	number of bytes sent/received
 */
struct scifioctl_msg {
	__u64	msg;
	__s32	len;
	__s32	flags;
	__s32	out_len;
};

/**
 * struct scifioctl_node_ids - used for SCIF_GET_NODEIDS IOCTL
 * @nodes:	pointer to an array of node_ids
 * @self:	ID of the current node
 * @len:	length of array
 */
struct scifioctl_node_ids {
	__u64	nodes;
	__u64	self;
	__s32	len;
};

#define SCIF_BIND		_IOWR('s', 1, __u64)
#define SCIF_LISTEN		_IOW('s', 2, __s32)
#define SCIF_CONNECT		_IOWR('s', 3, struct scifioctl_connect)
#define SCIF_ACCEPTREQ		_IOWR('s', 4, struct scifioctl_accept)
#define SCIF_ACCEPTREG		_IOWR('s', 5, __u64)
#define SCIF_SEND		_IOWR('s', 6, struct scifioctl_msg)
#define SCIF_RECV		_IOWR('s', 7, struct scifioctl_msg)
#define SCIF_GET_NODEIDS	_IOWR('s', 14, struct scifioctl_node_ids)

#endif /* SCIF_IOCTL_H */
