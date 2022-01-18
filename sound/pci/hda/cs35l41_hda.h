/* SPDX-License-Identifier: GPL-2.0
 *
 * CS35L41 ALSA HDA audio driver
 *
 * Copyright 2021 Cirrus Logic, Inc.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 */

#ifndef __CS35L41_HDA_H__
#define __CS35L41_HDA_H__

#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <sound/cs35l41.h>

enum cs35l41_hda_spk_pos {
	CS35l41_LEFT,
	CS35l41_RIGHT,
};

enum cs35l41_hda_gpio_function {
	CS35L41_NOT_USED,
	CS35l41_VSPK_SWITCH,
	CS35L41_INTERRUPT,
	CS35l41_SYNC,
};

struct cs35l41_hda_reg_sequence {
	const struct reg_sequence *probe;
	unsigned int num_probe;
	const struct reg_sequence *open;
	unsigned int num_open;
	const struct reg_sequence *prepare;
	unsigned int num_prepare;
	const struct reg_sequence *cleanup;
	unsigned int num_cleanup;
	const struct reg_sequence *close;
	unsigned int num_close;
};

struct cs35l41_hda_hw_config {
	unsigned int spk_pos;
	unsigned int gpio1_func;
	unsigned int gpio2_func;
	int bst_ind;
	int bst_ipk;
	int bst_cap;
};

struct cs35l41_hda {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	const struct cs35l41_hda_reg_sequence *reg_seq;

	int irq;
	int index;

	/* Don't put the AMP in reset of VSPK can not be turned off */
	bool vspk_always_on;
};

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap);
void cs35l41_hda_remove(struct device *dev);

#endif /*__CS35L41_HDA_H__*/
