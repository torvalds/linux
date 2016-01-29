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
 * @TUNER_PAD_OUTPUT:	Tuner video output source pad. Contains the video
 *			chrominance and luminance or the hole bandwidth
 *			of the signal converted to an Intermediate Frequency
 *			(IF) or to baseband (on zero-IF tuners).
 * @TUNER_PAD_AUD_OUT:	Tuner audio output source pad. Tuners used to decode
 *			analog TV signals have an extra pad for audio output.
 *			Old tuners use an analog stage with a saw filter for
 *			the audio IF frequency. The output of the pad is, in
 *			this case, the audio IF, with should be decoded either
 *			by the bridge chipset (that's the case of cx2388x
 *			chipsets) or may require an external IF sound
 *			processor, like msp34xx. On modern silicon tuners,
 *			the audio IF decoder is usually incorporated at the
 *			tuner. On such case, the output of this pad is an
 *			audio sampled data.
 * @TUNER_NUM_PADS:	Number of pads of the tuner.
 */
enum tuner_pad_index {
	TUNER_PAD_RF_INPUT,
	TUNER_PAD_OUTPUT,
	TUNER_PAD_AUD_OUT,
	TUNER_NUM_PADS
};
