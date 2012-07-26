/*
 * tps65912.h  --  TI TPS6591x
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_TPS65912_H
#define __LINUX_MFD_TPS65912_H

/* TPS regulator type list */
#define REGULATOR_LDO		0
#define REGULATOR_DCDC		1

/*
 * List of registers for TPS65912
 */

#define TPS65912_DCDC1_CTRL		0x00
#define TPS65912_DCDC2_CTRL		0x01
#define TPS65912_DCDC3_CTRL		0x02
#define TPS65912_DCDC4_CTRL		0x03
#define TPS65912_DCDC1_OP		0x04
#define TPS65912_DCDC1_AVS		0x05
#define TPS65912_DCDC1_LIMIT		0x06
#define TPS65912_DCDC2_OP		0x07
#define TPS65912_DCDC2_AVS		0x08
#define TPS65912_DCDC2_LIMIT		0x09
#define TPS65912_DCDC3_OP		0x0A
#define TPS65912_DCDC3_AVS		0x0B
#define TPS65912_DCDC3_LIMIT		0x0C
#define TPS65912_DCDC4_OP		0x0D
#define TPS65912_DCDC4_AVS		0x0E
#define TPS65912_DCDC4_LIMIT		0x0F
#define TPS65912_LDO1_OP		0x10
#define TPS65912_LDO1_AVS		0x11
#define TPS65912_LDO1_LIMIT		0x12
#define TPS65912_LDO2_OP		0x13
#define TPS65912_LDO2_AVS		0x14
#define TPS65912_LDO2_LIMIT		0x15
#define TPS65912_LDO3_OP		0x16
#define TPS65912_LDO3_AVS		0x17
#define TPS65912_LDO3_LIMIT		0x18
#define TPS65912_LDO4_OP		0x19
#define TPS65912_LDO4_AVS		0x1A
#define TPS65912_LDO4_LIMIT		0x1B
#define TPS65912_LDO5			0x1C
#define TPS65912_LDO6			0x1D
#define TPS65912_LDO7			0x1E
#define TPS65912_LDO8			0x1F
#define TPS65912_LDO9			0x20
#define TPS65912_LDO10			0x21
#define TPS65912_THRM			0x22
#define TPS65912_CLK32OUT		0x23
#define TPS65912_DEVCTRL		0x24
#define TPS65912_DEVCTRL2		0x25
#define TPS65912_I2C_SPI_CFG		0x26
#define TPS65912_KEEP_ON		0x27
#define TPS65912_KEEP_ON2		0x28
#define TPS65912_SET_OFF1		0x29
#define TPS65912_SET_OFF2		0x2A
#define TPS65912_DEF_VOLT		0x2B
#define TPS65912_DEF_VOLT_MAPPING	0x2C
#define TPS65912_DISCHARGE		0x2D
#define TPS65912_DISCHARGE2		0x2E
#define TPS65912_EN1_SET1		0x2F
#define TPS65912_EN1_SET2		0x30
#define TPS65912_EN2_SET1		0x31
#define TPS65912_EN2_SET2		0x32
#define TPS65912_EN3_SET1		0x33
#define TPS65912_EN3_SET2		0x34
#define TPS65912_EN4_SET1		0x35
#define TPS65912_EN4_SET2		0x36
#define TPS65912_PGOOD			0x37
#define TPS65912_PGOOD2			0x38
#define TPS65912_INT_STS		0x39
#define TPS65912_INT_MSK		0x3A
#define TPS65912_INT_STS2		0x3B
#define TPS65912_INT_MSK2		0x3C
#define TPS65912_INT_STS3		0x3D
#define TPS65912_INT_MSK3		0x3E
#define TPS65912_INT_STS4		0x3F
#define TPS65912_INT_MSK4		0x40
#define TPS65912_GPIO1			0x41
#define TPS65912_GPIO2			0x42
#define TPS65912_GPIO3			0x43
#define TPS65912_GPIO4			0x44
#define TPS65912_GPIO5			0x45
#define TPS65912_VMON			0x46
#define TPS65912_LEDA_CTRL1		0x47
#define TPS65912_LEDA_CTRL2		0x48
#define TPS65912_LEDA_CTRL3		0x49
#define TPS65912_LEDA_CTRL4		0x4A
#define TPS65912_LEDA_CTRL5		0x4B
#define TPS65912_LEDA_CTRL6		0x4C
#define TPS65912_LEDA_CTRL7		0x4D
#define TPS65912_LEDA_CTRL8		0x4E
#define TPS65912_LEDB_CTRL1		0x4F
#define TPS65912_LEDB_CTRL2		0x50
#define TPS65912_LEDB_CTRL3		0x51
#define TPS65912_LEDB_CTRL4		0x52
#define TPS65912_LEDB_CTRL5		0x53
#define TPS65912_LEDB_CTRL6		0x54
#define TPS65912_LEDB_CTRL7		0x55
#define TPS65912_LEDB_CTRL8		0x56
#define TPS65912_LEDC_CTRL1		0x57
#define TPS65912_LEDC_CTRL2		0x58
#define TPS65912_LEDC_CTRL3		0x59
#define TPS65912_LEDC_CTRL4		0x5A
#define TPS65912_LEDC_CTRL5		0x5B
#define TPS65912_LEDC_CTRL6		0x5C
#define TPS65912_LEDC_CTRL7		0x5D
#define TPS65912_LEDC_CTRL8		0x5E
#define TPS65912_LED_RAMP_UP_TIME	0x5F
#define TPS65912_LED_RAMP_DOWN_TIME	0x60
#define TPS65912_LED_SEQ_EN		0x61
#define TPS65912_LOADSWITCH		0x62
#define TPS65912_SPARE			0x63
#define TPS65912_VERNUM			0x64
#define TPS6591X_MAX_REGISTER		0x64

