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
void fbcon_update_vcs(struct fb_info *info, bool all);
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
static inline void fbcon_update_vcs(struct fb_info *info, bool all) {}
#endif

#endif /* _LINUX_FBCON_H */
