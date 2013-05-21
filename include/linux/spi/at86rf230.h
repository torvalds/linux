/*
 * AT86RF230/RF231 driver
 *
 * Copyright (C) 2009-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dmitry.baryshkov@siemens.com>
 */
#ifndef AT86RF230_H
#define AT86RF230_H

struct at86rf230_platform_data {
	int rstn;
	int slp_tr;
	int dig2;

	/* Setting the irq_type will configure the driver to request
	 * the platform irq trigger type according to the given value
	 * and configure the interrupt polarity of the device to the
	 * corresponding polarity.
	 *
	 * Allowed values are: IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING,
	 *                     IRQF_TRIGGER_HIGH and IRQF_TRIGGER_LOW
	 *
	 * Setting it to 0, the driver does not touch the trigger type
	 * configuration of the interrupt and sets the interrupt polarity
	 * of the device to high active (the default value).
	 */
	int irq_type;
};

#endif
