/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.17
 *
 *  FEATURES currently supported:
 *    See ca0106_main.c for features.
 * 
 *  Changelog:
 *    Support interrupts per period.
 *    Removed noise from Center/LFE channel when in Analog mode.
 *    Rename and remove mixer controls.
 *  0.0.6
 *    Use separate card based DMA buffer for periods table list.
 *  0.0.7
 *    Change remove and rename ctrls into lists.
 *  0.0.8
 *    Try to fix capture sources.
 *  0.0.9
 *    Fix AC3 output.
 *    Enable S32_LE format support.
 *  0.0.10
 *    Enable playback 48000 and 96000 rates. (Rates other that these do not work, even with "plug:front".)
 *  0.0.11
 *    Add Model name recognition.
 *  0.0.12
 *    Correct interrupt timing. interrupt at end of period, instead of in the middle of a playback period.
 *    Remove redundent "voice" handling.
 *  0.0.13
 *    Single trigger call for multi channels.
 *  0.0.14
 *    Set limits based on what the sound card hardware can do.
 *    playback periods_min=2, periods_max=8
 *    capture hw constraints require period_size = n * 64 bytes.
 *    playback hw constraints require period_size = n * 64 bytes.
 *  0.0.15
 *    Separated ca0106.c into separate functional .c files.
 *  0.0.16
 *    Modified Copyright message.
 *  0.0.17
 *    Implement Mic and Line in Capture.
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>

#include "ca0106.h"

static int snd_ca0106_shared_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ca0106_shared_spdif_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->spdif_enable;
	return 0;
}

static int snd_ca0106_shared_spdif_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	u32 mask;

	val = ucontrol->value.enumerated.item[0] ;
	change = (emu->spdif_enable != val);
	if (change) {
		emu->spdif_enable = val;
		if (val == 1) {
			/* Digital */
			snd_ca0106_ptr_write(emu, SPDIF_SELECT1, 0, 0xf);
			snd_ca0106_ptr_write(emu, SPDIF_SELECT2, 0, 0x0b000000);
			snd_ca0106_ptr_write(emu, CAPTURE_CONTROL, 0,
				snd_ca0106_ptr_read(emu, CAPTURE_CONTROL, 0) & ~0x1000);
			mask = inl(emu->port + GPIO) & ~0x101;
			outl(mask, emu->port + GPIO);

		} else {
			/* Analog */
			snd_ca0106_ptr_write(emu, SPDIF_SELECT1, 0, 0xf);
			snd_ca0106_ptr_write(emu, SPDIF_SELECT2, 0, 0x000f0000);
			snd_ca0106_ptr_write(emu, CAPTURE_CONTROL, 0,
				snd_ca0106_ptr_read(emu, CAPTURE_CONTROL, 0) | 0x1000);
			mask = inl(emu->port + GPIO) | 0x101;
			outl(mask, emu->port + GPIO);
		}
	}
        return change;
}

static snd_kcontrol_new_t snd_ca0106_shared_spdif __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"SPDIF Out",
	.info =		snd_ca0106_shared_spdif_info,
	.get =		snd_ca0106_shared_spdif_get,
	.put =		snd_ca0106_shared_spdif_put
};

static int snd_ca0106_capture_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[6] = { "SPDIF out", "i2s mixer out", "SPDIF in", "i2s in", "AC97 in", "SRC out" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 6;
	if (uinfo->value.enumerated.item > 5)
                uinfo->value.enumerated.item = 5;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ca0106_capture_source_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->capture_source;
	return 0;
}

static int snd_ca0106_capture_source_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	u32 mask;
	u32 source;

	val = ucontrol->value.enumerated.item[0] ;
	change = (emu->capture_source != val);
	if (change) {
		emu->capture_source = val;
		source = (val << 28) | (val << 24) | (val << 20) | (val << 16);
		mask = snd_ca0106_ptr_read(emu, CAPTURE_SOURCE, 0) & 0xffff;
		snd_ca0106_ptr_write(emu, CAPTURE_SOURCE, 0, source | mask);
	}
        return change;
}

