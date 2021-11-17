/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_NOR_H
#define __LINUX_MTD_SPI_NOR_H

#include <linux/bitops.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/spi/spi-mem.h>

/*
 * Note on opcode nomenclature: some opcodes have a format like
 * SPINOR_OP_FUNCTION{4,}_x_y_z. The numbers x, y, and z stand for the number
 * of I/O lines used for the opcode, address, and data (respectively). The
 * FUNCTION has an optional suffix of '4', to represent an opcode which
 * requires a 4-byte (32-bit) address.
 */

/* Flash opcodes. */
#define SPINOR_OP_WRDI		0x04	/* Write disable */
#define SPINOR_OP_WREN		0x06	/* Write enable */
#define SPINOR_OP_RDSR		0x05	/* Read status register */
#define SPINOR_OP_WRSR		0x01	/* Write status register 1 byte */
#define SPINOR_OP_RDSR2		0x3f	/* Read status register 2 */
#define SPINOR_OP_WRSR2		0x3e	/* Write status register 2 */
#define SPINOR_OP_READ		0x03	/* Read data bytes (low frequency) */
#define SPINOR_OP_READ_FAST	0x0b	/* Read data bytes (high frequency) */
#define SPINOR_OP_READ_1_1_2	0x3b	/* Read data bytes (Dual Output SPI) */
#define SPINOR_OP_READ_1_2_2	0xbb	/* Read data bytes (Dual I/O SPI) */
#define SPINOR_OP_READ_1_1_4	0x6b	/* Read data bytes (Quad Output SPI) */
#define SPINOR_OP_READ_1_4_4	0xeb	/* Read data bytes (Quad I/O SPI) */
#define SPINOR_OP_READ_1_1_8	0x8b	/* Read data bytes (Octal Output SPI) */
#define SPINOR_OP_READ_1_8_8	0xcb	/* Read data bytes (Octal I/O SPI) */
#define SPINOR_OP_PP		0x02	/* Page program (up to 256 bytes) */
#define SPINOR_OP_PP_1_1_4	0x32	/* Quad page program */
#define SPINOR_OP_PP_1_4_4	0x38	/* Quad page program */
#define SPINOR_OP_PP_1_1_8	0x82	/* Octal page program */
#define SPINOR_OP_PP_1_8_8	0xc2	/* Octal page program */
#define SPINOR_OP_BE_4K		0x20	/* Erase 4KiB block */
#define SPINOR_OP_BE_4K_PMC	0xd7	/* Erase 4KiB block on PMC chips */
#define SPINOR_OP_BE_32K	0x52	/* Erase 32KiB block */
#define SPINOR_OP_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define SPINOR_OP_SE		0xd8	/* Sector erase (usually 64KiB) */
#define SPINOR_OP_RDID		0x9f	/* Read JEDEC ID */
#define SPINOR_OP_RDSFDP	0x5a	/* Read SFDP */
#define SPINOR_OP_RDCR		0x35	/* Read configuration register */
#define SPINOR_OP_RDFSR		0x70	/* Read flag status register */
#define SPINOR_OP_CLFSR		0x50	/* Clear flag status register */
#define SPINOR_OP_RDEAR		0xc8	/* Read Extended Address Register */
#define SPINOR_OP_WREAR		0xc5	/* Write Extended Address Register */
#define SPINOR_OP_SRSTEN	0x66	/* Software Reset Enable */
#define SPINOR_OP_SRST		0x99	/* Software Reset */
#define SPINOR_OP_GBULK		0x98    /* Global Block Unlock */

