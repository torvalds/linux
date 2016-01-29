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

/**
 * enum if_vid_dec_index - video IF-PLL pad index for
 *			   MEDIA_ENT_F_IF_VID_DECODER
 *
 * @IF_VID_DEC_PAD_IF_INPUT:	video Intermediate Frequency (IF) sink pad
 * @IF_VID_DEC_PAD_OUT:		IF-PLL video output source pad. Contains the
 *				video chrominance and luminance IF signals.
 * @IF_VID_DEC_PAD_NUM_PADS:	Number of pads of the video IF-PLL.
 */
enum if_vid_dec_pad_index {
	IF_VID_DEC_PAD_IF_INPUT,
	IF_VID_DEC_PAD_OUT,
	IF_VID_DEC_PAD_NUM_PADS
};

/**
 * enum if_aud_dec_index - audio/sound IF-PLL pad index for
 *			   MEDIA_ENT_F_IF_AUD_DECODER
 *
 * @IF_AUD_DEC_PAD_IF_INPUT:	audio Intermediate Frequency (IF) sink pad
 * @IF_AUD_DEC_PAD_OUT:		IF-PLL audio output source pad. Contains the
 *				audio sampled stream data, usually connected
 *				to the bridge bus via an Inter-IC Sound (I2S)
 *				bus.
 * @IF_AUD_DEC_PAD_NUM_PADS:	Number of pads of the audio IF-PLL.
 */
enum if_aud_dec_pad_index {
	IF_AUD_DEC_PAD_IF_INPUT,
	IF_AUD_DEC_PAD_OUT,
	IF_AUD_DEC_PAD_NUM_PADS
};
