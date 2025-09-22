/*	$OpenBSD: atavar.h,v 1.25 2025/07/15 13:40:02 jsg Exp $	*/
/*	$NetBSD: atavar.h,v 1.13 1999/03/10 13:11:43 bouyer Exp $	*/

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

#ifndef _DEV_ATA_ATAVAR_H_
#define _DEV_ATA_ATAVAR_H_

/* High-level functions and structures used by both ATA and ATAPI devices */
#include <dev/ata/atareg.h>

/* Datas common to drives and controller drivers */
struct ata_drive_datas {
	u_int8_t drive; /* drive number */
	int8_t ata_vers; /* ATA version supported */
	u_int16_t drive_flags; /* bitmask for drives present/absent and cap */
#define DRIVE_ATA	0x0001
#define DRIVE_ATAPI	0x0002
#define DRIVE_OLD	0x0004
#define DRIVE (DRIVE_ATA|DRIVE_ATAPI|DRIVE_OLD)
#define DRIVE_CAP32	0x0008
#define DRIVE_DMA	0x0010
#define DRIVE_UDMA	0x0020
#define DRIVE_MODE	0x0040 /* the drive reported its mode */
#define DRIVE_RESET	0x0080 /* reset the drive state at next xfer */
#define DRIVE_DMAERR	0x0100 /* Udma transfer had crc error, don't try DMA */
#define DRIVE_DSCBA	0x0200 /* DSC in buffer availability mode */
#define DRIVE_DSCWAIT	0x0400 /* In wait for DSC to be asserted */
#define DRIVE_DEVICE_RESET 0x0800 /* Drive supports DEVICE RESET command */
#define DRIVE_SATA	0x1000 /* SATA drive */
	/*
	 * Current setting of drive's PIO, DMA and UDMA modes.
	 * Is initialised by the disks drivers at attach time, and may be
	 * changed later by the controller's code if needed
	 */
	u_int8_t PIO_mode; /* Current setting of drive's PIO mode */
	u_int8_t DMA_mode; /* Current setting of drive's DMA mode */
	u_int8_t UDMA_mode; /* Current setting of drive's UDMA mode */
	/* Supported modes for this drive */
	u_int8_t PIO_cap; /* supported drive's PIO mode */
	u_int8_t DMA_cap; /* supported drive's DMA mode */
	u_int8_t UDMA_cap; /* supported drive's UDMA mode */
	/*
	 * Drive state. This is drive-type (ATA or ATAPI) dependant
	 * This is reset to 0 after a channel reset.
	 */
	u_int8_t state;

#define ACAP_LEN            0x01  /* 16 byte commands */
#define ACAP_DSC            0x02  /* use DSC signalling */
	/* 0x20-0x40 reserved for ATAPI_CFG_DRQ_MASK */
	u_int8_t atapi_cap;

	/* Keeps track of the number of resets that have occurred in a row
	   without a successful command completion. */
	u_int8_t n_resets;
	u_int8_t n_dmaerrs;
	u_int32_t n_xfers;
#define NERRS_MAX 4
#define NXFER 1000

	char drive_name[31];
	int  cf_flags;
	void *chnl_softc; /* channel softc */

	struct ataparams id;
};

/* ATA/ATAPI common attachment data */
struct ata_atapi_attach {
    u_int8_t aa_type; /* Type of device */
#define T_ATA 0
#define T_ATAPI 1
    u_int8_t aa_channel; /* controller's channel */
    u_int8_t aa_openings; /* Number of simultaneous commands possible */
    struct ata_drive_datas *aa_drv_data;
    void *aa_bus_private; /* info specific to this bus */
};

/* User config flags that force (or disable) the use of a mode */
#define ATA_CONFIG_PIO_MODES	0x0007
#define ATA_CONFIG_PIO_SET	0x0008
#define ATA_CONFIG_PIO_OFF	0
#define ATA_CONFIG_DMA_MODES	0x0070
#define ATA_CONFIG_DMA_SET	0x0080
#define ATA_CONFIG_DMA_DISABLE	0x0070
#define ATA_CONFIG_DMA_OFF	4
#define ATA_CONFIG_UDMA_MODES	0x0700
#define ATA_CONFIG_UDMA_SET	0x0800
#define ATA_CONFIG_UDMA_DISABLE	0x0700
#define ATA_CONFIG_UDMA_OFF	8

/*
 * ATA/ATAPI commands description
 *
 * This structure defines the interface between the ATA/ATAPI device driver
 * and the controller for short commands. It contains the command's parameter,
 * the len of data's to read/write (if any), and a function to call upon
 * completion.
 * If no sleep is allowed, the driver can poll for command completion.
 * Once the command completed, if the error register is valid, the flag
 * AT_ERROR is set and the error register value is copied to r_error .
 * A separate interface is needed for read/write or ATAPI packet commands
 * (which need multiple interrupts per commands).
 */
struct wdc_command {
    u_int8_t r_command;  /* Parameters to upload to registers */
    u_int8_t r_head;
    u_int16_t r_cyl;
    u_int8_t r_sector;
    u_int8_t r_count;
    u_int8_t r_features;
    u_int8_t r_st_bmask; /* status register mask to wait for before command */
    u_int8_t r_st_pmask; /* status register mask to wait for after command */
    u_int8_t r_error;    /* error register after command done */
    volatile u_int16_t flags;
#define AT_READ     0x0001 /* There is data to read */
#define AT_WRITE    0x0002 /* There is data to write (excl. with AT_READ) */
#define AT_WAIT     0x0008 /* wait in controller code for command completion */
#define AT_POLL     0x0010 /* poll for command completion (no interrupts) */
#define AT_DONE     0x0020 /* command is done */
#define AT_ERROR    0x0040 /* command is done with error */
#define AT_TIMEOU   0x0080 /* command timed out */
#define AT_DF       0x0100 /* Drive fault */
#define AT_READREG  0x0200 /* Read registers on completion */
    int timeout;         /* timeout (in ms) */
    void *data;          /* Data buffer address */
    int bcount;          /* number of bytes to transfer */
    void (*callback)(void *); /* command to call once command completed */
    void *callback_arg;  /* argument passed to *callback() */
};

extern int at_poll;

int wdc_exec_command(struct ata_drive_datas *, struct wdc_command*);
#define WDC_COMPLETE  0x01
#define WDC_QUEUED    0x02
#define WDC_TRY_AGAIN 0x03

void wdc_probe_caps(struct ata_drive_datas*, struct ataparams *);
void wdc_print_caps(struct ata_drive_datas*);
int  wdc_downgrade_mode(struct ata_drive_datas*);

void wdc_reset_channel(struct ata_drive_datas *, int);

int ata_get_params(struct ata_drive_datas*, u_int8_t,
	struct ataparams *);
int ata_set_mode(struct ata_drive_datas*, u_int8_t, u_int8_t);
/* return code for these cmds */
#define CMD_OK    0
#define CMD_ERR   1
#define CMD_AGAIN 2

void ata_dmaerr(struct ata_drive_datas *);
void ata_perror(struct ata_drive_datas *, int, char *, size_t);

#endif	/* !_DEV_ATA_ATAVAR_H_ */