/* 4-byte address opcodes - used on Spansion and some Macronix flashes. */
#define SPINOR_OP_READ_4B	0x13	/* Read data bytes (low frequency) */
#define SPINOR_OP_READ_FAST_4B	0x0c	/* Read data bytes (high frequency) */
#define SPINOR_OP_READ_1_1_2_4B	0x3c	/* Read data bytes (Dual Output SPI) */
#define SPINOR_OP_READ_1_2_2_4B	0xbc	/* Read data bytes (Dual I/O SPI) */
#define SPINOR_OP_READ_1_1_4_4B	0x6c	/* Read data bytes (Quad Output SPI) */
#define SPINOR_OP_READ_1_4_4_4B	0xec	/* Read data bytes (Quad I/O SPI) */
#define SPINOR_OP_READ_1_1_8_4B	0x7c	/* Read data bytes (Octal Output SPI) */
#define SPINOR_OP_READ_1_8_8_4B	0xcc	/* Read data bytes (Octal I/O SPI) */
#define SPINOR_OP_PP_4B		0x12	/* Page program (up to 256 bytes) */
#define SPINOR_OP_PP_1_1_4_4B	0x34	/* Quad page program */
#define SPINOR_OP_PP_1_4_4_4B	0x3e	/* Quad page program */
#define SPINOR_OP_PP_1_1_8_4B	0x84	/* Octal page program */
#define SPINOR_OP_PP_1_8_8_4B	0x8e	/* Octal page program */
#define SPINOR_OP_BE_4K_4B	0x21	/* Erase 4KiB block */
#define SPINOR_OP_BE_32K_4B	0x5c	/* Erase 32KiB block */
#define SPINOR_OP_SE_4B		0xdc	/* Sector erase (usually 64KiB) */

/* Double Transfer Rate opcodes - defined in JEDEC JESD216B. */
#define SPINOR_OP_READ_1_1_1_DTR	0x0d
#define SPINOR_OP_READ_1_2_2_DTR	0xbd
#define SPINOR_OP_READ_1_4_4_DTR	0xed

#define SPINOR_OP_READ_1_1_1_DTR_4B	0x0e
#define SPINOR_OP_READ_1_2_2_DTR_4B	0xbe
#define SPINOR_OP_READ_1_4_4_DTR_4B	0xee

/* Used for SST flashes only. */
#define SPINOR_OP_BP		0x02	/* Byte program */
#define SPINOR_OP_AAI_WP	0xad	/* Auto address increment word program */

/* Used for S3AN flashes only */
#define SPINOR_OP_XSE		0x50	/* Sector erase */
#define SPINOR_OP_XPP		0x82	/* Page program */
#define SPINOR_OP_XRDSR		0xd7	/* Read status register */

#define XSR_PAGESIZE		BIT(0)	/* Page size in Po2 or Linear */
#define XSR_RDY			BIT(7)	/* Ready */


/* Used for Macronix and Winbond flashes. */
#define SPINOR_OP_EN4B		0xb7	/* Enter 4-byte mode */
#define SPINOR_OP_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define SPINOR_OP_BRWR		0x17	/* Bank register write */
#define SPINOR_OP_CLSR		0x30	/* Clear status register 1 */

/* Used for Micron flashes only. */
#define SPINOR_OP_RD_EVCR      0x65    /* Read EVCR register */
#define SPINOR_OP_WD_EVCR      0x61    /* Write EVCR register */

/* Used for GigaDevices and Winbond flashes. */
#define SPINOR_OP_ESECR		0x44	/* Erase Security registers */
#define SPINOR_OP_PSECR		0x42	/* Program Security registers */
#define SPINOR_OP_RSECR		0x48	/* Read Security registers */

/* Status Register bits. */
#define SR_WIP			BIT(0)	/* Write in progress */
#define SR_WEL			BIT(1)	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define SR_BP0			BIT(2)	/* Block protect 0 */
#define SR_BP1			BIT(3)	/* Block protect 1 */
#define SR_BP2			BIT(4)	/* Block protect 2 */
#define SR_BP3			BIT(5)	/* Block protect 3 */
#define SR_TB_BIT5		BIT(5)	/* Top/Bottom protect */
#define SR_BP3_BIT6		BIT(6)	/* Block protect 3 */
#define SR_TB_BIT6		BIT(6)	/* Top/Bottom protect */
#define SR_SRWD			BIT(7)	/* SR write protect */
/* Spansion/Cypress specific status bits */
#define SR_E_ERR		BIT(5)
#define SR_P_ERR		BIT(6)

#define SR1_QUAD_EN_BIT6	BIT(6)

