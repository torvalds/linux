#ifndef __LINUX_MTD_SPI_NOR_H
#define __LINUX_MTD_SPI_NOR_H

/* Flash opcodes. */
#define	OPCODE_WREN		0x06	/* Write enable */
#define	OPCODE_RDSR		0x05	/* Read status register */
#define	OPCODE_WRSR		0x01	/* Write status register 1 byte */
#define	OPCODE_NORM_READ	0x03	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ	0x0b	/* Read data bytes (high frequency) */
#define	OPCODE_DUAL_READ        0x3b    /* Read data bytes (Dual SPI) */
#define	OPCODE_QUAD_READ        0x6b    /* Read data bytes (Quad SPI) */
#define	OPCODE_PP		0x02	/* Page program (up to 256 bytes) */
#define	OPCODE_BE_4K		0x20	/* Erase 4KiB block */
#define	OPCODE_BE_4K_PMC	0xd7	/* Erase 4KiB block on PMC chips */
#define	OPCODE_BE_32K		0x52	/* Erase 32KiB block */
#define	OPCODE_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define	OPCODE_SE		0xd8	/* Sector erase (usually 64KiB) */
#define	OPCODE_RDID		0x9f	/* Read JEDEC ID */
#define	OPCODE_RDCR             0x35    /* Read configuration register */

/* 4-byte address opcodes - used on Spansion and some Macronix flashes. */
#define	OPCODE_NORM_READ_4B	0x13	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ_4B	0x0c	/* Read data bytes (high frequency) */
#define	OPCODE_DUAL_READ_4B	0x3c    /* Read data bytes (Dual SPI) */
#define	OPCODE_QUAD_READ_4B	0x6c    /* Read data bytes (Quad SPI) */
#define	OPCODE_PP_4B		0x12	/* Page program (up to 256 bytes) */
#define	OPCODE_SE_4B		0xdc	/* Sector erase (usually 64KiB) */

/* Used for SST flashes only. */
#define	OPCODE_BP		0x02	/* Byte program */
#define	OPCODE_WRDI		0x04	/* Write disable */
#define	OPCODE_AAI_WP		0xad	/* Auto address increment word program */

/* Used for Macronix and Winbond flashes. */
#define	OPCODE_EN4B		0xb7	/* Enter 4-byte mode */
#define	OPCODE_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define	OPCODE_BRWR		0x17	/* Bank register write */

/* Status Register bits. */
#define	SR_WIP			1	/* Write in progress */
#define	SR_WEL			2	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define	SR_BP0			4	/* Block protect 0 */
#define	SR_BP1			8	/* Block protect 1 */
#define	SR_BP2			0x10	/* Block protect 2 */
#define	SR_SRWD			0x80	/* SR write protect */

#define SR_QUAD_EN_MX           0x40    /* Macronix Quad I/O */

/* Configuration Register bits. */
#define CR_QUAD_EN_SPAN		0x2     /* Spansion Quad I/O */

enum read_mode {
	SPI_NOR_NORMAL = 0,
	SPI_NOR_FAST,
	SPI_NOR_DUAL,
	SPI_NOR_QUAD,
};

/**
 * struct spi_nor_xfer_cfg - Structure for defining a Serial Flash transfer
 * @wren:		command for "Write Enable", or 0x00 for not required
 * @cmd:		command for operation
 * @cmd_pins:		number of pins to send @cmd (1, 2, 4)
 * @addr:		address for operation
 * @addr_pins:		number of pins to send @addr (1, 2, 4)
 * @addr_width:		number of address bytes
 *			(3,4, or 0 for address not required)
 * @mode:		mode data
 * @mode_pins:		number of pins to send @mode (1, 2, 4)
 * @mode_cycles:	number of mode cycles (0 for mode not required)
 * @dummy_cycles:	number of dummy cycles (0 for dummy not required)
 */
struct spi_nor_xfer_cfg {
	u8		wren;
	u8		cmd;
	u8		cmd_pins;
	u32		addr;
	u8		addr_pins;
	u8		addr_width;
	u8		mode;
	u8		mode_pins;
	u8		mode_cycles;
	u8		dummy_cycles;
};

