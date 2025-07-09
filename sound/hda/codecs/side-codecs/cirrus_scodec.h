/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2023 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CIRRUS_SCODEC_H
#define CIRRUS_SCODEC_H

int cirrus_scodec_get_speaker_id(struct device *dev, int amp_index,
				 int num_amps, int fixed_gpio_id);

#endif /* CIRRUS_SCODEC_H */
