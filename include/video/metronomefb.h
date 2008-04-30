/*
 * metronomefb.h - definitions for the metronome framebuffer driver
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

/* address and control descriptors used by metronome controller */
struct metromem_desc {
	u32 mFDADR0;
	u32 mFSADR0;
	u32 mFIDR0;
	u32 mLDCMD0;
};

/* command structure used by metronome controller */
struct metromem_cmd {
	u16 opcode;
	u16 args[((64-2)/2)];
	u16 csum;
};

/* struct used by metronome. board specific stuff comes from *board */
struct metronomefb_par {
	unsigned char *metromem;
	struct metromem_desc *metromem_desc;
	struct metromem_cmd *metromem_cmd;
	unsigned char *metromem_wfm;
	unsigned char *metromem_img;
	u16 *metromem_img_csum;
	u16 *csum_table;
	int metromemsize;
	dma_addr_t metromem_dma;
	dma_addr_t metromem_desc_dma;
	struct fb_info *info;
	struct metronome_board *board;
	wait_queue_head_t waitq;
	u8 frame_count;
};

/* board specific routines */
struct metronome_board {
	struct module *owner;
	void (*free_irq)(struct fb_info *);
	void (*init_gpio_regs)(struct metronomefb_par *);
	void (*init_lcdc_regs)(struct metronomefb_par *);
	void (*post_dma_setup)(struct metronomefb_par *);
	void (*set_rst)(struct metronomefb_par *, int);
	void (*set_stdby)(struct metronomefb_par *, int);
	int (*met_wait_event)(struct metronomefb_par *);
	int (*met_wait_event_intr)(struct metronomefb_par *);
	int (*setup_irq)(struct fb_info *);
};

#endif
