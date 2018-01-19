#ifndef _LINUX_FBCON_H
#define _LINUX_FBCON_H

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
void __init fb_console_init(void);
void __exit fb_console_exit(void);
#else
static inline void fb_console_init(void) {}
static inline void fb_console_exit(void) {}
#endif

#endif /* _LINUX_FBCON_H */
