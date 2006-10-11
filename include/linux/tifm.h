/*
 *  tifm.h - TI FlashMedia driver
 *
 *  Copyright (C) 2006 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _TIFM_H
#define _TIFM_H

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>

/* Host registers (relative to pci base address): */
enum {
	FM_SET_INTERRUPT_ENABLE   = 0x008,
	FM_CLEAR_INTERRUPT_ENABLE = 0x00c,
	FM_INTERRUPT_STATUS       = 0x014 };

/* Socket registers (relative to socket base address): */
enum {
	SOCK_CONTROL                   = 0x004,
	SOCK_PRESENT_STATE             = 0x008,
	SOCK_DMA_ADDRESS               = 0x00c,
	SOCK_DMA_CONTROL               = 0x010,
	SOCK_DMA_FIFO_INT_ENABLE_SET   = 0x014,
	SOCK_DMA_FIFO_INT_ENABLE_CLEAR = 0x018,
	SOCK_DMA_FIFO_STATUS           = 0x020,
	SOCK_FIFO_CONTROL              = 0x024,
	SOCK_FIFO_PAGE_SIZE            = 0x028,
	SOCK_MMCSD_COMMAND             = 0x104,
	SOCK_MMCSD_ARG_LOW             = 0x108,
	SOCK_MMCSD_ARG_HIGH            = 0x10c,
	SOCK_MMCSD_CONFIG              = 0x110,
	SOCK_MMCSD_STATUS              = 0x114,
	SOCK_MMCSD_INT_ENABLE          = 0x118,
	SOCK_MMCSD_COMMAND_TO          = 0x11c,
	SOCK_MMCSD_DATA_TO             = 0x120,
	SOCK_MMCSD_DATA                = 0x124,
	SOCK_MMCSD_BLOCK_LEN           = 0x128,
	SOCK_MMCSD_NUM_BLOCKS          = 0x12c,
	SOCK_MMCSD_BUFFER_CONFIG       = 0x130,
	SOCK_MMCSD_SPI_CONFIG          = 0x134,
	SOCK_MMCSD_SDIO_MODE_CONFIG    = 0x138,
	SOCK_MMCSD_RESPONSE            = 0x144,
	SOCK_MMCSD_SDIO_SR             = 0x164,
	SOCK_MMCSD_SYSTEM_CONTROL      = 0x168,
	SOCK_MMCSD_SYSTEM_STATUS       = 0x16c,
	SOCK_MS_COMMAND                = 0x184,
	SOCK_MS_DATA                   = 0x188,
	SOCK_MS_STATUS                 = 0x18c,
	SOCK_MS_SYSTEM                 = 0x190,
	SOCK_FIFO_ACCESS               = 0x200 };


#define TIFM_IRQ_ENABLE           0x80000000
#define TIFM_IRQ_SOCKMASK         0x00000001
#define TIFM_IRQ_CARDMASK         0x00000100
#define TIFM_IRQ_FIFOMASK         0x00010000
#define TIFM_IRQ_SETALL           0xffffffff
#define TIFM_IRQ_SETALLSOCK       0x0000000f

#define TIFM_CTRL_LED             0x00000040
#define TIFM_CTRL_FAST_CLK        0x00000100

#define TIFM_SOCK_STATE_OCCUPIED  0x00000008
#define TIFM_SOCK_STATE_POWERED   0x00000080

#define TIFM_FIFO_ENABLE          0x00000001 /* Meaning of this constant is unverified */
#define TIFM_FIFO_INT_SETALL      0x0000ffff
#define TIFM_FIFO_INTMASK         0x00000005 /* Meaning of this constant is unverified */

#define TIFM_DMA_RESET            0x00000002 /* Meaning of this constant is unverified */
#define TIFM_DMA_TX               0x00008000 /* Meaning of this constant is unverified */
#define TIFM_DMA_EN               0x00000001 /* Meaning of this constant is unverified */

typedef enum {FM_NULL = 0, FM_XD = 0x01, FM_MS = 0x02, FM_SD = 0x03} tifm_media_id;

struct tifm_driver;
struct tifm_dev {
	char __iomem            *addr;
	spinlock_t              lock;
	tifm_media_id           media_id;
	char                    wq_name[KOBJ_NAME_LEN];
	struct workqueue_struct *wq;

	unsigned int            (*signal_irq)(struct tifm_dev *sock,
					      unsigned int sock_irq_status);

	struct tifm_driver      *drv;
	struct device           dev;
};

struct tifm_driver {
	tifm_media_id        *id_table;
	int                  (*probe)(struct tifm_dev *dev);
	void                 (*remove)(struct tifm_dev *dev);

	struct device_driver driver;
};

struct tifm_adapter {
	char __iomem            *addr;
	unsigned int            irq_status;
	unsigned int            insert_mask;
	unsigned int            remove_mask;
	spinlock_t              lock;
	unsigned int            id;
	unsigned int            max_sockets;
	char                    wq_name[KOBJ_NAME_LEN];
	unsigned int            inhibit_new_cards;
	struct workqueue_struct *wq;
	struct work_struct      media_inserter;
	struct work_struct      media_remover;
	struct tifm_dev         **sockets;
	struct class_device     cdev;
	struct device           *dev;

	void                    (*eject)(struct tifm_adapter *fm, struct tifm_dev *sock);
};

struct tifm_adapter *tifm_alloc_adapter(void);
void tifm_free_device(struct device *dev);
void tifm_free_adapter(struct tifm_adapter *fm);
int tifm_add_adapter(struct tifm_adapter *fm);
void tifm_remove_adapter(struct tifm_adapter *fm);
struct tifm_dev *tifm_alloc_device(struct tifm_adapter *fm, unsigned int id);
int tifm_register_driver(struct tifm_driver *drv);
void tifm_unregister_driver(struct tifm_driver *drv);
void tifm_eject(struct tifm_dev *sock);
int tifm_map_sg(struct tifm_dev *sock, struct scatterlist *sg, int nents,
		int direction);
void tifm_unmap_sg(struct tifm_dev *sock, struct scatterlist *sg, int nents,
		   int direction);


static inline void *tifm_get_drvdata(struct tifm_dev *dev)
{
        return dev_get_drvdata(&dev->dev);
}

static inline void tifm_set_drvdata(struct tifm_dev *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

struct tifm_device_id {
	tifm_media_id media_id;
};

#endif
