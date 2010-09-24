/*
 * Driver for the Semtech SX150x I2C GPIO Expanders
 *
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef __LINUX_I2C_SX150X_H
#define __LINUX_I2C_SX150X_H

/**
 * struct sx150x_platform_data - config data for SX150x driver
 * @gpio_base: The index number of the first GPIO assigned to this
 *             GPIO expander.  The expander will create a block of
 *             consecutively numbered gpios beginning at the given base,
 *             with the size of the block depending on the model of the
 *             expander chip.
 * @oscio_is_gpo: If set to true, the driver will configure OSCIO as a GPO
 *                instead of as an oscillator, increasing the size of the
 *                GP(I)O pool created by this expander by one.  The
 *                output-only GPO pin will be added at the end of the block.
 * @io_pullup_ena: A bit-mask which enables or disables the pull-up resistor
 *                 for each IO line in the expander.  Setting the bit at
 *                 position n will enable the pull-up for the IO at
 *                 the corresponding offset.  For chips with fewer than
 *                 16 IO pins, high-end bits are ignored.
 * @io_pulldn_ena: A bit-mask which enables-or disables the pull-down
 *                 resistor for each IO line in the expander. Setting the
 *                 bit at position n will enable the pull-down for the IO at
 *                 the corresponding offset.  For chips with fewer than
 *                 16 IO pins, high-end bits are ignored.
 * @io_open_drain_ena: A bit-mask which enables-or disables open-drain
 *                     operation for each IO line in the expander. Setting the
 *                     bit at position n enables open-drain operation for
 *                     the IO at the corresponding offset.  Clearing the bit
 *                     enables regular push-pull operation for that IO.
 *                     For chips with fewer than 16 IO pins, high-end bits
 *                     are ignored.
 * @io_polarity: A bit-mask which enables polarity inversion for each IO line
 *               in the expander.  Setting the bit at position n inverts
 *               the polarity of that IO line, while clearing it results
 *               in normal polarity. For chips with fewer than 16 IO pins,
 *               high-end bits are ignored.
 * @irq_summary: The 'summary IRQ' line to which the GPIO expander's INT line
 *               is connected, via which it reports interrupt events
 *               across all GPIO lines.  This must be a real,
 *               pre-existing IRQ line.
 *               Setting this value < 0 disables the irq_chip functionality
 *               of the driver.
 * @irq_base: The first 'virtual IRQ' line at which our block of GPIO-based
 *            IRQ lines will appear.  Similarly to gpio_base, the expander
 *            will create a block of irqs beginning at this number.
 *            This value is ignored if irq_summary is < 0.
 * @reset_during_probe: If set to true, the driver will trigger a full
 *                      reset of the chip at the beginning of the probe
 *                      in order to place it in a known state.
 */
struct sx150x_platform_data {
	unsigned gpio_base;
	bool     oscio_is_gpo;
	u16      io_pullup_ena;
	u16      io_pulldn_ena;
	u16      io_open_drain_ena;
	u16      io_polarity;
	int      irq_summary;
	unsigned irq_base;
	bool     reset_during_probe;
};

#endif /* __LINUX_I2C_SX150X_H */
