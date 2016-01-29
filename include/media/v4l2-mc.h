/*
 * v4l2-mc.h - Media Controller V4L2 types and prototypes
 *
 * Copyright (C) 2016 Mauro Carvalho Chehab <mchehab@osg.samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/**
 * enum tuner_pad_index - tuner pad index for MEDIA_ENT_F_TUNER
 *
 * @TUNER_PAD_RF_INPUT:	Radiofrequency (RF) sink pad, usually linked to a
 *			RF connector entity.
 * @TUNER_PAD_OUTPUT:	Tuner output pad. This is actually more complex than
 *			a single pad output, as, in addition to luminance and
 *			chrominance IF a tuner may have internally an
 *			audio decoder (like xc3028) or it may produce an audio
 *			IF that will be used by an audio decoder like msp34xx.
 *			It may also have an IF-PLL demodulator on it, like
 *			tuners with tda9887. Yet, currently, we don't need to
 *			represent all the dirty details, as this is transparent
 *			for the V4L2 API usage. So, let's represent all kinds
 *			of different outputs as a single source pad.
 * @TUNER_NUM_PADS:	Number of pads of the tuner.
 */
enum tuner_pad_index {
	TUNER_PAD_RF_INPUT,
	TUNER_PAD_OUTPUT,
	TUNER_NUM_PADS
};