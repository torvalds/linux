#ifndef _LINUX_CONSOLE_DECOR_H_
#define _LINUX_CONSOLE_DECOR_H_ 1

/* A structure used by the framebuffer console decorations (drivers/video/console/fbcondecor.c) */
struct vc_decor {
	__u8 bg_color;				/* The color that is to be treated as transparent */
	__u8 state;				/* Current decor state: 0 = off, 1 = on */
	__u16 tx, ty;				/* Top left corner coordinates of the text field */
	__u16 twidth, theight;			/* Width and height of the text field */
	char* theme;
};

#ifdef __KERNEL__
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

struct vc_decor32 {
	__u8 bg_color;				/* The color that is to be treated as transparent */
	__u8 state;				/* Current decor state: 0 = off, 1 = on */
	__u16 tx, ty;				/* Top left corner coordinates of the text field */
	__u16 twidth, theight;			/* Width and height of the text field */
	compat_uptr_t theme;
};

#define vc_decor_from_compat(to, from) \
	(to).bg_color = (from).bg_color; \
	(to).state    = (from).state; \
	(to).tx       = (from).tx; \
	(to).ty       = (from).ty; \
	(to).twidth   = (from).twidth; \
	(to).theight  = (from).theight; \
	(to).theme    = compat_ptr((from).theme)

#define vc_decor_to_compat(to, from) \
	(to).bg_color = (from).bg_color; \
	(to).state    = (from).state; \
	(to).tx       = (from).tx; \
	(to).ty       = (from).ty; \
	(to).twidth   = (from).twidth; \
	(to).theight  = (from).theight; \
	(to).theme    = ptr_to_compat((from).theme)

#endif /* CONFIG_COMPAT */
#endif /* __KERNEL__ */

#endif
