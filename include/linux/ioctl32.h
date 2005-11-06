#ifndef IOCTL32_H
#define IOCTL32_H 1

#include <linux/compiler.h>	/* for __deprecated */

struct file;

typedef int (*ioctl_trans_handler_t)(unsigned int, unsigned int,
					unsigned long, struct file *);

struct ioctl_trans {
	unsigned long cmd;
	ioctl_trans_handler_t handler;
	struct ioctl_trans *next;
};

#endif