#define	SPI_NOR_MAX_CMD_SIZE	8
enum spi_nor_ops {
	SPI_NOR_OPS_READ = 0,
	SPI_NOR_OPS_WRITE,
	SPI_NOR_OPS_ERASE,
	SPI_NOR_OPS_LOCK,
	SPI_NOR_OPS_UNLOCK,
};

/**
 * struct spi_nor - Structure for defining a the SPI NOR layer
 * @mtd:		point to a mtd_info structure
 * @lock:		the lock for the read/write/erase/lock/unlock operations
 * @dev:		point to a spi device, or a spi nor controller device.
 * @page_size:		the page size of the SPI NOR
 * @addr_width:		number of address bytes
 * @erase_opcode:	the opcode for erasing a sector
 * @read_opcode:	the read opcode
 * @read_dummy:		the dummy needed by the read operation
 * @program_opcode:	the program opcode
 * @flash_read:		the mode of the read
 * @sst_write_second:	used by the SST write operation
 * @cfg:		used by the read_xfer/write_xfer
 * @cmd_buf:		used by the write_reg
 * @prepare:		[OPTIONAL] do some preparations for the
 *			read/write/erase/lock/unlock operations
 * @unprepare:		[OPTIONAL] do some post work after the
 *			read/write/erase/lock/unlock operations
 * @read_xfer:		[OPTIONAL] the read fundamental primitive
 * @write_xfer:		[OPTIONAL] the writefundamental primitive
 * @read_reg:		[DRIVER-SPECIFIC] read out the register
 * @write_reg:		[DRIVER-SPECIFIC] write data to the register
 * @read_id:		[REPLACEABLE] read out the ID data, and find
 *			the proper spi_device_id
 * @wait_till_ready:	[REPLACEABLE] wait till the NOR becomes ready
 * @read:		[DRIVER-SPECIFIC] read data from the SPI NOR
 * @write:		[DRIVER-SPECIFIC] write data to the SPI NOR
 * @erase:		[DRIVER-SPECIFIC] erase a sector of the SPI NOR
 *			at the offset @offs
 * @priv:		the private data
 */
struct spi_nor {
	struct mtd_info		*mtd;
	struct mutex		lock;
	struct device		*dev;
	u32			page_size;
	u8			addr_width;
	u8			erase_opcode;
	u8			read_opcode;
	u8			read_dummy;
	u8			program_opcode;
	enum read_mode		flash_read;
	bool			sst_write_second;
	struct spi_nor_xfer_cfg	cfg;
	u8			cmd_buf[SPI_NOR_MAX_CMD_SIZE];

	int (*prepare)(struct spi_nor *nor, enum spi_nor_ops ops);
	void (*unprepare)(struct spi_nor *nor, enum spi_nor_ops ops);
	int (*read_xfer)(struct spi_nor *nor, struct spi_nor_xfer_cfg *cfg,
			 u8 *buf, size_t len);
	int (*write_xfer)(struct spi_nor *nor, struct spi_nor_xfer_cfg *cfg,
			  u8 *buf, size_t len);
	int (*read_reg)(struct spi_nor *nor, u8 opcode, u8 *buf, int len);
	int (*write_reg)(struct spi_nor *nor, u8 opcode, u8 *buf, int len,
			int write_enable);
	const struct spi_device_id *(*read_id)(struct spi_nor *nor);
	int (*wait_till_ready)(struct spi_nor *nor);

	int (*read)(struct spi_nor *nor, loff_t from,
			size_t len, size_t *retlen, u_char *read_buf);
	void (*write)(struct spi_nor *nor, loff_t to,
			size_t len, size_t *retlen, const u_char *write_buf);
	int (*erase)(struct spi_nor *nor, loff_t offs);

	void *priv;
};

/**
 * spi_nor_scan() - scan the SPI NOR
 * @nor:	the spi_nor structure
 * @id:		the spi_device_id provided by the driver
 * @mode:	the read mode supported by the driver
 *
 * The drivers can use this fuction to scan the SPI NOR.
 * In the scanning, it will try to get all the necessary information to
 * fill the mtd_info{} and the spi_nor{}.
 *
 * The board may assigns a spi_device_id with @id which be used to compared with
 * the spi_device_id detected by the scanning.
 *
 * Return: 0 for success, others for failure.
 */
int spi_nor_scan(struct spi_nor *nor, const struct spi_device_id *id,
			enum read_mode mode);
extern const struct spi_device_id spi_nor_ids[];

#endif
