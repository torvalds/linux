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

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

struct file;
struct soc_camera_desc;

struct soc_camera_device {
	struct list_head list;		/* list of all registered devices */
	struct soc_camera_desc *sdesc;
	struct device *pdev;		/* Platform device */
	struct device *parent;		/* Camera host device */
	struct device *control;		/* E.g., the i2c client */
	s32 user_width;
	s32 user_height;
	u32 bytesperline;		/* for padding, zero if unused */
	u32 sizeimage;
	enum v4l2_colorspace colorspace;
	unsigned char iface;		/* Host number */
	unsigned char devnum;		/* Device number per host */
	struct soc_camera_sense *sense;	/* See comment in struct definition */
	struct video_device *vdev;
	struct v4l2_ctrl_handler ctrl_handler;
	const struct soc_camera_format_xlate *current_fmt;
	struct soc_camera_format_xlate *user_formats;
	int num_user_formats;
	enum v4l2_field field;		/* Preserve field over close() */
	void *host_priv;		/* Per-device host private data */
	/* soc_camera.c private count. Only accessed with .host_lock held */
	int use_count;
	struct file *streamer;		/* stream owner */
	struct v4l2_clk *clk;
	union {
		struct videobuf_queue vb_vidq;
		struct vb2_queue vb2_vidq;
	};
};

/* Host supports programmable stride */
#define SOCAM_HOST_CAP_STRIDE		(1 << 0)

struct soc_camera_host {
	struct v4l2_device v4l2_dev;
	struct list_head list;
	struct mutex host_lock;		/* Protect pipeline modifications */
	unsigned char nr;		/* Host number */
	u32 capabilities;
	struct soc_camera_device *icd;	/* Currently attached client */
	void *priv;
	const char *drv_name;
	struct soc_camera_host_ops *ops;
};

struct soc_camera_host_ops {
	struct module *owner;
	int (*add)(struct soc_camera_device *);
	void (*remove)(struct soc_camera_device *);
	int (*clock_start)(struct soc_camera_host *);
	void (*clock_stop)(struct soc_camera_host *);
	/*
	 * .get_formats() is called for each client device format, but
	 * .put_formats() is only called once. Further, if any of the calls to
	 * .get_formats() fail, .put_formats() will not be called at all, the
	 * failing .get_formats() must then clean up internally.
	 */
	int (*get_formats)(struct soc_camera_device *, unsigned int,
			   struct soc_camera_format_xlate *);
	void (*put_formats)(struct soc_camera_device *);
	int (*cropcap)(struct soc_camera_device *, struct v4l2_cropcap *);
	int (*get_crop)(struct soc_camera_device *, struct v4l2_crop *);
	int (*set_crop)(struct soc_camera_device *, const struct v4l2_crop *);
	int (*get_selection)(struct soc_camera_device *, struct v4l2_selection *);
	int (*set_selection)(struct soc_camera_device *, struct v4l2_selection *);
	/*
	 * The difference to .set_crop() is, that .set_livecrop is not allowed
	 * to change the output sizes
	 */
	int (*set_livecrop)(struct soc_camera_device *, const struct v4l2_crop *);
	int (*set_fmt)(struct soc_camera_device *, struct v4l2_format *);
	int (*try_fmt)(struct soc_camera_device *, struct v4l2_format *);
	void (*init_videobuf)(struct videobuf_queue *,
			      struct soc_camera_device *);
	int (*init_videobuf2)(struct vb2_queue *,
			      struct soc_camera_device *);
	int (*reqbufs)(struct soc_camera_device *, struct v4l2_requestbuffers *);
	int (*querycap)(struct soc_camera_host *, struct v4l2_capability *);
	int (*set_bus_param)(struct soc_camera_device *);
	int (*get_parm)(struct soc_camera_device *, struct v4l2_streamparm *);
	int (*set_parm)(struct soc_camera_device *, struct v4l2_streamparm *);
	int (*enum_framesizes)(struct soc_camera_device *, struct v4l2_frmsizeenum *);
	unsigned int (*poll)(struct file *, poll_table *);
};

#define SOCAM_SENSOR_INVERT_PCLK	(1 << 0)
#define SOCAM_SENSOR_INVERT_MCLK	(1 << 1)
#define SOCAM_SENSOR_INVERT_HSYNC	(1 << 2)
#define SOCAM_SENSOR_INVERT_VSYNC	(1 << 3)
#define SOCAM_SENSOR_INVERT_DATA	(1 << 4)

struct i2c_board_info;
struct regulator_bulk_data;

struct soc_camera_subdev_desc {
	/* Per camera SOCAM_SENSOR_* bus flags */
	unsigned long flags;

	/* sensor driver private platform data */
	void *drv_priv;

	/* Optional regulators that have to be managed on power on/off events */
	struct regulator_bulk_data *regulators;
	int num_regulators;

	/* Optional callbacks to power on or off and reset the sensor */
	int (*power)(struct device *, int);
	int (*reset)(struct device *);

	/*
	 * some platforms may support different data widths than the sensors
	 * native ones due to different data line routing. Let the board code
	 * overwrite the width flags.
	 */
	int (*set_bus_param)(struct soc_camera_subdev_desc *, unsigned long flags);
	unsigned long (*query_bus_param)(struct soc_camera_subdev_desc *);
	void (*free_bus)(struct soc_camera_subdev_desc *);
};

struct soc_camera_host_desc {
	/* Camera bus id, used to match a camera and a bus */
	int bus_id;
	int i2c_adapter_id;
	struct i2c_board_info *board_info;
	const char *module_name;

