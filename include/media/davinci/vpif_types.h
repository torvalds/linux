/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Texas Instruments Inc
 */
#ifndef _VPIF_TYPES_H
#define _VPIF_TYPES_H

#include <linux/i2c.h>

#define VPIF_CAPTURE_MAX_CHANNELS	2
#define VPIF_DISPLAY_MAX_CHANNELS	2

enum vpif_if_type {
	VPIF_IF_BT656,
	VPIF_IF_BT1120,
	VPIF_IF_RAW_BAYER
};

struct vpif_interface {
	enum vpif_if_type if_type;
	unsigned hd_pol:1;
	unsigned vd_pol:1;
	unsigned fid_pol:1;
};

struct vpif_subdev_info {
	const char *name;
	struct i2c_board_info board_info;
};

struct vpif_output {
	struct v4l2_output output;
	const char *subdev_name;
	u32 input_route;
	u32 output_route;
};

struct vpif_display_chan_config {
	const struct vpif_output *outputs;
	int output_count;
	bool clip_en;
};

struct vpif_display_config {
	int (*set_clock)(int, int);
	struct vpif_subdev_info *subdevinfo;
	int subdev_count;
	int i2c_adapter_id;
	struct vpif_display_chan_config chan_config[VPIF_DISPLAY_MAX_CHANNELS];
	const char *card_name;
	struct v4l2_async_subdev **asd;	/* Flat array, arranged in groups */
	int *asd_sizes;		/* 0-terminated array of asd group sizes */
};

struct vpif_input {
	struct v4l2_input input;
	char *subdev_name;
	u32 input_route;
	u32 output_route;
};

struct vpif_capture_chan_config {
	struct vpif_interface vpif_if;
	struct vpif_input *inputs;
	int input_count;
};

struct vpif_capture_config {
	int (*setup_input_channel_mode)(int);
	int (*setup_input_path)(int, const char *);
	struct vpif_capture_chan_config chan_config[VPIF_CAPTURE_MAX_CHANNELS];
	struct vpif_subdev_info *subdev_info;
	int subdev_count;
	int i2c_adapter_id;
	const char *card_name;

	struct v4l2_async_subdev *asd[VPIF_CAPTURE_MAX_CHANNELS];
	int asd_sizes[VPIF_CAPTURE_MAX_CHANNELS];
};
#endif /* _VPIF_TYPES_H */
