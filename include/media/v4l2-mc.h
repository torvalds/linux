/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * v4l2-mc.h - Media Controller V4L2 types and prototypes
 *
 * Copyright (C) 2016 Mauro Carvalho Chehab <mchehab@kernel.org>
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (c) 2016 Intel Corporation.
 */

#ifndef _V4L2_MC_H
#define _V4L2_MC_H

#include <media/media-device.h>
#include <media/v4l2-dev.h>
#include <linux/types.h>

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
 *
 * .. note::
 *
 *    Webcams are modelled on a very simple way: the sensor is
 *    connected directly to the I/O entity. All dirty details, like
 *    scaler and crop HW are hidden. While such mapping is enough for v4l2
 *    interface centric PC-consumer's hardware, V4L2 subdev centric camera
 *    hardware should not use this routine, as it will not build the right graph.
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
 * v4l2_pipeline_pm_get - Increase the use count of a pipeline
 * @entity: The root entity of a pipeline
 *
 * Update the use count of all entities in the pipeline and power entities on.
 *
 * This function is intended to be called in video node open. It uses
 * struct media_entity.use_count to track the power status. The use
 * of this function should be paired with v4l2_pipeline_link_notify().
 *
 * Return 0 on success or a negative error code on failure.
 */
int v4l2_pipeline_pm_get(struct media_entity *entity);

/**
 * v4l2_pipeline_pm_put - Decrease the use count of a pipeline
 * @entity: The root entity of a pipeline
 *
 * Update the use count of all entities in the pipeline and power entities off.
 *
 * This function is intended to be called in video node release. It uses
 * struct media_entity.use_count to track the power status. The use
 * of this function should be paired with v4l2_pipeline_link_notify().
 */
void v4l2_pipeline_pm_put(struct media_entity *entity);


/**
 * v4l2_pipeline_link_notify - Link management notification callback
 * @link: The link
 * @flags: New link flags that will be applied
 * @notification: The link's state change notification type (MEDIA_DEV_NOTIFY_*)
 *
 * React to link management on powered pipelines by updating the use count of
 * all entities in the source and sink sides of the link. Entities are powered
 * on or off accordingly. The use of this function should be paired
 * with v4l2_pipeline_pm_{get,put}().
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

static inline int v4l2_pipeline_pm_get(struct media_entity *entity)
{
	return 0;
}

static inline void v4l2_pipeline_pm_put(struct media_entity *entity)
{}

static inline int v4l2_pipeline_link_notify(struct media_link *link, u32 flags,
					    unsigned int notification)
{
	return 0;
}

#endif /* CONFIG_MEDIA_CONTROLLER */
#endif /* _V4L2_MC_H */
