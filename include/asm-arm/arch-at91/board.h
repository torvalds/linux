/*
 * include/asm-arm/arch-at91/board.h
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
 * and describing board-specific data needed by drivers.  For example,
 * which pin is used for a given GPIO role.
 *
 * In 2.6, drivers should strongly avoid board-specific knowledge so
 * that supporting new boards normally won't require driver patches.
 * Most board-specific knowledge should be in arch/.../board-*.c files.
 */

#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

#include <linux/mtd/partitions.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>

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
	u8	chipselect;		/* EBI Chip Select number */
};
extern void __init at91_add_device_cf(struct at91_cf_data *data);

 /* MMC / SD */
struct at91_mmc_data {
	u8		det_pin;	/* card detect IRQ */
	unsigned	slot_b:1;	/* uses Slot B */
	unsigned	wire4:1;	/* (SD) supports DAT0..DAT3 */
	u8		wp_pin;		/* (SD) writeprotect detect */
	u8		vcc_pin;	/* power switching (high == on) */
};
extern void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data);

 /* Ethernet (EMAC & MACB) */
struct at91_eth_data {
	u32		phy_mask;
	u8		phy_irq_pin;	/* PHY IRQ */
	u8		is_rmii;	/* using RMII interface? */
};
extern void __init at91_add_device_eth(struct at91_eth_data *data);

#if defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9263)
#define eth_platform_data	at91_eth_data
#endif

 /* USB Host */
struct at91_usbh_data {
	u8		ports;		/* number of ports on root hub */
	u8		vbus_pin[];	/* port power-control pin */
};
extern void __init at91_add_device_usbh(struct at91_usbh_data *data);

 /* NAND / SmartMedia */
struct at91_nand_data {
	u8		enable_pin;	/* chip enable */
	u8		det_pin;	/* card detect */
	u8		rdy_pin;	/* ready/busy */
	u8		ale;		/* address line number connected to ALE */
	u8		cle;		/* address line number connected to CLE */
	u8		bus_width_16;	/* buswidth is 16 bit */
	struct mtd_partition* (*partition_info)(int, int*);
};
extern void __init at91_add_device_nand(struct at91_nand_data *data);

 /* I2C*/
extern void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices);

 /* SPI */
extern void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices);

 /* Serial */
struct at91_uart_config {
	unsigned short	console_tty;	/* tty number of serial console */
	unsigned short	nr_tty;		/* number of serial tty's */
	short		tty_map[];	/* map UART to tty number */
};
extern struct platform_device *atmel_default_console_device;
extern void __init at91_init_serial(struct at91_uart_config *config);

struct atmel_uart_data {
	short		use_dma_tx;	/* use transmit DMA? */
	short		use_dma_rx;	/* use receive DMA? */
	void __iomem	*regs;		/* virtual base address, if any */
};
extern void __init at91_add_device_serial(void);

 /* LCD Controller */
struct atmel_lcdfb_info;
extern void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data);

 /* AC97 */
struct atmel_ac97_data {
	u8		reset_pin;	/* reset */
};
extern void __init at91_add_device_ac97(struct atmel_ac97_data *data);

 /* LEDs */
extern u8 at91_leds_cpu;
extern u8 at91_leds_timer;
extern void __init at91_init_leds(u8 cpu_led, u8 timer_led);

/* FIXME: this needs a better location, but gets stuff building again */
extern int at91_suspend_entering_slow_clock(void);

#endif
