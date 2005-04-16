/*
 * Routines for handling backlight control on PowerBooks
 *
 * For now, implementation resides in arch/ppc/kernel/pmac_support.c
 *
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_BACKLIGHT_H
#define __ASM_PPC_BACKLIGHT_H

/* Abstract values */
#define BACKLIGHT_OFF	0
#define BACKLIGHT_MIN	1
#define BACKLIGHT_MAX	0xf

struct backlight_controller {
	int (*set_enable)(int enable, int level, void *data);
	int (*set_level)(int level, void *data);
};

extern void register_backlight_controller(struct backlight_controller *ctrler, void *data, char *type);
extern void unregister_backlight_controller(struct backlight_controller *ctrler, void *data);

extern int set_backlight_enable(int enable);
extern int get_backlight_enable(void);
extern int set_backlight_level(int level);
extern int get_backlight_level(void);

#endif
#endif /* __KERNEL__ */
