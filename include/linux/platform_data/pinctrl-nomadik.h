/*
 * Structures and registers for GPIO access in the Nomadik SoC
 *
 * Copyright (C) 2008 STMicroelectronics
 *     Author: Prafulla WADASKAR <prafulla.wadaskar@st.com>
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PLAT_NOMADIK_GPIO
#define __PLAT_NOMADIK_GPIO

/*
 * pin configurations are represented by 32-bit integers:
 *
 *	bit  0.. 8 - Pin Number (512 Pins Maximum)
 *	bit  9..10 - Alternate Function Selection
 *	bit 11..12 - Pull up/down state
 *	bit     13 - Sleep mode behaviour
 *	bit     14 - Direction
 *	bit     15 - Value (if output)
 *	bit 16..18 - SLPM pull up/down state
 *	bit 19..20 - SLPM direction
 *	bit 21..22 - SLPM Value (if output)
 *	bit 23..25 - PDIS value (if input)
 *	bit	26 - Gpio mode
 *	bit	27 - Sleep mode
 *
 * to facilitate the definition, the following macros are provided
 *
 * PIN_CFG_DEFAULT - default config (0):
 *		     pull up/down = disabled
 *		     sleep mode = input/wakeup
 *		     direction = input
 *		     value = low
 *		     SLPM direction = same as normal
 *		     SLPM pull = same as normal
 *		     SLPM value = same as normal
 *
 * PIN_CFG	   - default config with alternate function
 */

typedef unsigned long pin_cfg_t;

#define PIN_NUM_MASK		0x1ff
#define PIN_NUM(x)		((x) & PIN_NUM_MASK)

#define PIN_ALT_SHIFT		9
#define PIN_ALT_MASK		(0x3 << PIN_ALT_SHIFT)
#define PIN_ALT(x)		(((x) & PIN_ALT_MASK) >> PIN_ALT_SHIFT)
#define PIN_GPIO		(NMK_GPIO_ALT_GPIO << PIN_ALT_SHIFT)
#define PIN_ALT_A		(NMK_GPIO_ALT_A << PIN_ALT_SHIFT)
#define PIN_ALT_B		(NMK_GPIO_ALT_B << PIN_ALT_SHIFT)
#define PIN_ALT_C		(NMK_GPIO_ALT_C << PIN_ALT_SHIFT)

#define PIN_PULL_SHIFT		11
#define PIN_PULL_MASK		(0x3 << PIN_PULL_SHIFT)
#define PIN_PULL(x)		(((x) & PIN_PULL_MASK) >> PIN_PULL_SHIFT)
#define PIN_PULL_NONE		(NMK_GPIO_PULL_NONE << PIN_PULL_SHIFT)
#define PIN_PULL_UP		(NMK_GPIO_PULL_UP << PIN_PULL_SHIFT)
#define PIN_PULL_DOWN		(NMK_GPIO_PULL_DOWN << PIN_PULL_SHIFT)

#define PIN_SLPM_SHIFT		13
#define PIN_SLPM_MASK		(0x1 << PIN_SLPM_SHIFT)
#define PIN_SLPM(x)		(((x) & PIN_SLPM_MASK) >> PIN_SLPM_SHIFT)
#define PIN_SLPM_MAKE_INPUT	(NMK_GPIO_SLPM_INPUT << PIN_SLPM_SHIFT)
#define PIN_SLPM_NOCHANGE	(NMK_GPIO_SLPM_NOCHANGE << PIN_SLPM_SHIFT)
/* These two replace the above in DB8500v2+ */
#define PIN_SLPM_WAKEUP_ENABLE	(NMK_GPIO_SLPM_WAKEUP_ENABLE << PIN_SLPM_SHIFT)
#define PIN_SLPM_WAKEUP_DISABLE	(NMK_GPIO_SLPM_WAKEUP_DISABLE << PIN_SLPM_SHIFT)
#define PIN_SLPM_USE_MUX_SETTINGS_IN_SLEEP PIN_SLPM_WAKEUP_DISABLE

#define PIN_SLPM_GPIO  PIN_SLPM_WAKEUP_ENABLE /* In SLPM, pin is a gpio */
#define PIN_SLPM_ALTFUNC PIN_SLPM_WAKEUP_DISABLE /* In SLPM, pin is altfunc */

#define PIN_DIR_SHIFT		14
#define PIN_DIR_MASK		(0x1 << PIN_DIR_SHIFT)
#define PIN_DIR(x)		(((x) & PIN_DIR_MASK) >> PIN_DIR_SHIFT)
#define PIN_DIR_INPUT		(0 << PIN_DIR_SHIFT)
#define PIN_DIR_OUTPUT		(1 << PIN_DIR_SHIFT)

