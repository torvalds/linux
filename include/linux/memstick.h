/*
 *  Sony MemoryStick support
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _MEMSTICK_H
#define _MEMSTICK_H

#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/device.h>

/*** Hardware based structures ***/

struct ms_status_register {
	unsigned char reserved;
	unsigned char interrupt;
#define MEMSTICK_INT_CMDNAK             0x0001
#define MEMSTICK_INT_BREQ               0x0020
#define MEMSTICK_INT_ERR                0x0040
#define MEMSTICK_INT_CED                0x0080

	unsigned char status0;
#define MEMSTICK_STATUS0_WP             0x0001
#define MEMSTICK_STATUS0_SL             0x0002
#define MEMSTICK_STATUS0_BF             0x0010
#define MEMSTICK_STATUS0_BE             0x0020
#define MEMSTICK_STATUS0_FB0            0x0040
#define MEMSTICK_STATUS0_MB             0x0080

	unsigned char status1;
#define MEMSTICK_STATUS1_UCFG           0x0001
#define MEMSTICK_STATUS1_FGER           0x0002
#define MEMSTICK_STATUS1_UCEX           0x0004
#define MEMSTICK_STATUS1_EXER           0x0008
#define MEMSTICK_STATUS1_UCDT           0x0010
#define MEMSTICK_STATUS1_DTER           0x0020
#define MEMSTICK_STATUS1_FBI            0x0040
#define MEMSTICK_STATUS1_MB             0x0080
} __attribute__((packed));

struct ms_id_register {
	unsigned char type;
	unsigned char reserved;
	unsigned char category;
	unsigned char class;
} __attribute__((packed));

struct ms_param_register {
	unsigned char system;
	unsigned char block_address_msb;
	unsigned short block_address;
	unsigned char cp;
#define MEMSTICK_CP_BLOCK               0x0000
#define MEMSTICK_CP_PAGE                0x0020
#define MEMSTICK_CP_EXTRA               0x0040
#define MEMSTICK_CP_OVERWRITE           0x0080

	unsigned char page_address;
} __attribute__((packed));

struct ms_extra_data_register {
	unsigned char  overwrite_flag;
#define MEMSTICK_OVERWRITE_UPDATA       0x0010
#define MEMSTICK_OVERWRITE_PAGE         0x0060
#define MEMSTICK_OVERWRITE_BLOCK        0x0080

	unsigned char  management_flag;
#define MEMSTICK_MANAGEMENT_SYSTEM      0x0004
#define MEMSTICK_MANAGEMENT_TRANS_TABLE 0x0008
#define MEMSTICK_MANAGEMENT_COPY        0x0010
#define MEMSTICK_MANAGEMENT_ACCESS      0x0020

	unsigned short logical_address;
} __attribute__((packed));

struct ms_register {
	struct ms_status_register     status;
	struct ms_id_register         id;
	unsigned char                 reserved[8];
	struct ms_param_register      param;
	struct ms_extra_data_register extra_data;
} __attribute__((packed));

struct mspro_param_register {
	unsigned char  system;
	unsigned short data_count;
	unsigned int   data_address;
	unsigned char  cmd_param;
} __attribute__((packed));

struct mspro_register {
	struct ms_status_register    status;
	struct ms_id_register        id;
	unsigned char                reserved[8];
	struct mspro_param_register  param;
} __attribute__((packed));

struct ms_register_addr {
	unsigned char r_offset;
	unsigned char r_length;
	unsigned char w_offset;
	unsigned char w_length;
} __attribute__((packed));

enum {
	MS_TPC_READ_LONG_DATA   = 0x02,
	MS_TPC_READ_SHORT_DATA  = 0x03,
	MS_TPC_READ_REG         = 0x04,
	MS_TPC_READ_IO_DATA     = 0x05, /* unverified */
	MS_TPC_GET_INT          = 0x07,
	MS_TPC_SET_RW_REG_ADRS  = 0x08,
	MS_TPC_EX_SET_CMD       = 0x09,
	MS_TPC_WRITE_IO_DATA    = 0x0a, /* unverified */
	MS_TPC_WRITE_REG        = 0x0b,
	MS_TPC_WRITE_SHORT_DATA = 0x0c,
	MS_TPC_WRITE_LONG_DATA  = 0x0d,
	MS_TPC_SET_CMD          = 0x0e
};

