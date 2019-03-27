/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
 * Copyright (c) 2006 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2008-2012 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Intel High Definition Audio (Audio function quirks) driver for FreeBSD.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <sys/ctype.h>

#include <dev/sound/pci/hda/hdac.h>
#include <dev/sound/pci/hda/hdaa.h>
#include <dev/sound/pci/hda/hda_reg.h>

SND_DECLARE_FILE("$FreeBSD$");

static const struct {
	uint32_t model;
	uint32_t id;
	uint32_t subsystemid;
	uint32_t set, unset;
	uint32_t gpio;
} hdac_quirks[] = {
	/*
	 * XXX Force stereo quirk. Monoural recording / playback
	 *     on few codecs (especially ALC880) seems broken or
	 *     perhaps unsupported.
	 */
	{ HDA_MATCH_ALL, HDA_MATCH_ALL, HDA_MATCH_ALL,
	    HDAA_QUIRK_FORCESTEREO | HDAA_QUIRK_IVREF, 0,
	    0 },
	{ ACER_ALL_SUBVENDOR, HDA_MATCH_ALL, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_G2K_SUBVENDOR, HDA_CODEC_ALC660, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_M5200_SUBVENDOR, HDA_CODEC_ALC880, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_A7M_SUBVENDOR, HDA_CODEC_ALC880, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_A7T_SUBVENDOR, HDA_CODEC_ALC882, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_W2J_SUBVENDOR, HDA_CODEC_ALC882, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ ASUS_U5F_SUBVENDOR, HDA_CODEC_AD1986A, HDA_MATCH_ALL,
	    HDAA_QUIRK_EAPDINV, 0,
	    0 },
	{ ASUS_A8X_SUBVENDOR, HDA_CODEC_AD1986A, HDA_MATCH_ALL,
	    HDAA_QUIRK_EAPDINV, 0,
	    0 },
	{ ASUS_F3JC_SUBVENDOR, HDA_CODEC_ALC861, HDA_MATCH_ALL,
	    HDAA_QUIRK_OVREF, 0,
	    0 },
	{ UNIWILL_9075_SUBVENDOR, HDA_CODEC_ALC861, HDA_MATCH_ALL,
	    HDAA_QUIRK_OVREF, 0,
	    0 },
	/*{ ASUS_M2N_SUBVENDOR, HDA_CODEC_AD1988, HDA_MATCH_ALL,
	    HDAA_QUIRK_IVREF80, HDAA_QUIRK_IVREF50 | HDAA_QUIRK_IVREF100,
	    0 },*/
	{ MEDION_MD95257_SUBVENDOR, HDA_CODEC_ALC880, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(1) },
	{ LENOVO_3KN100_SUBVENDOR, HDA_CODEC_AD1986A, HDA_MATCH_ALL,
	    HDAA_QUIRK_EAPDINV | HDAA_QUIRK_SENSEINV, 0,
	    0 },
	{ SAMSUNG_Q1_SUBVENDOR, HDA_CODEC_AD1986A, HDA_MATCH_ALL,
	    HDAA_QUIRK_EAPDINV, 0,
	    0 },
	{ APPLE_MB3_SUBVENDOR, HDA_CODEC_ALC885, HDA_MATCH_ALL,
	    HDAA_QUIRK_OVREF50, 0,
	    HDAA_GPIO_SET(0) },
	{ APPLE_INTEL_MAC, HDA_CODEC_STAC9221, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) | HDAA_GPIO_SET(1) },
	{ APPLE_MACBOOKAIR31, HDA_CODEC_CS4206, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(1) | HDAA_GPIO_SET(3) },
	{ APPLE_MACBOOKPRO55, HDA_CODEC_CS4206, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(1) | HDAA_GPIO_SET(3) },
	{ APPLE_MACBOOKPRO71, HDA_CODEC_CS4206, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(1) | HDAA_GPIO_SET(3) },
	{ HDA_INTEL_MACBOOKPRO92, HDA_CODEC_CS4206, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(1) | HDAA_GPIO_SET(3) },
	{ DELL_D630_SUBVENDOR, HDA_CODEC_STAC9205X, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ DELL_V1400_SUBVENDOR, HDA_CODEC_STAC9228X, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(2) },
	{ DELL_V1500_SUBVENDOR, HDA_CODEC_STAC9205X, HDA_MATCH_ALL,
	    0, 0,
	    HDAA_GPIO_SET(0) },
	{ HDA_MATCH_ALL, HDA_CODEC_AD1988, HDA_MATCH_ALL,
	    HDAA_QUIRK_IVREF80, HDAA_QUIRK_IVREF50 | HDAA_QUIRK_IVREF100,
	    0 },
	{ HDA_MATCH_ALL, HDA_CODEC_AD1988B, HDA_MATCH_ALL,
	    HDAA_QUIRK_IVREF80, HDAA_QUIRK_IVREF50 | HDAA_QUIRK_IVREF100,
	    0 },
	{ HDA_MATCH_ALL, HDA_CODEC_CX20549, HDA_MATCH_ALL,
	    0, HDAA_QUIRK_FORCESTEREO,
	    0 },
	/* Mac Pro 1,1 requires ovref for proper volume level. */
	{ 0x00000000, HDA_CODEC_ALC885, 0x106b0c00,
	    0, HDAA_QUIRK_OVREF,
	    0 }
};

