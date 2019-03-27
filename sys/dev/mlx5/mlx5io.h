/*-
 * Copyright (c) 2018, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_MLX5_MLX5IO_H_
#define _DEV_MLX5_MLX5IO_H_

#include <sys/ioccom.h>

struct mlx5_fwdump_reg {
	uint32_t addr;
	uint32_t val;
};

struct mlx5_fwdump_addr {
	uint32_t domain;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
};

struct mlx5_fwdump_get {
	struct mlx5_fwdump_addr devaddr;
	struct mlx5_fwdump_reg *buf;
	size_t reg_cnt;
	size_t reg_filled; /* out */
};

#define	MLX5_FWDUMP_GET		_IOWR('m', 1, struct mlx5_fwdump_get)
#define	MLX5_FWDUMP_RESET	_IOW('m', 2, struct mlx5_fwdump_addr)
#define	MLX5_FWDUMP_FORCE	_IOW('m', 3, struct mlx5_fwdump_addr)

#ifndef _KERNEL
#define	MLX5_DEV_PATH	_PATH_DEV"mlx5ctl"
#endif

enum mlx5_fpga_id {
	MLX5_FPGA_NEWTON = 0,
	MLX5_FPGA_EDISON = 1,
	MLX5_FPGA_MORSE = 2,
	MLX5_FPGA_MORSEQ = 3,
};

enum mlx5_fpga_image {
	MLX5_FPGA_IMAGE_USER = 0,
	MLX5_FPGA_IMAGE_FACTORY = 1,
	MLX5_FPGA_IMAGE_MAX = MLX5_FPGA_IMAGE_FACTORY,
	MLX5_FPGA_IMAGE_FACTORY_FAILOVER = 2,
};

enum mlx5_fpga_status {
	MLX5_FPGA_STATUS_SUCCESS = 0,
	MLX5_FPGA_STATUS_FAILURE = 1,
	MLX5_FPGA_STATUS_IN_PROGRESS = 2,
	MLX5_FPGA_STATUS_DISCONNECTED = 3,
};

struct mlx5_fpga_query {
	enum mlx5_fpga_image admin_image;
	enum mlx5_fpga_image oper_image;
	enum mlx5_fpga_status image_status;
};

enum mlx5_fpga_tee {
	MLX5_FPGA_TEE_DISABLE = 0,
	MLX5_FPGA_TEE_GENERATE_EVENT = 1,
	MLX5_FPGA_TEE_GENERATE_SINGLE_EVENT = 2,
};

enum mlx5_fpga_connect {
	MLX5_FPGA_CONNECT_QUERY = 0,
	MLX5_FPGA_CONNECT_DISCONNECT = 0x9,
	MLX5_FPGA_CONNECT_CONNECT = 0xA,
};

/**
 * enum mlx5_fpga_access_type - Enumerated the different methods possible for
 * accessing the device memory address space
 */
enum mlx5_fpga_access_type {
	/** Use the slow CX-FPGA I2C bus*/
	MLX5_FPGA_ACCESS_TYPE_I2C = 0x0,
	/** Use the fast 'shell QP' */
	MLX5_FPGA_ACCESS_TYPE_RDMA,
	/** Use the fastest available method */
	MLX5_FPGA_ACCESS_TYPE_DONTCARE,
	MLX5_FPGA_ACCESS_TYPE_MAX = MLX5_FPGA_ACCESS_TYPE_DONTCARE,
};

#define MLX5_FPGA_INTERNAL_SENSORS_LOW 63
#define MLX5_FPGA_INTERNAL_SENSORS_HIGH 63

struct mlx5_fpga_temperature {
	uint32_t temperature;
	uint32_t index;
	uint32_t tee;
	uint32_t max_temperature;
	uint32_t temperature_threshold_hi;
	uint32_t temperature_threshold_lo;
	uint32_t mte;
	uint32_t mtr;
	char sensor_name[16];
};

#define	MLX5_FPGA_CAP_ARR_SZ 0x40

#define	MLX5_FPGA_ACCESS_TYPE	_IOWINT('m', 0x80)
#define	MLX5_FPGA_LOAD		_IOWINT('m', 0x81)
#define	MLX5_FPGA_RESET		_IO('m', 0x82)
#define	MLX5_FPGA_IMAGE_SEL	_IOWINT('m', 0x83)
#define	MLX5_FPGA_QUERY		_IOR('m', 0x84, struct mlx5_fpga_query)
#define	MLX5_FPGA_CAP		_IOR('m', 0x85, uint32_t[MLX5_FPGA_CAP_ARR_SZ])
#define	MLX5_FPGA_TEMPERATURE	_IOWR('m', 0x86, struct mlx5_fpga_temperature)
#define	MLX5_FPGA_CONNECT	_IOWR('m', 0x87, enum mlx5_fpga_connect)

#define	MLX5_FPGA_TOOLS_NAME_SUFFIX	"_mlx5_fpga_tools"

#endif