/* IRQ Definitions */
#define TPS65912_IRQ_PWRHOLD_F		0
#define TPS65912_IRQ_VMON		1
#define TPS65912_IRQ_PWRON		2
#define TPS65912_IRQ_PWRON_LP		3
#define TPS65912_IRQ_PWRHOLD_R		4
#define TPS65912_IRQ_HOTDIE		5
#define TPS65912_IRQ_GPIO1_R		6
#define TPS65912_IRQ_GPIO1_F		7
#define TPS65912_IRQ_GPIO2_R		8
#define TPS65912_IRQ_GPIO2_F		9
#define TPS65912_IRQ_GPIO3_R		10
#define TPS65912_IRQ_GPIO3_F		11
#define TPS65912_IRQ_GPIO4_R		12
#define TPS65912_IRQ_GPIO4_F		13
#define TPS65912_IRQ_GPIO5_R		14
#define TPS65912_IRQ_GPIO5_F		15
#define TPS65912_IRQ_PGOOD_DCDC1	16
#define TPS65912_IRQ_PGOOD_DCDC2	17
#define TPS65912_IRQ_PGOOD_DCDC3	18
#define TPS65912_IRQ_PGOOD_DCDC4	19
#define TPS65912_IRQ_PGOOD_LDO1		20
#define TPS65912_IRQ_PGOOD_LDO2		21
#define TPS65912_IRQ_PGOOD_LDO3		22
#define TPS65912_IRQ_PGOOD_LDO4		23
#define TPS65912_IRQ_PGOOD_LDO5		24
#define TPS65912_IRQ_PGOOD_LDO6		25
#define TPS65912_IRQ_PGOOD_LDO7		26
#define TPS65912_IRQ_PGOOD_LD08		27
#define TPS65912_IRQ_PGOOD_LDO9		28
#define TPS65912_IRQ_PGOOD_LDO10	29

#define TPS65912_NUM_IRQ		30

/* GPIO 1 and 2 Register Definitions */
#define GPIO_SLEEP_MASK			0x80
#define GPIO_SLEEP_SHIFT		7
#define GPIO_DEB_MASK			0x10
#define GPIO_DEB_SHIFT			4
#define GPIO_CFG_MASK			0x04
#define GPIO_CFG_SHIFT			2
#define GPIO_STS_MASK			0x02
#define GPIO_STS_SHIFT			1
#define GPIO_SET_MASK			0x01
#define GPIO_SET_SHIFT			0

/* GPIO 3 Register Definitions */
#define GPIO3_SLEEP_MASK		0x80
#define GPIO3_SLEEP_SHIFT		7
#define GPIO3_SEL_MASK			0x40
#define GPIO3_SEL_SHIFT			6
#define GPIO3_ODEN_MASK			0x20
#define GPIO3_ODEN_SHIFT		5
#define GPIO3_DEB_MASK			0x10
#define GPIO3_DEB_SHIFT			4
#define GPIO3_PDEN_MASK			0x08
#define GPIO3_PDEN_SHIFT		3
#define GPIO3_CFG_MASK			0x04
#define GPIO3_CFG_SHIFT			2
#define GPIO3_STS_MASK			0x02
#define GPIO3_STS_SHIFT			1
#define GPIO3_SET_MASK			0x01
#define GPIO3_SET_SHIFT			0

/* GPIO 4 Register Definitions */
#define GPIO4_SLEEP_MASK		0x80
#define GPIO4_SLEEP_SHIFT		7
#define GPIO4_SEL_MASK			0x40
#define GPIO4_SEL_SHIFT			6
#define GPIO4_ODEN_MASK			0x20
#define GPIO4_ODEN_SHIFT		5
#define GPIO4_DEB_MASK			0x10
#define GPIO4_DEB_SHIFT			4
#define GPIO4_PDEN_MASK			0x08
#define GPIO4_PDEN_SHIFT		3
#define GPIO4_CFG_MASK			0x04
#define GPIO4_CFG_SHIFT			2
#define GPIO4_STS_MASK			0x02
#define GPIO4_STS_SHIFT			1
#define GPIO4_SET_MASK			0x01
#define GPIO4_SET_SHIFT			0