enum {
	MS_CMD_BLOCK_END     = 0x33,
	MS_CMD_RESET         = 0x3c,
	MS_CMD_BLOCK_WRITE   = 0x55,
	MS_CMD_SLEEP         = 0x5a,
	MS_CMD_BLOCK_ERASE   = 0x99,
	MS_CMD_BLOCK_READ    = 0xaa,
	MS_CMD_CLEAR_BUF     = 0xc3,
	MS_CMD_FLASH_STOP    = 0xcc,
	MSPRO_CMD_FORMAT     = 0x10,
	MSPRO_CMD_SLEEP      = 0x11,
	MSPRO_CMD_READ_DATA  = 0x20,
	MSPRO_CMD_WRITE_DATA = 0x21,
	MSPRO_CMD_READ_ATRB  = 0x24,
	MSPRO_CMD_STOP       = 0x25,
	MSPRO_CMD_ERASE      = 0x26,
	MSPRO_CMD_SET_IBA    = 0x46,
	MSPRO_CMD_SET_IBD    = 0x47
/*
	MSPRO_CMD_RESET
	MSPRO_CMD_WAKEUP
	MSPRO_CMD_IN_IO_DATA
	MSPRO_CMD_OUT_IO_DATA
	MSPRO_CMD_READ_IO_ATRB
	MSPRO_CMD_IN_IO_FIFO
	MSPRO_CMD_OUT_IO_FIFO
	MSPRO_CMD_IN_IOM
	MSPRO_CMD_OUT_IOM
*/
};

/*** Driver structures and functions ***/

#define MEMSTICK_PART_SHIFT 3

enum memstick_param { MEMSTICK_POWER = 1, MEMSTICK_INTERFACE };

#define MEMSTICK_POWER_OFF 0
#define MEMSTICK_POWER_ON  1

#define MEMSTICK_SERIAL   0
#define MEMSTICK_PARALLEL 1

struct memstick_host;
struct memstick_driver;

#define MEMSTICK_MATCH_ALL            0x01

#define MEMSTICK_TYPE_LEGACY          0xff
#define MEMSTICK_TYPE_DUO             0x00
#define MEMSTICK_TYPE_PRO             0x01

#define MEMSTICK_CATEGORY_STORAGE     0xff
#define MEMSTICK_CATEGORY_STORAGE_DUO 0x00

#define MEMSTICK_CLASS_GENERIC        0xff
#define MEMSTICK_CLASS_GENERIC_DUO    0x00


struct memstick_device_id {
	unsigned char match_flags;
	unsigned char type;
	unsigned char category;
	unsigned char class;
};

struct memstick_request {
	unsigned char tpc;
	unsigned char data_dir:1,
		      need_card_int:1,
		      get_int_reg:1,
		      io_type:2;
#define               MEMSTICK_IO_NONE 0
#define               MEMSTICK_IO_VAL  1
#define               MEMSTICK_IO_SG   2

	unsigned char int_reg;
	int           error;
	union {
		struct scatterlist sg;
		struct {
			unsigned char data_len;
			unsigned char data[15];
		};
	};
};

struct memstick_dev {
	struct memstick_device_id id;
	struct memstick_host     *host;
	struct ms_register_addr  reg_addr;
	struct completion        mrq_complete;
	struct memstick_request  current_mrq;

	/* Check that media driver is still willing to operate the device. */
	int                      (*check)(struct memstick_dev *card);
	/* Get next request from the media driver.                         */
	int                      (*next_request)(struct memstick_dev *card,
						 struct memstick_request **mrq);

	struct device            dev;
};

struct memstick_host {
	struct mutex        lock;
	unsigned int        id;
	unsigned int        caps;
#define MEMSTICK_CAP_PARALLEL      1
#define MEMSTICK_CAP_AUTO_GET_INT  2

	struct work_struct  media_checker;
	struct class_device cdev;

	struct memstick_dev *card;
	unsigned int        retries;

	/* Notify the host that some requests are pending. */
	void                (*request)(struct memstick_host *host);
	/* Set host IO parameters (power, clock, etc).     */
	void                (*set_param)(struct memstick_host *host,
					 enum memstick_param param,
					 int value);
	unsigned long       private[0] ____cacheline_aligned;
};

struct memstick_driver {
	struct memstick_device_id *id_table;
	int                       (*probe)(struct memstick_dev *card);
	void                      (*remove)(struct memstick_dev *card);
	int                       (*suspend)(struct memstick_dev *card,
					     pm_message_t state);
	int                       (*resume)(struct memstick_dev *card);

	struct device_driver      driver;
};

int memstick_register_driver(struct memstick_driver *drv);
void memstick_unregister_driver(struct memstick_driver *drv);

struct memstick_host *memstick_alloc_host(unsigned int extra,
					  struct device *dev);

int memstick_add_host(struct memstick_host *host);
void memstick_remove_host(struct memstick_host *host);
void memstick_free_host(struct memstick_host *host);
void memstick_detect_change(struct memstick_host *host);

void memstick_init_req_sg(struct memstick_request *mrq, unsigned char tpc,
			  struct scatterlist *sg);
void memstick_init_req(struct memstick_request *mrq, unsigned char tpc,
		       void *buf, size_t length);
int memstick_next_req(struct memstick_host *host,
		      struct memstick_request **mrq);
void memstick_new_req(struct memstick_host *host);

int memstick_set_rw_addr(struct memstick_dev *card);

static inline void *memstick_priv(struct memstick_host *host)
{
	return (void *)host->private;
}

static inline void *memstick_get_drvdata(struct memstick_dev *card)
{
	return dev_get_drvdata(&card->dev);
}

static inline void memstick_set_drvdata(struct memstick_dev *card, void *data)
{
	dev_set_drvdata(&card->dev, data);
}

#endif
