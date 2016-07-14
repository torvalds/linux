/*
* Huawei SSD device driver
* Copyright (c) 2016, Huawei Technologies Co., Ltd.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*/

#ifndef _HIO_H
#define _HIO_H

#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>



typedef int (*ssd_event_call)(struct gendisk *, int, int);	/* gendisk, event id, event level */
extern int ssd_register_event_notifier(struct block_device *bdev, ssd_event_call event_call);
/* unregister event notifier before module exit */
extern int ssd_unregister_event_notifier(struct block_device *bdev);


/* label */
#define SSD_LABEL_FIELD_SZ	32
#define SSD_SN_SZ			16

typedef struct ssd_label
{
	char date[SSD_LABEL_FIELD_SZ];
	char sn[SSD_LABEL_FIELD_SZ];
	char part[SSD_LABEL_FIELD_SZ];
	char desc[SSD_LABEL_FIELD_SZ];
	char other[SSD_LABEL_FIELD_SZ];
	char maf[SSD_LABEL_FIELD_SZ];
} ssd_label_t;


/* version */
typedef struct ssd_version_info
{
	uint32_t bridge_ver;	/* bridge fw version: hex */
	uint32_t ctrl_ver;		/* controller fw version: hex */
	uint32_t bm_ver;		/* battery manager fw version: hex */
	uint8_t  pcb_ver;		/* main pcb version: char */
	uint8_t  upper_pcb_ver;
	uint8_t  pad0;
	uint8_t  pad1;
} ssd_version_info_t;

extern int ssd_get_label(struct block_device *bdev, struct ssd_label *label);
extern int ssd_get_version(struct block_device *bdev, struct ssd_version_info *ver);
extern int ssd_get_temperature(struct block_device *bdev, int *temp);


enum ssd_bmstatus
{
	SSD_BMSTATUS_OK = 0,
	SSD_BMSTATUS_CHARGING, 
	SSD_BMSTATUS_WARNING
};
extern int ssd_bm_status(struct block_device *bdev, int *status);

enum ssd_otprotect
{
	SSD_OTPROTECT_OFF = 0,
	SSD_OTPROTECT_ON
};
extern int ssd_set_otprotect(struct block_device *bdev, int otprotect);

typedef struct pci_addr
{
	uint16_t domain;
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
} pci_addr_t;
extern int ssd_get_pciaddr(struct block_device *bdev, struct pci_addr *paddr);

/* submit phys bio: phys addr in iovec */
extern void ssd_submit_pbio(struct request_queue *q, struct bio *bio);

extern int ssd_reset(struct block_device *bdev);

enum ssd_write_mode
{
	SSD_WMODE_BUFFER = 0,
	SSD_WMODE_BUFFER_EX,
	SSD_WMODE_FUA,
	/* dummy */
	SSD_WMODE_AUTO, 
	SSD_WMODE_DEFAULT
};
extern int ssd_set_wmode(struct block_device *bdev, int wmode);

#endif

