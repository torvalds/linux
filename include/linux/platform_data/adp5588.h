/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Analog Devices ADP5588 I/O Expander and QWERTY Keypad Controller
 *
 * Copyright 2009-2010 Analog Devices Inc.
 */

#ifndef _ADP5588_H
#define _ADP5588_H

#define DEV_ID 0x00		/* Device ID */
#define CFG 0x01		/* Configuration Register1 */
#define INT_STAT 0x02		/* Interrupt Status Register */
#define KEY_LCK_EC_STAT 0x03	/* Key Lock and Event Counter Register */
#define Key_EVENTA 0x04		/* Key Event Register A */
#define Key_EVENTB 0x05		/* Key Event Register B */
#define Key_EVENTC 0x06		/* Key Event Register C */
#define Key_EVENTD 0x07		/* Key Event Register D */
#define Key_EVENTE 0x08		/* Key Event Register E */
#define Key_EVENTF 0x09		/* Key Event Register F */
#define Key_EVENTG 0x0A		/* Key Event Register G */
#define Key_EVENTH 0x0B		/* Key Event Register H */
#define Key_EVENTI 0x0C		/* Key Event Register I */
#define Key_EVENTJ 0x0D		/* Key Event Register J */
#define KP_LCK_TMR 0x0E		/* Keypad Lock1 to Lock2 Timer */
#define UNLOCK1 0x0F		/* Unlock Key1 */
#define UNLOCK2 0x10		/* Unlock Key2 */
#define GPIO_INT_STAT1 0x11	/* GPIO Interrupt Status */
#define GPIO_INT_STAT2 0x12	/* GPIO Interrupt Status */
#define GPIO_INT_STAT3 0x13	/* GPIO Interrupt Status */
#define GPIO_DAT_STAT1 0x14	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_STAT2 0x15	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_STAT3 0x16	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_OUT1 0x17	/* GPIO DATA OUT */
#define GPIO_DAT_OUT2 0x18	/* GPIO DATA OUT */
#define GPIO_DAT_OUT3 0x19	/* GPIO DATA OUT */
#define GPIO_INT_EN1 0x1A	/* GPIO Interrupt Enable */
#define GPIO_INT_EN2 0x1B	/* GPIO Interrupt Enable */
#define GPIO_INT_EN3 0x1C	/* GPIO Interrupt Enable */
#define KP_GPIO1 0x1D		/* Keypad or GPIO Selection */
#define KP_GPIO2 0x1E		/* Keypad or GPIO Selection */
#define KP_GPIO3 0x1F		/* Keypad or GPIO Selection */
#define GPI_EM1 0x20		/* GPI Event Mode 1 */
#define GPI_EM2 0x21		/* GPI Event Mode 2 */
#define GPI_EM3 0x22		/* GPI Event Mode 3 */
#define GPIO_DIR1 0x23		/* GPIO Data Direction */
#define GPIO_DIR2 0x24		/* GPIO Data Direction */
#define GPIO_DIR3 0x25		/* GPIO Data Direction */
#define GPIO_INT_LVL1 0x26	/* GPIO Edge/Level Detect */
#define GPIO_INT_LVL2 0x27	/* GPIO Edge/Level Detect */
#define GPIO_INT_LVL3 0x28	/* GPIO Edge/Level Detect */
#define Debounce_DIS1 0x29	/* Debounce Disable */
#define Debounce_DIS2 0x2A	/* Debounce Disable */
#define Debounce_DIS3 0x2B	/* Debounce Disable */
#define GPIO_PULL1 0x2C		/* GPIO Pull Disable */
#define GPIO_PULL2 0x2D		/* GPIO Pull Disable */
#define GPIO_PULL3 0x2E		/* GPIO Pull Disable */
#define CMP_CFG_STAT 0x30	/* Comparator Configuration and Status Register */
#define CMP_CONFG_SENS1 0x31	/* Sensor1 Comparator Configuration Register */
#define CMP_CONFG_SENS2 0x32	/* L2 Light Sensor Reference Level, Output Falling for Sensor 1 */
#define CMP1_LVL2_TRIP 0x33	/* L2 Light Sensor Hysteresis (Active when Output Rising) for Sensor 1 */
#define CMP1_LVL2_HYS 0x34	/* L3 Light Sensor Reference Level, Output Falling For Sensor 1 */
#define CMP1_LVL3_TRIP 0x35	/* L3 Light Sensor Hysteresis (Active when Output Rising) For Sensor 1 */
#define CMP1_LVL3_HYS 0x36	/* Sensor 2 Comparator Configuration Register */
#define CMP2_LVL2_TRIP 0x37	/* L2 Light Sensor Reference Level, Output Falling for Sensor 2 */
#define CMP2_LVL2_HYS 0x38	/* L2 Light Sensor Hysteresis (Active when Output Rising) for Sensor 2 */
#define CMP2_LVL3_TRIP 0x39	/* L3 Light Sensor Reference Level, Output Falling For Sensor 2 */
#define CMP2_LVL3_HYS 0x3A	/* L3 Light Sensor Hysteresis (Active when Output Rising) For Sensor 2 */
#define CMP1_ADC_DAT_R1 0x3B	/* Comparator 1 ADC data Register1 */
#define CMP1_ADC_DAT_R2 0x3C	/* Comparator 1 ADC data Register2 */
#define CMP2_ADC_DAT_R1 0x3D	/* Comparator 2 ADC data Register1 */
#define CMP2_ADC_DAT_R2 0x3E	/* Comparator 2 ADC data Register2 */