#define SR_BP_SHIFT		2

/* Enhanced Volatile Configuration Register bits */
#define EVCR_QUAD_EN_MICRON	BIT(7)	/* Micron Quad I/O */

/* Flag Status Register bits */
#define FSR_READY		BIT(7)	/* Device status, 0 = Busy, 1 = Ready */
#define FSR_E_ERR		BIT(5)	/* Erase operation status */
#define FSR_P_ERR		BIT(4)	/* Program operation status */
#define FSR_PT_ERR		BIT(1)	/* Protection error bit */

/* Status Register 2 bits. */
#define SR2_QUAD_EN_BIT1	BIT(1)
#define SR2_LB1			BIT(3)	/* Security Register Lock Bit 1 */
#define SR2_LB2			BIT(4)	/* Security Register Lock Bit 2 */
#define SR2_LB3			BIT(5)	/* Security Register Lock Bit 3 */
#define SR2_QUAD_EN_BIT7	BIT(7)

/* Supported SPI protocols */
#define SNOR_PROTO_INST_MASK	GENMASK(23, 16)
#define SNOR_PROTO_INST_SHIFT	16
#define SNOR_PROTO_INST(_nbits)	\
	((((unsigned long)(_nbits)) << SNOR_PROTO_INST_SHIFT) & \
	 SNOR_PROTO_INST_MASK)

#define SNOR_PROTO_ADDR_MASK	GENMASK(15, 8)
#define SNOR_PROTO_ADDR_SHIFT	8
#define SNOR_PROTO_ADDR(_nbits)	\
	((((unsigned long)(_nbits)) << SNOR_PROTO_ADDR_SHIFT) & \
	 SNOR_PROTO_ADDR_MASK)

#define SNOR_PROTO_DATA_MASK	GENMASK(7, 0)
#define SNOR_PROTO_DATA_SHIFT	0
#define SNOR_PROTO_DATA(_nbits)	\
	((((unsigned long)(_nbits)) << SNOR_PROTO_DATA_SHIFT) & \
	 SNOR_PROTO_DATA_MASK)

#define SNOR_PROTO_IS_DTR	BIT(24)	/* Double Transfer Rate */

#define SNOR_PROTO_STR(_inst_nbits, _addr_nbits, _data_nbits)	\
	(SNOR_PROTO_INST(_inst_nbits) |				\
	 SNOR_PROTO_ADDR(_addr_nbits) |				\
	 SNOR_PROTO_DATA(_data_nbits))
#define SNOR_PROTO_DTR(_inst_nbits, _addr_nbits, _data_nbits)	\
	(SNOR_PROTO_IS_DTR |					\
	 SNOR_PROTO_STR(_inst_nbits, _addr_nbits, _data_nbits))

enum spi_nor_protocol {
	SNOR_PROTO_1_1_1 = SNOR_PROTO_STR(1, 1, 1),
	SNOR_PROTO_1_1_2 = SNOR_PROTO_STR(1, 1, 2),
	SNOR_PROTO_1_1_4 = SNOR_PROTO_STR(1, 1, 4),
	SNOR_PROTO_1_1_8 = SNOR_PROTO_STR(1, 1, 8),
	SNOR_PROTO_1_2_2 = SNOR_PROTO_STR(1, 2, 2),
	SNOR_PROTO_1_4_4 = SNOR_PROTO_STR(1, 4, 4),
	SNOR_PROTO_1_8_8 = SNOR_PROTO_STR(1, 8, 8),
	SNOR_PROTO_2_2_2 = SNOR_PROTO_STR(2, 2, 2),
	SNOR_PROTO_4_4_4 = SNOR_PROTO_STR(4, 4, 4),
	SNOR_PROTO_8_8_8 = SNOR_PROTO_STR(8, 8, 8),

	SNOR_PROTO_1_1_1_DTR = SNOR_PROTO_DTR(1, 1, 1),
	SNOR_PROTO_1_2_2_DTR = SNOR_PROTO_DTR(1, 2, 2),
	SNOR_PROTO_1_4_4_DTR = SNOR_PROTO_DTR(1, 4, 4),
	SNOR_PROTO_1_8_8_DTR = SNOR_PROTO_DTR(1, 8, 8),
	SNOR_PROTO_8_8_8_DTR = SNOR_PROTO_DTR(8, 8, 8),
};

