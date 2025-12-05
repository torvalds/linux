/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel INT3472 ACPI camera sensor power-management support
 *
 * Author: Dan Scally <djrscally@gmail.com>
 */

#ifndef __PLATFORM_DATA_X86_INT3472_H
#define __PLATFORM_DATA_X86_INT3472_H

#include <linux/clk-provider.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/types.h>

/* FIXME drop this once the I2C_DEV_NAME_FORMAT macro has been added to include/linux/i2c.h */
#ifndef I2C_DEV_NAME_FORMAT
#define I2C_DEV_NAME_FORMAT					"i2c-%s"
#endif

/* PMIC GPIO Types */
#define INT3472_GPIO_TYPE_RESET					0x00
#define INT3472_GPIO_TYPE_POWERDOWN				0x01
#define INT3472_GPIO_TYPE_POWER_ENABLE				0x0b
#define INT3472_GPIO_TYPE_CLK_ENABLE				0x0c
#define INT3472_GPIO_TYPE_PRIVACY_LED				0x0d
#define INT3472_GPIO_TYPE_HANDSHAKE				0x12
#define INT3472_GPIO_TYPE_HOTPLUG_DETECT			0x13

#define INT3472_PDEV_MAX_NAME_LEN				23
#define INT3472_MAX_SENSOR_GPIOS				3
#define INT3472_MAX_REGULATORS					3

/* E.g. "avdd\0" */
#define GPIO_SUPPLY_NAME_LENGTH				5
/* 12 chars for acpi_dev_name() + "-", e.g. "ABCD1234:00-" */
#define GPIO_REGULATOR_NAME_LENGTH				(12 + GPIO_SUPPLY_NAME_LENGTH)
/* lower- and upper-case mapping */
#define GPIO_REGULATOR_SUPPLY_MAP_COUNT				2
/*
 * Ensure the GPIO is driven low/high for at least 2 ms before changing.
 *
 * 2 ms has been chosen because it is the minimum time ovXXXX sensors need to
 * have their reset line driven logical high to properly register a reset.
 */
#define GPIO_REGULATOR_ENABLE_TIME				(2 * USEC_PER_MSEC)
#define GPIO_REGULATOR_OFF_ON_DELAY				(2 * USEC_PER_MSEC)

#define INT3472_LED_MAX_NAME_LEN				32

#define CIO2_SENSOR_SSDB_MCLKSPEED_OFFSET			86

#define INT3472_REGULATOR(_name, _ops, _enable_time, _off_on_delay) \
	(const struct regulator_desc) {				\
		.name = _name,					\
		.type = REGULATOR_VOLTAGE,			\
		.ops = _ops,					\
		.owner = THIS_MODULE,				\
		.enable_time = _enable_time,			\
		.off_on_delay = _off_on_delay,			\
	}

#define to_int3472_clk(hw)					\
	container_of(hw, struct int3472_clock, clk_hw)

#define to_int3472_device(clk)					\
	container_of(clk, struct int3472_discrete_device, clock)

struct acpi_device;
struct dmi_system_id;
struct i2c_client;
struct platform_device;

struct int3472_cldb {
	u8 version;
	/*
	 * control logic type
	 * 0: UNKNOWN
	 * 1: DISCRETE(CRD-D)
	 * 2: PMIC TPS68470
	 * 3: PMIC uP6641
	 */
	u8 control_logic_type;
	u8 control_logic_id;
	u8 sensor_card_sku;
	u8 reserved[10];
	u8 clock_source;
	u8 reserved2[17];
};

struct int3472_discrete_quirks {
	/* For models where AVDD GPIO is shared between sensors */
	const char *avdd_second_sensor;
};

struct int3472_gpio_regulator {
	/* SUPPLY_MAP_COUNT * 2 to make room for second sensor mappings */
	struct regulator_consumer_supply supply_map[GPIO_REGULATOR_SUPPLY_MAP_COUNT * 2];
	char supply_name_upper[GPIO_SUPPLY_NAME_LENGTH];
	char regulator_name[GPIO_REGULATOR_NAME_LENGTH];
	struct regulator_dev *rdev;
	struct regulator_desc rdesc;
};

struct int3472_discrete_device {
	struct acpi_device *adev;
	struct device *dev;
	struct acpi_device *sensor;
	const char *sensor_name;

	struct int3472_gpio_regulator regulators[INT3472_MAX_REGULATORS];

	struct int3472_clock {
		struct clk *clk;
		struct clk_hw clk_hw;
		struct clk_lookup *cl;
		struct gpio_desc *ena_gpio;
		u32 frequency;
		u8 imgclk_index;
	} clock;

	struct int3472_pled {
		struct led_classdev classdev;
		struct led_lookup_data lookup;
		char name[INT3472_LED_MAX_NAME_LEN];
		struct gpio_desc *gpio;
	} pled;

	struct int3472_discrete_quirks quirks;

	unsigned int ngpios; /* how many GPIOs have we seen */
	unsigned int n_sensor_gpios; /* how many have we mapped to sensor */
	unsigned int n_regulator_gpios; /* how many have we mapped to a regulator */
	struct gpiod_lookup_table gpios;
};

extern const struct dmi_system_id skl_int3472_discrete_quirks[];

union acpi_object *skl_int3472_get_acpi_buffer(struct acpi_device *adev,
					       char *id);
int skl_int3472_fill_cldb(struct acpi_device *adev, struct int3472_cldb *cldb);
int skl_int3472_get_sensor_adev_and_name(struct device *dev,
					 struct acpi_device **sensor_adev_ret,
					 const char **name_ret);

int int3472_discrete_parse_crs(struct int3472_discrete_device *int3472);
void int3472_discrete_cleanup(struct int3472_discrete_device *int3472);

int skl_int3472_register_gpio_clock(struct int3472_discrete_device *int3472,
				    struct gpio_desc *gpio);
int skl_int3472_register_dsm_clock(struct int3472_discrete_device *int3472);
void skl_int3472_unregister_clock(struct int3472_discrete_device *int3472);

int skl_int3472_register_regulator(struct int3472_discrete_device *int3472,
				   struct gpio_desc *gpio,
				   unsigned int enable_time,
				   const char *supply_name,
				   const char *second_sensor);
void skl_int3472_unregister_regulator(struct int3472_discrete_device *int3472);

int skl_int3472_register_pled(struct int3472_discrete_device *int3472, struct gpio_desc *gpio);
void skl_int3472_unregister_pled(struct int3472_discrete_device *int3472);

#endif
