/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Header File for FPGA DFL User API
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Zhang Yi <yi.z.zhang@intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 */

#ifndef _UAPI_LINUX_FPGA_DFL_H
#define _UAPI_LINUX_FPGA_DFL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define DFL_FPGA_API_VERSION 0

/*
 * The IOCTL interface for DFL based FPGA is designed for extensibility by
 * embedding the structure length (argsz) and flags into structures passed
 * between kernel and userspace. This design referenced the VFIO IOCTL
 * interface (include/uapi/linux/vfio.h).
 */

#define DFL_FPGA_MAGIC 0xB6

#define DFL_FPGA_BASE 0
#define DFL_PORT_BASE 0x40
#define DFL_FME_BASE 0x80

/* Common IOCTLs for both FME and AFU file descriptor */

/**
 * DFL_FPGA_GET_API_VERSION - _IO(DFL_FPGA_MAGIC, DFL_FPGA_BASE + 0)
 *
 * Report the version of the driver API.
 * Return: Driver API Version.
 */

#define DFL_FPGA_GET_API_VERSION	_IO(DFL_FPGA_MAGIC, DFL_FPGA_BASE + 0)

/**
 * DFL_FPGA_CHECK_EXTENSION - _IO(DFL_FPGA_MAGIC, DFL_FPGA_BASE + 1)
 *
 * Check whether an extension is supported.
 * Return: 0 if not supported, otherwise the extension is supported.
 */

#define DFL_FPGA_CHECK_EXTENSION	_IO(DFL_FPGA_MAGIC, DFL_FPGA_BASE + 1)

/* IOCTLs for AFU file descriptor */

/**
 * DFL_FPGA_PORT_RESET - _IO(DFL_FPGA_MAGIC, DFL_PORT_BASE + 0)
 *
 * Reset the FPGA Port and its AFU. No parameters are supported.
 * Userspace can do Port reset at any time, e.g. during DMA or PR. But
 * it should never cause any system level issue, only functional failure
 * (e.g. DMA or PR operation failure) and be recoverable from the failure.
 * Return: 0 on success, -errno of failure
 */

#define DFL_FPGA_PORT_RESET		_IO(DFL_FPGA_MAGIC, DFL_PORT_BASE + 0)

/* IOCTLs for FME file descriptor */

/**
 * DFL_FPGA_FME_PORT_PR - _IOW(DFL_FPGA_MAGIC, DFL_FME_BASE + 0,
 *						struct dfl_fpga_fme_port_pr)
 *
 * Driver does Partial Reconfiguration based on Port ID and Buffer (Image)
 * provided by caller.
 * Return: 0 on success, -errno on failure.
 * If DFL_FPGA_FME_PORT_PR returns -EIO, that indicates the HW has detected
 * some errors during PR, under this case, the user can fetch HW error info
 * from the status of FME's fpga manager.
 */

struct dfl_fpga_fme_port_pr {
	/* Input */
	__u32 argsz;		/* Structure length */
	__u32 flags;		/* Zero for now */
	__u32 port_id;
	__u32 buffer_size;
	__u64 buffer_address;	/* Userspace address to the buffer for PR */
};

#define DFL_FPGA_FME_PORT_PR	_IO(DFL_FPGA_MAGIC, DFL_FME_BASE + 0)

#endif /* _UAPI_LINUX_FPGA_DFL_H */
