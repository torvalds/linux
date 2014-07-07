/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 */

#ifndef __LINUX_MFD_STMPE_H
#define __LINUX_MFD_STMPE_H

#include <linux/mutex.h>

struct device;
struct regulator;

enum stmpe_block {
	STMPE_BLOCK_GPIO	= 1 << 0,
	STMPE_BLOCK_KEYPAD	= 1 << 1,
	STMPE_BLOCK_TOUCHSCREEN	= 1 << 2,
	STMPE_BLOCK_ADC		= 1 << 3,
	STMPE_BLOCK_PWM		= 1 << 4,
	STMPE_BLOCK_ROTATOR	= 1 << 5,
};

enum stmpe_partnum {
	STMPE610,
	STMPE801,
	STMPE811,
	STMPE1601,
	STMPE1801,
	STMPE2401,
	STMPE2403,
	STMPE_NBR_PARTS
};

/*
 * For registers whose locations differ on variants,  the correct address is
 * obtained by indexing stmpe->regs with one of the following.
 */
enum {
	STMPE_IDX_CHIP_ID,
	STMPE_IDX_ICR_LSB,
	STMPE_IDX_IER_LSB,
	STMPE_IDX_ISR_LSB,
	STMPE_IDX_ISR_MSB,
	STMPE_IDX_GPMR_LSB,
	STMPE_IDX_GPSR_LSB,
	STMPE_IDX_GPCR_LSB,
	STMPE_IDX_GPDR_LSB,
	STMPE_IDX_GPEDR_MSB,
	STMPE_IDX_GPRER_LSB,
	STMPE_IDX_GPFER_LSB,
	STMPE_IDX_GPAFR_U_MSB,
	STMPE_IDX_IEGPIOR_LSB,
	STMPE_IDX_ISGPIOR_LSB,
	STMPE_IDX_ISGPIOR_MSB,
	STMPE_IDX_MAX,
};


struct stmpe_variant_info;
struct stmpe_client_info;

/**
 * struct stmpe - STMPE MFD structure
 * @vcc: optional VCC regulator
 * @vio: optional VIO regulator
 * @lock: lock protecting I/O operations
 * @irq_lock: IRQ bus lock
 * @dev: device, mostly for dev_dbg()
 * @irq_domain: IRQ domain
 * @client: client - i2c or spi
 * @ci: client specific information
 * @partnum: part number
 * @variant: the detected STMPE model number
 * @regs: list of addresses of registers which are at different addresses on
 *	  different variants.  Indexed by one of STMPE_IDX_*.
 * @irq: irq number for stmpe
 * @num_gpios: number of gpios, differs for variants
 * @ier: cache of IER registers for bus_lock
 * @oldier: cache of IER registers for bus_lock
 * @pdata: platform data
 */
struct stmpe {
	struct regulator *vcc;
	struct regulator *vio;
	struct mutex lock;
	struct mutex irq_lock;
	struct device *dev;
	struct irq_domain *domain;
	void *client;
	struct stmpe_client_info *ci;
	enum stmpe_partnum partnum;
	struct stmpe_variant_info *variant;
	const u8 *regs;

	int irq;
	int num_gpios;
	u8 ier[2];
	u8 oldier[2];
	struct stmpe_platform_data *pdata;
};

extern int stmpe_reg_write(struct stmpe *stmpe, u8 reg, u8 data);
extern int stmpe_reg_read(struct stmpe *stmpe, u8 reg);
extern int stmpe_block_read(struct stmpe *stmpe, u8 reg, u8 length,
			    u8 *values);
extern int stmpe_block_write(struct stmpe *stmpe, u8 reg, u8 length,
			     const u8 *values);
extern int stmpe_set_bits(struct stmpe *stmpe, u8 reg, u8 mask, u8 val);
extern int stmpe_set_altfunc(struct stmpe *stmpe, u32 pins,
			     enum stmpe_block block);
extern int stmpe_enable(struct stmpe *stmpe, unsigned int blocks);
extern int stmpe_disable(struct stmpe *stmpe, unsigned int blocks);

struct matrix_keymap_data;

/**
 * struct stmpe_keypad_platform_data - STMPE keypad platform data
 * @keymap_data: key map table and size
 * @debounce_ms: debounce interval, in ms.  Maximum is
 *		 %STMPE_KEYPAD_MAX_DEBOUNCE.
 * @scan_count: number of key scanning cycles to confirm key data.
 *		Maximum is %STMPE_KEYPAD_MAX_SCAN_COUNT.
 * @no_autorepeat: disable key autorepeat
 */
