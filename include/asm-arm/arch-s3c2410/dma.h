/* linux/include/asm-arm/arch-s3c2410/dma.h
 *
 * Copyright (C) 2003,2004,2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C241XX DMA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H __FILE__

#include <linux/sysdev.h>
#include "hardware.h"

/*
 * This is the maximum DMA address(physical address) that can be DMAd to.
 *
 */
#define MAX_DMA_ADDRESS		0x40000000
#define MAX_DMA_TRANSFER_SIZE   0x100000 /* Data Unit is half word  */

/* We use `virtual` dma channels to hide the fact we have only a limited
 * number of DMA channels, and not of all of them (dependant on the device)
 * can be attached to any DMA source. We therefore let the DMA core handle
 * the allocation of hardware channels to clients.
*/

enum dma_ch {
	DMACH_XD0,
	DMACH_XD1,
	DMACH_SDI,
	DMACH_SPI0,
	DMACH_SPI1,
	DMACH_UART0,
	DMACH_UART1,
	DMACH_UART2,
	DMACH_TIMER,
	DMACH_I2S_IN,
	DMACH_I2S_OUT,
	DMACH_PCM_IN,
	DMACH_PCM_OUT,
	DMACH_MIC_IN,
	DMACH_USB_EP1,
	DMACH_USB_EP2,
	DMACH_USB_EP3,
	DMACH_USB_EP4,
	DMACH_UART0_SRC2,	/* s3c2412 second uart sources */
	DMACH_UART1_SRC2,
	DMACH_UART2_SRC2,
	DMACH_MAX,		/* the end entry */
};

#define DMACH_LOW_LEVEL	(1<<28)	/* use this to specifiy hardware ch no */

/* we have 4 dma channels */
#define S3C2410_DMA_CHANNELS        (4)

/* types */

enum s3c2410_dma_state {
	S3C2410_DMA_IDLE,
	S3C2410_DMA_RUNNING,
	S3C2410_DMA_PAUSED
};


/* enum s3c2410_dma_loadst
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

enum s3c2410_dma_loadst {
	S3C2410_DMALOAD_NONE,
	S3C2410_DMALOAD_1LOADED,
	S3C2410_DMALOAD_1RUNNING,
	S3C2410_DMALOAD_1LOADED_1RUNNING,
};

enum s3c2410_dma_buffresult {
	S3C2410_RES_OK,
	S3C2410_RES_ERR,
	S3C2410_RES_ABORT
};

enum s3c2410_dmasrc {
	S3C2410_DMASRC_HW,		/* source is memory */
	S3C2410_DMASRC_MEM		/* source is hardware */
};

/* enum s3c2410_chan_op
 *
 * operation codes passed to the DMA code by the user, and also used
 * to inform the current channel owner of any changes to the system state
*/

enum s3c2410_chan_op {
	S3C2410_DMAOP_START,
	S3C2410_DMAOP_STOP,
	S3C2410_DMAOP_PAUSE,
	S3C2410_DMAOP_RESUME,
	S3C2410_DMAOP_FLUSH,
	S3C2410_DMAOP_TIMEOUT,		/* internal signal to handler */
	S3C2410_DMAOP_STARTED,		/* indicate channel started */
};

/* flags */

#define S3C2410_DMAF_SLOW         (1<<0)   /* slow, so don't worry about
					    * waiting for reloads */
#define S3C2410_DMAF_AUTOSTART    (1<<1)   /* auto-start if buffer queued */

/* dma buffer */

struct s3c2410_dma_client {
	char                *name;
};

/* s3c2410_dma_buf_s
 *
 * internally used buffer structure to describe a queued or running
 * buffer.
*/

struct s3c2410_dma_buf;
struct s3c2410_dma_buf {
	struct s3c2410_dma_buf	*next;
	int			 magic;		/* magic */
	int			 size;		/* buffer size in bytes */
	dma_addr_t		 data;		/* start of DMA data */
	dma_addr_t		 ptr;		/* where the DMA got to [1] */
	void			*id;		/* client's id */
};

/* [1] is this updated for both recv/send modes? */

struct s3c2410_dma_chan;

/* s3c2410_dma_cbfn_t
 *
 * buffer callback routine type
*/

typedef void (*s3c2410_dma_cbfn_t)(struct s3c2410_dma_chan *,
				   void *buf, int size,
				   enum s3c2410_dma_buffresult result);

typedef int  (*s3c2410_dma_opfn_t)(struct s3c2410_dma_chan *,
				   enum s3c2410_chan_op );

struct s3c2410_dma_stats {
	unsigned long		loads;
	unsigned long		timeout_longest;
	unsigned long		timeout_shortest;
	unsigned long		timeout_avg;
	unsigned long		timeout_failed;
};

struct s3c2410_dma_map;

/* struct s3c2410_dma_chan
 *
 * full state information for each DMA channel
*/

