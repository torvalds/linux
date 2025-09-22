/*      $OpenBSD: wdcvar.h,v 1.59 2024/06/18 12:37:29 jsg Exp $     */
/*	$NetBSD: wdcvar.h,v 1.17 1999/04/11 20:50:29 bouyer Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, by Onno van der Linden and by Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_WDCVAR_H_
#define _DEV_IC_WDCVAR_H_

#include <sys/timeout.h>

struct channel_queue {  /* per channel queue (may be shared) */
	TAILQ_HEAD(xferhead, wdc_xfer) sc_xfer;
};

struct channel_softc_vtbl;


#define WDC_OPTION_PROBE_VERBOSE   0x10000

struct channel_softc { /* Per channel data */
	struct channel_softc_vtbl  *_vtbl;

	/* Our location */
	int channel;
	/* Our controller's softc */
	struct wdc_softc *wdc;
	/* Our registers */
	bus_space_tag_t       cmd_iot;
	bus_space_handle_t    cmd_ioh;
	bus_size_t            cmd_iosz;
	bus_space_tag_t       ctl_iot;
	bus_space_handle_t    ctl_ioh;
	bus_size_t            ctl_iosz;
	/* data32{iot,ioh} are only used for 32 bit xfers */
	bus_space_tag_t         data32iot;
	bus_space_handle_t      data32ioh;
	/* Our state */
	int ch_flags;
#define WDCF_ACTIVE		0x01 /* channel is active */
#define WDCF_ONESLAVE		0x02 /* slave-only channel */
#define WDCF_IRQ_WAIT		0x10 /* controller is waiting for irq */
#define WDCF_DMA_WAIT		0x20 /* controller is waiting for DMA */
#define WDCF_VERBOSE_PROBE	0x40 /* verbose probe */
#define WDCF_DMA_BEFORE_CMD	0x80 /* start dma before a command */
	u_int8_t ch_status;         /* copy of status register */
	u_int8_t ch_prev_log_status; /* previous logged value of status reg */
	u_int8_t ch_log_idx;
	u_int8_t ch_error;          /* copy of error register */
	/* per-drive infos */
	struct ata_drive_datas ch_drive[2];

	/*
	 * channel queues. May be the same for all channels, if hw channels
	 * are not independent.
	 */
	struct channel_queue *ch_queue;
	struct timeout ch_timo;

	int dying;
};

/*
 * Disk Controller register definitions.
 */
#define _WDC_REGMASK 7
#define _WDC_AUX     8
#define _WDC_RDONLY  16
#define _WDC_WRONLY  32
enum wdc_regs {
	wdr_error = _WDC_RDONLY | 1,
	wdr_features = _WDC_WRONLY | 1,
	wdr_seccnt = 2,
	wdr_ireason = 2,
	wdr_sector = 3,
	wdr_lba_lo = 3,
	wdr_cyl_lo = 4,
	wdr_lba_mi = 4,
	wdr_cyl_hi = 5,
	wdr_lba_hi = 5,
	wdr_sdh = 6,
	wdr_status = _WDC_RDONLY | 7,
	wdr_command = _WDC_WRONLY | 7,
	wdr_altsts = _WDC_RDONLY | _WDC_AUX,
	wdr_ctlr = _WDC_WRONLY | _WDC_AUX
};

#define WDC_NREG	8 /* number of command registers */
#define WDC_NSHADOWREG	2 /* number of command "shadow" registers */

struct channel_softc_vtbl {
	u_int8_t (*read_reg)(struct channel_softc *, enum wdc_regs reg);
	void (*write_reg)(struct channel_softc *, enum wdc_regs reg,
	    u_int8_t var);
	void (*lba48_write_reg)(struct channel_softc *, enum wdc_regs reg,
	    u_int16_t var);

	void (*read_raw_multi_2)(struct channel_softc *,
	    void *data, unsigned int nbytes);
	void (*write_raw_multi_2)(struct channel_softc *,
	    void *data, unsigned int nbytes);

	void (*read_raw_multi_4)(struct channel_softc *,
	    void *data, unsigned int nbytes);
	void (*write_raw_multi_4)(struct channel_softc *,
	    void *data, unsigned int nbytes);
};


#define CHP_READ_REG(chp, a)  ((chp)->_vtbl->read_reg)(chp, a)
#define CHP_WRITE_REG(chp, a, b)  ((chp)->_vtbl->write_reg)(chp, a, b)
#define CHP_LBA48_WRITE_REG(chp, a, b)	\
	((chp)->_vtbl->lba48_write_reg)(chp, a, b)

#define CHP_READ_RAW_MULTI_2(chp, a, b)  \
	((chp)->_vtbl->read_raw_multi_2)(chp, a, b)
