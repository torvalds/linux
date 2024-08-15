/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for OMAP-UART controller.
 * Based on drivers/serial/8250.c
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Authors:
 *	Govindraj R	<govindraj.raja@ti.com>
 *	Thara Gopinath	<thara@ti.com>
 */

#ifndef __OMAP_SERIAL_H__
#define __OMAP_SERIAL_H__

#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/pm_qos.h>

#define OMAP_SERIAL_DRIVER_NAME	"omap_uart"

/*
 * Use tty device name as ttyO, [O -> OMAP]
 * in bootargs we specify as console=ttyO0 if uart1
 * is used as console uart.
 */
#define OMAP_SERIAL_NAME	"ttyO"

struct omap_uart_port_info {
	bool			dma_enabled;	/* To specify DMA Mode */
	unsigned int		uartclk;	/* UART clock rate */
	upf_t			flags;		/* UPF_* flags */
	unsigned int		dma_rx_buf_size;
	unsigned int		dma_rx_timeout;
	unsigned int		autosuspend_timeout;
	unsigned int		dma_rx_poll_rate;

	int (*get_context_loss_count)(struct device *);
	void (*enable_wakeup)(struct device *, bool);
};

#endif /* __OMAP_SERIAL_H__ */
