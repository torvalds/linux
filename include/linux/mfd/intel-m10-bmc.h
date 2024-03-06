/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel MAX 10 Board Management Controller chip.
 *
 * Copyright (C) 2018-2020 Intel Corporation, Inc.
 */
#ifndef __MFD_INTEL_M10_BMC_H
#define __MFD_INTEL_M10_BMC_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>

#define M10BMC_N3000_LEGACY_BUILD_VER	0x300468
#define M10BMC_N3000_SYS_BASE		0x300800
#define M10BMC_N3000_SYS_END		0x300fff
#define M10BMC_N3000_FLASH_BASE		0x10000000
#define M10BMC_N3000_FLASH_END		0x1fffffff
#define M10BMC_N3000_MEM_END		M10BMC_N3000_FLASH_END

#define M10BMC_STAGING_BASE		0x18000000
#define M10BMC_STAGING_SIZE		0x3800000

/* Register offset of system registers */
#define NIOS2_N3000_FW_VERSION		0x0
#define M10BMC_N3000_MAC_LOW		0x10
#define M10BMC_N3000_MAC_BYTE4		GENMASK(7, 0)
#define M10BMC_N3000_MAC_BYTE3		GENMASK(15, 8)
#define M10BMC_N3000_MAC_BYTE2		GENMASK(23, 16)
#define M10BMC_N3000_MAC_BYTE1		GENMASK(31, 24)
#define M10BMC_N3000_MAC_HIGH		0x14
#define M10BMC_N3000_MAC_BYTE6		GENMASK(7, 0)
#define M10BMC_N3000_MAC_BYTE5		GENMASK(15, 8)
#define M10BMC_N3000_MAC_COUNT		GENMASK(23, 16)
#define M10BMC_N3000_TEST_REG		0x3c
#define M10BMC_N3000_BUILD_VER		0x68
#define M10BMC_N3000_VER_MAJOR_MSK	GENMASK(23, 16)
#define M10BMC_N3000_VER_PCB_INFO_MSK	GENMASK(31, 24)
#define M10BMC_N3000_VER_LEGACY_INVALID	0xffffffff

/* Telemetry registers */
#define M10BMC_N3000_TELEM_START	0x100
#define M10BMC_N3000_TELEM_END		0x250
#define M10BMC_D5005_TELEM_END		0x300

/* Secure update doorbell register, in system register region */
#define M10BMC_N3000_DOORBELL		0x400

/* Authorization Result register, in system register region */
#define M10BMC_N3000_AUTH_RESULT		0x404

/* Doorbell register fields */
#define DRBL_RSU_REQUEST		BIT(0)
#define DRBL_RSU_PROGRESS		GENMASK(7, 4)
#define DRBL_HOST_STATUS		GENMASK(11, 8)
#define DRBL_RSU_STATUS			GENMASK(23, 16)
#define DRBL_PKVL_EEPROM_LOAD_SEC	BIT(24)
#define DRBL_PKVL1_POLL_EN		BIT(25)
#define DRBL_PKVL2_POLL_EN		BIT(26)
#define DRBL_CONFIG_SEL			BIT(28)
#define DRBL_REBOOT_REQ			BIT(29)
#define DRBL_REBOOT_DISABLED		BIT(30)

/* Progress states */
#define RSU_PROG_IDLE			0x0
#define RSU_PROG_PREPARE		0x1
#define RSU_PROG_READY			0x3
#define RSU_PROG_AUTHENTICATING		0x4
#define RSU_PROG_COPYING		0x5
#define RSU_PROG_UPDATE_CANCEL		0x6
#define RSU_PROG_PROGRAM_KEY_HASH	0x7
#define RSU_PROG_RSU_DONE		0x8
#define RSU_PROG_PKVL_PROM_DONE		0x9

/* Device and error states */
#define RSU_STAT_NORMAL			0x0
#define RSU_STAT_TIMEOUT		0x1
#define RSU_STAT_AUTH_FAIL		0x2
#define RSU_STAT_COPY_FAIL		0x3
#define RSU_STAT_FATAL			0x4
#define RSU_STAT_PKVL_REJECT		0x5
#define RSU_STAT_NON_INC		0x6
#define RSU_STAT_ERASE_FAIL		0x7
#define RSU_STAT_WEAROUT		0x8
#define RSU_STAT_NIOS_OK		0x80
#define RSU_STAT_USER_OK		0x81
#define RSU_STAT_FACTORY_OK		0x82
#define RSU_STAT_USER_FAIL		0x83
#define RSU_STAT_FACTORY_FAIL		0x84
#define RSU_STAT_NIOS_FLASH_ERR		0x85
#define RSU_STAT_FPGA_FLASH_ERR		0x86

#define HOST_STATUS_IDLE		0x0
#define HOST_STATUS_WRITE_DONE		0x1
#define HOST_STATUS_ABORT_RSU		0x2

#define rsu_prog(doorbell)	FIELD_GET(DRBL_RSU_PROGRESS, doorbell)