static void
hdac_pin_patch(struct hdaa_widget *w)
{
	const char *patch = NULL;
	uint32_t config, orig, id, subid;
	nid_t nid = w->nid;

	config = orig = w->wclass.pin.config;
	id = hdaa_codec_id(w->devinfo);
	subid = hdaa_card_id(w->devinfo);

	/* XXX: Old patches require complete review.
	 * Now they may create more problem then solve due to
	 * incorrect associations.
	 */
	if (id == HDA_CODEC_ALC880 && subid == LG_LW20_SUBVENDOR) {
		switch (nid) {
		case 26:
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 27:
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT;
			break;
		default:
			break;
		}
	} else if (id == HDA_CODEC_ALC880 &&
	    (subid == CLEVO_D900T_SUBVENDOR ||
	    subid == ASUS_M5200_SUBVENDOR)) {
		/*
		 * Super broken BIOS
		 */
		switch (nid) {
		case 24:	/* MIC1 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN;
			break;
		case 25:	/* XXX MIC2 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN;
			break;
		case 26:	/* LINE1 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 27:	/* XXX LINE2 */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN;
			break;
		case 28:	/* CD */
			config &= ~HDA_CONFIG_DEFAULTCONF_DEVICE_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_DEVICE_CD;
			break;
		}
	} else if (id == HDA_CODEC_ALC883 &&
	    (subid == MSI_MS034A_SUBVENDOR ||
	    HDA_DEV_MATCH(ACER_ALL_SUBVENDOR, subid))) {
		switch (nid) {
		case 25:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 28:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_CD |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		}
	} else if (id == HDA_CODEC_CX20549 && subid ==
	    HP_V3000_SUBVENDOR) {
		switch (nid) {
		case 18:
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE;
			break;
		case 20:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 21:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_CD |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		}
	} else if (id == HDA_CODEC_CX20551 && subid ==
	    HP_DV5000_SUBVENDOR) {
		switch (nid) {
		case 20:
		case 21:
			config &= ~HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK;
			config |= HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE;
			break;
		}
	} else if (id == HDA_CODEC_ALC861 && subid ==
	    ASUS_W6F_SUBVENDOR) {
		switch (nid) {
		case 11:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 12:
		case 14:
		case 16:
		case 31:
		case 32:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_FIXED);
			break;
		case 15:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK);
			break;
		}
	} else if (id == HDA_CODEC_ALC861 && subid ==
	    UNIWILL_9075_SUBVENDOR) {
		switch (nid) {
		case 15:
			config &= ~(HDA_CONFIG_DEFAULTCONF_DEVICE_MASK |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK);
			config |= (HDA_CONFIG_DEFAULTCONF_DEVICE_HP_OUT |
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK);
			break;
		}
	}

	/* New patches */
	if (id == HDA_CODEC_AD1984A &&
	    subid == LENOVO_X300_SUBVENDOR) {
		switch (nid) {
		case 17: /* Headphones with redirection */
			patch = "as=1 seq=15";
			break;
		case 20: /* Two mics together */
			patch = "as=2 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_AD1986A &&
	    (subid == ASUS_M2NPVMX_SUBVENDOR ||
	    subid == ASUS_A8NVMCSM_SUBVENDOR ||
	    subid == ASUS_P5PL2_SUBVENDOR)) {
		switch (nid) {
		case 26: /* Headphones with redirection */
			patch = "as=1 seq=15";
			break;
		case 28: /* 5.1 out => 2.0 out + 1 input */
			patch = "device=Line-in as=8 seq=1";
			break;
		case 29: /* Can't use this as input, as the only available mic
			  * preamplifier is busy by front panel mic (nid 31).
			  * If you want to use this rear connector as mic input,
			  * you have to disable the front panel one. */
			patch = "as=0";
			break;
		case 31: /* Lot of inputs configured with as=15 and unusable */
			patch = "as=8 seq=3";
			break;
		case 32:
			patch = "as=8 seq=4";
			break;
		case 34:
			patch = "as=8 seq=5";
			break;
		case 36:
			patch = "as=8 seq=6";
			break;
		}
	} else if (id == HDA_CODEC_ALC260 &&
	    HDA_DEV_MATCH(SONY_S5_SUBVENDOR, subid)) {
		switch (nid) {
		case 16:
			patch = "seq=15 device=Headphones";
			break;
		}
	} else if (id == HDA_CODEC_ALC268) {
	    if (subid == ACER_T5320_SUBVENDOR) {
		switch (nid) {
		case 20: /* Headphones Jack */
			patch = "as=1 seq=15";
			break;
		}
	    }
	} else if (id == HDA_CODEC_CX20561 &&
	    subid == LENOVO_B450_SUBVENDOR) {
		switch (nid) {
		case 22:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_CX20561 &&
	    subid == LENOVO_T400_SUBVENDOR) {
		switch (nid) {
		case 22:
			patch = "as=1 seq=15";
			break;
		case 26:
			patch = "as=1 seq=0";
			break;
		}
	} else if (id == HDA_CODEC_CX20590 &&
	    (subid == LENOVO_X1_SUBVENDOR ||
	    subid == LENOVO_X220_SUBVENDOR ||
	    subid == LENOVO_T420_SUBVENDOR ||
	    subid == LENOVO_T520_SUBVENDOR ||
	    subid == LENOVO_G580_SUBVENDOR)) {
		switch (nid) {
		case 25:
			patch = "as=1 seq=15";
			break;
		/*
		 * Group onboard mic and headphone mic
		 * together.  Fixes onboard mic.
		 */
		case 27:
			patch = "as=2 seq=15";
			break;
		case 35:
			patch = "as=2";
			break;
		}
	} else if (id == HDA_CODEC_ALC269 &&
	    (subid == LENOVO_X1CRBN_SUBVENDOR ||
	    subid == LENOVO_T430_SUBVENDOR ||
	    subid == LENOVO_T430S_SUBVENDOR ||
	    subid == LENOVO_T530_SUBVENDOR)) {
		switch (nid) {
		case 21:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_ALC269 &&
	    subid == ASUS_UX31A_SUBVENDOR) {
		switch (nid) {
		case 33:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_ALC892 &&
	    subid == INTEL_DH87RL_SUBVENDOR) {
		switch (nid) {
		case 27:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_ALC292 &&
	    subid == LENOVO_X120BS_SUBVENDOR) {
		switch (nid) {
		case 21:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_ALC295 && subid == HP_AF006UR_SUBVENDOR) {
		switch (nid) {
		case 18:
			patch = "as=2";
			break;
		case 25:
			patch = "as=2 seq=15";
			break;
		case 33:
			patch = "as=1 seq=15";
			break;
		}
	} else if (id == HDA_CODEC_ALC298 && subid == DELL_XPS9560_SUBVENDOR) {
		switch (nid) {
		case 24:
			config  = 0x01a1913c;
			break;
		case 26:
			config  = 0x01a1913d;
			break;
		}
	}

	if (patch != NULL)
		config = hdaa_widget_pin_patch(config, patch);
	HDA_BOOTVERBOSE(
		if (config != orig)
			device_printf(w->devinfo->dev,
			    "Patching pin config nid=%u 0x%08x -> 0x%08x\n",
			    nid, orig, config);
	);
	w->wclass.pin.config = config;
}

static void
hdaa_widget_patch(struct hdaa_widget *w)
{
	struct hdaa_devinfo *devinfo = w->devinfo;
	uint32_t orig;
	nid_t beeper = -1;

	orig = w->param.widget_cap;
	/* On some codecs beeper is an input pin, but it is not recordable
	   alone. Also most of BIOSes does not declare beeper pin.
	   Change beeper pin node type to beeper to help parser. */
	switch (hdaa_codec_id(devinfo)) {
	case HDA_CODEC_AD1882:
	case HDA_CODEC_AD1883:
	case HDA_CODEC_AD1984:
	case HDA_CODEC_AD1984A:
	case HDA_CODEC_AD1984B:
	case HDA_CODEC_AD1987:
	case HDA_CODEC_AD1988:
	case HDA_CODEC_AD1988B:
	case HDA_CODEC_AD1989B:
		beeper = 26;
		break;
	case HDA_CODEC_ALC260:
		beeper = 23;
		break;
	}
	if (hda_get_vendor_id(devinfo->dev) == REALTEK_VENDORID &&
	    hdaa_codec_id(devinfo) != HDA_CODEC_ALC260)
		beeper = 29;
	if (w->nid == beeper) {
		w->param.widget_cap &= ~HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_MASK;
		w->param.widget_cap |= HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_BEEP_WIDGET <<
		    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_SHIFT;
		w->waspin = 1;
	}
	/*
	 * Clear "digital" flag from digital mic input, as its signal then goes
	 * to "analog" mixer and this separation just limits functionaity.
	 */
	if (hdaa_codec_id(devinfo) == HDA_CODEC_AD1984A &&
	    w->nid == 23)
		w->param.widget_cap &= ~HDA_PARAM_AUDIO_WIDGET_CAP_DIGITAL_MASK;
	HDA_BOOTVERBOSE(
		if (w->param.widget_cap != orig) {
			device_printf(w->devinfo->dev,
			    "Patching widget caps nid=%u 0x%08x -> 0x%08x\n",
			    w->nid, orig, w->param.widget_cap);
		}
	);

	if (w->type == HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX)
		hdac_pin_patch(w);
}

void
hdaa_patch(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	uint32_t id, subid, subsystemid;
	int i;

	id = hdaa_codec_id(devinfo);
	subid = hdaa_card_id(devinfo);
	subsystemid = hda_get_subsystem_id(devinfo->dev);

	/*
	 * Quirks
	 */
	for (i = 0; i < nitems(hdac_quirks); i++) {
		if (!(HDA_DEV_MATCH(hdac_quirks[i].model, subid) &&
		    HDA_DEV_MATCH(hdac_quirks[i].id, id) &&
		    HDA_DEV_MATCH(hdac_quirks[i].subsystemid, subsystemid)))
			continue;
		devinfo->quirks |= hdac_quirks[i].set;
		devinfo->quirks &= ~(hdac_quirks[i].unset);
		devinfo->gpio = hdac_quirks[i].gpio;
	}

	/* Apply per-widget patch. */
	for (i = devinfo->startnode; i < devinfo->endnode; i++) {
		w = hdaa_widget_get(devinfo, i);
		if (w == NULL)
			continue;
		hdaa_widget_patch(w);
	}

	switch (id) {
	case HDA_CODEC_AD1983:
		/*
		 * This CODEC has several possible usages, but none
		 * fit the parser best. Help parser to choose better.
		 */
		/* Disable direct unmixed playback to get pcm volume. */
		w = hdaa_widget_get(devinfo, 5);
		if (w != NULL)
			w->connsenable[0] = 0;
		w = hdaa_widget_get(devinfo, 6);
		if (w != NULL)
			w->connsenable[0] = 0;
		w = hdaa_widget_get(devinfo, 11);
		if (w != NULL)
			w->connsenable[0] = 0;
		/* Disable mic and line selectors. */
		w = hdaa_widget_get(devinfo, 12);
		if (w != NULL)
			w->connsenable[1] = 0;
		w = hdaa_widget_get(devinfo, 13);
		if (w != NULL)
			w->connsenable[1] = 0;
		/* Disable recording from mono playback mix. */
		w = hdaa_widget_get(devinfo, 20);
		if (w != NULL)
			w->connsenable[3] = 0;
		break;
	case HDA_CODEC_AD1986A:
		/*
		 * This CODEC has overcomplicated input mixing.
		 * Make some cleaning there.
		 */
		/* Disable input mono mixer. Not needed and not supported. */
		w = hdaa_widget_get(devinfo, 43);
		if (w != NULL)
			w->enable = 0;
		/* Disable any with any input mixing mesh. Use separately. */
		w = hdaa_widget_get(devinfo, 39);
		if (w != NULL)
			w->enable = 0;
		w = hdaa_widget_get(devinfo, 40);
		if (w != NULL)
			w->enable = 0;
		w = hdaa_widget_get(devinfo, 41);
		if (w != NULL)
			w->enable = 0;
		w = hdaa_widget_get(devinfo, 42);
		if (w != NULL)
			w->enable = 0;
		/* Disable duplicate mixer node connector. */
		w = hdaa_widget_get(devinfo, 15);
		if (w != NULL)
			w->connsenable[3] = 0;
		/* There is only one mic preamplifier, use it effectively. */
		w = hdaa_widget_get(devinfo, 31);
		if (w != NULL) {
			if ((w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) ==
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN) {
				w = hdaa_widget_get(devinfo, 16);
				if (w != NULL)
				    w->connsenable[2] = 0;
			} else {
				w = hdaa_widget_get(devinfo, 15);
				if (w != NULL)
				    w->connsenable[0] = 0;
			}
		}
		w = hdaa_widget_get(devinfo, 32);
		if (w != NULL) {
			if ((w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MASK) ==
			    HDA_CONFIG_DEFAULTCONF_DEVICE_MIC_IN) {
				w = hdaa_widget_get(devinfo, 16);
				if (w != NULL)
				    w->connsenable[0] = 0;
			} else {
				w = hdaa_widget_get(devinfo, 15);
				if (w != NULL)
				    w->connsenable[1] = 0;
			}
		}

		if (subid == ASUS_A8X_SUBVENDOR) {
			/*
			 * This is just plain ridiculous.. There
			 * are several A8 series that share the same
			 * pci id but works differently (EAPD).
			 */
			w = hdaa_widget_get(devinfo, 26);
			if (w != NULL && w->type ==
			    HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_PIN_COMPLEX &&
			    (w->wclass.pin.config &
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_MASK) !=
			    HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_NONE)
				devinfo->quirks &=
				    ~HDAA_QUIRK_EAPDINV;
		}
		break;
	case HDA_CODEC_AD1981HD:
		/*
		 * This CODEC has very unusual design with several
		 * points inappropriate for the present parser.
		 */
		/* Disable recording from mono playback mix. */
		w = hdaa_widget_get(devinfo, 21);
		if (w != NULL)
			w->connsenable[3] = 0;
		/* Disable rear to front mic mixer, use separately. */
		w = hdaa_widget_get(devinfo, 31);
		if (w != NULL)
			w->enable = 0;
		/* Disable direct playback, use mixer. */
		w = hdaa_widget_get(devinfo, 5);
		if (w != NULL)
			w->connsenable[0] = 0;
		w = hdaa_widget_get(devinfo, 6);
		if (w != NULL)
			w->connsenable[0] = 0;
		w = hdaa_widget_get(devinfo, 9);
		if (w != NULL)
			w->connsenable[0] = 0;
		w = hdaa_widget_get(devinfo, 24);
		if (w != NULL)
			w->connsenable[0] = 0;
		break;
	case HDA_CODEC_ALC269:
		/*
		 * ASUS EeePC 1001px has strange variant of ALC269 CODEC,
		 * that mutes speaker if unused mixer at NID 15 is muted.
		 * Probably CODEC incorrectly reports internal connections.
		 * Hide that muter from the driver.  There are several CODECs
		 * sharing this ID and I have not enough information about
		 * them to implement more universal solution.
		 */
		if (subid == 0x84371043) {
			w = hdaa_widget_get(devinfo, 15);
			if (w != NULL)
				w->param.inamp_cap = 0;
		}
		break;
	case HDA_CODEC_CX20582:
	case HDA_CODEC_CX20583:
	case HDA_CODEC_CX20584:
	case HDA_CODEC_CX20585:
	case HDA_CODEC_CX20590:
		/*
		 * These codecs have extra connectivity on record side
		 * too reach for the present parser.
		 */
		w = hdaa_widget_get(devinfo, 20);
		if (w != NULL)
			w->connsenable[1] = 0;
		w = hdaa_widget_get(devinfo, 21);
		if (w != NULL)
			w->connsenable[1] = 0;
		w = hdaa_widget_get(devinfo, 22);
		if (w != NULL)
			w->connsenable[0] = 0;
		break;
	case HDA_CODEC_VT1708S_0:
	case HDA_CODEC_VT1708S_1:
	case HDA_CODEC_VT1708S_2:
	case HDA_CODEC_VT1708S_3:
	case HDA_CODEC_VT1708S_4:
	case HDA_CODEC_VT1708S_5:
	case HDA_CODEC_VT1708S_6:
	case HDA_CODEC_VT1708S_7:
		/*
		 * These codecs have hidden mic boost controls.
		 */
		w = hdaa_widget_get(devinfo, 26);
		if (w != NULL)
			w->param.inamp_cap =
			    (40 << HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE_SHIFT) |
			    (3 << HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS_SHIFT) |
			    (0 << HDA_PARAM_OUTPUT_AMP_CAP_OFFSET_SHIFT);
		w = hdaa_widget_get(devinfo, 30);
		if (w != NULL)
			w->param.inamp_cap =
			    (40 << HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE_SHIFT) |
			    (3 << HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS_SHIFT) |
			    (0 << HDA_PARAM_OUTPUT_AMP_CAP_OFFSET_SHIFT);
		break;
	}
}

static uint32_t
hdaa_read_coef(device_t dev, nid_t nid, uint16_t idx)
{

	hda_command(dev, HDA_CMD_SET_COEFF_INDEX(0, nid, idx));
	return (hda_command(dev, HDA_CMD_GET_PROCESSING_COEFF(0, nid)));
}

static uint32_t
hdaa_write_coef(device_t dev, nid_t nid, uint16_t idx, uint16_t val)
{

	hda_command(dev, HDA_CMD_SET_COEFF_INDEX(0, nid, idx));
	return (hda_command(dev, HDA_CMD_SET_PROCESSING_COEFF(0, nid, val)));
}

void
hdaa_patch_direct(struct hdaa_devinfo *devinfo)
{
	device_t dev = devinfo->dev;
	uint32_t id, subid, val;

	id = hdaa_codec_id(devinfo);
	subid = hdaa_card_id(devinfo);

	switch (id) {
	case HDA_CODEC_VT1708S_0:
	case HDA_CODEC_VT1708S_1:
	case HDA_CODEC_VT1708S_2:
	case HDA_CODEC_VT1708S_3:
	case HDA_CODEC_VT1708S_4:
	case HDA_CODEC_VT1708S_5:
	case HDA_CODEC_VT1708S_6:
	case HDA_CODEC_VT1708S_7:
		/* Enable Mic Boost Volume controls. */
		hda_command(dev, HDA_CMD_12BIT(0, devinfo->nid,
		    0xf98, 0x01));
		/* Fall though */
	case HDA_CODEC_VT1818S:
		/* Don't bypass mixer. */
		hda_command(dev, HDA_CMD_12BIT(0, devinfo->nid,
		    0xf88, 0xc0));
		break;
	case HDA_CODEC_ALC1150:
		if (subid == 0xd9781462) {
			/* Too low volume on MSI H170 GAMING M3. */
			hdaa_write_coef(dev, 0x20, 0x07, 0x7cb);
		}
		break;
	}
	if (subid == APPLE_INTEL_MAC)
		hda_command(dev, HDA_CMD_12BIT(0, devinfo->nid,
		    0x7e7, 0));
	if (id == HDA_CODEC_ALC269) {
		if (subid == 0x16e31043 || subid == 0x831a1043 ||
		    subid == 0x834a1043 || subid == 0x83981043 ||
		    subid == 0x83ce1043) {
			/*
			 * The ditital mics on some Asus laptops produce
			 * differential signals instead of expected stereo.
			 * That results in silence if downmix it to mono.
			 * To workaround, make codec to handle signal as mono.
			 */
			val = hdaa_read_coef(dev, 0x20, 0x07);
			hdaa_write_coef(dev, 0x20, 0x07, val|0x80);
		}
		if (subid == 0x15171043) {
			/* Increase output amp on ASUS UX31A by +5dB. */
			hdaa_write_coef(dev, 0x20, 0x12, 0x2800);
		}
	}
}
