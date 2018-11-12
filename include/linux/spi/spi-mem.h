/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Exceet Electronics GmbH
 * Copyright (C) 2018 Bootlin
 *
 * Author:
 *	Peter Pan <peterpandong@micron.com>
 *	Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef __LINUX_SPI_MEM_H
#define __LINUX_SPI_MEM_H

#include <linux/spi/spi.h>

#define SPI_MEM_OP_CMD(__opcode, __buswidth)			\
	{							\
		.buswidth = __buswidth,				\
		.opcode = __opcode,				\
	}

#define SPI_MEM_OP_ADDR(__nbytes, __val, __buswidth)		\
	{							\
		.nbytes = __nbytes,				\
		.val = __val,					\
		.buswidth = __buswidth,				\
	}

#define SPI_MEM_OP_NO_ADDR	{ }

#define SPI_MEM_OP_DUMMY(__nbytes, __buswidth)			\
	{							\
		.nbytes = __nbytes,				\
		.buswidth = __buswidth,				\
	}

#define SPI_MEM_OP_NO_DUMMY	{ }

#define SPI_MEM_OP_DATA_IN(__nbytes, __buf, __buswidth)		\
	{							\
		.dir = SPI_MEM_DATA_IN,				\
		.nbytes = __nbytes,				\
		.buf.in = __buf,				\
		.buswidth = __buswidth,				\
	}

#define SPI_MEM_OP_DATA_OUT(__nbytes, __buf, __buswidth)	\
	{							\
		.dir = SPI_MEM_DATA_OUT,			\
		.nbytes = __nbytes,				\
		.buf.out = __buf,				\
		.buswidth = __buswidth,				\
	}

#define SPI_MEM_OP_NO_DATA	{ }

/**
 * enum spi_mem_data_dir - describes the direction of a SPI memory data
 *			   transfer from the controller perspective
 * @SPI_MEM_DATA_IN: data coming from the SPI memory
 * @SPI_MEM_DATA_OUT: data sent the SPI memory
 */
enum spi_mem_data_dir {
	SPI_MEM_DATA_IN,
	SPI_MEM_DATA_OUT,
};

/**
 * struct spi_mem_op - describes a SPI memory operation
 * @cmd.buswidth: number of IO lines used to transmit the command
 * @cmd.opcode: operation opcode
 * @addr.nbytes: number of address bytes to send. Can be zero if the operation
 *		 does not need to send an address
 * @addr.buswidth: number of IO lines used to transmit the address cycles
 * @addr.val: address value. This value is always sent MSB first on the bus.
 *	      Note that only @addr.nbytes are taken into account in this
 *	      address value, so users should make sure the value fits in the
 *	      assigned number of bytes.
 * @dummy.nbytes: number of dummy bytes to send after an opcode or address. Can
 *		  be zero if the operation does not require dummy bytes
 * @dummy.buswidth: number of IO lanes used to transmit the dummy bytes
 * @data.buswidth: number of IO lanes used to send/receive the data
 * @data.dir: direction of the transfer
 * @data.nbytes: number of data bytes to send/receive. Can be zero if the
 *		 operation does not involve transferring data
 * @data.buf.in: input buffer (must be DMA-able)
 * @data.buf.out: output buffer (must be DMA-able)
 */
struct spi_mem_op {
	struct {
		u8 buswidth;
		u8 opcode;
	} cmd;

	struct {
		u8 nbytes;
		u8 buswidth;
		u64 val;
	} addr;

	struct {
		u8 nbytes;
		u8 buswidth;
	} dummy;

	struct {
		u8 buswidth;
		enum spi_mem_data_dir dir;
		unsigned int nbytes;
		union {
			void *in;
			const void *out;
		} buf;
	} data;
};

#define SPI_MEM_OP(__cmd, __addr, __dummy, __data)		\
	{							\
		.cmd = __cmd,					\
		.addr = __addr,					\
		.dummy = __dummy,				\
		.data = __data,					\
	}

/**
 * struct spi_mem - describes a SPI memory device
 * @spi: the underlying SPI device
 * @drvpriv: spi_mem_driver private data
 * @name: name of the SPI memory device
 *
 * Extra information that describe the SPI memory device and may be needed by
 * the controller to properly handle this device should be placed here.
 *
 * One example would be the device size since some controller expose their SPI
 * mem devices through a io-mapped region.
 */
struct spi_mem {
	struct spi_device *spi;
	void *drvpriv;
	const char *name;
};

