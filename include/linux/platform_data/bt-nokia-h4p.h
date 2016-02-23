/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2010 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */


/**
 * struct hci_h4p_platform data - hci_h4p Platform data structure
 */
struct hci_h4p_platform_data {
	int chip_type;
	int bt_sysclk;
	unsigned int bt_wakeup_gpio;
	unsigned int host_wakeup_gpio;
	unsigned int reset_gpio;
	int reset_gpio_shared;
	unsigned int uart_irq;
	phys_addr_t uart_base;
	const char *uart_iclk;
	const char *uart_fclk;
	void (*set_pm_limits)(struct device *dev, bool set);
};
