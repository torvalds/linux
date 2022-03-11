/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __SOUND_SOC_INTEL_AVS_H
#define __SOUND_SOC_INTEL_AVS_H

#include <linux/device.h>
#include <sound/hda_codec.h>

struct avs_dev;

struct avs_dsp_ops {
	int (* const power)(struct avs_dev *, u32, bool);
	int (* const reset)(struct avs_dev *, u32, bool);
	int (* const stall)(struct avs_dev *, u32, bool);
};

#define avs_dsp_op(adev, op, ...) \
	((adev)->spec->dsp_ops->op(adev, ## __VA_ARGS__))

#define avs_platattr_test(adev, attr) \
	((adev)->spec->attributes & AVS_PLATATTR_##attr)

/* Platform specific descriptor */
struct avs_spec {
	const char *name;

	const struct avs_dsp_ops *const dsp_ops;

	const u32 core_init_mask;	/* used during DSP boot */
	const u64 attributes;		/* bitmask of AVS_PLATATTR_* */
};

/*
 * struct avs_dev - Intel HD-Audio driver data
 *
 * @dev: PCI device
 * @dsp_ba: DSP bar address
 * @spec: platform-specific descriptor
 */
struct avs_dev {
	struct hda_bus base;
	struct device *dev;

	void __iomem *dsp_ba;
	const struct avs_spec *spec;
};

/* from hda_bus to avs_dev */
#define hda_to_avs(hda) container_of(hda, struct avs_dev, base)
/* from hdac_bus to avs_dev */
#define hdac_to_avs(hdac) hda_to_avs(to_hda_bus(hdac))
/* from device to avs_dev */
#define to_avs_dev(dev) \
({ \
	struct hdac_bus *__bus = dev_get_drvdata(dev); \
	hdac_to_avs(__bus); \
})

int avs_dsp_core_power(struct avs_dev *adev, u32 core_mask, bool power);
int avs_dsp_core_reset(struct avs_dev *adev, u32 core_mask, bool reset);
int avs_dsp_core_stall(struct avs_dev *adev, u32 core_mask, bool stall);
int avs_dsp_core_enable(struct avs_dev *adev, u32 core_mask);
int avs_dsp_core_disable(struct avs_dev *adev, u32 core_mask);

#endif /* __SOUND_SOC_INTEL_AVS_H */
