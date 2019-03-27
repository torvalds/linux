/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Konstantin Dimitrov <kosio.dimitrov@gmail.com>
 * Copyright (c) 2001 Katsurajima Naoto <raven@katsurajima.seya.yokohama.jp>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <dev/sound/pci/spicds.h>

static MALLOC_DEFINE(M_SPICDS, "spicds", "SPI codec");

#define SPICDS_NAMELEN	16
struct spicds_info {
	device_t dev;
	spicds_ctrl ctrl;
	void *devinfo;
	int num; /* number of this device */
	unsigned int type;   /* codec type */
	unsigned int cif;    /* Controll data Interface Format (0/1) */
	unsigned int format; /* data format and master clock frequency */
	unsigned int dvc;    /* De-emphasis and Volume Control */
	unsigned int left, right;
	char name[SPICDS_NAMELEN];
	struct mtx *lock;
};

static void
spicds_wrbit(struct spicds_info *codec, int bit)
{
	unsigned int cs, cdti;
	if (codec->cif)
		cs = 1;
	else
		cs = 0;
	if (bit)
		cdti = 1;
	else
		cdti = 0;
	codec->ctrl(codec->devinfo, cs, 0, cdti);
	DELAY(1);
	codec->ctrl(codec->devinfo, cs, 1, cdti);
	DELAY(1);

	return;
}

static void
spicds_wrcd(struct spicds_info *codec, int reg, u_int16_t val)
{
	int mask;

#if(0)
	device_printf(codec->dev, "spicds_wrcd(codec, 0x%02x, 0x%02x)\n", reg, val);
#endif
	/* start */
	if (codec->cif)
		codec->ctrl(codec->devinfo, 1, 1, 0);
	else
		codec->ctrl(codec->devinfo, 0, 1, 0);
	DELAY(1);
	if (codec->type != SPICDS_TYPE_WM8770) {
	if (codec->type == SPICDS_TYPE_AK4381) {
	/* AK4381 chip address */
        spicds_wrbit(codec, 0);
        spicds_wrbit(codec, 1);
	}
	else if (codec->type == SPICDS_TYPE_AK4396)
	{
	/* AK4396 chip address */
        spicds_wrbit(codec, 0);
        spicds_wrbit(codec, 0);
	}
	else {
	/* chip address */
	spicds_wrbit(codec, 1);
	spicds_wrbit(codec, 0);
	}
	/* write */
	spicds_wrbit(codec, 1);
	/* register address */
	for (mask = 0x10; mask != 0; mask >>= 1)
		spicds_wrbit(codec, reg & mask);
	/* data */
	for (mask = 0x80; mask != 0; mask >>= 1)
		spicds_wrbit(codec, val & mask);
	/* stop */
	DELAY(1);
	}
	else {
        /* register address */
        for (mask = 0x40; mask != 0; mask >>= 1)
                spicds_wrbit(codec, reg & mask);
        /* data */
        for (mask = 0x100; mask != 0; mask >>= 1)
                spicds_wrbit(codec, val & mask);
        /* stop */
        DELAY(1);
	}
	if (codec->cif) {
		codec->ctrl(codec->devinfo, 0, 1, 0);
		DELAY(1);
		codec->ctrl(codec->devinfo, 1, 1, 0);
	}
	else {
		codec->ctrl(codec->devinfo, 1, 1, 0);
	}

	return;
}

struct spicds_info *
spicds_create(device_t dev, void *devinfo, int num, spicds_ctrl ctrl)
{
	struct spicds_info *codec;

#if(0)
	device_printf(dev, "spicds_create(dev, devinfo, %d, ctrl)\n", num);
#endif
	codec = (struct spicds_info *)malloc(sizeof *codec, M_SPICDS, M_NOWAIT);
	if (codec == NULL)
		return NULL;

	snprintf(codec->name, SPICDS_NAMELEN, "%s:spicds%d", device_get_nameunit(dev), num);
	codec->lock = snd_mtxcreate(codec->name, codec->name);
	codec->dev = dev;
	codec->ctrl = ctrl;
	codec->devinfo = devinfo;
	codec->num = num;
	codec->type = SPICDS_TYPE_AK4524;
	codec->cif = 0;
	codec->format = AK452X_FORMAT_I2S | AK452X_FORMAT_256FSN | AK452X_FORMAT_1X;
	codec->dvc = AK452X_DVC_DEMOFF | AK452X_DVC_ZTM1024 | AK452X_DVC_ZCE;

	return codec;
}

