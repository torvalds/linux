/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_CLDMA_H
#define __SOUND_SOC_INTEL_AVS_CLDMA_H

#include <linux/sizes.h>

#define AVS_CL_DEFAULT_BUFFER_SIZE	SZ_128K

struct hda_cldma;
extern struct hda_cldma code_loader;

void hda_cldma_fill(struct hda_cldma *cl);
void hda_cldma_transfer(struct hda_cldma *cl, unsigned long start_delay);

int hda_cldma_start(struct hda_cldma *cl);
int hda_cldma_stop(struct hda_cldma *cl);
int hda_cldma_reset(struct hda_cldma *cl);

void hda_cldma_set_data(struct hda_cldma *cl, void *data, unsigned int size);
void hda_cldma_setup(struct hda_cldma *cl);
int hda_cldma_init(struct hda_cldma *cl, struct hdac_bus *bus, void __iomem *dsp_ba,
		   unsigned int buffer_size);
void hda_cldma_free(struct hda_cldma *cl);

#endif
