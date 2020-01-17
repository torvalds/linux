/*
 * metroyesmefb.h - definitions for the metroyesme framebuffer driver
 *
 * Copyright (C) 2008 by Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#ifndef _LINUX_METRONOMEFB_H_
#define _LINUX_METRONOMEFB_H_

/* command structure used by metroyesme controller */
struct metromem_cmd {
	u16 opcode;
	u16 args[((64-2)/2)];
	u16 csum;
};

/* struct used by metroyesme. board specific stuff comes from *board */
struct metroyesmefb_par {
	struct metromem_cmd *metromem_cmd;
	unsigned char *metromem_wfm;
	unsigned char *metromem_img;
	u16 *metromem_img_csum;
	u16 *csum_table;
	dma_addr_t metromem_dma;
	struct fb_info *info;
	struct metroyesme_board *board;
	wait_queue_head_t waitq;
	u8 frame_count;
	int extra_size;
	int dt;
};

/* board specific routines and data */
struct metroyesme_board {
	struct module *owner; /* the platform device */
	void (*set_rst)(struct metroyesmefb_par *, int);
	void (*set_stdby)(struct metroyesmefb_par *, int);
	void (*cleanup)(struct metroyesmefb_par *);
	int (*met_wait_event)(struct metroyesmefb_par *);
	int (*met_wait_event_intr)(struct metroyesmefb_par *);
	int (*setup_irq)(struct fb_info *);
	int (*setup_fb)(struct metroyesmefb_par *);
	int (*setup_io)(struct metroyesmefb_par *);
	int (*get_panel_type)(void);
	unsigned char *metromem;
	int fw;
	int fh;
	int wfm_size;
	struct fb_info *host_fbinfo; /* the host LCD controller's fbi */
};

#endif
