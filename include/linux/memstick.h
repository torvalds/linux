/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Sony MemoryStick support
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
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
#define MEMSTICK_INT_CMDNAK 0x01
#define MEMSTICK_INT_IOREQ  0x08
#define MEMSTICK_INT_IOBREQ 0x10
#define MEMSTICK_INT_BREQ   0x20
#define MEMSTICK_INT_ERR    0x40
#define MEMSTICK_INT_CED    0x80

	unsigned char status0;
#define MEMSTICK_STATUS0_WP  0x01
#define MEMSTICK_STATUS0_SL  0x02
#define MEMSTICK_STATUS0_BF  0x10
#define MEMSTICK_STATUS0_BE  0x20
#define MEMSTICK_STATUS0_FB0 0x40
#define MEMSTICK_STATUS0_MB  0x80

	unsigned char status1;
#define MEMSTICK_STATUS1_UCFG 0x01
#define MEMSTICK_STATUS1_FGER 0x02
#define MEMSTICK_STATUS1_UCEX 0x04
#define MEMSTICK_STATUS1_EXER 0x08
#define MEMSTICK_STATUS1_UCDT 0x10
#define MEMSTICK_STATUS1_DTER 0x20
#define MEMSTICK_STATUS1_FB1  0x40
#define MEMSTICK_STATUS1_MB   0x80
} __attribute__((packed));

struct ms_id_register {
	unsigned char type;
	unsigned char if_mode;
	unsigned char category;
	unsigned char class;
} __attribute__((packed));

struct ms_param_register {
	unsigned char system;
#define MEMSTICK_SYS_PAM  0x08
#define MEMSTICK_SYS_BAMD 0x80

	unsigned char block_address_msb;
	unsigned short block_address;
	unsigned char cp;
#define MEMSTICK_CP_BLOCK     0x00
#define MEMSTICK_CP_PAGE      0x20
#define MEMSTICK_CP_EXTRA     0x40
#define MEMSTICK_CP_OVERWRITE 0x80

	unsigned char page_address;
} __attribute__((packed));

struct ms_extra_data_register {
	unsigned char  overwrite_flag;
#define MEMSTICK_OVERWRITE_UDST  0x10
#define MEMSTICK_OVERWRITE_PGST1 0x20
#define MEMSTICK_OVERWRITE_PGST0 0x40
#define MEMSTICK_OVERWRITE_BKST  0x80

	unsigned char  management_flag;
#define MEMSTICK_MANAGEMENT_SYSFLG 0x04
#define MEMSTICK_MANAGEMENT_ATFLG  0x08
#define MEMSTICK_MANAGEMENT_SCMS1  0x10
#define MEMSTICK_MANAGEMENT_SCMS0  0x20

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
#define MEMSTICK_SYS_PAR4   0x00
#define MEMSTICK_SYS_PAR8   0x40
#define MEMSTICK_SYS_SERIAL 0x80

	__be16 data_count;
	__be32 data_address;
	unsigned char  tpc_param;
} __attribute__((packed));

struct mspro_io_info_register {
	unsigned char version;
	unsigned char io_category;
	unsigned char current_req;
	unsigned char card_opt_info;
	unsigned char rdy_wait_time;
} __attribute__((packed));

struct mspro_io_func_register {
	unsigned char func_enable;
	unsigned char func_select;
	unsigned char func_intmask;
	unsigned char transfer_mode;
} __attribute__((packed));

struct mspro_io_cmd_register {
	unsigned short tpc_param;
	unsigned short data_count;
	unsigned int   data_address;
} __attribute__((packed));

struct mspro_register {
	struct ms_status_register     status;
	struct ms_id_register         id;
	unsigned char                 reserved0[8];
	struct mspro_param_register   param;
	unsigned char                 reserved1[8];
	struct mspro_io_info_register io_info;
	struct mspro_io_func_register io_func;
	unsigned char                 reserved2[7];
	struct mspro_io_cmd_register  io_cmd;
	unsigned char                 io_int;
	unsigned char                 io_int_func;
} __attribute__((packed));

struct ms_register_addr {
	unsigned char r_offset;
	unsigned char r_length;
	unsigned char w_offset;
	unsigned char w_length;
} __attribute__((packed));

enum memstick_tpc {
	MS_TPC_READ_MG_STATUS   = 0x01,
	MS_TPC_READ_LONG_DATA   = 0x02,
	MS_TPC_READ_SHORT_DATA  = 0x03,
	MS_TPC_READ_MG_DATA     = 0x03,
	MS_TPC_READ_REG         = 0x04,
	MS_TPC_READ_QUAD_DATA   = 0x05,
	MS_TPC_READ_IO_DATA     = 0x05,
	MS_TPC_GET_INT          = 0x07,
	MS_TPC_SET_RW_REG_ADRS  = 0x08,
	MS_TPC_EX_SET_CMD       = 0x09,
	MS_TPC_WRITE_QUAD_DATA  = 0x0a,
	MS_TPC_WRITE_IO_DATA    = 0x0a,
	MS_TPC_WRITE_REG        = 0x0b,
	MS_TPC_WRITE_SHORT_DATA = 0x0c,
	MS_TPC_WRITE_MG_DATA    = 0x0c,
	MS_TPC_WRITE_LONG_DATA  = 0x0d,
	MS_TPC_SET_CMD          = 0x0e
};

