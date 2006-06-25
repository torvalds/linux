/* linux/include/asm-arm/arch-bast/dma.h
 *
 * Copyright (C) 2003,2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2410X DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  ??-May-2003 BJD   Created file
 *  ??-Jun-2003 BJD   Added more dma functionality to go with arch
 *  10-Nov-2004 BJD   Added sys_device support
*/

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H __FILE__

#include <linux/sysdev.h>
#include "hardware.h"


/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 *
 */
#define MAX_DMA_ADDRESS		0x20000000
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */


/* we have 4 dma channels */
#define S3C2410_DMA_CHANNELS        (4)

/* types */

typedef enum {
	S3C2410_DMA_IDLE,
	S3C2410_DMA_RUNNING,
	S3C2410_DMA_PAUSED
} s3c2410_dma_state_t;


/* s3c2410_dma_loadst_t
 *
 * This represents the state of the DMA engine, wrt to the loaded / running
 * transfers. Since we don't have any way of knowing exactly the state of
 * the DMA transfers, we need to know the state to make decisions on wether
 * we can
 *
 * S3C2410_DMA_NONE
 *
 * There are no buffers loaded (the channel should be inactive)
 *
 * S3C2410_DMA_1LOADED
 *
 * There is one buffer loaded, however it has not been confirmed to be
 * loaded by the DMA engine. This may be because the channel is not
 * yet running, or the DMA driver decided that it was too costly to
 * sit and wait for it to happen.
 *
 * S3C2410_DMA_1RUNNING
 *
 * The buffer has been confirmed running, and not finisged
 *
 * S3C2410_DMA_1LOADED_1RUNNING
 *
 * There is a buffer waiting to be loaded by the DMA engine, and one
 * currently running.
*/

typedef enum {
	S3C2410_DMALOAD_NONE,
	S3C2410_DMALOAD_1LOADED,
	S3C2410_DMALOAD_1RUNNING,
	S3C2410_DMALOAD_1LOADED_1RUNNING,
} s3c2410_dma_loadst_t;

typedef enum {
	S3C2410_RES_OK,
	S3C2410_RES_ERR,
	S3C2410_RES_ABORT
} s3c2410_dma_buffresult_t;


typedef enum s3c2410_dmasrc_e s3c2410_dmasrc_t;

enum s3c2410_dmasrc_e {
	S3C2410_DMASRC_HW,      /* source is memory */
	S3C2410_DMASRC_MEM      /* source is hardware */
};

/* enum s3c2410_chan_op_e
 *
 * operation codes passed to the DMA code by the user, and also used
 * to inform the current channel owner of any changes to the system state
*/

enum s3c2410_chan_op_e {
	S3C2410_DMAOP_START,
	S3C2410_DMAOP_STOP,
	S3C2410_DMAOP_PAUSE,
	S3C2410_DMAOP_RESUME,
	S3C2410_DMAOP_FLUSH,
	S3C2410_DMAOP_TIMEOUT,           /* internal signal to handler */
};

typedef enum s3c2410_chan_op_e s3c2410_chan_op_t;

/* flags */

#define S3C2410_DMAF_SLOW         (1<<0)   /* slow, so don't worry about
					    * waiting for reloads */
#define S3C2410_DMAF_AUTOSTART    (1<<1)   /* auto-start if buffer queued */

/* dma buffer */

typedef struct s3c2410_dma_buf_s s3c2410_dma_buf_t;

struct s3c2410_dma_client {
	char                *name;
};

typedef struct s3c2410_dma_client s3c2410_dma_client_t;

/* s3c2410_dma_buf_s
 *
 * internally used buffer structure to describe a queued or running
 * buffer.
*/

struct s3c2410_dma_buf_s {
	s3c2410_dma_buf_t   *next;
	int                  magic;        /* magic */
	int                  size;         /* buffer size in bytes */
	dma_addr_t           data;         /* start of DMA data */
	dma_addr_t           ptr;          /* where the DMA got to [1] */
	void                *id;           /* client's id */
};

/* [1] is this updated for both recv/send modes? */

typedef struct s3c2410_dma_chan_s s3c2410_dma_chan_t;

/* s3c2410_dma_cbfn_t
 *
 * buffer callback routine type
*/