static snd_kcontrol_new_t snd_ca0106_capture_source __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Capture Source",
	.info =		snd_ca0106_capture_source_info,
	.get =		snd_ca0106_capture_source_get,
	.put =		snd_ca0106_capture_source_put
};

static int snd_ca0106_capture_mic_line_in_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	static char *texts[2] = { "Line in", "Mic in" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
                uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ca0106_capture_mic_line_in_get(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->capture_mic_line_in;
	return 0;
}

static int snd_ca0106_capture_mic_line_in_put(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	u32 tmp;

	val = ucontrol->value.enumerated.item[0] ;
	change = (emu->capture_mic_line_in != val);
	if (change) {
		emu->capture_mic_line_in = val;
		if (val) {
			snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_PHONE); /* Mute input */
			tmp = inl(emu->port+GPIO) & ~0x400;
			tmp = tmp | 0x400;
			outl(tmp, emu->port+GPIO);
			snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_MIC);
		} else {
			snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_PHONE); /* Mute input */
			tmp = inl(emu->port+GPIO) & ~0x400;
			outl(tmp, emu->port+GPIO);
			snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_LINEIN);
		}
	}
        return change;
}

static snd_kcontrol_new_t snd_ca0106_capture_mic_line_in __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Mic/Line in Capture",
	.info =		snd_ca0106_capture_mic_line_in_info,
	.get =		snd_ca0106_capture_mic_line_in_get,
	.put =		snd_ca0106_capture_mic_line_in_put
};

static int snd_ca0106_spdif_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ca0106_spdif_get(snd_kcontrol_t * kcontrol,
                                 snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.iec958.status[0] = (emu->spdif_bits[idx] >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (emu->spdif_bits[idx] >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (emu->spdif_bits[idx] >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (emu->spdif_bits[idx] >> 24) & 0xff;
        return 0;
}

static int snd_ca0106_spdif_get_mask(snd_kcontrol_t * kcontrol,
				      snd_ctl_elem_value_t * ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
        return 0;
}

static int snd_ca0106_spdif_put(snd_kcontrol_t * kcontrol,
                                 snd_ctl_elem_value_t * ucontrol)
{
	ca0106_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	int change;
	unsigned int val;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	change = val != emu->spdif_bits[idx];
	if (change) {
		snd_ca0106_ptr_write(emu, SPCS0 + idx, 0, val);
		emu->spdif_bits[idx] = val;
	}
        return change;
}

static snd_kcontrol_new_t snd_ca0106_spdif_mask_control =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
        .iface =        SNDRV_CTL_ELEM_IFACE_PCM,
        .name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.count =	4,
        .info =         snd_ca0106_spdif_info,
        .get =          snd_ca0106_spdif_get_mask
};

static snd_kcontrol_new_t snd_ca0106_spdif_control =
{
        .iface =	SNDRV_CTL_ELEM_IFACE_PCM,
        .name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.count =	4,
        .info =         snd_ca0106_spdif_info,
        .get =          snd_ca0106_spdif_get,
        .put =          snd_ca0106_spdif_put
};

static int snd_ca0106_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}

static int snd_ca0106_volume_get(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int reg, int channel_id)
{
        ca0106_t *emu = snd_kcontrol_chip(kcontrol);
        unsigned int value;

        value = snd_ca0106_ptr_read(emu, reg, channel_id);
        ucontrol->value.integer.value[0] = 0xff - ((value >> 24) & 0xff); /* Left */
        ucontrol->value.integer.value[1] = 0xff - ((value >> 16) & 0xff); /* Right */
        return 0;
}

static int snd_ca0106_volume_get_spdif_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_FRONT_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}

