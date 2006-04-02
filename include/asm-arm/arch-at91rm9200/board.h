/*
 * include/asm-arm/arch-at91rm9200/board.h
 *
 *  Copyright (C) 2005 HP Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * These are data structures found in platform_device.dev.platform_data,
 * and describing board-specfic data needed by drivers.  For example,
 * which pin is used for a given GPIO role.
 *
 * In 2.6, drivers should strongly avoid board-specific knowledge so
 * that supporting new boards normally won't require driver patches.
 * Most board-specific knowledge should be in arch/.../board-*.c files.
 */

#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

 /* Clocks */
extern unsigned long at91_master_clock;

 /* Serial Port */
extern int at91_serial_map[AT91_NR_UART];
extern int at91_console_port;

#include <linux/mtd/partitions.h>

 /* USB Device */
struct at91_udc_data {
	u8	vbus_pin;		/* high == host powering us */
	u8	pullup_pin;		/* high == D+ pulled up */
};
extern void __init at91_add_device_udc(struct at91_udc_data *data);

 /* Compact Flash */
struct at91_cf_data {
	u8	irq_pin;		/* I/O IRQ */
	u8	det_pin;		/* Card detect */
	u8	vcc_pin;		/* power switching */
	u8	rst_pin;		/* card reset */
};
extern void __init at91_add_device_cf(struct at91_cf_data *data);

 /* MMC / SD */
struct at91_mmc_data {
	u8		det_pin;	/* card detect IRQ */
	unsigned	is_b:1;		/* uses B side (vs A) */
	unsigned	wire4:1;	/* (SD) supports DAT0..DAT3 */
	u8		wp_pin;		/* (SD) writeprotect detect */
	u8		vcc_pin;	/* power switching (high == on) */
};
extern void __init at91_add_device_mmc(struct at91_mmc_data *data);

 /* Ethernet */
struct at91_eth_data {
	u8		phy_irq_pin;	/* PHY IRQ */
	u8		is_rmii;	/* using RMII interface? */
};
extern void __init at91_add_device_eth(struct at91_eth_data *data);

 /* USB Host */
struct at91_usbh_data {
	u8		ports;		/* number of ports on root hub */
};
extern void __init at91_add_device_usbh(struct at91_usbh_data *data);

 /* NAND / SmartMedia */
struct at91_nand_data {
	u8		enable_pin;	/* chip enable */
	u8		det_pin;	/* card detect */
	u8		rdy_pin;	/* ready/busy */
	u8		ale;		/* address line number connected to ALE */
	u8		cle;		/* address line number connected to CLE */
        struct mtd_partition* (*partition_info)(int, int*);
};
extern void __init at91_add_device_nand(struct at91_nand_data *data);

 /* I2C*/
void __init at91_add_device_i2c(void);

 /* RTC */
void __init at91_add_device_rtc(void);

 /* LEDs */
extern u8 at91_leds_cpu;
extern u8 at91_leds_timer;
extern void __init at91_init_leds(u8 cpu_led, u8 timer_led);

#endif
