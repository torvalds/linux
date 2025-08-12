/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1.h  --  R-Car VSP1 API
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __MEDIA_VSP1_H__
#define __MEDIA_VSP1_H__

#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/videodev2.h>

struct device;
struct vsp1_dl_list;

/* -----------------------------------------------------------------------------
 * VSP1 DU interface
 */

int vsp1_du_init(struct device *dev);

#define VSP1_DU_STATUS_COMPLETE		BIT(0)
#define VSP1_DU_STATUS_WRITEBACK	BIT(1)

/**
 * struct vsp1_du_lif_config - VSP LIF configuration
 * @width: output frame width
 * @height: output frame height
 * @interlaced: true for interlaced pipelines
 * @callback: frame completion callback function (optional). When a callback
 *	      is provided, the VSP driver guarantees that it will be called once
 *	      and only once for each vsp1_du_atomic_flush() call.
 * @callback_data: data to be passed to the frame completion callback
 */
struct vsp1_du_lif_config {
	unsigned int width;
	unsigned int height;
	bool interlaced;

	void (*callback)(void *data, unsigned int status, u32 crc);
	void *callback_data;
};

int vsp1_du_setup_lif(struct device *dev, unsigned int pipe_index,
		      const struct vsp1_du_lif_config *cfg);

/**
 * struct vsp1_du_atomic_config - VSP atomic configuration parameters
 * @pixelformat: plane pixel format (V4L2 4CC)
 * @pitch: line pitch in bytes for the first plane
 * @mem: DMA memory address for each plane of the frame buffer
 * @src: source rectangle in the frame buffer (integer coordinates)
 * @dst: destination rectangle on the display (integer coordinates)
 * @alpha: alpha value (0: fully transparent, 255: fully opaque)
 * @zpos: Z position of the plane (from 0 to number of planes minus 1)
 * @premult: true for premultiplied alpha
 * @color_encoding: color encoding (valid for YUV formats only)
 * @color_range: color range (valid for YUV formats only)
 */
struct vsp1_du_atomic_config {
	u32 pixelformat;
	unsigned int pitch;
	dma_addr_t mem[3];
	struct v4l2_rect src;
	struct v4l2_rect dst;
	unsigned int alpha;
	unsigned int zpos;
	bool premult;
	enum v4l2_ycbcr_encoding color_encoding;
	enum v4l2_quantization color_range;
};

/**
 * enum vsp1_du_crc_source - Source used for CRC calculation
 * @VSP1_DU_CRC_NONE: CRC calculation disabled
 * @VSP1_DU_CRC_PLANE: Perform CRC calculation on an input plane
 * @VSP1_DU_CRC_OUTPUT: Perform CRC calculation on the composed output
 */
enum vsp1_du_crc_source {
	VSP1_DU_CRC_NONE,
	VSP1_DU_CRC_PLANE,
	VSP1_DU_CRC_OUTPUT,
};

/**
 * struct vsp1_du_crc_config - VSP CRC computation configuration parameters
 * @source: source for CRC calculation
 * @index: index of the CRC source plane (when source is set to plane)
 */
struct vsp1_du_crc_config {
	enum vsp1_du_crc_source source;
	unsigned int index;
};

/**
 * struct vsp1_du_writeback_config - VSP writeback configuration parameters
 * @pixelformat: plane pixel format (V4L2 4CC)
 * @pitch: line pitch in bytes for the first plane
 * @mem: DMA memory address for each plane of the frame buffer
 */
struct vsp1_du_writeback_config {
	u32 pixelformat;
	unsigned int pitch;
	dma_addr_t mem[3];
};

/**
 * struct vsp1_du_atomic_pipe_config - VSP atomic pipe configuration parameters
 * @crc: CRC computation configuration
 * @writeback: writeback configuration
 */
struct vsp1_du_atomic_pipe_config {
	struct vsp1_du_crc_config crc;
	struct vsp1_du_writeback_config writeback;
};

void vsp1_du_atomic_begin(struct device *dev, unsigned int pipe_index);
int vsp1_du_atomic_update(struct device *dev, unsigned int pipe_index,
			  unsigned int rpf,
			  const struct vsp1_du_atomic_config *cfg);