enum memstick_command {
	MS_CMD_BLOCK_END       = 0x33,
	MS_CMD_RESET           = 0x3c,
	MS_CMD_BLOCK_WRITE     = 0x55,
	MS_CMD_SLEEP           = 0x5a,
	MS_CMD_BLOCK_ERASE     = 0x99,
	MS_CMD_BLOCK_READ      = 0xaa,
	MS_CMD_CLEAR_BUF       = 0xc3,
	MS_CMD_FLASH_STOP      = 0xcc,
	MS_CMD_LOAD_ID         = 0x60,
	MS_CMD_CMP_ICV         = 0x7f,
	MSPRO_CMD_FORMAT       = 0x10,
	MSPRO_CMD_SLEEP        = 0x11,
	MSPRO_CMD_WAKEUP       = 0x12,
	MSPRO_CMD_READ_DATA    = 0x20,
	MSPRO_CMD_WRITE_DATA   = 0x21,
	MSPRO_CMD_READ_ATRB    = 0x24,
	MSPRO_CMD_STOP         = 0x25,
	MSPRO_CMD_ERASE        = 0x26,
	MSPRO_CMD_READ_QUAD    = 0x27,
	MSPRO_CMD_WRITE_QUAD   = 0x28,
	MSPRO_CMD_SET_IBD      = 0x46,
	MSPRO_CMD_GET_IBD      = 0x47,
	MSPRO_CMD_IN_IO_DATA   = 0xb0,
	MSPRO_CMD_OUT_IO_DATA  = 0xb1,
	MSPRO_CMD_READ_IO_ATRB = 0xb2,
	MSPRO_CMD_IN_IO_FIFO   = 0xb3,
	MSPRO_CMD_OUT_IO_FIFO  = 0xb4,
	MSPRO_CMD_IN_IOM       = 0xb5,
	MSPRO_CMD_OUT_IOM      = 0xb6,
};

/*** Driver structures and functions ***/

enum memstick_param { MEMSTICK_POWER = 1, MEMSTICK_INTERFACE };

#define MEMSTICK_POWER_OFF 0
#define MEMSTICK_POWER_ON  1

#define MEMSTICK_SERIAL   0
#define MEMSTICK_PAR4     1
#define MEMSTICK_PAR8     2

struct memstick_host;
struct memstick_driver;

struct memstick_device_id {
	unsigned char match_flags;
#define MEMSTICK_MATCH_ALL            0x01

	unsigned char type;
#define MEMSTICK_TYPE_LEGACY          0xff
#define MEMSTICK_TYPE_DUO             0x00
#define MEMSTICK_TYPE_PRO             0x01

	unsigned char category;
#define MEMSTICK_CATEGORY_STORAGE     0xff
#define MEMSTICK_CATEGORY_STORAGE_DUO 0x00
#define MEMSTICK_CATEGORY_IO          0x01
#define MEMSTICK_CATEGORY_IO_PRO      0x10

	unsigned char class;
#define MEMSTICK_CLASS_FLASH          0xff
#define MEMSTICK_CLASS_DUO            0x00
#define MEMSTICK_CLASS_ROM            0x01
#define MEMSTICK_CLASS_RO             0x02
#define MEMSTICK_CLASS_WP             0x03
};

struct memstick_request {
	unsigned char tpc;
	unsigned char data_dir:1,
		      need_card_int:1,
		      long_data:1;
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
	/* Tell the media driver to stop doing things                      */
	void                     (*stop)(struct memstick_dev *card);
	/* Allow the media driver to continue                              */
	void                     (*start)(struct memstick_dev *card);

	struct device            dev;
};

struct memstick_host {
	struct mutex        lock;
	unsigned int        id;
	unsigned int        caps;
#define MEMSTICK_CAP_AUTO_GET_INT  1
#define MEMSTICK_CAP_PAR4          2
#define MEMSTICK_CAP_PAR8          4

	struct work_struct  media_checker;
	struct device       dev;

	struct memstick_dev *card;
	unsigned int        retries;
	bool removing;

	/* Notify the host that some requests are pending. */
	void                (*request)(struct memstick_host *host);
	/* Set host IO parameters (power, clock, etc).     */
	int                 (*set_param)(struct memstick_host *host,
					 enum memstick_param param,
					 int value);
	unsigned long       private[] ____cacheline_aligned;
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
void memstick_suspend_host(struct memstick_host *host);
void memstick_resume_host(struct memstick_host *host);

void memstick_init_req_sg(struct memstick_request *mrq, unsigned char tpc,
			  const struct scatterlist *sg);
void memstick_init_req(struct memstick_request *mrq, unsigned char tpc,
		       const void *buf, size_t length);
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
