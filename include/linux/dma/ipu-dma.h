/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * Copyright (C) 2005-2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_DMA_IPU_DMA_H
#define __LINUX_DMA_IPU_DMA_H

#include <linux/types.h>
#include <linux/dmaengine.h>

/* IPU DMA Controller channel definitions. */
enum ipu_channel {
	IDMAC_IC_0 = 0,		/* IC (encoding task) to memory */
	IDMAC_IC_1 = 1,		/* IC (viewfinder task) to memory */
	IDMAC_ADC_0 = 1,
	IDMAC_IC_2 = 2,
	IDMAC_ADC_1 = 2,
	IDMAC_IC_3 = 3,
	IDMAC_IC_4 = 4,
	IDMAC_IC_5 = 5,
	IDMAC_IC_6 = 6,
	IDMAC_IC_7 = 7,		/* IC (sensor data) to memory */
	IDMAC_IC_8 = 8,
	IDMAC_IC_9 = 9,
	IDMAC_IC_10 = 10,
	IDMAC_IC_11 = 11,
	IDMAC_IC_12 = 12,
	IDMAC_IC_13 = 13,
	IDMAC_SDC_0 = 14,	/* Background synchronous display data */
	IDMAC_SDC_1 = 15,	/* Foreground data (overlay) */
	IDMAC_SDC_2 = 16,
	IDMAC_SDC_3 = 17,
	IDMAC_ADC_2 = 18,
	IDMAC_ADC_3 = 19,
	IDMAC_ADC_4 = 20,
	IDMAC_ADC_5 = 21,
	IDMAC_ADC_6 = 22,
	IDMAC_ADC_7 = 23,
	IDMAC_PF_0 = 24,
	IDMAC_PF_1 = 25,
	IDMAC_PF_2 = 26,
	IDMAC_PF_3 = 27,
	IDMAC_PF_4 = 28,
	IDMAC_PF_5 = 29,
	IDMAC_PF_6 = 30,
	IDMAC_PF_7 = 31,
};

/* Order significant! */
enum ipu_channel_status {
	IPU_CHANNEL_FREE,
	IPU_CHANNEL_INITIALIZED,
	IPU_CHANNEL_READY,
	IPU_CHANNEL_ENABLED,
};

#define IPU_CHANNELS_NUM 32

enum pixel_fmt {
	/* 1 byte */
	IPU_PIX_FMT_GENERIC,
	IPU_PIX_FMT_RGB332,
	IPU_PIX_FMT_YUV420P,
	IPU_PIX_FMT_YUV422P,
	IPU_PIX_FMT_YUV420P2,
	IPU_PIX_FMT_YVU422P,
	/* 2 bytes */
	IPU_PIX_FMT_RGB565,
	IPU_PIX_FMT_RGB666,
	IPU_PIX_FMT_BGR666,
	IPU_PIX_FMT_YUYV,
	IPU_PIX_FMT_UYVY,
	/* 3 bytes */
	IPU_PIX_FMT_RGB24,
	IPU_PIX_FMT_BGR24,
	/* 4 bytes */
	IPU_PIX_FMT_GENERIC_32,
	IPU_PIX_FMT_RGB32,
	IPU_PIX_FMT_BGR32,
	IPU_PIX_FMT_ABGR32,
	IPU_PIX_FMT_BGRA32,
	IPU_PIX_FMT_RGBA32,
};

enum ipu_color_space {
	IPU_COLORSPACE_RGB,
	IPU_COLORSPACE_YCBCR,
	IPU_COLORSPACE_YUV
};

/*
 * Enumeration of IPU rotation modes
 */
enum ipu_rotate_mode {
	/* Note the enum values correspond to BAM value */
	IPU_ROTATE_NONE = 0,
	IPU_ROTATE_VERT_FLIP = 1,
	IPU_ROTATE_HORIZ_FLIP = 2,
	IPU_ROTATE_180 = 3,
	IPU_ROTATE_90_RIGHT = 4,
	IPU_ROTATE_90_RIGHT_VFLIP = 5,
	IPU_ROTATE_90_RIGHT_HFLIP = 6,
	IPU_ROTATE_90_LEFT = 7,
};

/*
 * Enumeration of DI ports for ADC.
 */
enum display_port {
	DISP0,
	DISP1,
	DISP2,
	DISP3
};

struct idmac_video_param {
	unsigned short		in_width;
	unsigned short		in_height;
	uint32_t		in_pixel_fmt;
	unsigned short		out_width;
	unsigned short		out_height;
	uint32_t		out_pixel_fmt;
	unsigned short		out_stride;
	bool			graphics_combine_en;
	bool			global_alpha_en;
	bool			key_color_en;
	enum display_port	disp;
	unsigned short		out_left;
	unsigned short		out_top;
};

/*
 * Union of initialization parameters for a logical channel. So far only video
 * parameters are used.
 */
union ipu_channel_param {
	struct idmac_video_param video;
};

struct idmac_tx_desc {
	struct dma_async_tx_descriptor	txd;
	struct scatterlist		*sg;	/* scatterlist for this */
	unsigned int			sg_len;	/* tx-descriptor. */
	struct list_head		list;
};

struct idmac_channel {
	struct dma_chan		dma_chan;
	dma_cookie_t		completed;	/* last completed cookie	   */
	union ipu_channel_param	params;
	enum ipu_channel	link;	/* input channel, linked to the output	   */
	enum ipu_channel_status	status;
	void			*client;	/* Only one client per channel	   */
	unsigned int		n_tx_desc;
	struct idmac_tx_desc	*desc;		/* allocated tx-descriptors	   */
	struct scatterlist	*sg[2];	/* scatterlist elements in buffer-0 and -1 */
	struct list_head	free_list;	/* free tx-descriptors		   */
	struct list_head	queue;		/* queued tx-descriptors	   */
	spinlock_t		lock;		/* protects sg[0,1], queue	   */
	struct mutex		chan_mutex; /* protects status, cookie, free_list  */
	bool			sec_chan_en;
	int			active_buffer;
	unsigned int		eof_irq;
	char			eof_name[16];	/* EOF IRQ name for request_irq()  */
};

#define to_tx_desc(tx) container_of(tx, struct idmac_tx_desc, txd)
#define to_idmac_chan(c) container_of(c, struct idmac_channel, dma_chan)

#endif /* __LINUX_DMA_IPU_DMA_H */
