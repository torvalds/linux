#ifndef _LKL_HOST_H
#define _LKL_HOST_H

#include <lkl/asm/host_ops.h>
#include <lkl.h>

extern struct lkl_host_operations lkl_host_ops;

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt - printf like format string
 */
int lkl_printf(const char *fmt, ...);

char lkl_virtio_devs[256];

struct lkl_dev_buf {
	void *addr;
	unsigned int len;
};

extern struct lkl_dev_blk_ops lkl_dev_blk_ops;

#define LKL_DEV_BLK_TYPE_READ		0
#define LKL_DEV_BLK_TYPE_WRITE		1
#define LKL_DEV_BLK_TYPE_FLUSH		4
#define LKL_DEV_BLK_TYPE_FLUSH_OUT	5

struct lkl_dev_blk_ops {
	int (*get_capacity)(union lkl_disk disk,
			    unsigned long long *res);
	void (*request)(union lkl_disk disk, unsigned int type,
			unsigned int prio, unsigned long long sector,
			struct lkl_dev_buf *bufs, int count);
};

#define LKL_DEV_BLK_STATUS_OK		0
#define LKL_DEV_BLK_STATUS_IOERR	1
#define LKL_DEV_BLK_STATUS_UNSUP	2

void lkl_dev_blk_complete(struct lkl_dev_buf *bufs, unsigned char status,
			  int len);
#endif
