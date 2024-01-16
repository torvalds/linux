/*
 * hecubafb.h - definitions for the hecuba framebuffer driver
 *
 * Copyright (C) 2008 by Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#ifndef _LINUX_HECUBAFB_H_
#define _LINUX_HECUBAFB_H_

/* Apollo controller specific defines */
#define APOLLO_START_NEW_IMG	0xA0
#define APOLLO_STOP_IMG_DATA	0xA1
#define APOLLO_DISPLAY_IMG	0xA2
#define APOLLO_ERASE_DISPLAY	0xA3
#define APOLLO_INIT_DISPLAY	0xA4

/* Hecuba interface specific defines */
#define HCB_WUP_BIT	0x01
#define HCB_DS_BIT 	0x02
#define HCB_RW_BIT 	0x04
#define HCB_CD_BIT 	0x08
#define HCB_ACK_BIT 	0x80

/* struct used by hecuba. board specific stuff comes from *board */
struct hecubafb_par {
	struct fb_info *info;
	struct hecuba_board *board;
	void (*send_command)(struct hecubafb_par *, unsigned char);
	void (*send_data)(struct hecubafb_par *, unsigned char);
};

/* board specific routines
board drivers can implement wait_for_ack with interrupts if desired. if
wait_for_ack is called with clear=0, then go to sleep and return when ack
goes hi or if wait_for_ack with clear=1, then return when ack goes lo */
struct hecuba_board {
	struct module *owner;
	void (*remove)(struct hecubafb_par *);
	void (*set_ctl)(struct hecubafb_par *, unsigned char, unsigned char);
	void (*set_data)(struct hecubafb_par *, unsigned char);
	void (*wait_for_ack)(struct hecubafb_par *, int);
	int (*init)(struct hecubafb_par *);
};


#endif