	/*
	 * For non-I2C devices platform has to provide methods to add a device
	 * to the system and to remove it
	 */
	int (*add_device)(struct soc_camera_device *);
	void (*del_device)(struct soc_camera_device *);
};

/*
 * This MUST be kept binary-identical to struct soc_camera_link below, until
 * it is completely replaced by this one, after which we can split it into its
 * two components.
 */
struct soc_camera_desc {
	struct soc_camera_subdev_desc subdev_desc;
	struct soc_camera_host_desc host_desc;
};

/* Prepare to replace this struct: don't change its layout any more! */
struct soc_camera_link {
	/*
	 * Subdevice part - keep at top and compatible to
	 * struct soc_camera_subdev_desc
	 */

	/* Per camera SOCAM_SENSOR_* bus flags */
	unsigned long flags;

	void *priv;

	/* Optional regulators that have to be managed on power on/off events */
	struct regulator_bulk_data *regulators;
	int num_regulators;

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

	/*
	 * Host part - keep at bottom and compatible to
	 * struct soc_camera_host_desc
	 */

	/* Camera bus id, used to match a camera and a bus */
	int bus_id;
	int i2c_adapter_id;
	struct i2c_board_info *board_info;
	const char *module_name;

	/*
	 * For non-I2C devices platform has to provide methods to add a device
	 * to the system and to remove it
	 */
	int (*add_device)(struct soc_camera_device *);
	void (*del_device)(struct soc_camera_device *);
};

static inline struct soc_camera_host *to_soc_camera_host(
	const struct device *dev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);

	return container_of(v4l2_dev, struct soc_camera_host, v4l2_dev);
}

static inline struct soc_camera_desc *to_soc_camera_desc(
	const struct soc_camera_device *icd)
{
	return icd->sdesc;
}

static inline struct device *to_soc_camera_control(
	const struct soc_camera_device *icd)
{
	return icd->control;
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

#define SOCAM_DATAWIDTH(x)	BIT((x) - 1)
#define SOCAM_DATAWIDTH_4	SOCAM_DATAWIDTH(4)
#define SOCAM_DATAWIDTH_8	SOCAM_DATAWIDTH(8)
#define SOCAM_DATAWIDTH_9	SOCAM_DATAWIDTH(9)
#define SOCAM_DATAWIDTH_10	SOCAM_DATAWIDTH(10)
#define SOCAM_DATAWIDTH_12	SOCAM_DATAWIDTH(12)
#define SOCAM_DATAWIDTH_15	SOCAM_DATAWIDTH(15)
#define SOCAM_DATAWIDTH_16	SOCAM_DATAWIDTH(16)
#define SOCAM_DATAWIDTH_18	SOCAM_DATAWIDTH(18)
#define SOCAM_DATAWIDTH_24	SOCAM_DATAWIDTH(24)

#define SOCAM_DATAWIDTH_MASK (SOCAM_DATAWIDTH_4 | SOCAM_DATAWIDTH_8 | \
			      SOCAM_DATAWIDTH_9 | SOCAM_DATAWIDTH_10 | \
			      SOCAM_DATAWIDTH_12 | SOCAM_DATAWIDTH_15 | \
			      SOCAM_DATAWIDTH_16 | SOCAM_DATAWIDTH_18 | \
			      SOCAM_DATAWIDTH_24)

static inline void soc_camera_limit_side(int *start, int *length,
		unsigned int start_min,
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

unsigned long soc_camera_apply_board_flags(struct soc_camera_subdev_desc *ssdd,
					   const struct v4l2_mbus_config *cfg);

int soc_camera_power_on(struct device *dev, struct soc_camera_subdev_desc *ssdd,
			struct v4l2_clk *clk);
int soc_camera_power_off(struct device *dev, struct soc_camera_subdev_desc *ssdd,
			 struct v4l2_clk *clk);

static inline int soc_camera_set_power(struct device *dev,
		struct soc_camera_subdev_desc *ssdd, struct v4l2_clk *clk, bool on)
{
	return on ? soc_camera_power_on(dev, ssdd, clk)
		  : soc_camera_power_off(dev, ssdd, clk);
}

/* This is only temporary here - until v4l2-subdev begins to link to video_device */
#include <linux/i2c.h>
static inline struct video_device *soc_camera_i2c_to_vdev(const struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct soc_camera_device *icd = v4l2_get_subdev_hostdata(sd);
	return icd ? icd->vdev : NULL;
}

static inline struct soc_camera_subdev_desc *soc_camera_i2c_to_desc(const struct i2c_client *client)
{
	return client->dev.platform_data;
}

static inline struct v4l2_subdev *soc_camera_vdev_to_subdev(struct video_device *vdev)
{
	struct soc_camera_device *icd = video_get_drvdata(vdev);
	return soc_camera_to_subdev(icd);
}

static inline struct soc_camera_device *soc_camera_from_vb2q(const struct vb2_queue *vq)
{
	return container_of(vq, struct soc_camera_device, vb2_vidq);
}

static inline struct soc_camera_device *soc_camera_from_vbq(const struct videobuf_queue *vq)
{
	return container_of(vq, struct soc_camera_device, vb_vidq);
}

static inline u32 soc_camera_grp_id(const struct soc_camera_device *icd)
{
	return (icd->iface << 8) | (icd->devnum + 1);
}

void soc_camera_lock(struct vb2_queue *vq);
void soc_camera_unlock(struct vb2_queue *vq);

#endif
