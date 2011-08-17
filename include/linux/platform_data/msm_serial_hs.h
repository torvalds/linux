/*
 * Copyright (C) 2008 Google, Inc.
 * Author: Nick Pelly <npelly@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_MSM_SERIAL_HS_H
#define __ASM_ARCH_MSM_SERIAL_HS_H

#include <linux/serial_core.h>

/* API to request the uart clock off or on for low power management
 * Clients should call request_clock_off() when no uart data is expected,
 * and must call request_clock_on() before any further uart data can be
 * received. */
extern void msm_hs_request_clock_off(struct uart_port *uport);
extern void msm_hs_request_clock_on(struct uart_port *uport);

/**
 * struct msm_serial_hs_platform_data
 * @rx_wakeup_irq: Rx activity irq
 * @rx_to_inject: extra character to be inserted to Rx tty on wakeup
 * @inject_rx: 1 = insert rx_to_inject. 0 = do not insert extra character
 * @exit_lpm_cb: function called before every Tx transaction
 *
 * This is an optional structure required for UART Rx GPIO IRQ based
 * wakeup from low power state. UART wakeup can be triggered by RX activity
 * (using a wakeup GPIO on the UART RX pin). This should only be used if
 * there is not a wakeup GPIO on the UART CTS, and the first RX byte is
 * known (eg., with the Bluetooth Texas Instruments HCILL protocol),
 * since the first RX byte will always be lost. RTS will be asserted even
 * while the UART is clocked off in this mode of operation.
 */
struct msm_serial_hs_platform_data {
	int rx_wakeup_irq;
	unsigned char inject_rx_on_wakeup;
	char rx_to_inject;
	void (*exit_lpm_cb)(struct uart_port *);
};

#endif
