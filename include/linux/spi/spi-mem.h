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
		.nbytes = 1,					\
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
 * @SPI_MEM_NO_DATA: no data transferred
 * @SPI_MEM_DATA_IN: data coming from the SPI memory
 * @SPI_MEM_DATA_OUT: data sent to the SPI memory
 */
enum spi_mem_data_dir {
	SPI_MEM_NO_DATA,
	SPI_MEM_DATA_IN,
	SPI_MEM_DATA_OUT,
};

/**
 * struct spi_mem_op - describes a SPI memory operation
 * @cmd.nbytes: number of opcode bytes (only 1 or 2 are valid). The opcode is
 *		sent MSB-first.
 * @cmd.buswidth: number of IO lines used to transmit the command
 * @cmd.opcode: operation opcode
 * @cmd.dtr: whether the command opcode should be sent in DTR mode or not
 * @addr.nbytes: number of address bytes to send. Can be zero if the operation
 *		 does not need to send an address
 * @addr.buswidth: number of IO lines used to transmit the address cycles
 * @addr.dtr: whether the address should be sent in DTR mode or not
 * @addr.val: address value. This value is always sent MSB first on the bus.
 *	      Note that only @addr.nbytes are taken into account in this
 *	      address value, so users should make sure the value fits in the
 *	      assigned number of bytes.
 * @dummy.nbytes: number of dummy bytes to send after an opcode or address. Can
 *		  be zero if the operation does not require dummy bytes
 * @dummy.buswidth: number of IO lanes used to transmit the dummy bytes
 * @dummy.dtr: whether the dummy bytes should be sent in DTR mode or not
 * @data.buswidth: number of IO lanes used to send/receive the data
 * @data.dtr: whether the data should be sent in DTR mode or not
 * @data.dir: direction of the transfer
 * @data.nbytes: number of data bytes to send/receive. Can be zero if the
 *		 operation does not involve transferring data
 * @data.buf.in: input buffer (must be DMA-able)
 * @data.buf.out: output buffer (must be DMA-able)
 */
struct spi_mem_op {
	struct {
		u8 nbytes;
		u8 buswidth;
		u8 dtr : 1;
		u16 opcode;
	} cmd;

	struct {
		u8 nbytes;
		u8 buswidth;
		u8 dtr : 1;
		u64 val;
	} addr;

	struct {
		u8 nbytes;
		u8 buswidth;
		u8 dtr : 1;
	} dummy;

