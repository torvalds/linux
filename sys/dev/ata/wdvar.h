/*	$OpenBSD: wdvar.h,v 1.21 2014/07/09 12:56:28 mpi Exp $	*/
/*	$NetBSD: wdvar.h,v 1.3 1998/11/11 19:38:27 bouyer Exp $	*/

/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _DEV_ATA_WDVAR_H_
#define _DEV_ATA_WDVAR_H_

/* Params needed by the controller to perform an ATA bio */
struct ata_bio {
    volatile u_int16_t flags; /* cmd flags */
#define ATA_NOSLEEP 0x0001 /* Can't sleep */
#define ATA_POLL    0x0002 /* poll for completion */
#define ATA_ITSDONE 0x0004 /* the transfer is as done as it gets */
#define ATA_SINGLE  0x0008 /* transfer has to be done in single-sector mode */
#define ATA_LBA     0x0010 /* transfer uses LBA addressing */
#define ATA_READ    0x0020 /* transfer is a read (otherwise a write) */
#define ATA_CORR    0x0040 /* transfer had a corrected error */
#define ATA_LBA48   0x0080 /* transfer uses 48-bit LBA addressing */
    int multi; /* number of blocks to transfer in multi-mode */
    struct disklabel *lp; /* pointer to drive's label info */
    daddr_t blkno; /* block addr */
    daddr_t blkdone; /* number of blks transferred */
    daddr_t nblks; /* number of block currently transferring */
    int     nbytes; /* number of bytes currently transferring */
    long    bcount; /* total number of bytes */
    char   *databuf; /* data buffer address */
    volatile int error;
#define NOERROR   0 /* There was no error (r_error invalid) */
#define ERROR     1 /* check r_error */
#define ERR_DF    2 /* Drive fault */
#define ERR_DMA   3 /* DMA error */
#define TIMEOUT   4 /* device timed out */
#define ERR_NODEV 5 /* device bas been detached */
    u_int8_t r_error; /* copy of error register */
    struct wd_softc *wd;
};

struct wd_softc {
	/* General disk infos */
	struct device sc_dev;
	struct disk sc_dk;
	struct bufq sc_bufq;

	/* IDE disk soft states */
	struct ata_bio sc_wdc_bio; /* current transfer */
	struct buf *sc_bp; /* buf being transferred */
	struct ata_drive_datas *drvp; /* Our controller's infos */
	int openings;
	struct ataparams sc_params;/* drive characteristics found */
	int sc_flags;
/*
 * XXX Nothing resets this yet, but disk change sensing will when ATA-4 is
 * more fully implemented.
 */
#define WDF_LOADED	0x10 /* parameters loaded */
#define WDF_WAIT	0x20 /* waiting for resources */
#define WDF_LBA		0x40 /* using LBA mode */
#define WDF_LBA48	0x80 /* using 48-bit LBA mode */

	u_int64_t sc_capacity;
	int cyl; /* actual drive parameters */
	int heads;
	int sectors;
	int retries; /* number of xfer retry */
	struct timeout sc_restart_timeout;
};

/* drive states stored in ata_drive_datas */
#define RECAL          0
#define RECAL_WAIT     1
#define PIOMODE        2
#define PIOMODE_WAIT   3
#define DMAMODE        4
#define DMAMODE_WAIT   5
#define GEOMETRY       6
#define GEOMETRY_WAIT  7
#define MULTIMODE      8
#define MULTIMODE_WAIT 9
#define READY          10

int wdc_ata_bio(struct ata_drive_datas*, struct ata_bio*);
int wd_hibernate_io(dev_t dev, daddr_t blkno, vaddr_t addr, size_t size,
	    int wr, void *page);

void wddone(void *);

#endif	/* !_DEV_ATA_WDVAR_H_ */
