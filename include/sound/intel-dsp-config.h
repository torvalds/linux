/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  intel-dsp-config.h - Intel DSP config
 *
 *  Copyright (c) 2019 Jaroslav Kysela <perex@perex.cz>
 */

#ifndef __INTEL_DSP_CONFIG_H__
#define __INTEL_DSP_CONFIG_H__

struct pci_dev;

enum {
	SND_INTEL_DSP_DRIVER_ANY = 0,
	SND_INTEL_DSP_DRIVER_LEGACY,
	SND_INTEL_DSP_DRIVER_SST,
	SND_INTEL_DSP_DRIVER_SOF,
	SND_INTEL_DSP_DRIVER_LAST = SND_INTEL_DSP_DRIVER_SOF
};

#if IS_ENABLED(CONFIG_SND_INTEL_DSP_CONFIG)

int snd_intel_dsp_driver_probe(struct pci_dev *pci);

#else

static inline int snd_intel_dsp_driver_probe(struct pci_dev *pci)
{
	return SND_INTEL_DSP_DRIVER_ANY;
}

#endif

#endif
