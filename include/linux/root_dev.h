/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ROOT_DEV_H_
#define _ROOT_DEV_H_

#include <linux/major.h>
#include <linux/types.h>
#include <linux/kdev_t.h>

enum {
	Root_NFS = MKDEV(UNNAMED_MAJOR, 255),
	Root_CIFS = MKDEV(UNNAMED_MAJOR, 254),
	Root_Generic = MKDEV(UNNAMED_MAJOR, 253),
	Root_RAM0 = MKDEV(RAMDISK_MAJOR, 0),
};

extern dev_t ROOT_DEV;

#endif
