/*
 * camera image capture (abstract) bus driver header
 *
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SOC_CAMERA_H
#define SOC_CAMERA_H

#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/v4l2-device.h>

struct soc_camera_device {
	struct list_head list;
	struct device dev;
	struct device *pdev;		/* Platform device */
	s32 user_width;
	s32 user_height;
	enum v4l2_colorspace colorspace;
	unsigned char iface;		/* Host number */
	unsigned char devnum;		/* Device number per host */
	struct soc_camera_sense *sense;	/* See comment in struct definition */
	struct soc_camera_ops *ops;
	struct video_device *vdev;
	const struct soc_camera_format_xlate *current_fmt;
	struct soc_camera_format_xlate *user_formats;
	int num_user_formats;
	enum v4l2_field field;		/* Preserve field over close() */
	void *host_priv;		/* Per-device host private data */
	/* soc_camera.c private count. Only accessed with .video_lock held */
	int use_count;
	struct mutex video_lock;	/* Protects device data */
};

struct soc_camera_file {
	struct soc_camera_device *icd;
	struct videobuf_queue vb_vidq;
};

struct soc_camera_host {
	struct v4l2_device v4l2_dev;
	struct list_head list;
	unsigned char nr;				/* Host number */
	void *priv;
	const char *drv_name;
	struct soc_camera_host_ops *ops;
};

struct soc_camera_host_ops {
	struct module *owner;
	int (*add)(struct soc_camera_device *);
	void (*remove)(struct soc_camera_device *);
	int (*suspend)(struct soc_camera_device *, pm_message_t);
	int (*resume)(struct soc_camera_device *);
	/*
	 * .get_formats() is called for each client device format, but
	 * .put_formats() is only called once. Further, if any of the calls to
	 * .get_formats() fail, .put_formats() will not be called at all, the
	 * failing .get_formats() must then clean up internally.
	 */
	int (*get_formats)(struct soc_camera_device *, int,
			   struct soc_camera_format_xlate *);
	void (*put_formats)(struct soc_camera_device *);
	int (*cropcap)(struct soc_camera_device *, struct v4l2_cropcap *);
	int (*get_crop)(struct soc_camera_device *, struct v4l2_crop *);
	int (*set_crop)(struct soc_camera_device *, struct v4l2_crop *);
	int (*set_fmt)(struct soc_camera_device *, struct v4l2_format *);
	int (*try_fmt)(struct soc_camera_device *, struct v4l2_format *);
	void (*init_videobuf)(struct videobuf_queue *,
			      struct soc_camera_device *);
	int (*reqbufs)(struct soc_camera_file *, struct v4l2_requestbuffers *);
	int (*querycap)(struct soc_camera_host *, struct v4l2_capability *);
	int (*set_bus_param)(struct soc_camera_device *, __u32);
	int (*get_ctrl)(struct soc_camera_device *, struct v4l2_control *);
	int (*set_ctrl)(struct soc_camera_device *, struct v4l2_control *);
	unsigned int (*poll)(struct file *, poll_table *);
	const struct v4l2_queryctrl *controls;
	int num_controls;
};

#define SOCAM_SENSOR_INVERT_PCLK	(1 << 0)
#define SOCAM_SENSOR_INVERT_MCLK	(1 << 1)
#define SOCAM_SENSOR_INVERT_HSYNC	(1 << 2)
#define SOCAM_SENSOR_INVERT_VSYNC	(1 << 3)
#define SOCAM_SENSOR_INVERT_DATA	(1 << 4)

struct i2c_board_info;

struct soc_camera_link {
	/* Camera bus id, used to match a camera and a bus */
	int bus_id;
	/* Per camera SOCAM_SENSOR_* bus flags */
	unsigned long flags;
	int i2c_adapter_id;
	struct i2c_board_info *board_info;
	const char *module_name;
	void *priv;

	/*
	 * For non-I2C devices platform platform has to provide methods to
	 * add a device to the system and to remove
	 */
	int (*add_device)(struct soc_camera_link *, struct device *);
	void (*del_device)(struct soc_camera_link *);
	/* Optional callbacks to power on or off and reset the sensor */
	int (*power)(struct device *, int);
	int (*reset)(struct device *);
	/*
	 * some platforms may support different data widths than the sensors
	 * native ones due to different data line routing. Let the board code
	 * overwrite the width flags.
	 */
	int (*set_bus_param)(struct soc_camera_link *, unsigned long flags);
	unsigned long (*query_bus_param)(struct soc_camera_link *);
	void (*free_bus)(struct soc_camera_link *);
};

static inline struct soc_camera_device *to_soc_camera_dev(
	const struct device *dev)
{
	return container_of(dev, struct soc_camera_device, dev);
}

static inline struct soc_camera_host *to_soc_camera_host(
	const struct device *dev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);

	return container_of(v4l2_dev, struct soc_camera_host, v4l2_dev);
}

static inline struct soc_camera_link *to_soc_camera_link(
	const struct soc_camera_device *icd)
{
	return icd->dev.platform_data;
}

static inline struct device *to_soc_camera_control(
	const struct soc_camera_device *icd)
{
	return dev_get_drvdata(&icd->dev);
}

static inline struct v4l2_subdev *soc_camera_to_subdev(
	const struct soc_camera_device *icd)
{
	struct device *control = to_soc_camera_control(icd);
	return dev_get_drvdata(control);
}