static inline bool spi_nor_protocol_is_dtr(enum spi_nor_protocol proto)
{
	return !!(proto & SNOR_PROTO_IS_DTR);
}

static inline u8 spi_nor_get_protocol_inst_nbits(enum spi_nor_protocol proto)
{
	return ((unsigned long)(proto & SNOR_PROTO_INST_MASK)) >>
		SNOR_PROTO_INST_SHIFT;
}

static inline u8 spi_nor_get_protocol_addr_nbits(enum spi_nor_protocol proto)
{
	return ((unsigned long)(proto & SNOR_PROTO_ADDR_MASK)) >>
		SNOR_PROTO_ADDR_SHIFT;
}

static inline u8 spi_nor_get_protocol_data_nbits(enum spi_nor_protocol proto)
{
	return ((unsigned long)(proto & SNOR_PROTO_DATA_MASK)) >>
		SNOR_PROTO_DATA_SHIFT;
}

static inline u8 spi_nor_get_protocol_width(enum spi_nor_protocol proto)
{
	return spi_nor_get_protocol_data_nbits(proto);
}

/**
 * struct spi_nor_hwcaps - Structure for describing the hardware capabilies
 * supported by the SPI controller (bus master).
 * @mask:		the bitmask listing all the supported hw capabilies
 */
struct spi_nor_hwcaps {
	u32	mask;
};

/*
 *(Fast) Read capabilities.
 * MUST be ordered by priority: the higher bit position, the higher priority.
 * As a matter of performances, it is relevant to use Octal SPI protocols first,
 * then Quad SPI protocols before Dual SPI protocols, Fast Read and lastly
 * (Slow) Read.
 */
#define SNOR_HWCAPS_READ_MASK		GENMASK(15, 0)
#define SNOR_HWCAPS_READ		BIT(0)
#define SNOR_HWCAPS_READ_FAST		BIT(1)
#define SNOR_HWCAPS_READ_1_1_1_DTR	BIT(2)

#define SNOR_HWCAPS_READ_DUAL		GENMASK(6, 3)
#define SNOR_HWCAPS_READ_1_1_2		BIT(3)
#define SNOR_HWCAPS_READ_1_2_2		BIT(4)
#define SNOR_HWCAPS_READ_2_2_2		BIT(5)
#define SNOR_HWCAPS_READ_1_2_2_DTR	BIT(6)

#define SNOR_HWCAPS_READ_QUAD		GENMASK(10, 7)
#define SNOR_HWCAPS_READ_1_1_4		BIT(7)
#define SNOR_HWCAPS_READ_1_4_4		BIT(8)
#define SNOR_HWCAPS_READ_4_4_4		BIT(9)
#define SNOR_HWCAPS_READ_1_4_4_DTR	BIT(10)

#define SNOR_HWCAPS_READ_OCTAL		GENMASK(15, 11)
#define SNOR_HWCAPS_READ_1_1_8		BIT(11)
#define SNOR_HWCAPS_READ_1_8_8		BIT(12)
#define SNOR_HWCAPS_READ_8_8_8		BIT(13)
#define SNOR_HWCAPS_READ_1_8_8_DTR	BIT(14)
#define SNOR_HWCAPS_READ_8_8_8_DTR	BIT(15)

/*
 * Page Program capabilities.
 * MUST be ordered by priority: the higher bit position, the higher priority.
 * Like (Fast) Read capabilities, Octal/Quad SPI protocols are preferred to the
 * legacy SPI 1-1-1 protocol.
 * Note that Dual Page Programs are not supported because there is no existing
 * JEDEC/SFDP standard to define them. Also at this moment no SPI flash memory
 * implements such commands.
 */
#define SNOR_HWCAPS_PP_MASK		GENMASK(23, 16)
#define SNOR_HWCAPS_PP			BIT(16)

