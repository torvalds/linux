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

/*
 * at91: 6 USARTs and one DBGU port (SAM9260)
 * avr32: 4
 */
#define ATMEL_MAX_UART	7

 /* USB Device */
struct at91_udc_data {
	int	vbus_pin;		/* high == host powering us */
	u8	vbus_active_low;	/* vbus polarity */
	u8	vbus_polled;		/* Use polling, not interrupt */
	int	pullup_pin;		/* active == D+ pulled up */
	u8	pullup_active_low;	/* true == pullup_pin is active low */
};

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

 /* USB Host */
#define AT91_MAX_USBH_PORTS	3
struct at91_usbh_data {
	int		vbus_pin[AT91_MAX_USBH_PORTS];	/* port power-control pin */
	int             overcurrent_pin[AT91_MAX_USBH_PORTS];
	u8		ports;				/* number of ports on root hub */
	u8              overcurrent_supported;
	u8              vbus_pin_active_low[AT91_MAX_USBH_PORTS];
	u8              overcurrent_status[AT91_MAX_USBH_PORTS];
	u8              overcurrent_changed[AT91_MAX_USBH_PORTS];
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
};

 /* Serial */
struct atmel_uart_data {
	int			num;		/* port num */
	short			use_dma_tx;	/* use transmit DMA? */
	short			use_dma_rx;	/* use receive DMA? */
	void __iomem		*regs;		/* virt. base address, if any */
	struct serial_rs485	rs485;		/* rs485 settings */
};

 /* Touchscreen Controller */
struct at91_tsadcc_data {
	unsigned int    adc_clock;
	u8		pendet_debounce;
	u8		ts_sample_hold_time;
};

/* CAN */
struct at91_can_data {
	void (*transceiver_switch)(int on);
};

/* FIXME: this needs a better location, but gets stuff building again */
extern int at91_suspend_entering_slow_clock(void);

#endif /* __ATMEL_H__ */
