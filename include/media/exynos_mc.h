/* linux/inclue/media/exynos_mc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * header file for exynos media device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GSC_MDEVICE_H_
#define GSC_MDEVICE_H_

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#define err(fmt, args...) \
	printk(KERN_ERR "%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#define MDEV_MODULE_NAME "exynos-mdev"
#define MAX_GSC_SUBDEV		4
#define MDEV_MAX_NUM	3

#define GSC_OUT_PAD_SINK	0
#define GSC_OUT_PAD_SOURCE	1

#define GSC_CAP_PAD_SINK	0
#define GSC_CAP_PAD_SOURCE	1

#define FLITE_PAD_SINK		0
#define FLITE_PAD_SOURCE_PREV	1
#define FLITE_PAD_SOURCE_CAMCORD	2
#define FLITE_PAD_SOURCE_MEM		3
#define FLITE_PADS_NUM		4

#define CSIS_PAD_SINK		0
#define CSIS_PAD_SOURCE		1
#define CSIS_PADS_NUM		2

#define MAX_CAMIF_CLIENTS	2
#if defined(CONFIG_SOC_EXYNOS5410)
#define MAX_CAMIF_CHANNEL	3
#else
#define MAX_CAMIF_CHANNEL	2
#endif

#define MXR_SUBDEV_NAME		"s5p-mixer"

#define GSC_MODULE_NAME			"exynos-gsc"
#define GSC_SUBDEV_NAME			"exynos-gsc-sd"
#define FIMD_MODULE_NAME		"s5p-fimd1"
#define FIMD_ENTITY_NAME		"s3c-fb-window"
#define FLITE_MODULE_NAME		"exynos-fimc-lite"
#define CSIS_MODULE_NAME		"s5p-mipi-csis"

#define GSC_CAP_GRP_ID			(1 << 0)
#define FLITE_GRP_ID			(1 << 1)
#define CSIS_GRP_ID			(1 << 2)
#define SENSOR_GRP_ID			(1 << 3)
#define FIMD_GRP_ID			(1 << 4)

#define SENSOR_MAX_ENTITIES		MAX_CAMIF_CLIENTS
#define FLITE_MAX_ENTITIES		MAX_CAMIF_CHANNEL
#define CSIS_MAX_ENTITIES		MAX_CAMIF_CHANNEL

enum mdev_node {
	MDEV_OUTPUT,
	MDEV_CAPTURE,
	MDEV_ISP,
};

enum mxr_data_from {
	FROM_GSC_SD,
	FROM_MXR_VD,
};

struct exynos_media_ops {
	int (*power_off)(struct v4l2_subdev *sd);
};

struct exynos_entity_data {
	const struct exynos_media_ops *media_ops;
	enum mxr_data_from mxr_data_from;
};

/**
 * struct exynos_md - Exynos media device information
 * @media_dev: top level media device
 * @v4l2_dev: top level v4l2_device holding up the subdevs
 * @pdev: platform device this media device is hooked up into
 * @slock: spinlock protecting @sensor array
 * @id: media device IDs
 * @gsc_sd: each pointer of g-scaler's subdev array
 */
struct exynos_md {
	struct media_device	media_dev;
	struct v4l2_device	v4l2_dev;
	struct platform_device	*pdev;
	struct v4l2_subdev	*gsc_sd[MAX_GSC_SUBDEV];
	struct v4l2_subdev	*gsc_cap_sd[MAX_GSC_SUBDEV];
	struct v4l2_subdev	*csis_sd[CSIS_MAX_ENTITIES];
	struct v4l2_subdev	*flite_sd[FLITE_MAX_ENTITIES];
	struct v4l2_subdev	*sensor_sd[SENSOR_MAX_ENTITIES];
	u16			id;
	bool			is_flite_on;
	spinlock_t slock;
};

static int dummy_callback(struct device *dev, void *md)
{
	/* non-zero return stops iteration */
	return -1;
}

static inline void *module_name_to_driver_data(char *module_name)
{
	struct device_driver *drv;
	struct device *dev;
	void *driver_data;

	drv = driver_find(module_name, &platform_bus_type);
	if (drv) {
		dev = driver_find_device(drv, NULL, NULL, dummy_callback);
		driver_data = dev_get_drvdata(dev);
		return driver_data;
	} else
		return NULL;
}

/* print entity information for debug*/
static inline void entity_info_print(struct media_entity *me, struct device *dev)
{
	u16 num_pads = me->num_pads;
	u16 num_links = me->num_links;
	int i;

	dev_dbg(dev, "entity name : %s\n", me->name);
	dev_dbg(dev, "number of pads = %d\n", num_pads);
	for (i = 0; i < num_pads; ++i) {
		dev_dbg(dev, "pad[%d] flag : %s\n", i,
			(me->pads[i].flags == 1) ? "SINK" : "SOURCE");
	}

	dev_dbg(dev, "number of links = %d\n", num_links);

	for (i = 0; i < num_links; ++i) {
		dev_dbg(dev, "link[%d] info  =  ", i);
		dev_dbg(dev, "%s : %s[%d]  --->  %s : %s[%d]\n",
			me->links[i].source->entity->name,
			me->links[i].source->flags == 1 ? "SINK" : "SOURCE",
			me->links[i].source->index,
			me->links[i].sink->entity->name,
			me->links[i].sink->flags == 1 ? "SINK" : "SOURCE",
			me->links[i].sink->index);
	}
}
#endif
