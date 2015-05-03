/*
 * cmt-speech interface definitions
 *
 * Copyright (C) 2008,2009,2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _CS_PROTOCOL_H
#define _CS_PROTOCOL_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* chardev parameters */
#define CS_DEV_FILE_NAME		"/dev/cmt_speech"

/* user-space API versioning */
#define CS_IF_VERSION			2

/* APE kernel <-> user space messages */
#define CS_CMD_SHIFT			28
#define CS_DOMAIN_SHIFT			24

#define CS_CMD_MASK			0xff000000
#define CS_PARAM_MASK			0xffffff

#define CS_CMD(id, dom) \
	(((id) << CS_CMD_SHIFT) | ((dom) << CS_DOMAIN_SHIFT))

#define CS_ERROR			CS_CMD(1, 0)
#define CS_RX_DATA_RECEIVED		CS_CMD(2, 0)
#define CS_TX_DATA_READY		CS_CMD(3, 0)
#define CS_TX_DATA_SENT			CS_CMD(4, 0)

/* params to CS_ERROR indication */
#define CS_ERR_PEER_RESET		0

/* ioctl interface */

/* parameters to CS_CONFIG_BUFS ioctl */
#define CS_FEAT_TSTAMP_RX_CTRL		(1 << 0)
#define CS_FEAT_ROLLING_RX_COUNTER	(2 << 0)

/* parameters to CS_GET_STATE ioctl */
#define CS_STATE_CLOSED			0
#define CS_STATE_OPENED			1 /* resource allocated */
#define CS_STATE_CONFIGURED		2 /* data path active */

/* maximum number of TX/RX buffers */
#define CS_MAX_BUFFERS_SHIFT		4
#define CS_MAX_BUFFERS			(1 << CS_MAX_BUFFERS_SHIFT)

/* Parameters for setting up the data buffers */
struct cs_buffer_config {
	__u32 rx_bufs;	/* number of RX buffer slots */
	__u32 tx_bufs;	/* number of TX buffer slots */
	__u32 buf_size;	/* bytes */
	__u32 flags;	/* see CS_FEAT_* */
	__u32 reserved[4];
};

/*
 * Struct describing the layout and contents of the driver mmap area.
 * This information is meant as read-only information for the application.
 */
struct cs_mmap_config_block {
	__u32 reserved1;
	__u32 buf_size;		/* 0=disabled, otherwise the transfer size */
	__u32 rx_bufs;		/* # of RX buffers */
	__u32 tx_bufs;		/* # of TX buffers */
	__u32 reserved2;
	/* array of offsets within the mmap area for each RX and TX buffer */
	__u32 rx_offsets[CS_MAX_BUFFERS];
	__u32 tx_offsets[CS_MAX_BUFFERS];
	__u32 rx_ptr;
	__u32 rx_ptr_boundary;
	__u32 reserved3[2];
	/*
	 * if enabled with CS_FEAT_TSTAMP_RX_CTRL, monotonic
	 * timestamp taken when the last control command was received
	 */
	struct timespec tstamp_rx_ctrl;
};

#define CS_IO_MAGIC		'C'

#define CS_IOW(num, dtype)	_IOW(CS_IO_MAGIC, num, dtype)
#define CS_IOR(num, dtype)	_IOR(CS_IO_MAGIC, num, dtype)
#define CS_IOWR(num, dtype)	_IOWR(CS_IO_MAGIC, num, dtype)
#define CS_IO(num)		_IO(CS_IO_MAGIC, num)

#define CS_GET_STATE		CS_IOR(21, unsigned int)
#define CS_SET_WAKELINE		CS_IOW(23, unsigned int)
#define CS_GET_IF_VERSION	CS_IOR(30, unsigned int)
#define CS_CONFIG_BUFS		CS_IOW(31, struct cs_buffer_config)

#endif /* _CS_PROTOCOL_H */
