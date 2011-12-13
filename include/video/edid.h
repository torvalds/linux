#ifndef __linux_video_edid_h__
#define __linux_video_edid_h__

struct edid_info {
	unsigned char dummy[128];
};

#ifdef __KERNEL__
#ifdef CONFIG_X86
extern struct edid_info edid_info;
#endif
#endif

#endif /* __linux_video_edid_h__ */