/* interval 100ms and timeout 5s */
#define NIOS_HANDSHAKE_INTERVAL_US	(100 * 1000)
#define NIOS_HANDSHAKE_TIMEOUT_US	(5 * 1000 * 1000)

/* RSU PREP Timeout (2 minutes) to erase flash staging area */
#define RSU_PREP_INTERVAL_MS		100
#define RSU_PREP_TIMEOUT_MS		(2 * 60 * 1000)

/* RSU Complete Timeout (40 minutes) for full flash update */
#define RSU_COMPLETE_INTERVAL_MS	1000
#define RSU_COMPLETE_TIMEOUT_MS		(40 * 60 * 1000)

/* Addresses for security related data in FLASH */
#define M10BMC_N3000_BMC_REH_ADDR	0x17ffc004
#define M10BMC_N3000_BMC_PROG_ADDR	0x17ffc000
#define M10BMC_N3000_BMC_PROG_MAGIC	0x5746

#define M10BMC_N3000_SR_REH_ADDR	0x17ffd004
#define M10BMC_N3000_SR_PROG_ADDR	0x17ffd000
#define M10BMC_N3000_SR_PROG_MAGIC	0x5253

#define M10BMC_N3000_PR_REH_ADDR	0x17ffe004
#define M10BMC_N3000_PR_PROG_ADDR	0x17ffe000
#define M10BMC_N3000_PR_PROG_MAGIC	0x5250

/* Address of 4KB inverted bit vector containing staging area FLASH count */
#define M10BMC_N3000_STAGING_FLASH_COUNT	0x17ffb000

#define M10BMC_N6000_INDIRECT_BASE		0x400

#define M10BMC_N6000_SYS_BASE			0x0
#define M10BMC_N6000_SYS_END			0xfff

#define M10BMC_N6000_DOORBELL			0x1c0
#define M10BMC_N6000_AUTH_RESULT		0x1c4
#define AUTH_RESULT_RSU_STATUS			GENMASK(23, 16)

#define M10BMC_N6000_BUILD_VER			0x0
#define NIOS2_N6000_FW_VERSION			0x4
#define M10BMC_N6000_MAC_LOW			0x20
#define M10BMC_N6000_MAC_HIGH			(M10BMC_N6000_MAC_LOW + 4)

/* Addresses for security related data in FLASH */
#define M10BMC_N6000_BMC_REH_ADDR		0x7ffc004
#define M10BMC_N6000_BMC_PROG_ADDR		0x7ffc000
#define M10BMC_N6000_BMC_PROG_MAGIC		0x5746

#define M10BMC_N6000_SR_REH_ADDR		0x7ffd004
#define M10BMC_N6000_SR_PROG_ADDR		0x7ffd000
#define M10BMC_N6000_SR_PROG_MAGIC		0x5253

#define M10BMC_N6000_PR_REH_ADDR		0x7ffe004
#define M10BMC_N6000_PR_PROG_ADDR		0x7ffe000
#define M10BMC_N6000_PR_PROG_MAGIC		0x5250

#define M10BMC_N6000_STAGING_FLASH_COUNT	0x7ff5000

#define M10BMC_N6000_FLASH_MUX_CTRL		0x1d0
#define M10BMC_N6000_FLASH_MUX_SELECTION	GENMASK(2, 0)
#define M10BMC_N6000_FLASH_MUX_IDLE		0
#define M10BMC_N6000_FLASH_MUX_NIOS		1
#define M10BMC_N6000_FLASH_MUX_HOST		2
#define M10BMC_N6000_FLASH_MUX_PFL		4
#define get_flash_mux(mux)			FIELD_GET(M10BMC_N6000_FLASH_MUX_SELECTION, mux)

#define M10BMC_N6000_FLASH_NIOS_REQUEST		BIT(4)
#define M10BMC_N6000_FLASH_HOST_REQUEST		BIT(5)

#define M10BMC_N6000_FLASH_CTRL			0x40
#define M10BMC_N6000_FLASH_WR_MODE		BIT(0)
#define M10BMC_N6000_FLASH_RD_MODE		BIT(1)
#define M10BMC_N6000_FLASH_BUSY			BIT(2)
#define M10BMC_N6000_FLASH_FIFO_SPACE		GENMASK(13, 4)
#define M10BMC_N6000_FLASH_READ_COUNT		GENMASK(25, 16)

#define M10BMC_N6000_FLASH_ADDR			0x44
#define M10BMC_N6000_FLASH_FIFO			0x800
#define M10BMC_N6000_READ_BLOCK_SIZE		0x800
#define M10BMC_N6000_FIFO_MAX_BYTES		0x800
#define M10BMC_N6000_FIFO_WORD_SIZE		4
#define M10BMC_N6000_FIFO_MAX_WORDS		(M10BMC_N6000_FIFO_MAX_BYTES / \
						 M10BMC_N6000_FIFO_WORD_SIZE)