#define SNOR_HWCAPS_PP_QUAD		GENMASK(19, 17)
#define SNOR_HWCAPS_PP_1_1_4		BIT(17)
#define SNOR_HWCAPS_PP_1_4_4		BIT(18)
#define SNOR_HWCAPS_PP_4_4_4		BIT(19)

#define SNOR_HWCAPS_PP_OCTAL		GENMASK(23, 20)
#define SNOR_HWCAPS_PP_1_1_8		BIT(20)
#define SNOR_HWCAPS_PP_1_8_8		BIT(21)
#define SNOR_HWCAPS_PP_8_8_8		BIT(22)
#define SNOR_HWCAPS_PP_8_8_8_DTR	BIT(23)

#define SNOR_HWCAPS_X_X_X	(SNOR_HWCAPS_READ_2_2_2 |	\
				 SNOR_HWCAPS_READ_4_4_4 |	\
				 SNOR_HWCAPS_READ_8_8_8 |	\
				 SNOR_HWCAPS_PP_4_4_4 |		\
				 SNOR_HWCAPS_PP_8_8_8)

#define SNOR_HWCAPS_X_X_X_DTR	(SNOR_HWCAPS_READ_8_8_8_DTR |	\
				 SNOR_HWCAPS_PP_8_8_8_DTR)

#define SNOR_HWCAPS_DTR		(SNOR_HWCAPS_READ_1_1_1_DTR |	\
				 SNOR_HWCAPS_READ_1_2_2_DTR |	\
				 SNOR_HWCAPS_READ_1_4_4_DTR |	\
				 SNOR_HWCAPS_READ_1_8_8_DTR |	\
				 SNOR_HWCAPS_READ_8_8_8_DTR)

#define SNOR_HWCAPS_ALL		(SNOR_HWCAPS_READ_MASK |	\
				 SNOR_HWCAPS_PP_MASK)

/* Forward declaration that is used in 'struct spi_nor_controller_ops' */
struct spi_nor;

/**
 * struct spi_nor_controller_ops - SPI NOR controller driver specific
 *                                 operations.
 * @prepare:		[OPTIONAL] do some preparations for the
 *			read/write/erase/lock/unlock operations.
 * @unprepare:		[OPTIONAL] do some post work after the
 *			read/write/erase/lock/unlock operations.
 * @read_reg:		read out the register.
 * @write_reg:		write data to the register.
 * @read:		read data from the SPI NOR.
 * @write:		write data to the SPI NOR.
 * @erase:		erase a sector of the SPI NOR at the offset @offs; if
 *			not provided by the driver, SPI NOR will send the erase
 *			opcode via write_reg().
 */
struct spi_nor_controller_ops {
	int (*prepare)(struct spi_nor *nor);
	void (*unprepare)(struct spi_nor *nor);
	int (*read_reg)(struct spi_nor *nor, u8 opcode, u8 *buf, size_t len);
	int (*write_reg)(struct spi_nor *nor, u8 opcode, const u8 *buf,
			 size_t len);

	ssize_t (*read)(struct spi_nor *nor, loff_t from, size_t len, u8 *buf);
	ssize_t (*write)(struct spi_nor *nor, loff_t to, size_t len,
			 const u8 *buf);
	int (*erase)(struct spi_nor *nor, loff_t offs);
};

/**
 * enum spi_nor_cmd_ext - describes the command opcode extension in DTR mode
 * @SPI_NOR_EXT_NONE: no extension. This is the default, and is used in Legacy
 *		      SPI mode
 * @SPI_NOR_EXT_REPEAT: the extension is same as the opcode
 * @SPI_NOR_EXT_INVERT: the extension is the bitwise inverse of the opcode
 * @SPI_NOR_EXT_HEX: the extension is any hex value. The command and opcode
 *		     combine to form a 16-bit opcode.
 */
enum spi_nor_cmd_ext {
	SPI_NOR_EXT_NONE = 0,
	SPI_NOR_EXT_REPEAT,
	SPI_NOR_EXT_INVERT,
	SPI_NOR_EXT_HEX,
};

/*
 * Forward declarations that are used internally by the core and manufacturer
 * drivers.
 */