void
spicds_destroy(struct spicds_info *codec)
{
	snd_mtxfree(codec->lock);
	free(codec, M_SPICDS);
}

void
spicds_settype(struct spicds_info *codec, unsigned int type)
{
	snd_mtxlock(codec->lock);
	codec->type = type;
	snd_mtxunlock(codec->lock);
}

void
spicds_setcif(struct spicds_info *codec, unsigned int cif)
{
	snd_mtxlock(codec->lock);
	codec->cif = cif;
	snd_mtxunlock(codec->lock);
}

void
spicds_setformat(struct spicds_info *codec, unsigned int format)
{
	snd_mtxlock(codec->lock);
	codec->format = format;
	snd_mtxunlock(codec->lock);
}

void
spicds_setdvc(struct spicds_info *codec, unsigned int dvc)
{
	snd_mtxlock(codec->lock);
	codec->dvc = dvc;
	snd_mtxunlock(codec->lock);
}

void
spicds_init(struct spicds_info *codec)
{
#if(0)
	device_printf(codec->dev, "spicds_init(codec)\n");
#endif
	snd_mtxlock(codec->lock);
	if (codec->type == SPICDS_TYPE_AK4524 ||\
	    codec->type == SPICDS_TYPE_AK4528) {
		/* power off */
		spicds_wrcd(codec, AK4524_POWER, 0);
		/* set parameter */
		spicds_wrcd(codec, AK4524_FORMAT, codec->format);
		spicds_wrcd(codec, AK4524_DVC, codec->dvc);
		/* power on */
		spicds_wrcd(codec, AK4524_POWER,
		    AK452X_POWER_PWDA | AK452X_POWER_PWAD | AK452X_POWER_PWVR);
		/* free reset register */
		spicds_wrcd(codec, AK4524_RESET,
		    AK452X_RESET_RSDA | AK452X_RESET_RSAD);
	}
	if (codec->type == SPICDS_TYPE_WM8770) {
		/* WM8770 init values are taken from ALSA */

		/* These come first to reduce init pop noise */
		spicds_wrcd(codec, 0x1b, 0x044);	/* ADC Mux (AC'97 source) */
		spicds_wrcd(codec, 0x1c, 0x00B);	/* Out Mux1 (VOUT1 = DAC+AUX, VOUT2 = DAC) */
		spicds_wrcd(codec, 0x1d, 0x009);	/* Out Mux2 (VOUT2 = DAC, VOUT3 = DAC) */

		spicds_wrcd(codec, 0x18, 0x000);	/* All power-up */

		spicds_wrcd(codec, 0x16, 0x122);	/* I2S, normal polarity, 24bit */
		spicds_wrcd(codec, 0x17, 0x022);	/* 256fs, slave mode */

		spicds_wrcd(codec, 0x19, 0x000);	/* -12dB ADC/L */
		spicds_wrcd(codec, 0x1a, 0x000);	/* -12dB ADC/R */
	}
	if (codec->type == SPICDS_TYPE_AK4358)
		spicds_wrcd(codec, 0x00, 0x07);		/* I2S, 24bit, power-up */
	if (codec->type == SPICDS_TYPE_AK4381)
		spicds_wrcd(codec, 0x00, 0x8f);		/* I2S, 24bit, power-up */
	if (codec->type == SPICDS_TYPE_AK4396)
		spicds_wrcd(codec, 0x00, 0x07);		/* I2S, 24bit, power-up */
	snd_mtxunlock(codec->lock);
}

void
spicds_reinit(struct spicds_info *codec)
{
	snd_mtxlock(codec->lock);
	if (codec->type != SPICDS_TYPE_WM8770) {
		/* reset */
		spicds_wrcd(codec, AK4524_RESET, 0);
		/* set parameter */
		spicds_wrcd(codec, AK4524_FORMAT, codec->format);
		spicds_wrcd(codec, AK4524_DVC, codec->dvc);
		/* free reset register */
		spicds_wrcd(codec, AK4524_RESET,
		    AK452X_RESET_RSDA | AK452X_RESET_RSAD);
	} else {
		/* WM8770 reinit */
		/* AK4358 reinit */
		/* AK4381 reinit */
	}
	snd_mtxunlock(codec->lock);
}