/**
 * struct spi_mem_set_drvdata() - attach driver private data to a SPI mem
 *				  device
 * @mem: memory device
 * @data: data to attach to the memory device
 */
static inline void spi_mem_set_drvdata(struct spi_mem *mem, void *data)
{
	mem->drvpriv = data;
}

/**
 * struct spi_mem_get_drvdata() - get driver private data attached to a SPI mem
 *				  device
 * @mem: memory device
 *
 * Return: the data attached to the mem device.
 */
static inline void *spi_mem_get_drvdata(struct spi_mem *mem)
{
	return mem->drvpriv;
}

/**
 * struct spi_controller_mem_ops - SPI memory operations
 * @adjust_op_size: shrink the data xfer of an operation to match controller's
 *		    limitations (can be alignment of max RX/TX size
 *		    limitations)
 * @supports_op: check if an operation is supported by the controller
 * @exec_op: execute a SPI memory operation
 * @get_name: get a custom name for the SPI mem device from the controller.
 *	      This might be needed if the controller driver has been ported
 *	      to use the SPI mem layer and a custom name is used to keep
 *	      mtdparts compatible.
 *	      Note that if the implementation of this function allocates memory
 *	      dynamically, then it should do so with devm_xxx(), as we don't
 *	      have a ->free_name() function.
 *
 * This interface should be implemented by SPI controllers providing an
 * high-level interface to execute SPI memory operation, which is usually the
 * case for QSPI controllers.
 */
struct spi_controller_mem_ops {
	int (*adjust_op_size)(struct spi_mem *mem, struct spi_mem_op *op);
	bool (*supports_op)(struct spi_mem *mem,
			    const struct spi_mem_op *op);
	int (*exec_op)(struct spi_mem *mem,
		       const struct spi_mem_op *op);
	const char *(*get_name)(struct spi_mem *mem);
};

/**
 * struct spi_mem_driver - SPI memory driver
 * @spidrv: inherit from a SPI driver
 * @probe: probe a SPI memory. Usually where detection/initialization takes
 *	   place
 * @remove: remove a SPI memory
 * @shutdown: take appropriate action when the system is shutdown
 *
 * This is just a thin wrapper around a spi_driver. The core takes care of
 * allocating the spi_mem object and forwarding the probe/remove/shutdown
 * request to the spi_mem_driver. The reason we use this wrapper is because
 * we might have to stuff more information into the spi_mem struct to let
 * SPI controllers know more about the SPI memory they interact with, and
 * having this intermediate layer allows us to do that without adding more
 * useless fields to the spi_device object.
 */
struct spi_mem_driver {
	struct spi_driver spidrv;
	int (*probe)(struct spi_mem *mem);
	int (*remove)(struct spi_mem *mem);
	void (*shutdown)(struct spi_mem *mem);
};

#if IS_ENABLED(CONFIG_SPI_MEM)
int spi_controller_dma_map_mem_op_data(struct spi_controller *ctlr,
				       const struct spi_mem_op *op,
				       struct sg_table *sg);

void spi_controller_dma_unmap_mem_op_data(struct spi_controller *ctlr,
					  const struct spi_mem_op *op,
					  struct sg_table *sg);
#else
static inline int
spi_controller_dma_map_mem_op_data(struct spi_controller *ctlr,
				   const struct spi_mem_op *op,
				   struct sg_table *sg)
{
	return -ENOTSUPP;
}

static inline void
spi_controller_dma_unmap_mem_op_data(struct spi_controller *ctlr,
				     const struct spi_mem_op *op,
				     struct sg_table *sg)
{
}
#endif /* CONFIG_SPI_MEM */

int spi_mem_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op);

bool spi_mem_supports_op(struct spi_mem *mem,
			 const struct spi_mem_op *op);

int spi_mem_exec_op(struct spi_mem *mem,
		    const struct spi_mem_op *op);

const char *spi_mem_get_name(struct spi_mem *mem);

int spi_mem_driver_register_with_owner(struct spi_mem_driver *drv,
				       struct module *owner);

void spi_mem_driver_unregister(struct spi_mem_driver *drv);

#define spi_mem_driver_register(__drv)                                  \
	spi_mem_driver_register_with_owner(__drv, THIS_MODULE)

#define module_spi_mem_driver(__drv)                                    \
	module_driver(__drv, spi_mem_driver_register,                   \
		      spi_mem_driver_unregister)

#endif /* __LINUX_SPI_MEM_H */
