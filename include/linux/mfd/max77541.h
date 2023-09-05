/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MFD_MAX77541_H
#define __MFD_MAX77541_H

#include <linux/bits.h>
#include <linux/types.h>

/* REGISTERS */
#define MAX77541_REG_INT_SRC                    0x00
#define MAX77541_REG_INT_SRC_M                  0x01

#define MAX77541_BIT_INT_SRC_TOPSYS             BIT(0)
#define MAX77541_BIT_INT_SRC_BUCK               BIT(1)

#define MAX77541_REG_TOPSYS_INT                 0x02
#define MAX77541_REG_TOPSYS_INT_M               0x03

#define MAX77541_BIT_TOPSYS_INT_TJ_120C         BIT(0)
#define MAX77541_BIT_TOPSYS_INT_TJ_140C         BIT(1)
#define MAX77541_BIT_TOPSYS_INT_TSHDN           BIT(2)
#define MAX77541_BIT_TOPSYS_INT_UVLO            BIT(3)
#define MAX77541_BIT_TOPSYS_INT_ALT_SWO         BIT(4)
#define MAX77541_BIT_TOPSYS_INT_EXT_FREQ_DET    BIT(5)

/* REGULATORS */
#define MAX77541_REG_BUCK_INT                   0x20
#define MAX77541_REG_BUCK_INT_M                 0x21

#define MAX77541_BIT_BUCK_INT_M1_POK_FLT        BIT(0)
#define MAX77541_BIT_BUCK_INT_M2_POK_FLT        BIT(1)
#define MAX77541_BIT_BUCK_INT_M1_SCFLT          BIT(4)
#define MAX77541_BIT_BUCK_INT_M2_SCFLT          BIT(5)

#define MAX77541_REG_EN_CTRL                    0x0B

#define MAX77541_BIT_M1_EN                      BIT(0)
#define MAX77541_BIT_M2_EN                      BIT(1)

#define MAX77541_REG_M1_VOUT                    0x23
#define MAX77541_REG_M2_VOUT                    0x33

#define MAX77541_BITS_MX_VOUT                   GENMASK(7, 0)

#define MAX77541_REG_M1_CFG1                    0x25
#define MAX77541_REG_M2_CFG1                    0x35

#define MAX77541_BITS_MX_CFG1_RNG               GENMASK(7, 6)

/* ADC */
#define MAX77541_REG_ADC_INT                    0x70
#define MAX77541_REG_ADC_INT_M                  0x71

#define MAX77541_BIT_ADC_INT_CH1_I              BIT(0)
#define MAX77541_BIT_ADC_INT_CH2_I              BIT(1)
#define MAX77541_BIT_ADC_INT_CH3_I              BIT(2)
#define MAX77541_BIT_ADC_INT_CH6_I              BIT(5)

#define MAX77541_REG_ADC_DATA_CH1               0x72
#define MAX77541_REG_ADC_DATA_CH2               0x73
#define MAX77541_REG_ADC_DATA_CH3               0x74
#define MAX77541_REG_ADC_DATA_CH6               0x77

/* INTERRUPT MASKS*/
#define MAX77541_REG_INT_SRC_MASK               0x00
#define MAX77541_REG_TOPSYS_INT_MASK            0x00
#define MAX77541_REG_BUCK_INT_MASK              0x00

#define MAX77541_MAX_REGULATORS 2

enum max7754x_ids {
	MAX77540 = 1,
	MAX77541,
};

struct regmap;
struct regmap_irq_chip_data;
struct i2c_client;

struct max77541 {
	struct i2c_client *i2c;
	struct regmap *regmap;
	enum max7754x_ids id;

	struct regmap_irq_chip_data *irq_data;
	struct regmap_irq_chip_data *irq_buck;
	struct regmap_irq_chip_data *irq_topsys;
	struct regmap_irq_chip_data *irq_adc;
};

#endif /* __MFD_MAX77541_H */