#define CHP_WRITE_RAW_MULTI_2(chp, a, b)  \
	((chp)->_vtbl->write_raw_multi_2)(chp, a, b)
#define CHP_READ_RAW_MULTI_4(chp, a, b)  \
	((chp)->_vtbl->read_raw_multi_4)(chp, a, b)
#define CHP_WRITE_RAW_MULTI_4(chp, a, b)  \
	((chp)->_vtbl->write_raw_multi_4)(chp, a, b)

struct wdc_softc { /* Per controller state */
	struct device sc_dev;
	/* mandatory fields */
	int           cap;
/* Capabilities supported by the controller */
#define WDC_CAPABILITY_DATA16 0x0001	/* can do  16-bit data access */
#define WDC_CAPABILITY_DATA32 0x0002	/* can do 32-bit data access */
#define WDC_CAPABILITY_MODE   0x0004	/* controller knows its PIO/DMA modes */
#define WDC_CAPABILITY_DMA    0x0008	/* DMA */
#define WDC_CAPABILITY_UDMA   0x0010	/* Ultra-DMA/33 */
#define WDC_CAPABILITY_NO_EXTRA_RESETS 0x0100 /* only reset once */
#define WDC_CAPABILITY_PREATA 0x0200	/* ctrl can be a pre-ata one */
#define WDC_CAPABILITY_IRQACK 0x0400	/* callback to ack interrupt */
#define WDC_CAPABILITY_SINGLE_DRIVE 0x800 /* Don't probe second drive */
#define WDC_CAPABILITY_NO_ATAPI_DMA 0x1000 /* Don't do DMA with ATAPI */
#define WDC_CAPABILITY_SATA   0x2000	/* SATA controller */
	u_int8_t      PIO_cap; /* highest PIO mode supported */
	u_int8_t      DMA_cap; /* highest DMA mode supported */
	u_int8_t      UDMA_cap; /* highest UDMA mode supported */
	int nchannels;	/* Number of channels on this controller */
	struct channel_softc **channels;  /* channel-specific data (array) */
	u_int16_t quirks;		/* per-device oddities */
#define WDC_QUIRK_NOSHORTDMA	0x0001	/* can't do short DMA transfers */
#define WDC_QUIRK_NOATA		0x0002	/* skip attaching ATA disks */
#define WDC_QUIRK_NOATAPI	0x0004	/* skip attaching ATAPI devices */

#if 0
	/*
	 * The reference count here is used for both IDE and ATAPI devices.
	 */
	struct scsipi_adapter sc_atapi_adapter;
#endif

	/* if WDC_CAPABILITY_DMA set in 'cap' */
	void            *dma_arg;
	int            (*dma_init)(void *, int, int, void *, size_t,
	                int);
	void           (*dma_start)(void *, int, int);
	int            (*dma_finish)(void *, int, int, int);
/* flags passed to DMA functions */
#define WDC_DMA_READ	0x01
#define WDC_DMA_IRQW	0x02
#define WDC_DMA_LBA48	0x04
	int             dma_status; /* status return from dma_finish() */
#define WDC_DMAST_NOIRQ	0x01 /* missing IRQ */
#define WDC_DMAST_ERR	0x02 /* DMA error */
#define WDC_DMAST_UNDER	0x04 /* DMA underrun */

	/* if WDC_CAPABILITY_MODE set in 'cap' */
	void            (*set_modes)(struct channel_softc *);

	/* if WDC_CAPABILITY_IRQACK set in 'cap' */
	void            (*irqack)(struct channel_softc *);

	void		(*reset)(struct channel_softc *);

	/* Driver callback to probe for drives */
	void (*drv_probe)(struct channel_softc *);
};

 /*
  * Description of a command to be handled by a controller.
  * These commands are queued in a list.
  */
struct atapi_return_args;

struct wdc_xfer {
	volatile u_int c_flags;
#define C_ATAPI		0x0002 /* xfer is ATAPI request */
#define C_TIMEOU	0x0004 /* xfer processing timed out */
#define C_NEEDDONE	0x0010 /* need to call upper-level done */
#define C_POLL		0x0020 /* cmd is polled */
#define C_DMA		0x0040 /* cmd uses DMA */
#define C_SENSE		0x0080 /* cmd is a internal command */
#define C_MEDIA_ACCESS	0x0100 /* is a media access command */
#define C_POLL_MACHINE	0x0200 /* machine has a poll handler */
#define C_PRIVATEXFER	0x0400 /* privately managed xfer */
#define C_SCSIXFER	0x0800 /* SCSI managed xfer */

	/* Information about our location */
	struct channel_softc *chp;
	u_int8_t drive;

