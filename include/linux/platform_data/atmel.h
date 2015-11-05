/*
 * atmel platform data
 *
 * GPL v2 Only
 */

#ifndef __ATMEL_H__
#define __ATMEL_H__

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/usb/atmel_usba_udc.h>
#include <linux/atmel-mci.h>
#include <sound/atmel-ac97c.h>
#include <linux/serial.h>
#include <linux/platform_data/macb.h>

 /* Compact Flash */
struct at91_cf_data {
	int	irq_pin;		/* I/O IRQ */
	int	det_pin;		/* Card detect */
	int	vcc_pin;		/* power switching */
	int	rst_pin;		/* card reset */
	u8	chipselect;		/* EBI Chip Select number */
	u8	flags;
#define AT91_CF_TRUE_IDE	0x01
#define AT91_IDE_SWAP_A0_A2	0x02
};

 /* NAND / SmartMedia */
struct atmel_nand_data {
	int		enable_pin;		/* chip enable */
	int		det_pin;		/* card detect */
	int		rdy_pin;		/* ready/busy */
	u8		rdy_pin_active_low;	/* rdy_pin value is inverted */
	u8		ale;			/* address line number connected to ALE */
	u8		cle;			/* address line number connected to CLE */
	u8		bus_width_16;		/* buswidth is 16 bit */
	u8		ecc_mode;		/* ecc mode */
	u8		on_flash_bbt;		/* bbt on flash */
	struct mtd_partition *parts;
	unsigned int	num_parts;
	bool		has_dma;		/* support dma transfer */

	/* default is false, only for at32ap7000 chip is true */
	bool		need_reset_workaround;
};

 /* Serial */
struct atmel_uart_data {
	int			num;		/* port num */
	short			use_dma_tx;	/* use transmit DMA? */
	short			use_dma_rx;	/* use receive DMA? */
	void __iomem		*regs;		/* virt. base address, if any */
	struct serial_rs485	rs485;		/* rs485 settings */
};

/* FIXME: this needs a better location, but gets stuff building again */
extern int at91_suspend_entering_slow_clock(void);

#endif /* __ATMEL_H__ */