struct flash_info;
struct spi_nor_manufacturer;
struct spi_nor_flash_parameter;

/**
 * struct spi_nor - Structure for defining the SPI NOR layer
 * @mtd:		an mtd_info structure
 * @lock:		the lock for the read/write/erase/lock/unlock operations
 * @dev:		pointer to an SPI device or an SPI NOR controller device
 * @spimem:		pointer to the SPI memory device
 * @bouncebuf:		bounce buffer used when the buffer passed by the MTD
 *                      layer is not DMA-able
 * @bouncebuf_size:	size of the bounce buffer
 * @info:		SPI NOR part JEDEC MFR ID and other info
 * @manufacturer:	SPI NOR manufacturer
 * @page_size:		the page size of the SPI NOR
 * @addr_width:		number of address bytes
 * @erase_opcode:	the opcode for erasing a sector
 * @read_opcode:	the read opcode
 * @read_dummy:		the dummy needed by the read operation
 * @program_opcode:	the program opcode
 * @sst_write_second:	used by the SST write operation
 * @flags:		flag options for the current SPI NOR (SNOR_F_*)
 * @cmd_ext_type:	the command opcode extension type for DTR mode.
 * @read_proto:		the SPI protocol for read operations
 * @write_proto:	the SPI protocol for write operations
 * @reg_proto:		the SPI protocol for read_reg/write_reg/erase operations
 * @sfdp:		the SFDP data of the flash
 * @controller_ops:	SPI NOR controller driver specific operations.
 * @params:		[FLASH-SPECIFIC] SPI NOR flash parameters and settings.
 *                      The structure includes legacy flash parameters and
 *                      settings that can be overwritten by the spi_nor_fixups
 *                      hooks, or dynamically when parsing the SFDP tables.
 * @dirmap:		pointers to struct spi_mem_dirmap_desc for reads/writes.
 * @priv:		pointer to the private data
 */
struct spi_nor {
	struct mtd_info		mtd;
	struct mutex		lock;
	struct device		*dev;
	struct spi_mem		*spimem;
	u8			*bouncebuf;
	size_t			bouncebuf_size;
	const struct flash_info	*info;
	const struct spi_nor_manufacturer *manufacturer;
	u32			page_size;
	u8			addr_width;
	u8			erase_opcode;
	u8			read_opcode;
	u8			read_dummy;
	u8			program_opcode;
	enum spi_nor_protocol	read_proto;
	enum spi_nor_protocol	write_proto;
	enum spi_nor_protocol	reg_proto;
	bool			sst_write_second;
	u32			flags;
	enum spi_nor_cmd_ext	cmd_ext_type;
	struct sfdp		*sfdp;

	const struct spi_nor_controller_ops *controller_ops;

	struct spi_nor_flash_parameter *params;

	struct {
		struct spi_mem_dirmap_desc *rdesc;
		struct spi_mem_dirmap_desc *wdesc;
	} dirmap;

	void *priv;
};

static inline void spi_nor_set_flash_node(struct spi_nor *nor,
					  struct device_node *np)
{
	mtd_set_of_node(&nor->mtd, np);
}

static inline struct device_node *spi_nor_get_flash_node(struct spi_nor *nor)
{
	return mtd_get_of_node(&nor->mtd);
}

/**
 * spi_nor_scan() - scan the SPI NOR
 * @nor:	the spi_nor structure
 * @name:	the chip type name
 * @hwcaps:	the hardware capabilities supported by the controller driver
 *
 * The drivers can use this function to scan the SPI NOR.
 * In the scanning, it will try to get all the necessary information to
 * fill the mtd_info{} and the spi_nor{}.
 *
 * The chip type name can be provided through the @name parameter.
 *
 * Return: 0 for success, others for failure.
 */
int spi_nor_scan(struct spi_nor *nor, const char *name,
		 const struct spi_nor_hwcaps *hwcaps);

/**
 * spi_nor_restore_addr_mode() - restore the status of SPI NOR
 * @nor:	the spi_nor structure
 */
void spi_nor_restore(struct spi_nor *nor);

#endif