	/* Information about the current transfer  */
	void *cmd; /* wdc, ata or scsipi command structure */
	void *databuf;
	int c_bcount;      /* byte count left */
	int c_skip;        /* bytes already transferred */
	TAILQ_ENTRY(wdc_xfer) c_xferchain;
	LIST_ENTRY(wdc_xfer) free_list;
	void (*c_start)(struct channel_softc *, struct wdc_xfer *);
	int  (*c_intr)(struct channel_softc *, struct wdc_xfer *, int);
        void (*c_kill_xfer)(struct channel_softc *, struct wdc_xfer *);

	/* Used by ATAPISCSI */
	volatile int endticks;
	struct timeout atapi_poll_to;
	void (*next)(struct channel_softc *, struct wdc_xfer *, int,
			 struct atapi_return_args *);
	void (*c_done)(struct channel_softc *, struct wdc_xfer *, int,
			 struct atapi_return_args *);

	/* Used for tape devices */
	int  transfer_len;
};

/*
 * Public functions which can be called by ATA or ATAPI specific parts,
 * or bus-specific backends.
 */

int   wdcprobe(struct channel_softc *);
void  wdcattach(struct channel_softc *);
int   wdcdetach(struct channel_softc *, int);
int   wdcintr(void *);
struct channel_queue *wdc_alloc_queue(void);
void  wdc_free_queue(struct channel_queue *);
void  wdc_exec_xfer(struct channel_softc *, struct wdc_xfer *);
struct wdc_xfer *wdc_get_xfer(int); /* int = WDC_NOSLEEP/CANSLEEP */
#define WDC_CANSLEEP	0x00
#define WDC_NOSLEEP	0x01
void   wdc_scrub_xfer(struct wdc_xfer *);
void   wdc_free_xfer(struct channel_softc *, struct wdc_xfer *);
void  wdcstart(struct channel_softc *);
int   wdcreset(struct channel_softc *, int);
#define NOWAIT  0x02
#define VERBOSE	0x01
#define SILENT	0x00 /* wdcreset will not print errors */
int   wdc_wait_for_status(struct channel_softc *, int, int, int);
int   wdc_dmawait(struct channel_softc *, struct wdc_xfer *, int);
void  wdcbit_bucket(struct channel_softc *, int);

void  wdccommand(struct channel_softc *, u_int8_t, u_int8_t, u_int16_t,
	u_int8_t, u_int8_t, u_int8_t, u_int8_t);
void  wdccommandext(struct channel_softc *, u_int8_t, u_int8_t, u_int64_t,
	u_int16_t);
void  wdccommandshort(struct channel_softc *, int, int);
void  wdctimeout(void *arg);
void  wdc_do_reset(struct channel_softc *);

/*
 * ST506 spec says that if READY or SEEKCMPLT go off, then the read or write
 * command is aborted.
 */
#define wdcwait(chp, status, mask, timeout) ((wdc_wait_for_status((chp), (status), (mask), (timeout)) >= 0) ? 0 : -1)
#define wait_for_drq(chp, timeout) wdcwait((chp), WDCS_DRQ, WDCS_DRQ, (timeout))
#define wait_for_unbusy(chp, timeout) wdcwait((chp), 0, 0, (timeout))
#define wait_for_ready(chp, timeout) wdcwait((chp), WDCS_DRDY, \
	WDCS_DRDY, (timeout))

/* ATA/ATAPI specs says a device can take 31s to reset */
#define WDC_RESET_WAIT 31000

void wdc_disable_intr(struct channel_softc *);
void wdc_enable_intr(struct channel_softc *);
void wdc_set_drive(struct channel_softc *, int drive);
void wdc_output_bytes(struct ata_drive_datas *drvp, void *, unsigned int);
void wdc_input_bytes(struct ata_drive_datas *drvp, void *, unsigned int);

void wdc_print_current_modes(struct channel_softc *);

int wdc_ioctl(struct ata_drive_datas *, u_long, caddr_t, int, struct proc *);

u_int8_t wdc_default_read_reg(struct channel_softc *,
		enum wdc_regs);
void     wdc_default_write_reg(struct channel_softc *,
		enum wdc_regs, u_int8_t);
void     wdc_default_lba48_write_reg(struct channel_softc *,
		enum wdc_regs, u_int16_t);
void     wdc_default_read_raw_multi_2(struct channel_softc *,
		void *, unsigned int);
void     wdc_default_write_raw_multi_2(struct channel_softc *,
		void *, unsigned int);
void     wdc_default_read_raw_multi_4(struct channel_softc *,
		void *, unsigned int);
void     wdc_default_write_raw_multi_4(struct channel_softc *,
		void *, unsigned int);

#endif	/* !_DEV_IC_WDCVAR_H_ */
