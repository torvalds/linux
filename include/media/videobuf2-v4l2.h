/*
 * videobuf2-v4l2.h - V4L2 driver helper framework
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#ifndef _MEDIA_VIDEOBUF2_V4L2_H
#define _MEDIA_VIDEOBUF2_V4L2_H

#include <linux/videodev2.h>
#include <media/videobuf2-core.h>

#if VB2_MAX_FRAME != VIDEO_MAX_FRAME
#error VB2_MAX_FRAME != VIDEO_MAX_FRAME
#endif

#if VB2_MAX_PLANES != VIDEO_MAX_PLANES
#error VB2_MAX_PLANES != VIDEO_MAX_PLANES
#endif

/**
 * struct vb2_v4l2_buffer - video buffer information for v4l2
 * @vb2_buf:	video buffer 2
 * @flags:	buffer informational flags
 * @field:	enum v4l2_field; field order of the image in the buffer
 * @timestamp:	frame timestamp
 * @timecode:	frame timecode
 * @sequence:	sequence count of this frame
 * Should contain enough information to be able to cover all the fields
 * of struct v4l2_buffer at videodev2.h
 */
struct vb2_v4l2_buffer {
	struct vb2_buffer	vb2_buf;

	__u32			flags;
	__u32			field;
	struct timeval		timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;
};

/*
 * to_vb2_v4l2_buffer() - cast struct vb2_buffer * to struct vb2_v4l2_buffer *
 */
#define to_vb2_v4l2_buffer(vb) \
	container_of(vb, struct vb2_v4l2_buffer, vb2_buf)

#endif /* _MEDIA_VIDEOBUF2_V4L2_H */