typedef void (*s3c2410_dma_cbfn_t)(s3c2410_dma_chan_t *, void *buf, int size,
				   s3c2410_dma_buffresult_t result);

typedef int  (*s3c2410_dma_opfn_t)(s3c2410_dma_chan_t *,
				   s3c2410_chan_op_t );

struct s3c2410_dma_stats_s {
	unsigned long          loads;
	unsigned long          timeout_longest;
	unsigned long          timeout_shortest;
	unsigned long          timeout_avg;
	unsigned long          timeout_failed;
};

typedef struct s3c2410_dma_stats_s s3c2410_dma_stats_t;

/* struct s3c2410_dma_chan_s
 *
 * full state information for each DMA channel
*/

struct s3c2410_dma_chan_s {
	/* channel state flags and information */
	unsigned char          number;      /* number of this dma channel */
	unsigned char          in_use;      /* channel allocated */
	unsigned char          irq_claimed; /* irq claimed for channel */
	unsigned char          irq_enabled; /* irq enabled for channel */
	unsigned char          xfer_unit;   /* size of an transfer */

	/* channel state */

	s3c2410_dma_state_t    state;
	s3c2410_dma_loadst_t   load_state;
	s3c2410_dma_client_t  *client;

	/* channel configuration */
	s3c2410_dmasrc_t       source;
	unsigned long          dev_addr;
	unsigned long          load_timeout;
	unsigned int           flags;        /* channel flags */

	/* channel's hardware position and configuration */
	void __iomem           *regs;        /* channels registers */
	void __iomem           *addr_reg;    /* data address register */
	unsigned int           irq;          /* channel irq */
	unsigned long          dcon;         /* default value of DCON */

	/* driver handles */
	s3c2410_dma_cbfn_t     callback_fn;  /* buffer done callback */
	s3c2410_dma_opfn_t     op_fn;        /* channel operation callback */

	/* stats gathering */
	s3c2410_dma_stats_t   *stats;
	s3c2410_dma_stats_t    stats_store;

	/* buffer list and information */
	s3c2410_dma_buf_t      *curr;        /* current dma buffer */
	s3c2410_dma_buf_t      *next;        /* next buffer to load */
	s3c2410_dma_buf_t      *end;         /* end of queue */

	/* system device */
	struct sys_device	dev;
};

/* the currently allocated channel information */
extern s3c2410_dma_chan_t s3c2410_chans[];

/* note, we don't really use dma_device_t at the moment */
typedef unsigned long dma_device_t;

/* functions --------------------------------------------------------------- */

/* s3c2410_dma_request
 *
 * request a dma channel exclusivley
*/

extern int s3c2410_dma_request(dmach_t channel,
			       s3c2410_dma_client_t *, void *dev);


/* s3c2410_dma_ctrl
 *
 * change the state of the dma channel
*/

extern int s3c2410_dma_ctrl(dmach_t channel, s3c2410_chan_op_t op);

/* s3c2410_dma_setflags
 *
 * set the channel's flags to a given state
*/

extern int s3c2410_dma_setflags(dmach_t channel,
				unsigned int flags);

/* s3c2410_dma_free
 *
 * free the dma channel (will also abort any outstanding operations)
*/

extern int s3c2410_dma_free(dmach_t channel, s3c2410_dma_client_t *);

/* s3c2410_dma_enqueue
 *
 * place the given buffer onto the queue of operations for the channel.
 * The buffer must be allocated from dma coherent memory, or the Dcache/WB
 * drained before the buffer is given to the DMA system.
*/

extern int s3c2410_dma_enqueue(dmach_t channel, void *id,
			       dma_addr_t data, int size);

/* s3c2410_dma_config
 *
 * configure the dma channel
*/

extern int s3c2410_dma_config(dmach_t channel, int xferunit, int dcon);

/* s3c2410_dma_devconfig
 *
 * configure the device we're talking to
*/

extern int s3c2410_dma_devconfig(int channel, s3c2410_dmasrc_t source,
				 int hwcfg, unsigned long devaddr);

/* s3c2410_dma_getposition
 *
 * get the position that the dma transfer is currently at
*/

extern int s3c2410_dma_getposition(dmach_t channel,
				   dma_addr_t *src, dma_addr_t *dest);