/* Register THERM  (0x80) register.RegisterDescription */
#define THERM_THERM_HD_MASK		0x20
#define THERM_THERM_HD_SHIFT		5
#define THERM_THERM_TS_MASK		0x10
#define THERM_THERM_TS_SHIFT		4
#define THERM_THERM_HDSEL_MASK		0x0C
#define THERM_THERM_HDSEL_SHIFT		2
#define THERM_RSVD1_MASK		0x02
#define THERM_RSVD1_SHIFT		1
#define THERM_THERM_STATE_MASK		0x01
#define THERM_THERM_STATE_SHIFT		0

/* Register DCDCCTRL1 register.RegisterDescription */
#define DCDCCTRL_VCON_ENABLE_MASK	0x80
#define DCDCCTRL_VCON_ENABLE_SHIFT	7
#define DCDCCTRL_VCON_RANGE1_MASK	0x40
#define DCDCCTRL_VCON_RANGE1_SHIFT	6
#define DCDCCTRL_VCON_RANGE0_MASK	0x20
#define DCDCCTRL_VCON_RANGE0_SHIFT	5
#define DCDCCTRL_TSTEP2_MASK		0x10
#define DCDCCTRL_TSTEP2_SHIFT		4
#define DCDCCTRL_TSTEP1_MASK		0x08
#define DCDCCTRL_TSTEP1_SHIFT		3
#define DCDCCTRL_TSTEP0_MASK		0x04
#define DCDCCTRL_TSTEP0_SHIFT		2
#define DCDCCTRL_DCDC1_MODE_MASK	0x02
#define DCDCCTRL_DCDC1_MODE_SHIFT	1

/* Register DCDCCTRL2 and DCDCCTRL3 register.RegisterDescription */
#define DCDCCTRL_TSTEP2_MASK		0x10
#define DCDCCTRL_TSTEP2_SHIFT		4
#define DCDCCTRL_TSTEP1_MASK		0x08
#define DCDCCTRL_TSTEP1_SHIFT		3
#define DCDCCTRL_TSTEP0_MASK		0x04
#define DCDCCTRL_TSTEP0_SHIFT		2
#define DCDCCTRL_DCDC_MODE_MASK		0x02
#define DCDCCTRL_DCDC_MODE_SHIFT	1
#define DCDCCTRL_RSVD0_MASK		0x01
#define DCDCCTRL_RSVD0_SHIFT		0

/* Register DCDCCTRL4 register.RegisterDescription */
#define DCDCCTRL_RAMP_TIME_MASK		0x01
#define DCDCCTRL_RAMP_TIME_SHIFT	0

/* Register DCDCx_AVS */
#define DCDC_AVS_ENABLE_MASK		0x80
#define DCDC_AVS_ENABLE_SHIFT		7
#define DCDC_AVS_ECO_MASK		0x40
#define DCDC_AVS_ECO_SHIFT		6

/* Register DCDCx_LIMIT */
#define DCDC_LIMIT_RANGE_MASK		0xC0
#define DCDC_LIMIT_RANGE_SHIFT		6
#define DCDC_LIMIT_MAX_SEL_MASK		0x3F
#define DCDC_LIMIT_MAX_SEL_SHIFT	0

/**
 * struct tps65912_board
 * Board platform dat may be used to initialize regulators.
 */
struct tps65912_board {
	int is_dcdc1_avs;
	int is_dcdc2_avs;
	int is_dcdc3_avs;
	int is_dcdc4_avs;
	int irq;
	int irq_base;
	int gpio_base;
	struct regulator_init_data *tps65912_pmic_init_data;
};

/**
 * struct tps65912 - tps65912 sub-driver chip access routines
 */

struct tps65912 {
	struct device *dev;
	/* for read/write acces */
	struct mutex io_mutex;

	/* For device IO interfaces: I2C or SPI */
	void *control_data;

	int (*read)(struct tps65912 *tps65912, u8 reg, int size, void *dest);
	int (*write)(struct tps65912 *tps65912, u8 reg, int size, void *src);

	/* Client devices */
	struct tps65912_pmic *pmic;

	/* GPIO Handling */
	struct gpio_chip gpio;

	/* IRQ Handling */
	struct mutex irq_lock;
	int chip_irq;
	int irq_base;
	int irq_num;
	u32 irq_mask;
};

struct tps65912_platform_data {
	int irq;
	int irq_base;
};

unsigned int tps_chip(void);

int tps65912_set_bits(struct tps65912 *tps65912, u8 reg, u8 mask);
int tps65912_clear_bits(struct tps65912 *tps65912, u8 reg, u8 mask);
int tps65912_reg_read(struct tps65912 *tps65912, u8 reg);
int tps65912_reg_write(struct tps65912 *tps65912, u8 reg, u8 val);
int tps65912_device_init(struct tps65912 *tps65912);
void tps65912_device_exit(struct tps65912 *tps65912);
int tps65912_irq_init(struct tps65912 *tps65912, int irq,
			struct tps65912_platform_data *pdata);

#endif /*  __LINUX_MFD_TPS65912_H */
