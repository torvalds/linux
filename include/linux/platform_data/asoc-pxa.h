/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_PXA_AUDIO_H__
#define __SOC_PXA_AUDIO_H__

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>

extern void pxa27x_configure_ac97reset(struct gpio_desc *reset_gpio, bool to_gpio);

#endif
