/*
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef MFD_STW481X_H
#define MFD_STW481X_H

#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>
#include <linux/bitops.h>

/* These registers are accessed from more than one driver */
#define STW_CONF1			0x11U
#define STW_CONF1_PDN_VMMC		0x01U
#define STW_CONF1_VMMC_MASK		0x0eU
#define STW_CONF1_VMMC_1_8V		0x02U
#define STW_CONF1_VMMC_2_85V		0x04U
#define STW_CONF1_VMMC_3V		0x06U
#define STW_CONF1_VMMC_1_85V		0x08U
#define STW_CONF1_VMMC_2_6V		0x0aU
#define STW_CONF1_VMMC_2_7V		0x0cU
#define STW_CONF1_VMMC_3_3V		0x0eU
#define STW_CONF1_MMC_LS_STATUS		0x10U
#define STW_PCTL_REG_LO			0x1eU
#define STW_PCTL_REG_HI			0x1fU
#define STW_CONF1_V_MONITORING		0x20U
#define STW_CONF1_IT_WARN		0x40U
#define STW_CONF1_PDN_VAUX		0x80U
#define STW_CONF2			0x20U
#define STW_CONF2_MASK_TWARN		0x01U
#define STW_CONF2_VMMC_EXT		0x02U
#define STW_CONF2_MASK_IT_WAKE_UP	0x04U
#define STW_CONF2_GPO1			0x08U
#define STW_CONF2_GPO2			0x10U
#define STW_VCORE_SLEEP			0x21U

/**
 * struct stw481x - state holder for the Stw481x drivers
 * @mutex: mutex to serialize I2C accesses
 * @i2c_client: corresponding I2C client
 * @regulator: regulator device for regulator children
 * @map: regmap handle to access device registers
 */
struct stw481x {
	struct mutex		lock;
	struct i2c_client	*client;
	struct regulator_dev	*vmmc_regulator;
	struct regmap		*map;
};

#endif