#define ADP5588_DEVICE_ID_MASK	0xF

 /* Configuration Register1 */
#define ADP5588_AUTO_INC	(1 << 7)
#define ADP5588_GPIEM_CFG	(1 << 6)
#define ADP5588_OVR_FLOW_M	(1 << 5)
#define ADP5588_INT_CFG		(1 << 4)
#define ADP5588_OVR_FLOW_IEN	(1 << 3)
#define ADP5588_K_LCK_IM	(1 << 2)
#define ADP5588_GPI_IEN		(1 << 1)
#define ADP5588_KE_IEN		(1 << 0)

/* Interrupt Status Register */
#define ADP5588_CMP2_INT	(1 << 5)
#define ADP5588_CMP1_INT	(1 << 4)
#define ADP5588_OVR_FLOW_INT	(1 << 3)
#define ADP5588_K_LCK_INT	(1 << 2)
#define ADP5588_GPI_INT		(1 << 1)
#define ADP5588_KE_INT		(1 << 0)

/* Key Lock and Event Counter Register */
#define ADP5588_K_LCK_EN	(1 << 6)
#define ADP5588_LCK21		0x30
#define ADP5588_KEC		0xF

#define ADP5588_MAXGPIO		18
#define ADP5588_BANK(offs)	((offs) >> 3)
#define ADP5588_BIT(offs)	(1u << ((offs) & 0x7))

/* Put one of these structures in i2c_board_info platform_data */

#define ADP5588_KEYMAPSIZE	80

#define GPI_PIN_ROW0 97
#define GPI_PIN_ROW1 98
#define GPI_PIN_ROW2 99
#define GPI_PIN_ROW3 100
#define GPI_PIN_ROW4 101
#define GPI_PIN_ROW5 102
#define GPI_PIN_ROW6 103
#define GPI_PIN_ROW7 104
#define GPI_PIN_COL0 105
#define GPI_PIN_COL1 106
#define GPI_PIN_COL2 107
#define GPI_PIN_COL3 108
#define GPI_PIN_COL4 109
#define GPI_PIN_COL5 110
#define GPI_PIN_COL6 111
#define GPI_PIN_COL7 112
#define GPI_PIN_COL8 113
#define GPI_PIN_COL9 114

#define GPI_PIN_ROW_BASE GPI_PIN_ROW0
#define GPI_PIN_ROW_END GPI_PIN_ROW7
#define GPI_PIN_COL_BASE GPI_PIN_COL0
#define GPI_PIN_COL_END GPI_PIN_COL9

#define GPI_PIN_BASE GPI_PIN_ROW_BASE
#define GPI_PIN_END GPI_PIN_COL_END

#define ADP5588_GPIMAPSIZE_MAX (GPI_PIN_END - GPI_PIN_BASE + 1)

struct adp5588_gpi_map {
	unsigned short pin;
	unsigned short sw_evt;
};

struct adp5588_kpad_platform_data {
	int rows;			/* Number of rows */
	int cols;			/* Number of columns */
	const unsigned short *keymap;	/* Pointer to keymap */
	unsigned short keymapsize;	/* Keymap size */
	unsigned repeat:1;		/* Enable key repeat */
	unsigned en_keylock:1;		/* Enable Key Lock feature */
	unsigned short unlock_key1;	/* Unlock Key 1 */
	unsigned short unlock_key2;	/* Unlock Key 2 */
	const struct adp5588_gpi_map *gpimap;
	unsigned short gpimapsize;
	const struct adp5588_gpio_platform_data *gpio_data;
};

struct i2c_client; /* forward declaration */

struct adp5588_gpio_platform_data {
	int gpio_start;		/* GPIO Chip base # */
	const char *const *names;
	unsigned irq_base;	/* interrupt base # */
	unsigned pullup_dis_mask; /* Pull-Up Disable Mask */
	int	(*setup)(struct i2c_client *client,
				unsigned gpio, unsigned ngpio,
				void *context);
	int	(*teardown)(struct i2c_client *client,
				unsigned gpio, unsigned ngpio,
				void *context);
	void	*context;
};

#endif