static int snd_ca0106_volume_get_spdif_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_CENTER_LFE_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_get_spdif_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_UNKNOWN_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_get_spdif_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_REAR_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_get_analog_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_FRONT_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}

static int snd_ca0106_volume_get_analog_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_CENTER_LFE_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_get_analog_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_UNKNOWN_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_get_analog_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_REAR_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}

static int snd_ca0106_volume_get_feedback(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = 1;
	int reg = CAPTURE_CONTROL;
        return snd_ca0106_volume_get(kcontrol, ucontrol, reg, channel_id);
}
                                                                                                                           
static int snd_ca0106_volume_put(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol, int reg, int channel_id)
{
        ca0106_t *emu = snd_kcontrol_chip(kcontrol);
        unsigned int value;
        //value = snd_ca0106_ptr_read(emu, reg, channel_id);
        //value = value & 0xffff;
        value = ((0xff - ucontrol->value.integer.value[0]) << 24) | ((0xff - ucontrol->value.integer.value[1]) << 16);
        value = value | ((0xff - ucontrol->value.integer.value[0]) << 8) | ((0xff - ucontrol->value.integer.value[1]) );
        snd_ca0106_ptr_write(emu, reg, channel_id, value);
        return 1;
}
static int snd_ca0106_volume_put_spdif_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_FRONT_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_spdif_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_CENTER_LFE_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_spdif_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_UNKNOWN_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_spdif_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_REAR_CHANNEL;
	int reg = PLAYBACK_VOLUME1;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_analog_front(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_FRONT_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_analog_center_lfe(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_CENTER_LFE_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_analog_unknown(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_UNKNOWN_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}
static int snd_ca0106_volume_put_analog_rear(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = CONTROL_REAR_CHANNEL;
	int reg = PLAYBACK_VOLUME2;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}

static int snd_ca0106_volume_put_feedback(snd_kcontrol_t * kcontrol,
                                       snd_ctl_elem_value_t * ucontrol)
{
	int channel_id = 1;
	int reg = CAPTURE_CONTROL;
        return snd_ca0106_volume_put(kcontrol, ucontrol, reg, channel_id);
}

static snd_kcontrol_new_t snd_ca0106_volume_control_analog_front =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Analog Front Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_analog_front,
        .put =          snd_ca0106_volume_put_analog_front
};
static snd_kcontrol_new_t snd_ca0106_volume_control_analog_center_lfe =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Analog Center/LFE Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_analog_center_lfe,
        .put =          snd_ca0106_volume_put_analog_center_lfe
};
static snd_kcontrol_new_t snd_ca0106_volume_control_analog_unknown =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Analog Side Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_analog_unknown,
        .put =          snd_ca0106_volume_put_analog_unknown
};
static snd_kcontrol_new_t snd_ca0106_volume_control_analog_rear =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "Analog Rear Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_analog_rear,
        .put =          snd_ca0106_volume_put_analog_rear
};
static snd_kcontrol_new_t snd_ca0106_volume_control_spdif_front =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "SPDIF Front Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_spdif_front,
        .put =          snd_ca0106_volume_put_spdif_front
};
static snd_kcontrol_new_t snd_ca0106_volume_control_spdif_center_lfe =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "SPDIF Center/LFE Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_spdif_center_lfe,
        .put =          snd_ca0106_volume_put_spdif_center_lfe
};
static snd_kcontrol_new_t snd_ca0106_volume_control_spdif_unknown =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "SPDIF Unknown Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_spdif_unknown,
        .put =          snd_ca0106_volume_put_spdif_unknown
};
static snd_kcontrol_new_t snd_ca0106_volume_control_spdif_rear =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "SPDIF Rear Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_spdif_rear,
        .put =          snd_ca0106_volume_put_spdif_rear
};

static snd_kcontrol_new_t snd_ca0106_volume_control_feedback =
{
        .iface =        SNDRV_CTL_ELEM_IFACE_MIXER,
        .name =         "CAPTURE feedback Playback Volume",
        .info =         snd_ca0106_volume_info,
        .get =          snd_ca0106_volume_get_feedback,
        .put =          snd_ca0106_volume_put_feedback
};