struct s3c2410_dma_chan {
	/* channel state flags and information */
	unsigned char		 number;      /* number of this dma channel */
	unsigned char		 in_use;      /* channel allocated */
	unsigned char		 irq_claimed; /* irq claimed for channel */
	unsigned char		 irq_enabled; /* irq enabled for channel */
	unsigned char		 xfer_unit;   /* size of an transfer */

	/* channel state */

	enum s3c2410_dma_state	 state;
	enum s3c2410_dma_loadst	 load_state;
	struct s3c2410_dma_client *client;

	/* channel configuration */
	enum s3c2410_dmasrc	 source;
	unsigned long		 dev_addr;
	unsigned long		 load_timeout;
	unsigned int		 flags;		/* channel flags */

	struct s3c24xx_dma_map	*map;		/* channel hw maps */

	/* channel's hardware position and configuration */
	void __iomem		*regs;		/* channels registers */
	void __iomem		*addr_reg;	/* data address register */
	unsigned int		 irq;		/* channel irq */
	unsigned long		 dcon;		/* default value of DCON */

	/* driver handles */
	s3c2410_dma_cbfn_t	 callback_fn;	/* buffer done callback */
	s3c2410_dma_opfn_t	 op_fn;		/* channel op callback */

	/* stats gathering */
	struct s3c2410_dma_stats *stats;
	struct s3c2410_dma_stats  stats_store;

	/* buffer list and information */
	struct s3c2410_dma_buf	*curr;		/* current dma buffer */
	struct s3c2410_dma_buf	*next;		/* next buffer to load */
	struct s3c2410_dma_buf	*end;		/* end of queue */

	/* system device */
	struct sys_device	dev;
};

/* the currently allocated channel information */
extern struct s3c2410_dma_chan s3c2410_chans[];

/* note, we don't really use dma_device_t at the moment */
typedef unsigned long dma_device_t;

/* functions --------------------------------------------------------------- */

/* s3c2410_dma_request
 *
 * request a dma channel exclusivley
*/

extern int s3c2410_dma_request(dmach_t channel,
			       struct s3c2410_dma_client *, void *dev);


/* s3c2410_dma_ctrl
 *
 * change the state of the dma channel
*/

extern int s3c2410_dma_ctrl(dmach_t channel, enum s3c2410_chan_op op);

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

extern int s3c2410_dma_free(dmach_t channel, struct s3c2410_dma_client *);

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

extern int s3c2410_dma_devconfig(int channel, enum s3c2410_dmasrc source,
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
#define S3C2412_DMA_DMAREQSEL	(0x24)

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

#ifdef CONFIG_CPU_S3C2412

#define S3C2412_DMAREQSEL_SRC(x)	((x)<<1)

#define S3C2412_DMAREQSEL_HW		(1)

#define S3C2412_DMAREQSEL_SPI0TX	S3C2412_DMAREQSEL_SRC(0)
#define S3C2412_DMAREQSEL_SPI0RX	S3C2412_DMAREQSEL_SRC(1)
#define S3C2412_DMAREQSEL_SPI1TX	S3C2412_DMAREQSEL_SRC(2)
#define S3C2412_DMAREQSEL_SPI1RX	S3C2412_DMAREQSEL_SRC(3)
#define S3C2412_DMAREQSEL_I2STX		S3C2412_DMAREQSEL_SRC(4)
#define S3C2412_DMAREQSEL_I2SRX		S3C2412_DMAREQSEL_SRC(5)
#define S3C2412_DMAREQSEL_TIMER		S3C2412_DMAREQSEL_SRC(9)
#define S3C2412_DMAREQSEL_SDI		S3C2412_DMAREQSEL_SRC(10)
#define S3C2412_DMAREQSEL_USBEP1	S3C2412_DMAREQSEL_SRC(13)
#define S3C2412_DMAREQSEL_USBEP2	S3C2412_DMAREQSEL_SRC(14)
#define S3C2412_DMAREQSEL_USBEP3	S3C2412_DMAREQSEL_SRC(15)
#define S3C2412_DMAREQSEL_USBEP4	S3C2412_DMAREQSEL_SRC(16)
#define S3C2412_DMAREQSEL_XDREQ0	S3C2412_DMAREQSEL_SRC(17)
#define S3C2412_DMAREQSEL_XDREQ1	S3C2412_DMAREQSEL_SRC(18)
#define S3C2412_DMAREQSEL_UART0_0	S3C2412_DMAREQSEL_SRC(19)
#define S3C2412_DMAREQSEL_UART0_1	S3C2412_DMAREQSEL_SRC(20)
#define S3C2412_DMAREQSEL_UART1_0	S3C2412_DMAREQSEL_SRC(21)
#define S3C2412_DMAREQSEL_UART1_1	S3C2412_DMAREQSEL_SRC(22)
#define S3C2412_DMAREQSEL_UART2_0	S3C2412_DMAREQSEL_SRC(23)
#define S3C2412_DMAREQSEL_UART2_1	S3C2412_DMAREQSEL_SRC(24)

#endif
#endif /* __ASM_ARCH_DMA_H */
