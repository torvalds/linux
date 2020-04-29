/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2012-2016 Mentor Graphics Inc.
 *
 * i.MX Queued image conversion support, with tiling and rotation.
 */
#ifndef __IMX_IPU_IMAGE_CONVERT_H__
#define __IMX_IPU_IMAGE_CONVERT_H__

#include <video/imx-ipu-v3.h>

struct ipu_image_convert_ctx;

/**
 * struct ipu_image_convert_run - image conversion run request struct
 *
 * @ctx:	the conversion context
 * @in_phys:	dma addr of input image buffer for this run
 * @out_phys:	dma addr of output image buffer for this run
 * @status:	completion status of this run
 */
struct ipu_image_convert_run {
	struct ipu_image_convert_ctx *ctx;

	dma_addr_t in_phys;
	dma_addr_t out_phys;

	int status;

	/* internal to image converter, callers don't touch */
	struct list_head list;
};

/**
 * ipu_image_convert_cb_t - conversion callback function prototype
 *
 * @run:	the completed conversion run pointer
 * @ctx:	a private context pointer for the callback
 */
typedef void (*ipu_image_convert_cb_t)(struct ipu_image_convert_run *run,
				       void *ctx);

/**
 * ipu_image_convert_enum_format() - enumerate the image converter's
 *	supported input and output pixel formats.
 *
 * @index:	pixel format index
 * @fourcc:	v4l2 fourcc for this index
 *
 * Returns 0 with a valid index and fills in v4l2 fourcc, -EINVAL otherwise.
 *
 * In V4L2, drivers can call ipu_image_enum_format() in .enum_fmt.
 */
int ipu_image_convert_enum_format(int index, u32 *fourcc);

/**
 * ipu_image_convert_adjust() - adjust input/output images to IPU restrictions.
 *
 * @in:		input image format, adjusted on return
 * @out:	output image format, adjusted on return
 * @rot_mode:	rotation mode
 *
 * In V4L2, drivers can call ipu_image_convert_adjust() in .try_fmt.
 */
void ipu_image_convert_adjust(struct ipu_image *in, struct ipu_image *out,
			      enum ipu_rotate_mode rot_mode);

/**
 * ipu_image_convert_verify() - verify that input/output image formats
 *         and rotation mode meet IPU restrictions.
 *
 * @in:		input image format
 * @out:	output image format
 * @rot_mode:	rotation mode
 *
 * Returns 0 if the formats and rotation mode meet IPU restrictions,
 * -EINVAL otherwise.
 */
int ipu_image_convert_verify(struct ipu_image *in, struct ipu_image *out,
			     enum ipu_rotate_mode rot_mode);

/**
 * ipu_image_convert_prepare() - prepare a conversion context.
 *
 * @ipu:	the IPU handle to use for the conversions
 * @ic_task:	the IC task to use for the conversions
 * @in:		input image format
 * @out:	output image format
 * @rot_mode:	rotation mode
 * @complete:	run completion callback
 * @complete_context:	a context pointer for the completion callback
 *
 * Returns an opaque conversion context pointer on success, error pointer
 * on failure. The input/output formats and rotation mode must already meet
 * IPU retrictions.
 *
 * In V4L2, drivers should call ipu_image_convert_prepare() at streamon.
 */
struct ipu_image_convert_ctx *
ipu_image_convert_prepare(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
			  struct ipu_image *in, struct ipu_image *out,
			  enum ipu_rotate_mode rot_mode,
			  ipu_image_convert_cb_t complete,
			  void *complete_context);

/**
 * ipu_image_convert_unprepare() - unprepare a conversion context.
 *
 * @ctx: the conversion context pointer to unprepare
 *
 * Aborts any active or pending conversions for this context and
 * frees the context. Any currently active or pending runs belonging
 * to this context are returned via the completion callback with an
 * error run status.
 *
 * In V4L2, drivers should call ipu_image_convert_unprepare() at
 * streamoff.
 */
void ipu_image_convert_unprepare(struct ipu_image_convert_ctx *ctx);

/**
 * ipu_image_convert_queue() - queue a conversion run
 *
 * @run: the run request pointer
 *
 * ipu_image_convert_run must be dynamically allocated (_not_ as a local
 * var) by callers and filled in with a previously prepared conversion
 * context handle and the dma addr's of the input and output image buffers
 * for this conversion run.
 *
 * When this conversion completes, the run pointer is returned via the
 * completion callback. The caller is responsible for freeing the run
 * object after it completes.
 *
 * In V4L2, drivers should call ipu_image_convert_queue() while
 * streaming to queue the conversion of a received input buffer.
 * For example mem2mem devices this would be called in .device_run.
 */
int ipu_image_convert_queue(struct ipu_image_convert_run *run);

/**
 * ipu_image_convert_abort() - abort conversions
 *
 * @ctx: the conversion context pointer
 *
 * This will abort any active or pending conversions for this context.
 * Any currently active or pending runs belonging to this context are
 * returned via the completion callback with an error run status.
 */
void ipu_image_convert_abort(struct ipu_image_convert_ctx *ctx);

/**
 * ipu_image_convert() - asynchronous image conversion request
 *
 * @ipu:	the IPU handle to use for the conversion
 * @ic_task:	the IC task to use for the conversion
 * @in:		input image format
 * @out:	output image format
 * @rot_mode:	rotation mode
 * @complete:	run completion callback
 * @complete_context:	a context pointer for the completion callback
 *
 * Request a single image conversion. Returns the run that has been queued.
 * A conversion context is automatically created and is available in run->ctx.
 * As with ipu_image_convert_prepare(), the input/output formats and rotation
 * mode must already meet IPU retrictions.
 *
 * On successful return the caller can queue more run requests if needed, using
 * the prepared context in run->ctx. The caller is responsible for unpreparing
 * the context when no more conversion requests are needed.
 */
struct ipu_image_convert_run *
ipu_image_convert(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
		  struct ipu_image *in, struct ipu_image *out,
		  enum ipu_rotate_mode rot_mode,
		  ipu_image_convert_cb_t complete,
		  void *complete_context);

/**
 * ipu_image_convert_sync() - synchronous single image conversion request
 *
 * @ipu:	the IPU handle to use for the conversion
 * @ic_task:	the IC task to use for the conversion
 * @in:		input image format
 * @out:	output image format
 * @rot_mode:	rotation mode
 *
 * Carry out a single image conversion. Returns when the conversion
 * completes. The input/output formats and rotation mode must already
 * meet IPU retrictions. The created context is automatically unprepared
 * and the run freed on return.
 */
int ipu_image_convert_sync(struct ipu_soc *ipu, enum ipu_ic_task ic_task,
			   struct ipu_image *in, struct ipu_image *out,
			   enum ipu_rotate_mode rot_mode);


#endif /* __IMX_IPU_IMAGE_CONVERT_H__ */
