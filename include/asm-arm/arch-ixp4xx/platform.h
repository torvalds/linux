/*
 * include/asm-arm/arch-ixp4xx/platform.h
 *
 * Constants and functions that are useful to IXP4xx platform-specific code
 * and device drivers.
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif

#ifndef __ASSEMBLY__

#include <asm/types.h>

#ifndef	__ARMEB__
#define	REG_OFFSET	0
#else
#define	REG_OFFSET	3
#endif

/*
 * Expansion bus memory regions
 */
#define IXP4XX_EXP_BUS_BASE_PHYS	(0x50000000)

/*
 * The expansion bus on the IXP4xx can be configured for either 16 or
 * 32MB windows and the CS offset for each region changes based on the
 * current configuration. This means that we cannot simply hardcode
 * each offset. ixp4xx_sys_init() looks at the expansion bus configuration
 * as setup by the bootloader to determine our window size.
 */
extern unsigned long ixp4xx_exp_bus_size;

#define	IXP4XX_EXP_BUS_BASE(region)\
		(IXP4XX_EXP_BUS_BASE_PHYS + ((region) * ixp4xx_exp_bus_size))

#define IXP4XX_FLASH_WRITABLE	(0x2)
#define IXP4XX_FLASH_DEFAULT	(0xbcd23c40)
#define IXP4XX_FLASH_WRITE	(0xbcd23c42)

/*
 * Clock Speed Definitions.
 */
#define IXP4XX_PERIPHERAL_BUS_CLOCK 	(66) /* 66Mhzi APB BUS   */ 
#define IXP4XX_UART_XTAL        	14745600

/*
 * The IXP4xx chips do not have an I2C unit, so GPIO lines are just
 * used to 
 * Used as platform_data to provide GPIO pin information to the ixp42x
 * I2C driver.
 */
struct ixp4xx_i2c_pins {
	unsigned long sda_pin;
	unsigned long scl_pin;
};


struct sys_timer;

/*
 * Functions used by platform-level setup code
 */
extern void ixp4xx_map_io(void);
extern void ixp4xx_init_irq(void);
extern void ixp4xx_sys_init(void);
extern struct sys_timer ixp4xx_timer;
extern void ixp4xx_pci_preinit(void);
struct pci_sys_data;
extern int ixp4xx_setup(int nr, struct pci_sys_data *sys);
extern struct pci_bus *ixp4xx_scan_bus(int nr, struct pci_sys_data *sys);

/*
 * GPIO-functions
 */
/*
 * The following converted to the real HW bits the gpio_line_config
 */
/* GPIO pin types */
#define IXP4XX_GPIO_OUT 		0x1
#define IXP4XX_GPIO_IN  		0x2

/* GPIO signal types */
#define IXP4XX_GPIO_LOW			0
#define IXP4XX_GPIO_HIGH		1

/* GPIO Clocks */
#define IXP4XX_GPIO_CLK_0		14
#define IXP4XX_GPIO_CLK_1		15

static inline void gpio_line_config(u8 line, u32 direction)
{
	if (direction == IXP4XX_GPIO_IN)
		*IXP4XX_GPIO_GPOER |= (1 << line);
	else
		*IXP4XX_GPIO_GPOER &= ~(1 << line);
}

static inline void gpio_line_get(u8 line, int *value)
{
	*value = (*IXP4XX_GPIO_GPINR >> line) & 0x1;
}

static inline void gpio_line_set(u8 line, int value)
{
	if (value == IXP4XX_GPIO_HIGH)
	    *IXP4XX_GPIO_GPOUTR |= (1 << line);
	else if (value == IXP4XX_GPIO_LOW)
	    *IXP4XX_GPIO_GPOUTR &= ~(1 << line);
}

#endif // __ASSEMBLY__

