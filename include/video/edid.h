/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __linux_video_edid_h__
#define __linux_video_edid_h__

#include <uapi/video/edid.h>

#if defined(CONFIG_FIRMWARE_EDID)
extern struct edid_info edid_info;
#endif

#endif /* __linux_video_edid_h__ */
