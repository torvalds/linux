/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * AB3100 core access functions
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#include <linux/regulator/machine.h>

struct device;

#ifndef MFD_AB3100_H
#define MFD_AB3100_H


#define AB3100_P1A	0xc0
#define AB3100_P1B	0xc1
#define AB3100_P1C	0xc2
#define AB3100_P1D	0xc3
#define AB3100_P1E	0xc4
#define AB3100_P1F	0xc5
#define AB3100_P1G	0xc6
#define AB3100_R2A	0xc7
#define AB3100_R2B	0xc8

/*
 * AB3100, EVENTA1, A2 and A3 event register flags
 * these are catenated into a single 32-bit flag in the code
 * for event notification broadcasts.
 */
#define AB3100_EVENTA1_ONSWA				(0x01<<16)
#define AB3100_EVENTA1_ONSWB				(0x02<<16)
#define AB3100_EVENTA1_ONSWC				(0x04<<16)
#define AB3100_EVENTA1_DCIO				(0x08<<16)
#define AB3100_EVENTA1_OVER_TEMP			(0x10<<16)
#define AB3100_EVENTA1_SIM_OFF				(0x20<<16)
#define AB3100_EVENTA1_VBUS				(0x40<<16)
#define AB3100_EVENTA1_VSET_USB				(0x80<<16)

#define AB3100_EVENTA2_READY_TX				(0x01<<8)
#define AB3100_EVENTA2_READY_RX				(0x02<<8)
#define AB3100_EVENTA2_OVERRUN_ERROR			(0x04<<8)
#define AB3100_EVENTA2_FRAMING_ERROR			(0x08<<8)
#define AB3100_EVENTA2_CHARG_OVERCURRENT		(0x10<<8)
#define AB3100_EVENTA2_MIDR				(0x20<<8)
#define AB3100_EVENTA2_BATTERY_REM			(0x40<<8)
#define AB3100_EVENTA2_ALARM				(0x80<<8)

#define AB3100_EVENTA3_ADC_TRIG5			(0x01)
#define AB3100_EVENTA3_ADC_TRIG4			(0x02)
#define AB3100_EVENTA3_ADC_TRIG3			(0x04)
#define AB3100_EVENTA3_ADC_TRIG2			(0x08)
#define AB3100_EVENTA3_ADC_TRIGVBAT			(0x10)
#define AB3100_EVENTA3_ADC_TRIGVTX			(0x20)
#define AB3100_EVENTA3_ADC_TRIG1			(0x40)
#define AB3100_EVENTA3_ADC_TRIG0			(0x80)

/* AB3100, STR register flags */
#define AB3100_STR_ONSWA				(0x01)
#define AB3100_STR_ONSWB				(0x02)
#define AB3100_STR_ONSWC				(0x04)
#define AB3100_STR_DCIO					(0x08)
#define AB3100_STR_BOOT_MODE				(0x10)
#define AB3100_STR_SIM_OFF				(0x20)
#define AB3100_STR_BATT_REMOVAL				(0x40)
#define AB3100_STR_VBUS					(0x80)

/*
 * AB3100 contains 8 regulators, one external regulator controller
 * and a buck converter, further the LDO E and buck converter can
 * have separate settings if they are in sleep mode, this is
 * modeled as a separate regulator.
 */
#define AB3100_NUM_REGULATORS				10

/**
 * struct ab3100
 * @access_mutex: lock out concurrent accesses to the AB3100 registers
 * @dev: pointer to the containing device
 * @i2c_client: I2C client for this chip
 * @testreg_client: secondary client for test registers
 * @chip_name: name of this chip variant
 * @chip_id: 8 bit chip ID for this chip variant
 * @event_subscribers: event subscribers are listed here
 * @startup_events: a copy of the first reading of the event registers
 * @startup_events_read: whether the first events have been read
 *
 * This struct is PRIVATE and devices using it should NOT
 * access ANY fields. It is used as a token for calling the
 * AB3100 functions.
 */
struct ab3100 {
	struct mutex access_mutex;
	struct device *dev;
	struct i2c_client *i2c_client;
	struct i2c_client *testreg_client;
	char chip_name[32];
	u8 chip_id;
	struct blocking_notifier_head event_subscribers;
	u8 startup_events[3];
	bool startup_events_read;
};

/**
 * struct ab3100_platform_data
 * Data supplied to initialize board connections to the AB3100
 * @reg_constraints: regulator constraints for target board
 *     the order of these constraints are: LDO A, C, D, E,
 *     F, G, H, K, EXT and BUCK.
 * @reg_initvals: initial values for the regulator registers
 *     plus two sleep settings for LDO E and the BUCK converter.
 *     exactly AB3100_NUM_REGULATORS+2 values must be sent in.
 *     Order: LDO A, C, E, E sleep, F, G, H, K, EXT, BUCK,
 *     BUCK sleep, LDO D. (LDO D need to be initialized last.)
 * @external_voltage: voltage level of the external regulator.
 */
struct ab3100_platform_data {
	struct regulator_init_data reg_constraints[AB3100_NUM_REGULATORS];
	u8 reg_initvals[AB3100_NUM_REGULATORS+2];
	int external_voltage;
};

int ab3100_event_register(struct ab3100 *ab3100,
			  struct notifier_block *nb);
int ab3100_event_unregister(struct ab3100 *ab3100,
			    struct notifier_block *nb);

#endif /*  MFD_AB3100_H */