#define M10BMC_FLASH_INT_US			1
#define M10BMC_FLASH_TIMEOUT_US			10000

/**
 * struct m10bmc_csr_map - Intel MAX 10 BMC CSR register map
 */
struct m10bmc_csr_map {
	unsigned int base;
	unsigned int build_version;
	unsigned int fw_version;
	unsigned int mac_low;
	unsigned int mac_high;
	unsigned int doorbell;
	unsigned int auth_result;
	unsigned int bmc_prog_addr;
	unsigned int bmc_reh_addr;
	unsigned int bmc_magic;
	unsigned int sr_prog_addr;
	unsigned int sr_reh_addr;
	unsigned int sr_magic;
	unsigned int pr_prog_addr;
	unsigned int pr_reh_addr;
	unsigned int pr_magic;
	unsigned int rsu_update_counter;
};

/**
 * struct intel_m10bmc_platform_info - Intel MAX 10 BMC platform specific information
 * @cells: MFD cells
 * @n_cells: MFD cells ARRAY_SIZE()
 * @handshake_sys_reg_ranges: array of register ranges for fw handshake regs
 * @handshake_sys_reg_nranges: number of register ranges for fw handshake regs
 * @csr_map: the mappings for register definition of MAX10 BMC
 */
struct intel_m10bmc_platform_info {
	struct mfd_cell *cells;
	int n_cells;
	const struct regmap_range *handshake_sys_reg_ranges;
	unsigned int handshake_sys_reg_nranges;
	const struct m10bmc_csr_map *csr_map;
};

struct intel_m10bmc;

/**
 * struct intel_m10bmc_flash_bulk_ops - device specific operations for flash R/W
 * @read: read a block of data from flash
 * @write: write a block of data to flash
 * @lock_write: locks flash access for erase+write
 * @unlock_write: unlock flash access
 *
 * Write must be protected with @lock_write and @unlock_write. While the flash
 * is locked, @read returns -EBUSY.
 */
struct intel_m10bmc_flash_bulk_ops {
	int (*read)(struct intel_m10bmc *m10bmc, u8 *buf, u32 addr, u32 size);
	int (*write)(struct intel_m10bmc *m10bmc, const u8 *buf, u32 offset, u32 size);
	int (*lock_write)(struct intel_m10bmc *m10bmc);
	void (*unlock_write)(struct intel_m10bmc *m10bmc);
};

enum m10bmc_fw_state {
	M10BMC_FW_STATE_NORMAL,
	M10BMC_FW_STATE_SEC_UPDATE_PREPARE,
	M10BMC_FW_STATE_SEC_UPDATE_WRITE,
	M10BMC_FW_STATE_SEC_UPDATE_PROGRAM,
};

/**
 * struct intel_m10bmc - Intel MAX 10 BMC parent driver data structure
 * @dev: this device
 * @regmap: the regmap used to access registers by m10bmc itself
 * @info: the platform information for MAX10 BMC
 * @flash_bulk_ops: optional device specific operations for flash R/W
 * @bmcfw_lock: read/write semaphore to BMC firmware running state
 * @bmcfw_state: BMC firmware running state. Available only when
 *		 handshake_sys_reg_nranges > 0.
 */
struct intel_m10bmc {
	struct device *dev;
	struct regmap *regmap;
	const struct intel_m10bmc_platform_info *info;
	const struct intel_m10bmc_flash_bulk_ops *flash_bulk_ops;
	struct rw_semaphore bmcfw_lock;		/* Protects bmcfw_state */
	enum m10bmc_fw_state bmcfw_state;
};

/*
 * register access helper functions.
 *
 * m10bmc_raw_read - read m10bmc register per addr
 * m10bmc_sys_read - read m10bmc system register per offset
 * m10bmc_sys_update_bits - update m10bmc system register per offset
 */
static inline int
m10bmc_raw_read(struct intel_m10bmc *m10bmc, unsigned int addr,
		unsigned int *val)
{
	int ret;

	ret = regmap_read(m10bmc->regmap, addr, val);
	if (ret)
		dev_err(m10bmc->dev, "fail to read raw reg %x: %d\n",
			addr, ret);

	return ret;
}

int m10bmc_sys_read(struct intel_m10bmc *m10bmc, unsigned int offset, unsigned int *val);
int m10bmc_sys_update_bits(struct intel_m10bmc *m10bmc, unsigned int offset,
			   unsigned int msk, unsigned int val);

/*
 * Track the state of the firmware, as it is not available for register
 * handshakes during secure updates on some MAX 10 cards.
 */
void m10bmc_fw_state_set(struct intel_m10bmc *m10bmc, enum m10bmc_fw_state new_state);

/*
 * MAX10 BMC Core support
 */
int m10bmc_dev_init(struct intel_m10bmc *m10bmc, const struct intel_m10bmc_platform_info *info);
extern const struct attribute_group *m10bmc_dev_groups[];

#endif /* __MFD_INTEL_M10_BMC_H */
