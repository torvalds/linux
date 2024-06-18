/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Controls header for NPCM video driver
 *
 * Copyright (C) 2022 Nuvoton Technologies
 */

#ifndef _UAPI_LINUX_NPCM_VIDEO_H
#define _UAPI_LINUX_NPCM_VIDEO_H

#include <linux/v4l2-controls.h>

/*
 * Check Documentation/userspace-api/media/drivers/npcm-video.rst for control
 * details.
 */

/*
 * This control is meant to set the mode of NPCM Video Capture/Differentiation
 * (VCD) engine.
 *
 * The VCD engine supports two modes:
 * COMPLETE - Capture the next complete frame into memory.
 * DIFF	    - Compare the incoming frame with the frame stored in memory, and
 *	      updates the differentiated frame in memory.
 */
#define V4L2_CID_NPCM_CAPTURE_MODE	(V4L2_CID_USER_NPCM_BASE + 0)

enum v4l2_npcm_capture_mode {
	V4L2_NPCM_CAPTURE_MODE_COMPLETE	= 0, /* COMPLETE mode */
	V4L2_NPCM_CAPTURE_MODE_DIFF	= 1, /* DIFF mode */
};

/*
 * This control is meant to get the count of compressed HEXTILE rectangles which
 * is relevant to the number of differentiated frames if VCD is in DIFF mode.
 * And the count will always be 1 if VCD is in COMPLETE mode.
 */
#define V4L2_CID_NPCM_RECT_COUNT	(V4L2_CID_USER_NPCM_BASE + 1)

#endif /* _UAPI_LINUX_NPCM_VIDEO_H */
