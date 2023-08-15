/* SPDX-License-Identifier: GPL-2.0
 *
 * CS35L41 ALSA HDA Property driver
 *
 * Copyright 2023 Cirrus Logic, Inc.
 *
 * Author: Stefan Binding <sbinding@opensource.cirrus.com>
 */

#ifndef CS35L41_HDA_PROP_H
#define CS35L41_HDA_PROP_H

#include <linux/device.h>
#include "cs35l41_hda.h"

int cs35l41_add_dsd_properties(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
			       const char *hid);
#endif /* CS35L41_HDA_PROP_H */
