/*
 * helper functions for Asus Xonar cards
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "xonar.h"


#define GPIO_CS53x1_M_MASK	0x000c
#define GPIO_CS53x1_M_SINGLE	0x0000
#define GPIO_CS53x1_M_DOUBLE	0x0004
#define GPIO_CS53x1_M_QUAD	0x0008


void xonar_enable_output(struct oxygen *chip)
{
	struct xonar_generic *data = chip->model_data;

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, data->output_enable_bit);
	msleep(data->anti_pop_delay);
	oxygen_set_bits16(chip, OXYGEN_GPIO_DATA, data->output_enable_bit);
}

void xonar_disable_output(struct oxygen *chip)
{
	struct xonar_generic *data = chip->model_data;

	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, data->output_enable_bit);
}

static void xonar_ext_power_gpio_changed(struct oxygen *chip)
{
	struct xonar_generic *data = chip->model_data;
	u8 has_power;

	has_power = !!(oxygen_read8(chip, data->ext_power_reg)
		       & data->ext_power_bit);
	if (has_power != data->has_power) {
		data->has_power = has_power;
		if (has_power) {
			snd_printk(KERN_NOTICE "power restored\n");
		} else {
			snd_printk(KERN_CRIT
				   "Hey! Don't unplug the power cable!\n");
			/* TODO: stop PCMs */
		}
	}
}

void xonar_init_ext_power(struct oxygen *chip)
{
	struct xonar_generic *data = chip->model_data;

	oxygen_set_bits8(chip, data->ext_power_int_reg,
			 data->ext_power_bit);
	chip->interrupt_mask |= OXYGEN_INT_GPIO;
	chip->model.gpio_changed = xonar_ext_power_gpio_changed;
	data->has_power = !!(oxygen_read8(chip, data->ext_power_reg)
			     & data->ext_power_bit);
}

void xonar_init_cs53x1(struct oxygen *chip)
{
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_CS53x1_M_MASK);
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
			      GPIO_CS53x1_M_SINGLE, GPIO_CS53x1_M_MASK);
}

void xonar_set_cs53x1_params(struct oxygen *chip,
			     struct snd_pcm_hw_params *params)
{
	unsigned int value;

	if (params_rate(params) <= 54000)
		value = GPIO_CS53x1_M_SINGLE;
	else if (params_rate(params) <= 108000)
		value = GPIO_CS53x1_M_DOUBLE;
	else
		value = GPIO_CS53x1_M_QUAD;
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
			      value, GPIO_CS53x1_M_MASK);
}

int xonar_gpio_bit_switch_get(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 bit = ctl->private_value;

	value->value.integer.value[0] =
		!!(oxygen_read16(chip, OXYGEN_GPIO_DATA) & bit);
	return 0;
}

int xonar_gpio_bit_switch_put(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 bit = ctl->private_value;
	u16 old_bits, new_bits;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	old_bits = oxygen_read16(chip, OXYGEN_GPIO_DATA);
	if (value->value.integer.value[0])
		new_bits = old_bits | bit;
	else
		new_bits = old_bits & ~bit;
	changed = new_bits != old_bits;
	if (changed)
		oxygen_write16(chip, OXYGEN_GPIO_DATA, new_bits);
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}
