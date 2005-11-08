/*
 * $Id: blktrans.h,v 1.6 2005/11/07 11:14:54 gleixner Exp $
 *
 * (C) 2003 David Woodhouse <dwmw2@infradead.org>
 *
 * Interface to Linux block layer for MTD 'translation layers'.
 *
 */

#ifndef __MTD_TRANS_H__
#define __MTD_TRANS_H__

#include <asm/semaphore.h>

struct hd_geometry;
struct mtd_info;
struct mtd_blktrans_ops;
struct file;
struct inode;

struct mtd_blktrans_dev {
	struct mtd_blktrans_ops *tr;
	struct list_head list;
	struct mtd_info *mtd;
	struct semaphore sem;
	int devnum;
	int blksize;
	unsigned long size;
	int readonly;
	void *blkcore_priv; /* gendisk in 2.5, devfs_handle in 2.4 */
};

struct blkcore_priv; /* Differs for 2.4 and 2.5 kernels; private */

struct mtd_blktrans_ops {
	char *name;
	int major;
	int part_bits;

	/* Access functions */
	int (*readsect)(struct mtd_blktrans_dev *dev,
		    unsigned long block, char *buffer);
	int (*writesect)(struct mtd_blktrans_dev *dev,
		     unsigned long block, char *buffer);

	/* Block layer ioctls */
	int (*getgeo)(struct mtd_blktrans_dev *dev, struct hd_geometry *geo);
	int (*flush)(struct mtd_blktrans_dev *dev);

	/* Called with mtd_table_mutex held; no race with add/remove */
	int (*open)(struct mtd_blktrans_dev *dev);
	int (*release)(struct mtd_blktrans_dev *dev);

	/* Called on {de,}registration and on subsequent addition/removal
	   of devices, with mtd_table_mutex held. */
	void (*add_mtd)(struct mtd_blktrans_ops *tr, struct mtd_info *mtd);
	void (*remove_dev)(struct mtd_blktrans_dev *dev);

	struct list_head devs;
	struct list_head list;
	struct module *owner;

	struct mtd_blkcore_priv *blkcore_priv;
};

extern int register_mtd_blktrans(struct mtd_blktrans_ops *tr);
extern int deregister_mtd_blktrans(struct mtd_blktrans_ops *tr);
extern int add_mtd_blktrans_dev(struct mtd_blktrans_dev *dev);
extern int del_mtd_blktrans_dev(struct mtd_blktrans_dev *dev);


#endif /* __MTD_TRANS_H__ */
