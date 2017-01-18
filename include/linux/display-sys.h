#ifndef _LINUX_DISPLAY_RK_H
#define _LINUX_DISPLAY_RK_H

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <dt-bindings/display/rk_fb.h>

struct rk_display_device;

enum rk_display_priority {
	DISPLAY_PRIORITY_TV = 0,
	DISPLAY_PRIORITY_YPBPR,
	DISPLAY_PRIORITY_VGA,
	DISPLAY_PRIORITY_HDMI,
	DISPLAY_PRIORITY_LCD,
};

enum {
	DISPLAY_SCALE_X = 0,
	DISPLAY_SCALE_Y
};

enum rk_display_property {
	DISPLAY_MAIN = 0,
	DISPLAY_AUX
};


/* HDMI mode list*/
struct display_modelist {
	struct list_head	list;
	struct fb_videomode	mode;
	unsigned int		vic;
	unsigned int		format_3d;
	unsigned int		detail_3d;
};

/* This structure defines all the properties of a Display. */
struct rk_display_driver {
	void (*suspend)(struct rk_display_device *, pm_message_t state);
	void (*resume)(struct rk_display_device *);
	int  (*probe)(struct rk_display_device *, void *);
	int  (*remove)(struct rk_display_device *);
};

struct rk_display_ops {
	int (*setenable)(struct rk_display_device *, int enable);
	int (*getenable)(struct rk_display_device *);
	int (*getstatus)(struct rk_display_device *);
	int (*getmodelist)(struct rk_display_device *,
			   struct list_head **modelist);
	int (*setmode)(struct rk_display_device *,
		       struct fb_videomode *mode);
	int (*getmode)(struct rk_display_device *,
		       struct fb_videomode *mode);
	int (*setscale)(struct rk_display_device *, int, int);
	int (*getscale)(struct rk_display_device *, int);
	int (*get3dmode)(struct rk_display_device *);
	int (*set3dmode)(struct rk_display_device *, int);
	int (*getcolor)(struct rk_display_device *, char *);
	int (*setcolor)(struct rk_display_device *, const char *, int);
	int (*setdebug)(struct rk_display_device *, int);
	int (*getdebug)(struct rk_display_device *, char *);
	int (*getedidaudioinfo)(struct rk_display_device *,
				char *audioinfo, int len);
	int (*getmonspecs)(struct rk_display_device *,
			   struct fb_monspecs *monspecs);
	int (*getvrinfo)(struct rk_display_device *, char *);
};

struct rk_display_device {
	struct module *owner;		/* Owner module */
	struct rk_display_driver *driver;
	struct device *parent;		/* This is the parent */
	struct device *dev;		/* This is this display device */
	struct mutex lock;
	void *priv_data;
	char type[16];
	char *name;
	int idx;
	struct rk_display_ops *ops;
	int priority;
	int property;
	struct list_head list;
};

struct rk_display_devicelist {
	struct list_head list;
	struct rk_display_device *dev;
};

struct rk_display_device
	*rk_display_device_register(struct rk_display_driver *driver,
				    struct device *parent, void *devdata);
void rk_display_device_unregister(struct rk_display_device *dev);
void rk_display_device_enable(struct rk_display_device *ddev);
void rk_display_device_enable_other(struct rk_display_device *ddev);
void rk_display_device_disable_other(struct rk_display_device *ddev);
void rk_display_device_select(int property, int priority);

int display_add_videomode(const struct fb_videomode *mode,
			  struct list_head *head);
#endif
