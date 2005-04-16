#ifndef __LINUX_PHONEDEV_H
#define __LINUX_PHONEDEV_H

#include <linux/types.h>
#include <linux/version.h>

#ifdef __KERNEL__

#include <linux/poll.h>

struct phone_device {
	struct phone_device *next;
	struct file_operations *f_op;
	int (*open) (struct phone_device *, struct file *);
	int board;		/* Device private index */
	int minor;
};

extern int phonedev_init(void);
#define PHONE_MAJOR	100
extern int phone_register_device(struct phone_device *, int unit);
#define PHONE_UNIT_ANY	-1
extern void phone_unregister_device(struct phone_device *);

#endif
#endif