	struct {
		u8 buswidth;
		u8 dtr : 1;
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
 * struct spi_mem_dirmap_info - Direct mapping information
 * @op_tmpl: operation template that should be used by the direct mapping when
 *	     the memory device is accessed
 * @offset: absolute offset this direct mapping is pointing to
 * @length: length in byte of this direct mapping
 *
 * These information are used by the controller specific implementation to know
 * the portion of memory that is directly mapped and the spi_mem_op that should
 * be used to access the device.
 * A direct mapping is only valid for one direction (read or write) and this
 * direction is directly encoded in the ->op_tmpl.data.dir field.
 */
struct spi_mem_dirmap_info {
	struct spi_mem_op op_tmpl;
	u64 offset;
	u64 length;
};

/**
 * struct spi_mem_dirmap_desc - Direct mapping descriptor
 * @mem: the SPI memory device this direct mapping is attached to
 * @info: information passed at direct mapping creation time
 * @nodirmap: set to 1 if the SPI controller does not implement
 *	      ->mem_ops->dirmap_create() or when this function returned an
 *	      error. If @nodirmap is true, all spi_mem_dirmap_{read,write}()
 *	      calls will use spi_mem_exec_op() to access the memory. This is a
 *	      degraded mode that allows spi_mem drivers to use the same code
 *	      no matter whether the controller supports direct mapping or not
 * @priv: field pointing to controller specific data
 *
 * Common part of a direct mapping descriptor. This object is created by
 * spi_mem_dirmap_create() and controller implementation of ->create_dirmap()
 * can create/attach direct mapping resources to the descriptor in the ->priv
 * field.
 */
struct spi_mem_dirmap_desc {
	struct spi_mem *mem;
	struct spi_mem_dirmap_info info;
	unsigned int nodirmap;
	void *priv;
};

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
 * @dirmap_create: create a direct mapping descriptor that can later be used to
 *		   access the memory device. This method is optional
 * @dirmap_destroy: destroy a memory descriptor previous created by
 *		    ->dirmap_create()
 * @dirmap_read: read data from the memory device using the direct mapping
 *		 created by ->dirmap_create(). The function can return less
 *		 data than requested (for example when the request is crossing
 *		 the currently mapped area), and the caller of
 *		 spi_mem_dirmap_read() is responsible for calling it again in
 *		 this case.
 * @dirmap_write: write data to the memory device using the direct mapping
 *		  created by ->dirmap_create(). The function can return less
 *		  data than requested (for example when the request is crossing
 *		  the currently mapped area), and the caller of
 *		  spi_mem_dirmap_write() is responsible for calling it again in
 *		  this case.
 *
 * This interface should be implemented by SPI controllers providing an
 * high-level interface to execute SPI memory operation, which is usually the
 * case for QSPI controllers.
 *
 * Note on ->dirmap_{read,write}(): drivers should avoid accessing the direct
 * mapping from the CPU because doing that can stall the CPU waiting for the
 * SPI mem transaction to finish, and this will make real-time maintainers
 * unhappy and might make your system less reactive. Instead, drivers should
 * use DMA to access this direct mapping.
 */
struct spi_controller_mem_ops {
	int (*adjust_op_size)(struct spi_mem *mem, struct spi_mem_op *op);
	bool (*supports_op)(struct spi_mem *mem,
			    const struct spi_mem_op *op);
	int (*exec_op)(struct spi_mem *mem,
		       const struct spi_mem_op *op);
	const char *(*get_name)(struct spi_mem *mem);
	int (*dirmap_create)(struct spi_mem_dirmap_desc *desc);
	void (*dirmap_destroy)(struct spi_mem_dirmap_desc *desc);
	ssize_t (*dirmap_read)(struct spi_mem_dirmap_desc *desc,
			       u64 offs, size_t len, void *buf);
	ssize_t (*dirmap_write)(struct spi_mem_dirmap_desc *desc,
				u64 offs, size_t len, const void *buf);
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

bool spi_mem_default_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op);

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

static inline
bool spi_mem_default_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	return false;
}

#endif /* CONFIG_SPI_MEM */

int spi_mem_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op);

bool spi_mem_supports_op(struct spi_mem *mem,
			 const struct spi_mem_op *op);

int spi_mem_exec_op(struct spi_mem *mem,
		    const struct spi_mem_op *op);

const char *spi_mem_get_name(struct spi_mem *mem);

struct spi_mem_dirmap_desc *
spi_mem_dirmap_create(struct spi_mem *mem,
		      const struct spi_mem_dirmap_info *info);
void spi_mem_dirmap_destroy(struct spi_mem_dirmap_desc *desc);
ssize_t spi_mem_dirmap_read(struct spi_mem_dirmap_desc *desc,
			    u64 offs, size_t len, void *buf);
ssize_t spi_mem_dirmap_write(struct spi_mem_dirmap_desc *desc,
			     u64 offs, size_t len, const void *buf);
struct spi_mem_dirmap_desc *
devm_spi_mem_dirmap_create(struct device *dev, struct spi_mem *mem,
			   const struct spi_mem_dirmap_info *info);
void devm_spi_mem_dirmap_destroy(struct device *dev,
				 struct spi_mem_dirmap_desc *desc);

int spi_mem_driver_register_with_owner(struct spi_mem_driver *drv,
				       struct module *owner);

void spi_mem_driver_unregister(struct spi_mem_driver *drv);

#define spi_mem_driver_register(__drv)                                  \
	spi_mem_driver_register_with_owner(__drv, THIS_MODULE)

#define module_spi_mem_driver(__drv)                                    \
	module_driver(__drv, spi_mem_driver_register,                   \
		      spi_mem_driver_unregister)

#endif /* __LINUX_SPI_MEM_H */
