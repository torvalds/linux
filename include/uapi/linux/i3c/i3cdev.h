/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#ifndef _UAPI_I3C_DEV_H_
#define _UAPI_I3C_DEV_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/* IOCTL commands */
#define I3C_DEV_IOC_MAGIC	0x07

/**
 * struct i3c_ioc_priv_xfer - I3C SDR ioctl private transfer
 * @data: Holds pointer to userspace buffer with transmit data.
 * @len: Length of data buffer buffers, in bytes.
 * @rnw: encodes the transfer direction. true for a read, false for a write
 */
struct i3c_ioc_priv_xfer {
	__u64 data;
	__u16 len;
	__u8 rnw;
	__u8 pad[5];
};

#define I3C_PRIV_XFER_SIZE(N)	\
	((((sizeof(struct i3c_ioc_priv_xfer)) * (N)) < (1 << _IOC_SIZEBITS)) \
	? ((sizeof(struct i3c_ioc_priv_xfer)) * (N)) : 0)

#define I3C_IOC_PRIV_XFER(N)	\
	_IOC(_IOC_READ|_IOC_WRITE, I3C_DEV_IOC_MAGIC, 30, I3C_PRIV_XFER_SIZE(N))

#endif
