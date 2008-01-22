/*
 * File:         include/asm-blackfin/simple_bf533_dma.h
 * Based on:     none - original work
 * Author:       LG Soft India
 *               Copyright (C) 2004-2005 Analog Devices Inc.
 * Created:      Tue Sep 21 2004
 * Description:  This file contains the major Data structures and constants
 * 		 used for DMA Implementation in BF533
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BLACKFIN_DMA_H_
#define _BLACKFIN_DMA_H_

#include <asm/io.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <asm/signal.h>
#include <asm/semaphore.h>

#include <linux/kernel.h>
#include <asm/mach/dma.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/blackfin.h>

#define MAX_DMA_ADDRESS PAGE_OFFSET

/*****************************************************************************
*        Generic DMA  Declarations
*
****************************************************************************/
enum dma_chan_status {
	DMA_CHANNEL_FREE,
	DMA_CHANNEL_REQUESTED,
	DMA_CHANNEL_ENABLED,
};

/*-------------------------
 * config reg bits value
 *-------------------------*/
#define DATA_SIZE_8 		0
#define DATA_SIZE_16 		1
#define DATA_SIZE_32 		2

#define DMA_FLOW_STOP 		0
#define DMA_FLOW_AUTO 		1
#define DMA_FLOW_ARRAY 		4
#define DMA_FLOW_SMALL 		6
#define DMA_FLOW_LARGE 		7

#define DIMENSION_LINEAR    0
#define DIMENSION_2D           1

#define DIR_READ     0
#define DIR_WRITE    1

#define INTR_DISABLE   0
#define INTR_ON_BUF    2
#define INTR_ON_ROW    3

#define DMA_NOSYNC_KEEP_DMA_BUF	0
#define DMA_SYNC_RESTART	1

struct dmasg {
	unsigned long next_desc_addr;
	unsigned long start_addr;
	unsigned short cfg;
	unsigned short x_count;
	short x_modify;
	unsigned short y_count;
	short y_modify;
} __attribute__((packed));

struct dma_register {
	unsigned long next_desc_ptr;	/* DMA Next Descriptor Pointer register */
	unsigned long start_addr;	/* DMA Start address  register */

	unsigned short cfg;	/* DMA Configuration register */
	unsigned short dummy1;	/* DMA Configuration register */

	unsigned long reserved;

	unsigned short x_count;	/* DMA x_count register */
	unsigned short dummy2;

	short x_modify;	/* DMA x_modify register */
	unsigned short dummy3;

	unsigned short y_count;	/* DMA y_count register */
	unsigned short dummy4;

	short y_modify;	/* DMA y_modify register */
	unsigned short dummy5;

	unsigned long curr_desc_ptr;	/* DMA Current Descriptor Pointer
					   register */
	unsigned long curr_addr_ptr;	/* DMA Current Address Pointer
						   register */
	unsigned short irq_status;	/* DMA irq status register */
	unsigned short dummy6;

	unsigned short peripheral_map;	/* DMA peripheral map register */
	unsigned short dummy7;

	unsigned short curr_x_count;	/* DMA Current x-count register */
	unsigned short dummy8;

	unsigned long reserved2;

	unsigned short curr_y_count;	/* DMA Current y-count register */
	unsigned short dummy9;

	unsigned long reserved3;

};

typedef irqreturn_t(*dma_interrupt_t) (int irq, void *dev_id);

struct dma_channel {
	struct mutex dmalock;
	char *device_id;
	enum dma_chan_status chan_status;
	struct dma_register *regs;
	struct dmasg *sg;		/* large mode descriptor */
	unsigned int ctrl_num;	/* controller number */
	dma_interrupt_t irq_callback;
	void *data;
	unsigned int dma_enable_flag;
	unsigned int loopback_flag;
};

/*******************************************************************************
*	DMA API's
*******************************************************************************/
/* functions to set register mode */
void set_dma_start_addr(unsigned int channel, unsigned long addr);
void set_dma_next_desc_addr(unsigned int channel, unsigned long addr);
void set_dma_curr_desc_addr(unsigned int channel, unsigned long addr);
void set_dma_x_count(unsigned int channel, unsigned short x_count);
void set_dma_x_modify(unsigned int channel, short x_modify);
void set_dma_y_count(unsigned int channel, unsigned short y_count);
void set_dma_y_modify(unsigned int channel, short y_modify);
void set_dma_config(unsigned int channel, unsigned short config);
unsigned short set_bfin_dma_config(char direction, char flow_mode,
				   char intr_mode, char dma_mode, char width,
				   char syncmode);
void set_dma_curr_addr(unsigned int channel, unsigned long addr);

/* get curr status for polling */
unsigned short get_dma_curr_irqstat(unsigned int channel);
unsigned short get_dma_curr_xcount(unsigned int channel);
unsigned short get_dma_curr_ycount(unsigned int channel);
unsigned long get_dma_next_desc_ptr(unsigned int channel);
unsigned long get_dma_curr_desc_ptr(unsigned int channel);
unsigned long get_dma_curr_addr(unsigned int channel);

/* set large DMA mode descriptor */
void set_dma_sg(unsigned int channel, struct dmasg *sg, int nr_sg);

/* check if current channel is in use */
int dma_channel_active(unsigned int channel);

/* common functions must be called in any mode */
void free_dma(unsigned int channel);
int dma_channel_active(unsigned int channel); /* check if a channel is in use */
void disable_dma(unsigned int channel);
void enable_dma(unsigned int channel);
int request_dma(unsigned int channel, char *device_id);
int set_dma_callback(unsigned int channel, dma_interrupt_t callback,
		     void *data);
void dma_disable_irq(unsigned int channel);
void dma_enable_irq(unsigned int channel);
void clear_dma_irqstat(unsigned int channel);
void *dma_memcpy(void *dest, const void *src, size_t count);
void *safe_dma_memcpy(void *dest, const void *src, size_t count);

#endif