void vsp1_du_atomic_flush(struct device *dev, unsigned int pipe_index,
			  const struct vsp1_du_atomic_pipe_config *cfg);
int vsp1_du_map_sg(struct device *dev, struct sg_table *sgt);
void vsp1_du_unmap_sg(struct device *dev, struct sg_table *sgt);

/* -----------------------------------------------------------------------------
 * VSP1 ISP interface
 */

/**
 * struct vsp1_isp_buffer_desc - Describe a buffer allocated by VSPX
 * @size: Byte size of the buffer allocated by VSPX
 * @cpu_addr: CPU-mapped address of a buffer allocated by VSPX
 * @dma_addr: bus address of a buffer allocated by VSPX
 */
struct vsp1_isp_buffer_desc {
	size_t size;
	void *cpu_addr;
	dma_addr_t dma_addr;
};

/**
 * struct vsp1_isp_job_desc - Describe a VSPX buffer transfer request
 * @config: ConfigDMA buffer descriptor
 * @config.pairs: number of reg-value pairs in the ConfigDMA buffer
 * @config.mem: bus address of the ConfigDMA buffer
 * @img: RAW image buffer descriptor
 * @img.fmt: RAW image format
 * @img.mem: bus address of the RAW image buffer
 * @dl: pointer to the display list populated by the VSPX driver in the
 *      vsp1_isp_job_prepare() function
 *
 * Describe a transfer request for the VSPX to perform on behalf of the ISP.
 * The job descriptor contains an optional ConfigDMA buffer and one RAW image
 * buffer. Set config.pairs to 0 if no ConfigDMA buffer should be transferred.
 * The minimum number of config.pairs that can be written using ConfigDMA is 17.
 * A number of pairs < 16 corrupts the output image. A number of pairs == 16
 * freezes the VSPX operation. If the ISP driver has to write less than 17 pairs
 * it shall pad the buffer with writes directed to registers that have no effect
 * or avoid using ConfigDMA at all for such small write sequences.
 *
 * The ISP driver shall pass an instance this type to the vsp1_isp_job_prepare()
 * function that will populate the display list pointer @dl using the @config
 * and @img descriptors. When the job has to be run on the VSPX, the descriptor
 * shall be passed to vsp1_isp_job_run() which consumes the display list.
 *
 * Job descriptors not yet run shall be released with a call to
 * vsp1_isp_job_release() when stopping the streaming in order to properly
 * release the resources acquired by vsp1_isp_job_prepare().
 */
struct vsp1_isp_job_desc {
	struct {
		unsigned int pairs;
		dma_addr_t mem;
	} config;
	struct {
		struct v4l2_pix_format_mplane fmt;
		dma_addr_t mem;
	} img;
	struct vsp1_dl_list *dl;
};

/**
 * struct vsp1_vspx_frame_end - VSPX frame end callback data
 * @vspx_frame_end: Frame end callback. Called after a transfer job has been
 *		    completed. If the job includes both a ConfigDMA and a
 *		    RAW image, the callback is called after both have been
 *		    transferred
 * @frame_end_data: Frame end callback data, passed to vspx_frame_end
 */
struct vsp1_vspx_frame_end {
	void (*vspx_frame_end)(void *data);
	void *frame_end_data;
};

int vsp1_isp_init(struct device *dev);
struct device *vsp1_isp_get_bus_master(struct device *dev);
int vsp1_isp_alloc_buffer(struct device *dev, size_t size,
			  struct vsp1_isp_buffer_desc *buffer_desc);
void vsp1_isp_free_buffer(struct device *dev,
			  struct vsp1_isp_buffer_desc *buffer_desc);
int vsp1_isp_start_streaming(struct device *dev,
			     struct vsp1_vspx_frame_end *frame_end);
void vsp1_isp_stop_streaming(struct device *dev);
int vsp1_isp_job_prepare(struct device *dev,
			 struct vsp1_isp_job_desc *job);
int vsp1_isp_job_run(struct device *dev, struct vsp1_isp_job_desc *job);
void vsp1_isp_job_release(struct device *dev,  struct vsp1_isp_job_desc *job);

#endif /* __MEDIA_VSP1_H__ */
