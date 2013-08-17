/*
 * videobuf2-fb.h - FrameBuffer API emulator on top of Videobuf2 framework
 *
 * Copyright (C) 2011 Samsung Electronics
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef _MEDIA_VIDEOBUF2_FB_H
#define _MEDIA_VIDEOBUF2_FB_H

#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>

void *vb2_fb_register(struct vb2_queue *q, struct video_device *vfd);
int vb2_fb_unregister(void *fb_emu);

#endif
