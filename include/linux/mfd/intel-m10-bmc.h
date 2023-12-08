/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel MAX 10 Board Management Controller chip.
 *
 * Copyright (C) 2018-2020 Intel Corporation, Inc.
 */
#ifndef __MFD_INTEL_M10_BMC_H
#define __MFD_INTEL_M10_BMC_H

#include <linux/regmap.h>

#define M10BMC_LEGACY_BUILD_VER		0x300468
#define M10BMC_SYS_BASE			0x300800
#define M10BMC_SYS_END			0x300fff
#define M10BMC_FLASH_BASE		0x10000000
#define M10BMC_FLASH_END		0x1fffffff
#define M10BMC_MEM_END			M10BMC_FLASH_END

#define M10BMC_STAGING_BASE		0x18000000
#define M10BMC_STAGING_SIZE		0x3800000

/* Register offset of system registers */
#define NIOS2_FW_VERSION		0x0
#define M10BMC_MAC_LOW			0x10
#define M10BMC_MAC_BYTE4		GENMASK(7, 0)
#define M10BMC_MAC_BYTE3		GENMASK(15, 8)
#define M10BMC_MAC_BYTE2		GENMASK(23, 16)
#define M10BMC_MAC_BYTE1		GENMASK(31, 24)
#define M10BMC_MAC_HIGH			0x14
#define M10BMC_MAC_BYTE6		GENMASK(7, 0)
#define M10BMC_MAC_BYTE5		GENMASK(15, 8)
#define M10BMC_MAC_COUNT		GENMASK(23, 16)
#define M10BMC_TEST_REG			0x3c
#define M10BMC_BUILD_VER		0x68
#define M10BMC_VER_MAJOR_MSK		GENMASK(23, 16)
#define M10BMC_VER_PCB_INFO_MSK		GENMASK(31, 24)
#define M10BMC_VER_LEGACY_INVALID	0xffffffff

/* Secure update doorbell register, in system register region */
#define M10BMC_DOORBELL			0x400

/* Authorization Result register, in system register region */
#define M10BMC_AUTH_RESULT		0x404

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
#define rsu_stat(doorbell)	FIELD_GET(DRBL_RSU_STATUS, doorbell)

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
#define BMC_REH_ADDR	0x17ffc004
#define BMC_PROG_ADDR	0x17ffc000
#define BMC_PROG_MAGIC	0x5746

#define SR_REH_ADDR	0x17ffd004
#define SR_PROG_ADDR	0x17ffd000
#define SR_PROG_MAGIC	0x5253

#define PR_REH_ADDR	0x17ffe004
#define PR_PROG_ADDR	0x17ffe000
#define PR_PROG_MAGIC	0x5250

/* Address of 4KB inverted bit vector containing staging area FLASH count */
#define STAGING_FLASH_COUNT	0x17ffb000

/**
 * struct intel_m10bmc - Intel MAX 10 BMC parent driver data structure
 * @dev: this device
 * @regmap: the regmap used to access registers by m10bmc itself
 */
struct intel_m10bmc {
	struct device *dev;
	struct regmap *regmap;
};

/*
 * register access helper functions.
 *
 * m10bmc_raw_read - read m10bmc register per addr
 * m10bmc_sys_read - read m10bmc system register per offset
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

/*
 * The base of the system registers could be configured by HW developers, and
 * in HW SPEC, the base is not added to the addresses of the system registers.
 *
 * This macro helps to simplify the accessing of the system registers. And if
 * the base is reconfigured in HW, SW developers could simply change the
 * M10BMC_SYS_BASE accordingly.
 */
#define m10bmc_sys_read(m10bmc, offset, val) \
	m10bmc_raw_read(m10bmc, M10BMC_SYS_BASE + (offset), val)

#endif /* __MFD_INTEL_M10_BMC_H */