#define PIN_VAL_SHIFT		15
#define PIN_VAL_MASK		(0x1 << PIN_VAL_SHIFT)
#define PIN_VAL(x)		(((x) & PIN_VAL_MASK) >> PIN_VAL_SHIFT)
#define PIN_VAL_LOW		(0 << PIN_VAL_SHIFT)
#define PIN_VAL_HIGH		(1 << PIN_VAL_SHIFT)

#define PIN_SLPM_PULL_SHIFT	16
#define PIN_SLPM_PULL_MASK	(0x7 << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL(x)	\
	(((x) & PIN_SLPM_PULL_MASK) >> PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_NONE	\
	((1 + NMK_GPIO_PULL_NONE) << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_UP	\
	((1 + NMK_GPIO_PULL_UP) << PIN_SLPM_PULL_SHIFT)
#define PIN_SLPM_PULL_DOWN	\
	((1 + NMK_GPIO_PULL_DOWN) << PIN_SLPM_PULL_SHIFT)

#define PIN_SLPM_DIR_SHIFT	19
#define PIN_SLPM_DIR_MASK	(0x3 << PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR(x)		\
	(((x) & PIN_SLPM_DIR_MASK) >> PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR_INPUT	((1 + 0) << PIN_SLPM_DIR_SHIFT)
#define PIN_SLPM_DIR_OUTPUT	((1 + 1) << PIN_SLPM_DIR_SHIFT)

#define PIN_SLPM_VAL_SHIFT	21
#define PIN_SLPM_VAL_MASK	(0x3 << PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL(x)		\
	(((x) & PIN_SLPM_VAL_MASK) >> PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL_LOW	((1 + 0) << PIN_SLPM_VAL_SHIFT)
#define PIN_SLPM_VAL_HIGH	((1 + 1) << PIN_SLPM_VAL_SHIFT)

#define PIN_SLPM_PDIS_SHIFT		23
#define PIN_SLPM_PDIS_MASK		(0x3 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS(x)	\
	(((x) & PIN_SLPM_PDIS_MASK) >> PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_NO_CHANGE		(0 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_DISABLED		(1 << PIN_SLPM_PDIS_SHIFT)
#define PIN_SLPM_PDIS_ENABLED		(2 << PIN_SLPM_PDIS_SHIFT)

#define PIN_LOWEMI_SHIFT	25
#define PIN_LOWEMI_MASK		(0x1 << PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI(x)		(((x) & PIN_LOWEMI_MASK) >> PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI_DISABLED	(0 << PIN_LOWEMI_SHIFT)
#define PIN_LOWEMI_ENABLED	(1 << PIN_LOWEMI_SHIFT)

#define PIN_GPIOMODE_SHIFT	26
#define PIN_GPIOMODE_MASK	(0x1 << PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE(x)		(((x) & PIN_GPIOMODE_MASK) >> PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE_DISABLED	(0 << PIN_GPIOMODE_SHIFT)
#define PIN_GPIOMODE_ENABLED	(1 << PIN_GPIOMODE_SHIFT)

#define PIN_SLEEPMODE_SHIFT	27
#define PIN_SLEEPMODE_MASK	(0x1 << PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE(x)	(((x) & PIN_SLEEPMODE_MASK) >> PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE_DISABLED	(0 << PIN_SLEEPMODE_SHIFT)
#define PIN_SLEEPMODE_ENABLED	(1 << PIN_SLEEPMODE_SHIFT)


/* Shortcuts.  Use these instead of separate DIR, PULL, and VAL.  */
#define PIN_INPUT_PULLDOWN	(PIN_DIR_INPUT | PIN_PULL_DOWN)
#define PIN_INPUT_PULLUP	(PIN_DIR_INPUT | PIN_PULL_UP)
#define PIN_INPUT_NOPULL	(PIN_DIR_INPUT | PIN_PULL_NONE)
#define PIN_OUTPUT_LOW		(PIN_DIR_OUTPUT | PIN_VAL_LOW)
#define PIN_OUTPUT_HIGH		(PIN_DIR_OUTPUT | PIN_VAL_HIGH)

#define PIN_SLPM_INPUT_PULLDOWN	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_DOWN)
#define PIN_SLPM_INPUT_PULLUP	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_UP)
#define PIN_SLPM_INPUT_NOPULL	(PIN_SLPM_DIR_INPUT | PIN_SLPM_PULL_NONE)
#define PIN_SLPM_OUTPUT_LOW	(PIN_SLPM_DIR_OUTPUT | PIN_SLPM_VAL_LOW)
#define PIN_SLPM_OUTPUT_HIGH	(PIN_SLPM_DIR_OUTPUT | PIN_SLPM_VAL_HIGH)

#define PIN_CFG_DEFAULT		(0)

#define PIN_CFG(num, alt)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt))

#define PIN_CFG_INPUT(num, alt, pull)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt | PIN_INPUT_##pull))

#define PIN_CFG_OUTPUT(num, alt, val)		\
	(PIN_CFG_DEFAULT |\
	 (PIN_NUM(num) | PIN_##alt | PIN_OUTPUT_##val))

/*
 * "nmk_gpio" and "NMK_GPIO" stand for "Nomadik GPIO", leaving
 * the "gpio" namespace for generic and cross-machine functions
 */

#define GPIO_BLOCK_SHIFT 5
#define NMK_GPIO_PER_CHIP (1 << GPIO_BLOCK_SHIFT)

/* Register in the logic block */
#define NMK_GPIO_DAT	0x00
#define NMK_GPIO_DATS	0x04
#define NMK_GPIO_DATC	0x08
#define NMK_GPIO_PDIS	0x0c
#define NMK_GPIO_DIR	0x10
#define NMK_GPIO_DIRS	0x14
#define NMK_GPIO_DIRC	0x18
#define NMK_GPIO_SLPC	0x1c
#define NMK_GPIO_AFSLA	0x20
#define NMK_GPIO_AFSLB	0x24
#define NMK_GPIO_LOWEMI	0x28

#define NMK_GPIO_RIMSC	0x40
#define NMK_GPIO_FIMSC	0x44
#define NMK_GPIO_IS	0x48
#define NMK_GPIO_IC	0x4c
#define NMK_GPIO_RWIMSC	0x50
#define NMK_GPIO_FWIMSC	0x54
#define NMK_GPIO_WKS	0x58
/* These appear in DB8540 and later ASICs */
#define NMK_GPIO_EDGELEVEL 0x5C
#define NMK_GPIO_LEVEL	0x60

/* Alternate functions: function C is set in hw by setting both A and B */
#define NMK_GPIO_ALT_GPIO	0
#define NMK_GPIO_ALT_A	1
#define NMK_GPIO_ALT_B	2
#define NMK_GPIO_ALT_C	(NMK_GPIO_ALT_A | NMK_GPIO_ALT_B)

#define NMK_GPIO_ALT_CX_SHIFT 2
#define NMK_GPIO_ALT_C1	((1<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C2	((2<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C3	((3<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C4	((4<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)

/* Pull up/down values */
enum nmk_gpio_pull {
	NMK_GPIO_PULL_NONE,
	NMK_GPIO_PULL_UP,
	NMK_GPIO_PULL_DOWN,
};

/* Sleep mode */
enum nmk_gpio_slpm {
	NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_WAKEUP_ENABLE = NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_NOCHANGE,
	NMK_GPIO_SLPM_WAKEUP_DISABLE = NMK_GPIO_SLPM_NOCHANGE,
};

/* Older deprecated pin config API that should go away soon */
extern int nmk_config_pin(pin_cfg_t cfg, bool sleep);
extern int nmk_config_pins(pin_cfg_t *cfgs, int num);
extern int nmk_config_pins_sleep(pin_cfg_t *cfgs, int num);
extern int nmk_gpio_set_slpm(int gpio, enum nmk_gpio_slpm mode);
extern int nmk_gpio_set_pull(int gpio, enum nmk_gpio_pull pull);
#ifdef CONFIG_PINCTRL_NOMADIK
extern int nmk_gpio_set_mode(int gpio, int gpio_mode);
#else
static inline int nmk_gpio_set_mode(int gpio, int gpio_mode)
{
	return -ENODEV;
}
#endif
extern int nmk_gpio_get_mode(int gpio);

extern void nmk_gpio_wakeups_suspend(void);
extern void nmk_gpio_wakeups_resume(void);

extern void nmk_gpio_clocks_enable(void);
extern void nmk_gpio_clocks_disable(void);

extern void nmk_gpio_read_pull(int gpio_bank, u32 *pull_up);

/*
 * Platform data to register a block: only the initial gpio/irq number.
 */
struct nmk_gpio_platform_data {
	char *name;
	int first_gpio;
	int first_irq;
	int num_gpio;
	u32 (*get_secondary_status)(unsigned int bank);
	void (*set_ioforce)(bool enable);
	bool supports_sleepmode;
};

#endif /* __PLAT_NOMADIK_GPIO */