struct stmpe_keypad_platform_data {
	const struct matrix_keymap_data *keymap_data;
	unsigned int debounce_ms;
	unsigned int scan_count;
	bool no_autorepeat;
};

#define STMPE_GPIO_NOREQ_811_TOUCH	(0xf0)

/**
 * struct stmpe_gpio_platform_data - STMPE GPIO platform data
 * @norequest_mask: bitmask specifying which GPIOs should _not_ be
 *		    requestable due to different usage (e.g. touch, keypad)
 *		    STMPE_GPIO_NOREQ_* macros can be used here.
 * @setup: board specific setup callback.
 * @remove: board specific remove callback
 */
struct stmpe_gpio_platform_data {
	unsigned norequest_mask;
	void (*setup)(struct stmpe *stmpe, unsigned gpio_base);
	void (*remove)(struct stmpe *stmpe, unsigned gpio_base);
};

/**
 * struct stmpe_ts_platform_data - stmpe811 touch screen controller platform
 * data
 * @sample_time: ADC converstion time in number of clock.
 * (0 -> 36 clocks, 1 -> 44 clocks, 2 -> 56 clocks, 3 -> 64 clocks,
 * 4 -> 80 clocks, 5 -> 96 clocks, 6 -> 144 clocks),
 * recommended is 4.
 * @mod_12b: ADC Bit mode (0 -> 10bit ADC, 1 -> 12bit ADC)
 * @ref_sel: ADC reference source
 * (0 -> internal reference, 1 -> external reference)
 * @adc_freq: ADC Clock speed
 * (0 -> 1.625 MHz, 1 -> 3.25 MHz, 2 || 3 -> 6.5 MHz)
 * @ave_ctrl: Sample average control
 * (0 -> 1 sample, 1 -> 2 samples, 2 -> 4 samples, 3 -> 8 samples)
 * @touch_det_delay: Touch detect interrupt delay
 * (0 -> 10 us, 1 -> 50 us, 2 -> 100 us, 3 -> 500 us,
 * 4-> 1 ms, 5 -> 5 ms, 6 -> 10 ms, 7 -> 50 ms)
 * recommended is 3
 * @settling: Panel driver settling time
 * (0 -> 10 us, 1 -> 100 us, 2 -> 500 us, 3 -> 1 ms,
 * 4 -> 5 ms, 5 -> 10 ms, 6 for 50 ms, 7 -> 100 ms)
 * recommended is 2
 * @fraction_z: Length of the fractional part in z
 * (fraction_z ([0..7]) = Count of the fractional part)
 * recommended is 7
 * @i_drive: current limit value of the touchscreen drivers
 * (0 -> 20 mA typical 35 mA max, 1 -> 50 mA typical 80 mA max)
 *
 * */
struct stmpe_ts_platform_data {
       u8 sample_time;
       u8 mod_12b;
       u8 ref_sel;
       u8 adc_freq;
       u8 ave_ctrl;
       u8 touch_det_delay;
       u8 settling;
       u8 fraction_z;
       u8 i_drive;
};

/**
 * struct stmpe_platform_data - STMPE platform data
 * @id: device id to distinguish between multiple STMPEs on the same board
 * @blocks: bitmask of blocks to enable (use STMPE_BLOCK_*)
 * @irq_trigger: IRQ trigger to use for the interrupt to the host
 * @autosleep: bool to enable/disable stmpe autosleep
 * @autosleep_timeout: inactivity timeout in milliseconds for autosleep
 * @irq_over_gpio: true if gpio is used to get irq
 * @irq_gpio: gpio number over which irq will be requested (significant only if
 *	      irq_over_gpio is true)
 * @gpio: GPIO-specific platform data
 * @keypad: keypad-specific platform data
 * @ts: touchscreen-specific platform data
 */
struct stmpe_platform_data {
	int id;
	unsigned int blocks;
	unsigned int irq_trigger;
	bool autosleep;
	bool irq_over_gpio;
	int irq_gpio;
	int autosleep_timeout;

	struct stmpe_gpio_platform_data *gpio;
	struct stmpe_keypad_platform_data *keypad;
	struct stmpe_ts_platform_data *ts;
};

#endif
