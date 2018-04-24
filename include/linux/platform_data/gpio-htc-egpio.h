/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HTC simple EGPIO irq and gpio extender
 */

#ifndef __HTC_EGPIO_H__
#define __HTC_EGPIO_H__

/* Descriptive values for all-in or all-out htc_egpio_chip descriptors. */
#define HTC_EGPIO_OUTPUT (~0)
#define HTC_EGPIO_INPUT  0

/**
 * struct htc_egpio_chip - descriptor to create gpio_chip for register range
 * @reg_start: index of first register
 * @gpio_base: gpio number of first pin in this register range
 * @num_gpios: number of gpios in this register range, max BITS_PER_LONG
 *    (number of registers = DIV_ROUND_UP(num_gpios, reg_width))
 * @direction: bitfield, '0' = input, '1' = output,
 */
struct htc_egpio_chip {
	int           reg_start;
	int           gpio_base;
	int           num_gpios;
	unsigned long direction;
	unsigned long initial_values;
};

/**
 * struct htc_egpio_platform_data - description provided by the arch
 * @irq_base: beginning of available IRQs (eg, IRQ_BOARD_START)
 * @num_irqs: number of irqs
 * @reg_width: number of bits per register, either 8 or 16 bit
 * @bus_width: alignment of the registers, either 16 or 32 bit
 * @invert_acks: set if chip requires writing '0' to ack an irq, instead of '1'
 * @ack_register: location of the irq/ack register
 * @chip: pointer to array of htc_egpio_chip descriptors
 * @num_chips: number of egpio chip descriptors
 */
struct htc_egpio_platform_data {
	int                   bus_width;
	int                   reg_width;

	int                   irq_base;
	int                   num_irqs;
	int                   invert_acks;
	int                   ack_register;

	struct htc_egpio_chip *chip;
	int                   num_chips;
};

/* Determine the wakeup irq, to be called during early resume */
extern int htc_egpio_get_wakeup_irq(struct device *dev);

#endif
