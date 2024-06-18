#ifndef _LINUX_FBCON_H
#define _LINUX_FBCON_H

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
void __init fb_console_init(void);
void __exit fb_console_exit(void);
int fbcon_fb_registered(struct fb_info *info);
void fbcon_fb_unregistered(struct fb_info *info);
void fbcon_fb_unbind(struct fb_info *info);
void fbcon_suspended(struct fb_info *info);
void fbcon_resumed(struct fb_info *info);
int fbcon_mode_deleted(struct fb_info *info,
		       struct fb_videomode *mode);
void fbcon_new_modelist(struct fb_info *info);
void fbcon_get_requirement(struct fb_info *info,
			   struct fb_blit_caps *caps);
void fbcon_fb_blanked(struct fb_info *info, int blank);
int  fbcon_modechange_possible(struct fb_info *info,
			       struct fb_var_screeninfo *var);
void fbcon_update_vcs(struct fb_info *info, bool all);
void fbcon_remap_all(struct fb_info *info);
int fbcon_set_con2fb_map_ioctl(void __user *argp);
int fbcon_get_con2fb_map_ioctl(void __user *argp);
#else
static inline void fb_console_init(void) {}
static inline void fb_console_exit(void) {}
static inline int fbcon_fb_registered(struct fb_info *info) { return 0; }
static inline void fbcon_fb_unregistered(struct fb_info *info) {}
static inline void fbcon_fb_unbind(struct fb_info *info) {}
static inline void fbcon_suspended(struct fb_info *info) {}
static inline void fbcon_resumed(struct fb_info *info) {}
static inline int fbcon_mode_deleted(struct fb_info *info,
				     struct fb_videomode *mode) { return 0; }
static inline void fbcon_new_modelist(struct fb_info *info) {}
static inline void fbcon_get_requirement(struct fb_info *info,
					 struct fb_blit_caps *caps) {}
static inline void fbcon_fb_blanked(struct fb_info *info, int blank) {}
static inline int  fbcon_modechange_possible(struct fb_info *info,
				struct fb_var_screeninfo *var) { return 0; }
static inline void fbcon_update_vcs(struct fb_info *info, bool all) {}
static inline void fbcon_remap_all(struct fb_info *info) {}
static inline int fbcon_set_con2fb_map_ioctl(void __user *argp) { return 0; }
static inline int fbcon_get_con2fb_map_ioctl(void __user *argp) { return 0; }
#endif

#endif /* _LINUX_FBCON_H */
