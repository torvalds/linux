/*
 * v4l2-mc.h - Media Controller V4L2 types and prototypes
 *
 * Copyright (C) 2016 Mauro Carvalho Chehab <mchehab@osg.samsung.com>
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (c) 2016 Intel Corporation.
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

#ifndef _V4L2_MC_H
#define _V4L2_MC_H

#include <media/media-device.h>
#include <media/v4l2-dev.h>
#include <linux/types.h>

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

/**
 * enum demod_pad_index - analog TV pad index for MEDIA_ENT_F_ATV_DECODER
 *
 * @DEMOD_PAD_IF_INPUT:	IF input sink pad.
 * @DEMOD_PAD_VID_OUT:	Video output source pad.
 * @DEMOD_PAD_VBI_OUT:	Vertical Blank Interface (VBI) output source pad.
 * @DEMOD_PAD_AUDIO_OUT: Audio output source pad.
 * @DEMOD_NUM_PADS:	Maximum number of output pads.
 */
enum demod_pad_index {
	DEMOD_PAD_IF_INPUT,
	DEMOD_PAD_VID_OUT,
	DEMOD_PAD_VBI_OUT,
	DEMOD_PAD_AUDIO_OUT,
	DEMOD_NUM_PADS
};

/* We don't need to include pci.h or usb.h here */
struct pci_dev;
struct usb_device;

#ifdef CONFIG_MEDIA_CONTROLLER
/**
 * v4l2_mc_create_media_graph() - create Media Controller links at the graph.
 *
 * @mdev:	pointer to the &media_device struct.
 *
 * Add links between the entities commonly found on PC customer's hardware at
 * the V4L2 side: camera sensors, audio and video PLL-IF decoders, tuners,
 * analog TV decoder and I/O entities (video, VBI and Software Defined Radio).
 * NOTE: webcams are modelled on a very simple way: the sensor is
 * connected directly to the I/O entity. All dirty details, like
 * scaler and crop HW are hidden. While such mapping is enough for v4l2
 * interface centric PC-consumer's hardware, V4L2 subdev centric camera
 * hardware should not use this routine, as it will not build the right graph.
 */
int v4l2_mc_create_media_graph(struct media_device *mdev);

/**
 * v4l_enable_media_source() -	Hold media source for exclusive use
 *				if free
 *
 * @vdev:	pointer to struct video_device
 *
 * This interface calls enable_source handler to determine if
 * media source is free for use. The enable_source handler is
 * responsible for checking is the media source is free and
 * start a pipeline between the media source and the media
 * entity associated with the video device. This interface
 * should be called from v4l2-core and dvb-core interfaces
 * that change the source configuration.
 *
 * Return: returns zero on success or a negative error code.
 */
int v4l_enable_media_source(struct video_device *vdev);

/**
 * v4l_disable_media_source() -	Release media source
 *
 * @vdev:	pointer to struct video_device
 *
 * This interface calls disable_source handler to release
 * the media source. The disable_source handler stops the
 * active media pipeline between the media source and the
 * media entity associated with the video device.
 *
 * Return: returns zero on success or a negative error code.
 */
void v4l_disable_media_source(struct video_device *vdev);

/*
 * v4l_vb2q_enable_media_tuner -  Hold media source for exclusive use
 *				  if free.
 * @q - pointer to struct vb2_queue
 *
 * Wrapper for v4l_enable_media_source(). This function should
 * be called from v4l2-core to enable the media source with
 * pointer to struct vb2_queue as the input argument. Some
 * v4l2-core interfaces don't have access to video device and
 * this interface finds the struct video_device for the q and
 * calls v4l_enable_media_source().
 */
int v4l_vb2q_enable_media_source(struct vb2_queue *q);


/**
 * v4l2_pipeline_pm_use - Update the use count of an entity
 * @entity: The entity
 * @use: Use (1) or stop using (0) the entity
 *
 * Update the use count of all entities in the pipeline and power entities on or
 * off accordingly.
 *
 * This function is intended to be called in video node open (use ==
 * 1) and release (use == 0). It uses struct media_entity.use_count to
 * track the power status. The use of this function should be paired
 * with v4l2_pipeline_link_notify().
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. No failure can occur when the use parameter is
 * set to 0.
 */
int v4l2_pipeline_pm_use(struct media_entity *entity, int use);


/**
 * v4l2_pipeline_link_notify - Link management notification callback
 * @link: The link
 * @flags: New link flags that will be applied
 * @notification: The link's state change notification type (MEDIA_DEV_NOTIFY_*)
 *
 * React to link management on powered pipelines by updating the use count of
 * all entities in the source and sink sides of the link. Entities are powered
 * on or off accordingly. The use of this function should be paired
 * with v4l2_pipeline_pm_use().
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. This function will not fail for disconnection
 * events.
 */
int v4l2_pipeline_link_notify(struct media_link *link, u32 flags,
			      unsigned int notification);

#else /* CONFIG_MEDIA_CONTROLLER */

static inline int v4l2_mc_create_media_graph(struct media_device *mdev)
{
	return 0;
}

static inline int v4l_enable_media_source(struct video_device *vdev)
{
	return 0;
}

static inline void v4l_disable_media_source(struct video_device *vdev)
{
}

static inline int v4l_vb2q_enable_media_source(struct vb2_queue *q)
{
	return 0;
}

static inline int v4l2_pipeline_pm_use(struct media_entity *entity, int use)
{
	return 0;
}

static inline int v4l2_pipeline_link_notify(struct media_link *link, u32 flags,
					    unsigned int notification)
{
	return 0;
}

#endif /* CONFIG_MEDIA_CONTROLLER */
#endif /* _V4L2_MC_H */
