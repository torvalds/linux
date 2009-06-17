/*
 *  include/linux/mg_disk.c
 *
 *  Private data for mflash platform driver
 *
 * (c) 2008 mGine Co.,LTD
 * (c) 2008 unsik Kim <donari75@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __MG_DISK_H__
#define __MG_DISK_H__

/* name for platform device */
#define MG_DEV_NAME "mg_disk"

/* names of GPIO resource */
#define MG_RST_PIN	"mg_rst"
/* except MG_BOOT_DEV, reset-out pin should be assigned */
#define MG_RSTOUT_PIN	"mg_rstout"

/* device attribution */
/* use mflash as boot device */
#define MG_BOOT_DEV		(1 << 0)
/* use mflash as storage device */
#define MG_STORAGE_DEV		(1 << 1)
/* same as MG_STORAGE_DEV, but bootloader already done reset sequence */
#define MG_STORAGE_DEV_SKIP_RST	(1 << 2)

/* private driver data */
struct mg_drv_data {
	/* disk resource */
	u32 use_polling;

	/* device attribution */
	u32 dev_attr;

	/* internally used */
	void *host;
};

#endif