int soc_camera_host_register(struct soc_camera_host *ici);
void soc_camera_host_unregister(struct soc_camera_host *ici);

const struct soc_camera_format_xlate *soc_camera_xlate_by_fourcc(
	struct soc_camera_device *icd, unsigned int fourcc);

/**
 * struct soc_camera_format_xlate - match between host and sensor formats
 * @code: code of a sensor provided format
 * @host_fmt: host format after host translation from code
 *
 * Host and sensor translation structure. Used in table of host and sensor
 * formats matchings in soc_camera_device. A host can override the generic list
 * generation by implementing get_formats(), and use it for format checks and
 * format setup.
 */
struct soc_camera_format_xlate {
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *host_fmt;
};

struct soc_camera_ops {
	int (*suspend)(struct soc_camera_device *, pm_message_t state);
	int (*resume)(struct soc_camera_device *);
	unsigned long (*query_bus_param)(struct soc_camera_device *);
	int (*set_bus_param)(struct soc_camera_device *, unsigned long);
	int (*enum_input)(struct soc_camera_device *, struct v4l2_input *);
	const struct v4l2_queryctrl *controls;
	int num_controls;
};

#define SOCAM_SENSE_PCLK_CHANGED	(1 << 0)

/**
 * This struct can be attached to struct soc_camera_device by the host driver
 * to request sense from the camera, for example, when calling .set_fmt(). The
 * host then can check which flags are set and verify respective values if any.
 * For example, if SOCAM_SENSE_PCLK_CHANGED is set, it means, pixclock has
 * changed during this operation. After completion the host should detach sense.
 *
 * @flags		ored SOCAM_SENSE_* flags
 * @master_clock	if the host wants to be informed about pixel-clock
 *			change, it better set master_clock.
 * @pixel_clock_max	maximum pixel clock frequency supported by the host,
 *			camera is not allowed to exceed this.
 * @pixel_clock		if the camera driver changed pixel clock during this
 *			operation, it sets SOCAM_SENSE_PCLK_CHANGED, uses
 *			master_clock to calculate the new pixel-clock and
 *			sets this field.
 */
struct soc_camera_sense {
	unsigned long flags;
	unsigned long master_clock;
	unsigned long pixel_clock_max;
	unsigned long pixel_clock;
};

static inline struct v4l2_queryctrl const *soc_camera_find_qctrl(
	struct soc_camera_ops *ops, int id)
{
	int i;

	for (i = 0; i < ops->num_controls; i++)
		if (ops->controls[i].id == id)
			return &ops->controls[i];

	return NULL;
}

#define SOCAM_MASTER			(1 << 0)
#define SOCAM_SLAVE			(1 << 1)
#define SOCAM_HSYNC_ACTIVE_HIGH		(1 << 2)
#define SOCAM_HSYNC_ACTIVE_LOW		(1 << 3)
#define SOCAM_VSYNC_ACTIVE_HIGH		(1 << 4)
#define SOCAM_VSYNC_ACTIVE_LOW		(1 << 5)
#define SOCAM_DATAWIDTH_4		(1 << 6)
#define SOCAM_DATAWIDTH_8		(1 << 7)
#define SOCAM_DATAWIDTH_9		(1 << 8)
#define SOCAM_DATAWIDTH_10		(1 << 9)
#define SOCAM_DATAWIDTH_15		(1 << 10)
#define SOCAM_DATAWIDTH_16		(1 << 11)
#define SOCAM_PCLK_SAMPLE_RISING	(1 << 12)
#define SOCAM_PCLK_SAMPLE_FALLING	(1 << 13)
#define SOCAM_DATA_ACTIVE_HIGH		(1 << 14)
#define SOCAM_DATA_ACTIVE_LOW		(1 << 15)

#define SOCAM_DATAWIDTH_MASK (SOCAM_DATAWIDTH_4 | SOCAM_DATAWIDTH_8 | \
			      SOCAM_DATAWIDTH_9 | SOCAM_DATAWIDTH_10 | \
			      SOCAM_DATAWIDTH_15 | SOCAM_DATAWIDTH_16)

static inline unsigned long soc_camera_bus_param_compatible(
			unsigned long camera_flags, unsigned long bus_flags)
{
	unsigned long common_flags, hsync, vsync, pclk, data, buswidth, mode;

	common_flags = camera_flags & bus_flags;

	hsync = common_flags & (SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_HSYNC_ACTIVE_LOW);
	vsync = common_flags & (SOCAM_VSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW);
	pclk = common_flags & (SOCAM_PCLK_SAMPLE_RISING | SOCAM_PCLK_SAMPLE_FALLING);
	data = common_flags & (SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATA_ACTIVE_LOW);
	mode = common_flags & (SOCAM_MASTER | SOCAM_SLAVE);
	buswidth = common_flags & SOCAM_DATAWIDTH_MASK;

	return (!hsync || !vsync || !pclk || !data || !mode || !buswidth) ? 0 :
		common_flags;
}

static inline void soc_camera_limit_side(unsigned int *start,
		unsigned int *length, unsigned int start_min,
		unsigned int length_min, unsigned int length_max)
{
	if (*length < length_min)
		*length = length_min;
	else if (*length > length_max)
		*length = length_max;

	if (*start < start_min)
		*start = start_min;
	else if (*start > start_min + length_max - *length)
		*start = start_min + length_max - *length;
}

extern unsigned long soc_camera_apply_sensor_flags(struct soc_camera_link *icl,
						   unsigned long flags);

#endif
