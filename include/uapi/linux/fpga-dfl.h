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

#endif /* _UAPI_LINUX_FPGA_DFL_H */