void
spicds_set(struct spicds_info *codec, int dir, unsigned int left, unsigned int right)
{
#if(0)
	device_printf(codec->dev, "spicds_set(codec, %d, %d, %d)\n", dir, left, right);
#endif
	snd_mtxlock(codec->lock);
	if (left >= 100)
		if ((codec->type == SPICDS_TYPE_AK4381) || \
		(codec->type == SPICDS_TYPE_AK4396))
			left = 255;
		else
			left = 127;
	else
		switch (codec->type) {
		case SPICDS_TYPE_WM8770:
			left = left + 27;
			break;
		case SPICDS_TYPE_AK4381:
		case SPICDS_TYPE_AK4396:
			left = left * 255 / 100;
			break;
		default:
			left = left * 127 / 100;
		}
	if (right >= 100)
		if ((codec->type == SPICDS_TYPE_AK4381) || \
		(codec->type == SPICDS_TYPE_AK4396))
                        right = 255;
                else
			right  = 127;
	else
		switch (codec->type) {
		case SPICDS_TYPE_WM8770:
                        right = right + 27;
			break;
		case SPICDS_TYPE_AK4381:
		case SPICDS_TYPE_AK4396:
			right = right * 255 / 100;
			break;
                default:
                        right = right * 127 / 100;
		}
	if (dir == PCMDIR_REC && codec->type == SPICDS_TYPE_AK4524) {
#if(0)
		device_printf(codec->dev, "spicds_set(): AK4524(REC) %d/%d\n", left, right);
#endif
		spicds_wrcd(codec, AK4524_LIPGA, left);
		spicds_wrcd(codec, AK4524_RIPGA, right);
	}
	if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_AK4524) {
#if(0)
		device_printf(codec->dev, "spicds_set(): AK4524(PLAY) %d/%d\n", left, right);
#endif
		spicds_wrcd(codec, AK4524_LOATT, left);
		spicds_wrcd(codec, AK4524_ROATT, right);
	}
	if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_AK4528) {
#if(0)
		device_printf(codec->dev, "spicds_set(): AK4528(PLAY) %d/%d\n", left, right);
#endif
		spicds_wrcd(codec, AK4528_LOATT, left);
		spicds_wrcd(codec, AK4528_ROATT, right);
	}
        if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_WM8770) {
#if(0)
                device_printf(codec->dev, "spicds_set(): WM8770(PLAY) %d/%d\n", left, right);
#endif
                spicds_wrcd(codec, WM8770_AOATT_L1, left | WM8770_AOATT_UPDATE);
                spicds_wrcd(codec, WM8770_AOATT_R1, right | WM8770_AOATT_UPDATE);
        }
        if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_AK4358) {
#if(0)
                device_printf(codec->dev, "spicds_set(): AK4358(PLAY) %d/%d\n", left, right);
#endif
                spicds_wrcd(codec, AK4358_LO1ATT, left | AK4358_OATT_ENABLE);
                spicds_wrcd(codec, AK4358_RO1ATT, right | AK4358_OATT_ENABLE);
        }
        if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_AK4381) {
#if(0)
                device_printf(codec->dev, "spicds_set(): AK4381(PLAY) %d/%d\n", left, right);
#endif
                spicds_wrcd(codec, AK4381_LOATT, left);
                spicds_wrcd(codec, AK4381_ROATT, right);
        }

        if (dir == PCMDIR_PLAY && codec->type == SPICDS_TYPE_AK4396) {
#if(0)
                device_printf(codec->dev, "spicds_set(): AK4396(PLAY) %d/%d\n", left, right);
#endif
                spicds_wrcd(codec, AK4396_LOATT, left);
                spicds_wrcd(codec, AK4396_ROATT, right);
        }

	snd_mtxunlock(codec->lock);
}

MODULE_DEPEND(snd_spicds, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_spicds, 1);