static int remove_ctl(snd_card_t *card, const char *name)
{
	snd_ctl_elem_id_t id;
	memset(&id, 0, sizeof(id));
	strcpy(id.name, name);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(card, &id);
}

static snd_kcontrol_t *ctl_find(snd_card_t *card, const char *name)
{
	snd_ctl_elem_id_t sid;
	memset(&sid, 0, sizeof(sid));
	/* FIXME: strcpy is bad. */
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(card, &sid);
}

static int rename_ctl(snd_card_t *card, const char *src, const char *dst)
{
	snd_kcontrol_t *kctl = ctl_find(card, src);
	if (kctl) {
		strcpy(kctl->id.name, dst);
		return 0;
	}
	return -ENOENT;
}

int __devinit snd_ca0106_mixer(ca0106_t *emu)
{
        int err;
        snd_kcontrol_t *kctl;
        snd_card_t *card = emu->card;
	char **c;
	static char *ca0106_remove_ctls[] = {
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
		"3D Control - Switch",
		"3D Control Sigmatel - Depth",
		"PCM Playback Switch",
		"PCM Playback Volume",
		"CD Playback Switch",
		"CD Playback Volume",
		"Phone Playback Switch",
		"Phone Playback Volume",
		"Video Playback Switch",
		"Video Playback Volume",
		"PC Speaker Playback Switch",
		"PC Speaker Playback Volume",
		"Mono Output Select",
		"Capture Source",
		"Capture Switch",
		"Capture Volume",
		"External Amplifier",
		"Sigmatel 4-Speaker Stereo Playback Switch",
		"Sigmatel Surround Phase Inversion Playback ",
		NULL
	};
	static char *ca0106_rename_ctls[] = {
		"Master Playback Switch", "Capture Switch",
		"Master Playback Volume", "Capture Volume",
		"Line Playback Switch", "AC97 Line Capture Switch",
		"Line Playback Volume", "AC97 Line Capture Volume",
		"Aux Playback Switch", "AC97 Aux Capture Switch",
		"Aux Playback Volume", "AC97 Aux Capture Volume",
		"Mic Playback Switch", "AC97 Mic Capture Switch",
		"Mic Playback Volume", "AC97 Mic Capture Volume",
		"Mic Select", "AC97 Mic Select",
		"Mic Boost (+20dB)", "AC97 Mic Boost (+20dB)",
		NULL
	};
#if 1
	for (c=ca0106_remove_ctls; *c; c++)
		remove_ctl(card, *c);
	for (c=ca0106_rename_ctls; *c; c += 2)
		rename_ctl(card, c[0], c[1]);
#endif

        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_analog_front, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_analog_rear, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_analog_center_lfe, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_analog_unknown, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_spdif_front, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_spdif_rear, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_spdif_center_lfe, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_spdif_unknown, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
        if ((kctl = snd_ctl_new1(&snd_ca0106_volume_control_feedback, emu)) == NULL)
                return -ENOMEM;
        if ((err = snd_ctl_add(card, kctl)))
                return err;
	if ((kctl = snd_ctl_new1(&snd_ca0106_spdif_mask_control, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = snd_ctl_new1(&snd_ca0106_shared_spdif, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if ((kctl = snd_ctl_new1(&snd_ca0106_capture_source, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
	if (emu->details->i2c_adc == 1) {
		if ((kctl = snd_ctl_new1(&snd_ca0106_capture_mic_line_in, emu)) == NULL)
			return -ENOMEM;
		if ((err = snd_ctl_add(card, kctl)))
			return err;
	}
	if ((kctl = snd_ctl_new1(&snd_ca0106_spdif_control, emu)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(card, kctl)))
		return err;
        return 0;
}