extern int s3c2410_dma_set_opfn(dmach_t, s3c2410_dma_opfn_t rtn);
extern int s3c2410_dma_set_buffdone_fn(dmach_t, s3c2410_dma_cbfn_t rtn);

/* DMA Register definitions */

#define S3C2410_DMA_DISRC       (0x00)
#define S3C2410_DMA_DISRCC      (0x04)
#define S3C2410_DMA_DIDST       (0x08)
#define S3C2410_DMA_DIDSTC      (0x0C)
#define S3C2410_DMA_DCON        (0x10)
#define S3C2410_DMA_DSTAT       (0x14)
#define S3C2410_DMA_DCSRC       (0x18)
#define S3C2410_DMA_DCDST       (0x1C)
#define S3C2410_DMA_DMASKTRIG   (0x20)

#define S3C2410_DISRCC_INC	(1<<0)
#define S3C2410_DISRCC_APB	(1<<1)

#define S3C2410_DMASKTRIG_STOP   (1<<2)
#define S3C2410_DMASKTRIG_ON     (1<<1)
#define S3C2410_DMASKTRIG_SWTRIG (1<<0)

#define S3C2410_DCON_DEMAND     (0<<31)
#define S3C2410_DCON_HANDSHAKE  (1<<31)
#define S3C2410_DCON_SYNC_PCLK  (0<<30)
#define S3C2410_DCON_SYNC_HCLK  (1<<30)

#define S3C2410_DCON_INTREQ     (1<<29)

#define S3C2410_DCON_CH0_XDREQ0	(0<<24)
#define S3C2410_DCON_CH0_UART0	(1<<24)
#define S3C2410_DCON_CH0_SDI	(2<<24)
#define S3C2410_DCON_CH0_TIMER	(3<<24)
#define S3C2410_DCON_CH0_USBEP1	(4<<24)

#define S3C2410_DCON_CH1_XDREQ1	(0<<24)
#define S3C2410_DCON_CH1_UART1	(1<<24)
#define S3C2410_DCON_CH1_I2SSDI	(2<<24)
#define S3C2410_DCON_CH1_SPI	(3<<24)
#define S3C2410_DCON_CH1_USBEP2	(4<<24)

#define S3C2410_DCON_CH2_I2SSDO	(0<<24)
#define S3C2410_DCON_CH2_I2SSDI	(1<<24)
#define S3C2410_DCON_CH2_SDI	(2<<24)
#define S3C2410_DCON_CH2_TIMER	(3<<24)
#define S3C2410_DCON_CH2_USBEP3	(4<<24)

#define S3C2410_DCON_CH3_UART2	(0<<24)
#define S3C2410_DCON_CH3_SDI	(1<<24)
#define S3C2410_DCON_CH3_SPI	(2<<24)
#define S3C2410_DCON_CH3_TIMER	(3<<24)
#define S3C2410_DCON_CH3_USBEP4	(4<<24)

#define S3C2410_DCON_SRCSHIFT   (24)
#define S3C2410_DCON_SRCMASK	(7<<24)

#define S3C2410_DCON_BYTE       (0<<20)
#define S3C2410_DCON_HALFWORD   (1<<20)
#define S3C2410_DCON_WORD       (2<<20)

#define S3C2410_DCON_AUTORELOAD (0<<22)
#define S3C2410_DCON_NORELOAD   (1<<22)
#define S3C2410_DCON_HWTRIG     (1<<23)

#ifdef CONFIG_CPU_S3C2440
#define S3C2440_DIDSTC_CHKINT	(1<<2)

#define S3C2440_DCON_CH0_I2SSDO	(5<<24)
#define S3C2440_DCON_CH0_PCMIN	(6<<24)

#define S3C2440_DCON_CH1_PCMOUT	(5<<24)
#define S3C2440_DCON_CH1_SDI	(6<<24)

#define S3C2440_DCON_CH2_PCMIN	(5<<24)
#define S3C2440_DCON_CH2_MICIN	(6<<24)

#define S3C2440_DCON_CH3_MICIN	(5<<24)
#define S3C2440_DCON_CH3_PCMOUT	(6<<24)
#endif

#endif /* __ASM_ARCH_DMA_H */
