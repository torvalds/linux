/* SPDX-License-Identifier: GPL-2.0-only
 *
 * HDA audio driver for Texas Instruments TAS2781 smart amp
 *
 * Copyright (C) 2025 Texas Instruments, Inc.
 */
#ifndef __TAS2781_HDA_H__
#define __TAS2781_HDA_H__

#include <sound/asound.h>

/*
 * No standard control callbacks for SNDRV_CTL_ELEM_IFACE_CARD
 * Define two controls, one is Volume control callbacks, the other is
 * flag setting control callbacks.
 */

/* Volume control callbacks for tas2781 */
#define ACARD_SINGLE_RANGE_EXT_TLV(xname, xreg, xshift, xmin, xmax, xinvert, \
	xhandler_get, xhandler_put, tlv_array) { \
	.iface = SNDRV_CTL_ELEM_IFACE_CARD, .name = (xname), \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = snd_soc_info_volsw, \
	.get = xhandler_get, .put = xhandler_put, \
	.private_value = (unsigned long)&(struct soc_mixer_control) { \
		.reg = xreg, .rreg = xreg, \
		.shift = xshift, .rshift = xshift,\
		.min = xmin, .max = xmax, .invert = xinvert, \
	} \
}

/* Flag control callbacks for tas2781 */
#define ACARD_SINGLE_BOOL_EXT(xname, xdata, xhandler_get, xhandler_put) { \
	.iface = SNDRV_CTL_ELEM_IFACE_CARD, \
	.name = xname, \
	.info = snd_ctl_boolean_mono_info, \
	.get = xhandler_get, \
	.put = xhandler_put, \
	.private_value = xdata, \
}

#endif
