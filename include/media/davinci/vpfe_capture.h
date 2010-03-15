/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _VPFE_CAPTURE_H
#define _VPFE_CAPTURE_H

#ifdef __KERNEL__

/* Header files */
#include <media/v4l2-dev.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/videobuf-dma-contig.h>
#include <media/davinci/vpfe_types.h>

#define VPFE_CAPTURE_NUM_DECODERS        5

/* Macros */
#define VPFE_MAJOR_RELEASE              0
#define VPFE_MINOR_RELEASE              0
#define VPFE_BUILD                      1
#define VPFE_CAPTURE_VERSION_CODE       ((VPFE_MAJOR_RELEASE << 16) | \
					(VPFE_MINOR_RELEASE << 8)  | \
					VPFE_BUILD)

#define CAPTURE_DRV_NAME		"vpfe-capture"

struct vpfe_pixel_format {
	struct v4l2_fmtdesc fmtdesc;
	/* bytes per pixel */
	int bpp;
};

struct vpfe_std_info {
	int active_pixels;
	int active_lines;
	/* current frame format */
	int frame_format;
};

struct vpfe_route {
	u32 input;
	u32 output;
};

struct vpfe_subdev_info {
	/* Sub device name */
	char name[32];
	/* Sub device group id */
	int grp_id;
	/* Number of inputs supported */
	int num_inputs;
	/* inputs available at the sub device */
	struct v4l2_input *inputs;
	/* Sub dev routing information for each input */
	struct vpfe_route *routes;
	/* check if sub dev supports routing */
	int can_route;
	/* ccdc bus/interface configuration */
	struct vpfe_hw_if_param ccdc_if_params;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
};

struct vpfe_config {
	/* Number of sub devices connected to vpfe */
	int num_subdevs;
	/* i2c bus adapter no */
	int i2c_adapter_id;
	/* information about each subdev */
	struct vpfe_subdev_info *sub_devs;
	/* evm card info */
	char *card_name;
	/* ccdc name */
	char *ccdc;
	/* vpfe clock */
	struct clk *vpssclk;
	struct clk *slaveclk;
};

struct vpfe_device {
	/* V4l2 specific parameters */
	/* Identifies video device for this channel */
	struct video_device *video_dev;
	/* sub devices */
	struct v4l2_subdev **sd;
	/* vpfe cfg */
	struct vpfe_config *cfg;
	/* V4l2 device */
	struct v4l2_device v4l2_dev;
	/* parent device */
	struct device *pdev;
	/* Used to keep track of state of the priority */
	struct v4l2_prio_state prio;
	/* number of open instances of the channel */
	u32 usrs;
	/* Indicates id of the field which is being displayed */
	u32 field_id;
	/* flag to indicate whether decoder is initialized */
	u8 initialized;
	/* current interface type */
	struct vpfe_hw_if_param vpfe_if_params;
	/* ptr to currently selected sub device */
	struct vpfe_subdev_info *current_subdev;
	/* current input at the sub device */
	int current_input;
	/* Keeps track of the information about the standard */
	struct vpfe_std_info std_info;
	/* std index into std table */
	int std_index;
	/* CCDC IRQs used when CCDC/ISIF output to SDRAM */
	unsigned int ccdc_irq0;
	unsigned int ccdc_irq1;
	/* number of buffers in fbuffers */
	u32 numbuffers;
	/* List of buffer pointers for storing frames */
	u8 *fbuffers[VIDEO_MAX_FRAME];
	/* Pointer pointing to current v4l2_buffer */
	struct videobuf_buffer *cur_frm;
	/* Pointer pointing to next v4l2_buffer */
	struct videobuf_buffer *next_frm;
	/*
	 * This field keeps track of type of buffer exchange mechanism
	 * user has selected
	 */
	enum v4l2_memory memory;
	/* Used to store pixel format */
	struct v4l2_format fmt;
	/*
	 * used when IMP is chained to store the crop window which
	 * is different from the image window
	 */
	struct v4l2_rect crop;
	/* Buffer queue used in video-buf */
	struct videobuf_queue buffer_queue;
	/* Queue of filled frames */
	struct list_head dma_queue;
	/* Used in video-buf */
	spinlock_t irqlock;
	/* IRQ lock for DMA queue */
	spinlock_t dma_queue_lock;
	/* lock used to access this structure */
	struct mutex lock;
	/* number of users performing IO */
	u32 io_usrs;
	/* Indicates whether streaming started */
	u8 started;
	/*
	 * offset where second field starts from the starting of the
	 * buffer for field separated YCbCr formats
	 */
	u32 field_off;
};

/* File handle structure */
struct vpfe_fh {
	struct vpfe_device *vpfe_dev;
	/* Indicates whether this file handle is doing IO */
	u8 io_allowed;
	/* Used to keep track priority of this instance */
	enum v4l2_priority prio;
};

struct vpfe_config_params {
	u8 min_numbuffers;
	u8 numbuffers;
	u32 min_bufsize;
	u32 device_bufsize;
};

#endif				/* End of __KERNEL__ */
/**
 * VPFE_CMD_S_CCDC_RAW_PARAMS - EXPERIMENTAL IOCTL to set raw capture params
 * This can be used to configure modules such as defect pixel correction,
 * color space conversion, culling etc. This is an experimental ioctl that
 * will change in future kernels. So use this ioctl with care !
 * TODO: This is to be split into multiple ioctls and also explore the
 * possibility of extending the v4l2 api to include this
 **/
#define VPFE_CMD_S_CCDC_RAW_PARAMS _IOW('V', BASE_VIDIOC_PRIVATE + 1, \
					void *)
#endif				/* _DAVINCI_VPFE_H */
