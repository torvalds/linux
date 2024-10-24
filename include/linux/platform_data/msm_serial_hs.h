/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2010-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Author: Nick Pelly <npelly@google.com>
 */

#ifndef __ASM_ARCH_MSM_SERIAL_HS_H
#define __ASM_ARCH_MSM_SERIAL_HS_H

#include <linux/serial_core.h>

/**
 * struct msm_serial_hs_platform_data - platform device data
 *					for msm hsuart device
 * @wakeup_irq : IRQ line to be configured as Wakeup source.
 * @inject_rx_on_wakeup : Set 1 if specific character to be inserted on wakeup
 * @rx_to_inject : Character to be inserted on wakeup
 * @gpio_config : Configure gpios that are used for uart communication
 * @userid : User-defined number to be used to enumerate device as tty<userid>
 * @uart_tx_gpio: GPIO number for UART Tx Line.
 * @uart_rx_gpio: GPIO number for UART Rx Line.
 * @uart_cts_gpio: GPIO number for UART CTS Line.
 * @uart_rfr_gpio: GPIO number for UART RFR Line.
 * @bam_tx_ep_pipe_index : BAM TX Endpoint Pipe Index for HSUART
 * @bam_tx_ep_pipe_index : BAM RX Endpoint Pipe Index for HSUART
 * @no_suspend_delay : Flag used to make system go to suspend
 * immediately or not
 * @obs: Flag to enable out of band sleep feature support
 */
struct msm_serial_hs_platform_data {
	int wakeup_irq;  /* wakeup irq */
	bool inject_rx_on_wakeup;
	u8 rx_to_inject;
	int (*gpio_config)(int gpio_config);
	int userid;

	int uart_tx_gpio;
	int uart_rx_gpio;
	int uart_cts_gpio;
	int uart_rfr_gpio;
	unsigned int bam_tx_ep_pipe_index;
	unsigned int bam_rx_ep_pipe_index;
	bool no_suspend_delay;
	bool obs;
};

/* return true when tx is empty */
unsigned int msm_hs_tx_empty(struct uart_port *uport);
int msm_hs_request_clock_off(struct uart_port *uport);
int msm_hs_request_clock_on(struct uart_port *uport);
struct uart_port *msm_hs_get_uart_port(int port_index);
void msm_hs_set_mctrl(struct uart_port *uport,
				    unsigned int mctrl);
#endif
