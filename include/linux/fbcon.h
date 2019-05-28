#ifndef _LINUX_FBCON_H
#define _LINUX_FBCON_H

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
void __init fb_console_init(void);
void __exit fb_console_exit(void);
int fbcon_fb_registered(struct fb_info *info);
void fbcon_fb_unregistered(struct fb_info *info);
#else
static inline void fb_console_init(void) {}
static inline void fb_console_exit(void) {}
static inline int fbcon_fb_registered(struct fb_info *info) { return 0; }
static inline void fbcon_fb_unregistered(struct fb_info *info) {}
#endif

#endif /* _LINUX_FBCON_H */
