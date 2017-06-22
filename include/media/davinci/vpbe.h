/*
 * Copyright (C) 2010 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _VPBE_H
#define _VPBE_H

#include <linux/videodev2.h>
#include <linux/i2c.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/davinci/vpbe_osd.h>
#include <media/davinci/vpbe_venc.h>
#include <media/davinci/vpbe_types.h>

/* OSD configuration info */
struct osd_config_info {
	char module_name[32];
};

struct vpbe_output {
	struct v4l2_output output;
	/*
	 * If output capabilities include dv_timings, list supported timings
	 * below
	 */
	char *subdev_name;
	/*
	 * defualt_mode identifies the default timings set at the venc or
	 * external encoder.
	 */
	char *default_mode;
	/*
	 * Fields below are used for supporting multiple modes. For example,
	 * LCD panel might support different modes and they are listed here.
	 * Similarly for supporting external encoders, lcd controller port
	 * requires a set of non-standard timing values to be listed here for
	 * each supported mode since venc is used in non-standard timing mode
	 * for interfacing with external encoder similar to configuring lcd
	 * panel timings
	 */
	unsigned int num_modes;
	struct vpbe_enc_mode_info *modes;
	/*
	 * Bus configuration goes here for external encoders. Some encoders
	 * may require multiple interface types for each of the output. For
	 * example, SD modes would use YCC8 where as HD mode would use YCC16.
	 * Not sure if this is needed on a per mode basis instead of per
	 * output basis. If per mode is needed, we may have to move this to
	 * mode_info structure
	 */
	u32 if_params;
};

/* encoder configuration info */
struct encoder_config_info {
	char module_name[32];
	/* Is this an i2c device ? */
	unsigned int is_i2c:1;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
};

/*amplifier configuration info */
struct amp_config_info {
	char module_name[32];
	/* Is this an i2c device ? */
	unsigned int is_i2c:1;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
};

/* structure for defining vpbe display subsystem components */
struct vpbe_config {
	char module_name[32];
	/* i2c bus adapter no */
	int i2c_adapter_id;
	struct osd_config_info osd;
	struct encoder_config_info venc;
	/* external encoder information goes here */
	int num_ext_encoders;
	struct encoder_config_info *ext_encoders;
	/* amplifier information goes here */
	struct amp_config_info *amp;
	int num_outputs;
	/* Order is venc outputs followed by LCD and then external encoders */
	struct vpbe_output *outputs;
};

struct vpbe_device;

struct vpbe_device_ops {
	/* crop cap for the display */
	int (*g_cropcap)(struct vpbe_device *vpbe_dev,
			 struct v4l2_cropcap *cropcap);

	/* Enumerate the outputs */
	int (*enum_outputs)(struct vpbe_device *vpbe_dev,
			    struct v4l2_output *output);

	/* Set output to the given index */
	int (*set_output)(struct vpbe_device *vpbe_dev,
			 int index);

	/* Get current output */
	unsigned int (*get_output)(struct vpbe_device *vpbe_dev);

	/* Set DV preset at current output */
	int (*s_dv_timings)(struct vpbe_device *vpbe_dev,
			   struct v4l2_dv_timings *dv_timings);

	/* Get DV presets supported at the output */
	int (*g_dv_timings)(struct vpbe_device *vpbe_dev,
			   struct v4l2_dv_timings *dv_timings);

	/* Enumerate the DV Presets supported at the output */
	int (*enum_dv_timings)(struct vpbe_device *vpbe_dev,
			       struct v4l2_enum_dv_timings *timings_info);

	/* Set std at the output */
	int (*s_std)(struct vpbe_device *vpbe_dev, v4l2_std_id std_id);

	/* Get the current std at the output */
	int (*g_std)(struct vpbe_device *vpbe_dev, v4l2_std_id *std_id);

	/* initialize the device */
	int (*initialize)(struct device *dev, struct vpbe_device *vpbe_dev);

	/* De-initialize the device */
	void (*deinitialize)(struct device *dev, struct vpbe_device *vpbe_dev);

	/* Get the current mode info */
	int (*get_mode_info)(struct vpbe_device *vpbe_dev,
			     struct vpbe_enc_mode_info*);

	/*
	 * Set the current mode in the encoder. Alternate way of setting
	 * standard or DV preset or custom timings in the encoder
	 */
	int (*set_mode)(struct vpbe_device *vpbe_dev,
			struct vpbe_enc_mode_info*);
	/* Power management operations */
	int (*suspend)(struct vpbe_device *vpbe_dev);
	int (*resume)(struct vpbe_device *vpbe_dev);
};

/* struct for vpbe device */
struct vpbe_device {
	/* V4l2 device */
	struct v4l2_device v4l2_dev;
	/* vpbe dispay controller cfg */
	struct vpbe_config *cfg;
	/* parent device */
	struct device *pdev;
	/* external encoder v4l2 sub devices */
	struct v4l2_subdev **encoders;
	/* current encoder index */
	int current_sd_index;
	/* external amplifier v4l2 subdevice */
	struct v4l2_subdev *amp;
	struct mutex lock;
	/* device initialized */
	int initialized;
	/* vpbe dac clock */
	struct clk *dac_clk;
	/* osd_device pointer */
	struct osd_state *osd_device;
	/* venc device pointer */
	struct venc_platform_data *venc_device;
	/*
	 * fields below are accessed by users of vpbe_device. Not the
	 * ones above
	 */

	/* current output */
	int current_out_index;
	/* lock used by caller to do atomic operation on vpbe device */
	/* current timings set in the controller */
	struct vpbe_enc_mode_info current_timings;
	/* venc sub device */
	struct v4l2_subdev *venc;
	/* device operations below */
	struct vpbe_device_ops ops;
};

#endif
