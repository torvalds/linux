#ifndef __linux_video_edid_h__
#define __linux_video_edid_h__

#include <uapi/video/edid.h>

#ifdef CONFIG_X86
extern struct edid_info edid_info;
#endif
#endif /* __linux_video_edid_h__ */
