#ifndef __linux_video_edid_h__
#define __linux_video_edid_h__

#ifdef __KERNEL__


#ifdef CONFIG_X86
struct edid_info {
	unsigned char dummy[128];
};

extern struct edid_info edid_info;
#endif /* CONFIG_X86 */

#endif /* __KERNEL__ */

#endif /* __linux_video_edid_h__ */
