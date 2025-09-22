/*	$OpenBSD: wdc.c,v 1.136 2019/12/31 10:05:32 mpi Exp $	*/
/*	$NetBSD: wdc.c,v 1.68 1999/06/23 19:00:17 bouyer Exp $	*/
/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.  All rights reserved.
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
 */

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
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/disk.h>
#include <sys/pool.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/ata/atavar.h>
#include <dev/ata/atareg.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcvar.h>
#include <dev/ic/wdcevent.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#define WDCDELAY  100 /* 100 microseconds */
#define WDCNDELAY_RST (WDC_RESET_WAIT * 1000 / WDCDELAY)
#if 0
/* If you enable this, it will report any delays more than WDCDELAY * N long. */
#define WDCNDELAY_DEBUG	50
#endif /* 0 */

struct pool wdc_xfer_pool;
struct scsi_iopool wdc_xfer_iopool;

void *	wdc_xfer_get(void *);
void	wdc_xfer_put(void *, void *);

void  __wdcerror(struct channel_softc *, char *);
int   __wdcwait_reset(struct channel_softc *, int);
void  __wdccommand_done(struct channel_softc *, struct wdc_xfer *);
void  __wdccommand_start(struct channel_softc *, struct wdc_xfer *);
int   __wdccommand_intr(struct channel_softc *, struct wdc_xfer *, int);
int   wdprint(void *, const char *);
void  wdc_kill_pending(struct channel_softc *);

#define DEBUG_INTR    0x01
#define DEBUG_XFERS   0x02
#define DEBUG_STATUS  0x04
#define DEBUG_FUNCS   0x08
#define DEBUG_PROBE   0x10
#define DEBUG_STATUSX 0x20
#define DEBUG_SDRIVE  0x40
#define DEBUG_DETACH  0x80

#ifdef WDCDEBUG
#ifndef WDCDEBUG_MASK
#define WDCDEBUG_MASK 0x00
#endif
int wdcdebug_mask = WDCDEBUG_MASK;
int wdc_nxfer = 0;
#define WDCDEBUG_PRINT(args, level) do {	\
	if ((wdcdebug_mask & (level)) != 0)	\
		printf args;			\
} while (0)
#else
#define WDCDEBUG_PRINT(args, level)
#endif /* WDCDEBUG */

int at_poll = AT_POLL;

int wdc_floating_bus(struct channel_softc *, int);
int wdc_preata_drive(struct channel_softc *, int);
int wdc_ata_present(struct channel_softc *, int);

struct cfdriver wdc_cd = {
	NULL, "wdc", DV_DULL
};

struct channel_softc_vtbl wdc_default_vtbl = {
	wdc_default_read_reg,
	wdc_default_write_reg,
	wdc_default_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

#ifdef WDCDEBUG
static char *wdc_log_buf = NULL;
static unsigned int wdc_tail = 0;
static unsigned int wdc_head = 0;
static unsigned int wdc_log_cap = 16 * 1024;
static int chp_idx = 1;

void
wdc_log(struct channel_softc *chp, enum wdcevent_type type,
    unsigned int size, char val[])
{
	unsigned int request_size;
	char *ptr;
	int log_size;
	unsigned int head = wdc_head;
	unsigned int tail = wdc_tail;

#ifdef DIAGNOSTIC
	if (head > wdc_log_cap || tail > wdc_log_cap) {
		printf ("wdc_log: head %x wdc_tail %x\n", head,
		    tail);
		return;
	}

	if (size > wdc_log_cap / 2) {
		printf ("wdc_log: type %d size %x\n", type, size);
		return;
	}
#endif

	if (wdc_log_buf == NULL) {
		wdc_log_buf = malloc(wdc_log_cap, M_DEVBUF, M_NOWAIT);
		if (wdc_log_buf == NULL)
			return;
	}
	if (chp->ch_log_idx == 0)
		chp->ch_log_idx = chp_idx++;

	request_size = size + 2;

	/* Check how many bytes are left */
	log_size = head - tail;
	if (log_size < 0) log_size += wdc_log_cap;

	if (log_size + request_size >= wdc_log_cap) {
		int nb = 0; 
		int rec_size;

		while (nb <= (request_size * 2)) {
			if (wdc_log_buf[tail] == 0)
				rec_size = 1;
			else
				rec_size = (wdc_log_buf[tail + 1] & 0x1f) + 2;
			tail = (tail + rec_size) % wdc_log_cap;
			nb += rec_size;
		}
	}

	/* Avoid wrapping in the middle of a request */
	if (head + request_size >= wdc_log_cap) {
		memset(&wdc_log_buf[head], 0, wdc_log_cap - head);
		head = 0;
	}

	ptr = &wdc_log_buf[head];
	*ptr++ = type & 0xff;
	*ptr++ = ((chp->ch_log_idx & 0x7) << 5) | (size & 0x1f);
	memcpy(ptr, val, size);

	wdc_head = (head + request_size) % wdc_log_cap;
	wdc_tail = tail;
}

char *wdc_get_log(unsigned int *, unsigned int *);

char *
wdc_get_log(unsigned int * size, unsigned int *left)
{
	int  log_size;
	char *retbuf = NULL;
	int  nb, tocopy;
	int  s;
	unsigned int head = wdc_head;
	unsigned int tail = wdc_tail;

	s = splbio();

	log_size = (head - tail);
	if (left != NULL)
		*left = 0;

	if (log_size < 0)
		log_size += wdc_log_cap;

	tocopy = log_size;
	if ((u_int)tocopy > *size)
		tocopy = *size;

	if (wdc_log_buf == NULL) {
		*size = 0;
		*left = 0;
		goto out;
	}

#ifdef DIAGNOSTIC
	if (head > wdc_log_cap || tail > wdc_log_cap) {
		printf ("wdc_log: head %x tail %x\n", head,
		    tail);
		*size = 0;
		*left = 0;
		goto out;
	}
#endif

	retbuf = malloc(tocopy, M_TEMP, M_NOWAIT);
	if (retbuf == NULL) {
		*size = 0;
		*left = log_size;
		goto out;
	}

	nb = 0;
	for (;;) {
		int rec_size;

		if (wdc_log_buf[tail] == 0)
			rec_size = 1;
		else
			rec_size = (wdc_log_buf[tail + 1] & 0x1f) + 2;

		if ((nb + rec_size) >= tocopy)
			break;

		memcpy(&retbuf[nb], &wdc_log_buf[tail], rec_size);
		tail = (tail + rec_size) % wdc_log_cap;
		nb += rec_size;
	}

	wdc_tail = tail;
	*size = nb;
	*left = log_size - nb;

 out:
	splx(s);
	return (retbuf);
}
#endif /* WDCDEBUG */

u_int8_t
wdc_default_read_reg(struct channel_softc *chp, enum wdc_regs reg)
{
#ifdef DIAGNOSTIC
	if (reg & _WDC_WRONLY) {
		printf ("wdc_default_read_reg: reading from a write-only register %d\n", reg);
	}
#endif /* DIAGNOSTIC */

	if (reg & _WDC_AUX)
		return (bus_space_read_1(chp->ctl_iot, chp->ctl_ioh,
		    reg & _WDC_REGMASK));
	else
		return (bus_space_read_1(chp->cmd_iot, chp->cmd_ioh,
		    reg & _WDC_REGMASK));
}

void
wdc_default_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int8_t val)
{
#ifdef DIAGNOSTIC
	if (reg & _WDC_RDONLY) {
		printf ("wdc_default_write_reg: writing to a read-only register %d\n", reg);
	}
#endif /* DIAGNOSTIC */

	if (reg & _WDC_AUX)
		bus_space_write_1(chp->ctl_iot, chp->ctl_ioh,
		    reg & _WDC_REGMASK, val);
	else
		bus_space_write_1(chp->cmd_iot, chp->cmd_ioh,
		    reg & _WDC_REGMASK, val);
}

void
wdc_default_lba48_write_reg(struct channel_softc *chp, enum wdc_regs reg,
    u_int16_t val)
{
	/* All registers are two byte deep FIFOs. */
	CHP_WRITE_REG(chp, reg, val >> 8);
	CHP_WRITE_REG(chp, reg, val);
}

void
wdc_default_read_raw_multi_2(struct channel_softc *chp, void *data,
    unsigned int nbytes)
{
	if (data == NULL) {
		unsigned int i;

		for (i = 0; i < nbytes; i += 2) {
			bus_space_read_2(chp->cmd_iot, chp->cmd_ioh, 0);
		}

		return;
	}

	bus_space_read_raw_multi_2(chp->cmd_iot, chp->cmd_ioh, 0,
	    data, nbytes);
}


void
wdc_default_write_raw_multi_2(struct channel_softc *chp, void *data,
    unsigned int nbytes)
{
	if (data == NULL) {
		unsigned int i;

		for (i = 0; i < nbytes; i += 2) {
			bus_space_write_2(chp->cmd_iot, chp->cmd_ioh, 0, 0);
		}

		return;
	}

	bus_space_write_raw_multi_2(chp->cmd_iot, chp->cmd_ioh, 0,
	    data, nbytes);
}


void
wdc_default_write_raw_multi_4(struct channel_softc *chp, void *data,
    unsigned int nbytes)
{
	if (data == NULL) {
		unsigned int i;

		for (i = 0; i < nbytes; i += 4) {
			bus_space_write_4(chp->cmd_iot, chp->cmd_ioh, 0, 0);
		}

		return;
	}

	bus_space_write_raw_multi_4(chp->cmd_iot, chp->cmd_ioh, 0,
	    data, nbytes);
}


void
wdc_default_read_raw_multi_4(struct channel_softc *chp, void *data,
    unsigned int nbytes)
{
	if (data == NULL) {
		unsigned int i;

		for (i = 0; i < nbytes; i += 4) {
			bus_space_read_4(chp->cmd_iot, chp->cmd_ioh, 0);
		}

		return;
	}

	bus_space_read_raw_multi_4(chp->cmd_iot, chp->cmd_ioh, 0,
	    data, nbytes);
}

int
wdprint(void *aux, const char *pnp)
{
	struct ata_atapi_attach *aa_link = aux;
	if (pnp)
		printf("drive at %s", pnp);
	printf(" channel %d drive %d", aa_link->aa_channel,
	    aa_link->aa_drv_data->drive);
	return (UNCONF);
}

void
wdc_disable_intr(struct channel_softc *chp)
{
	CHP_WRITE_REG(chp, wdr_ctlr, WDCTL_IDS);
}

void
wdc_enable_intr(struct channel_softc *chp)
{
	CHP_WRITE_REG(chp, wdr_ctlr, WDCTL_4BIT);
}

void
wdc_set_drive(struct channel_softc *chp, int drive)
{
	CHP_WRITE_REG(chp, wdr_sdh, (drive << 4) | WDSD_IBM);
	WDC_LOG_SET_DRIVE(chp, drive);
}

int
wdc_floating_bus(struct channel_softc *chp, int drive)
{
	u_int8_t cumulative_status, status;
	int      iter;

	wdc_set_drive(chp, drive);
	delay(10);

	/* Stolen from Phoenix BIOS Drive Autotyping document */
	cumulative_status = 0;
	for (iter = 0; iter < 100; iter++) {
		CHP_WRITE_REG(chp, wdr_seccnt, 0x7f);
		delay (1);

		status = CHP_READ_REG(chp, wdr_status);

		/* The other bits are meaningless if BSY is set */
		if (status & WDCS_BSY)
			continue;

		cumulative_status |= status;

#define BAD_BIT_COMBO  (WDCS_DRDY | WDCS_DSC | WDCS_DRQ | WDCS_ERR)
		if ((cumulative_status & BAD_BIT_COMBO) == BAD_BIT_COMBO)
			return 1;
	}


	return 0;
}

int
wdc_preata_drive(struct channel_softc *chp, int drive)
{
	if (wdc_floating_bus(chp, drive)) {
		WDCDEBUG_PRINT(("%s:%d:%d: floating bus detected\n",
		    chp->wdc->sc_dev.dv_xname,
		    chp->channel, drive), DEBUG_PROBE);
		return 0;
	}

	wdc_set_drive(chp, drive);
	delay(100);
	if (wdcwait(chp, WDCS_DRDY | WDCS_DRQ, WDCS_DRDY, 10000) != 0) {
		WDCDEBUG_PRINT(("%s:%d:%d: not ready\n",
		    chp->wdc->sc_dev.dv_xname,
		    chp->channel, drive), DEBUG_PROBE);
		return 0;
	}

	CHP_WRITE_REG(chp, wdr_command, WDCC_RECAL);
	WDC_LOG_ATA_CMDSHORT(chp, WDCC_RECAL);
	if (wdcwait(chp, WDCS_DRDY | WDCS_DRQ, WDCS_DRDY, 10000) != 0) {
		WDCDEBUG_PRINT(("%s:%d:%d: WDCC_RECAL failed\n",
		    chp->wdc->sc_dev.dv_xname,
		    chp->channel, drive), DEBUG_PROBE);
		return 0;
	}

	return 1;
}

int
wdc_ata_present(struct channel_softc *chp, int drive)
{
	int time_to_done;
	int retry_cnt = 0;

	wdc_set_drive(chp, drive);
	delay(10);

retry:
	/*
	   You're actually supposed to wait up to 10 seconds
	   for DRDY. However, as a practical matter, most
	   drives assert DRDY very quickly after dropping BSY.

	   The 10 seconds wait is sub-optimal because, according
	   to the ATA standard, the master should reply with 00
	   for any reads to a non-existent slave.
	*/
	time_to_done = wdc_wait_for_status(chp,
	    (WDCS_DRDY | WDCS_DSC | WDCS_DRQ),
	    (WDCS_DRDY | WDCS_DSC), 1000);
	if (time_to_done == -1) {
		if (retry_cnt == 0 && chp->ch_status == 0x00) {
			/* At least one flash card needs to be kicked */
			wdccommandshort(chp, drive, WDCC_CHECK_PWR);
			retry_cnt++;
			goto retry;
		}
		WDCDEBUG_PRINT(("%s:%d:%d: DRDY test timed out with status"
		    " %02x\n",
		    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
		    chp->channel, drive, chp->ch_status),
		    DEBUG_PROBE);
		return 0;
	}

	if ((chp->ch_status & 0xfc) != (WDCS_DRDY | WDCS_DSC)) {
		WDCDEBUG_PRINT(("%s:%d:%d: status test for 0x50 failed with"
		    " %02x\n",
		    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
		    chp->channel, drive, chp->ch_status),
		    DEBUG_PROBE);

		return 0;
	}

	WDCDEBUG_PRINT(("%s:%d:%d: waiting for ready %d msec\n",
	    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
	    chp->channel, drive, time_to_done), DEBUG_PROBE);

	/*
	 * Test register writability
	 */
	CHP_WRITE_REG(chp, wdr_cyl_lo, 0xaa);
	CHP_WRITE_REG(chp, wdr_cyl_hi, 0x55);
	CHP_WRITE_REG(chp, wdr_seccnt, 0xff);
	DELAY(10);

	if (CHP_READ_REG(chp, wdr_cyl_lo) != 0xaa &&
	    CHP_READ_REG(chp, wdr_cyl_hi) != 0x55) {
		WDCDEBUG_PRINT(("%s:%d:%d: register writability failed\n",
		    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
		    chp->channel, drive), DEBUG_PROBE);
		return 0;
	}

	return 1;
}


/*
 * Test to see controller with at least one attached drive is there.
 * Returns a bit for each possible drive found (0x01 for drive 0,
 * 0x02 for drive 1).
 * Logic:
 * - If a status register is at 0x7f or 0xff, assume there is no drive here
 *   (ISA has pull-up resistors).  Similarly if the status register has
 *   the value we last wrote to the bus (for IDE interfaces without pullups).
 *   If no drive at all -> return.
 * - reset the controller, wait for it to complete (may take up to 31s !).
 *   If timeout -> return.
 * - test ATA/ATAPI signatures. If at last one drive found -> return.
 * - try an ATA command on the master.
 */

int
wdcprobe(struct channel_softc *chp)
{
	u_int8_t st0, st1, sc, sn, cl, ch;
	u_int8_t ret_value = 0x03;
	u_int8_t drive;
#ifdef WDCDEBUG
	int savedmask = wdcdebug_mask;
#endif

	if (chp->_vtbl == 0) {
		int s = splbio();
		chp->_vtbl = &wdc_default_vtbl;
		splx(s);
	}

#ifdef WDCDEBUG
	if ((chp->ch_flags & WDCF_VERBOSE_PROBE) ||
	    (chp->wdc &&
	    (chp->wdc->sc_dev.dv_cfdata->cf_flags & WDC_OPTION_PROBE_VERBOSE)))
		wdcdebug_mask |= DEBUG_PROBE;
#endif /* WDCDEBUG */

	if (chp->wdc == NULL ||
	    (chp->wdc->cap & WDC_CAPABILITY_NO_EXTRA_RESETS) == 0) {
		/* Sample the statuses of drive 0 and 1 into st0 and st1 */
		wdc_set_drive(chp, 0);
		delay(10);
		st0 = CHP_READ_REG(chp, wdr_status);
		WDC_LOG_STATUS(chp, st0);
		wdc_set_drive(chp, 1);
		delay(10);
		st1 = CHP_READ_REG(chp, wdr_status);
		WDC_LOG_STATUS(chp, st1);

		WDCDEBUG_PRINT(("%s:%d: before reset, st0=0x%b, st1=0x%b\n",
		    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
		    chp->channel, st0, WDCS_BITS, st1, WDCS_BITS),
		    DEBUG_PROBE);

		if (st0 == 0xff || st0 == WDSD_IBM)
			ret_value &= ~0x01;
		if (st1 == 0xff || st1 == (WDSD_IBM | 0x10))
			ret_value &= ~0x02;
		if (ret_value == 0)
			return 0;
	}

	/* reset the channel */
	wdc_do_reset(chp);

	ret_value = __wdcwait_reset(chp, ret_value);
	WDCDEBUG_PRINT(("%s:%d: after reset, ret_value=0x%d\n",
	    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe", chp->channel,
	    ret_value), DEBUG_PROBE);

	if (ret_value == 0)
		return 0;

	if (chp->wdc && (chp->wdc->quirks & WDC_QUIRK_NOATAPI))
		goto noatapi;

	/*
	 * Use signatures to find potential ATAPI drives
	 */
	for (drive = 0; drive < 2; drive++) {
 		if ((ret_value & (0x01 << drive)) == 0)
			continue;
		wdc_set_drive(chp, drive);
		delay(10);
		/* Save registers contents */
		st0 = CHP_READ_REG(chp, wdr_status);
		sc = CHP_READ_REG(chp, wdr_seccnt);
		sn = CHP_READ_REG(chp, wdr_sector);
		cl = CHP_READ_REG(chp, wdr_cyl_lo);
		ch = CHP_READ_REG(chp, wdr_cyl_hi);
		WDC_LOG_REG(chp, wdr_cyl_lo, (ch << 8) | cl);

		WDCDEBUG_PRINT(("%s:%d:%d: after reset, st=0x%b, sc=0x%x"
		    " sn=0x%x cl=0x%x ch=0x%x\n",
		    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe",
		    chp->channel, drive, st0, WDCS_BITS, sc, sn, cl, ch),
		    DEBUG_PROBE);
		/*
		 * This is a simplification of the test in the ATAPI
		 * spec since not all drives seem to set the other regs
		 * correctly.
		 */
		if (cl == 0x14 && ch == 0xeb)
			chp->ch_drive[drive].drive_flags |= DRIVE_ATAPI;
	}

noatapi:
	if (chp->wdc && (chp->wdc->quirks & WDC_QUIRK_NOATA))
		goto noata;

	/*
	 * Detect ATA drives by poking around the registers
	 */
	for (drive = 0; drive < 2; drive++) {
 		if ((ret_value & (0x01 << drive)) == 0)
			continue;
		if (chp->ch_drive[drive].drive_flags & DRIVE_ATAPI)
			continue;

		wdc_disable_intr(chp);
		/* ATA detect */
		if (wdc_ata_present(chp, drive)) {
			chp->ch_drive[drive].drive_flags |= DRIVE_ATA;
			if (chp->wdc == NULL ||
			    (chp->wdc->cap & WDC_CAPABILITY_PREATA) != 0)
				chp->ch_drive[drive].drive_flags |= DRIVE_OLD;
		} else {
			ret_value &= ~(1 << drive);
		}
		wdc_enable_intr(chp);
	}

noata:

#ifdef WDCDEBUG
	wdcdebug_mask = savedmask;
#endif
	return (ret_value);
}

struct channel_queue *
wdc_alloc_queue(void)
{
	static int inited = 0;
	struct channel_queue *queue;

	/* Initialize global data. */
	if (inited == 0) {
		/* Initialize the wdc_xfer pool. */
		pool_init(&wdc_xfer_pool, sizeof(struct wdc_xfer), 0, IPL_BIO,
		    0, "wdcxfer", NULL);
		scsi_iopool_init(&wdc_xfer_iopool, NULL,
		    wdc_xfer_get, wdc_xfer_put);
		inited = 1;
	}

	queue = malloc(sizeof(*queue), M_DEVBUF, M_NOWAIT);
	if (queue != NULL) {
		TAILQ_INIT(&queue->sc_xfer);
	}
	return (queue);
}

void
wdc_free_queue(struct channel_queue *queue)
{
	free(queue, M_DEVBUF, sizeof(*queue));
}

void
wdcattach(struct channel_softc *chp)
{
	int i;
	struct ata_atapi_attach aa_link;
#ifdef WDCDEBUG
	int    savedmask = wdcdebug_mask;
#endif

	if (!cold)
		at_poll = AT_WAIT;

	if (chp->wdc->reset == NULL)
		chp->wdc->reset = wdc_do_reset;

	timeout_set(&chp->ch_timo, wdctimeout, chp);

	if (!chp->_vtbl)
		chp->_vtbl = &wdc_default_vtbl;

	for (i = 0; i < 2; i++) {
		chp->ch_drive[i].chnl_softc = chp;
		chp->ch_drive[i].drive = i;
	}

	if (chp->wdc->drv_probe != NULL) {
		chp->wdc->drv_probe(chp);
	} else {
		if (wdcprobe(chp) == 0)
			/* If no drives, abort attach here. */
			return;
	}

	/* ATAPI drives need settling time. Give them 250ms */
	if ((chp->ch_drive[0].drive_flags & DRIVE_ATAPI) ||
	    (chp->ch_drive[1].drive_flags & DRIVE_ATAPI)) {
		delay(250 * 1000);
	}

#ifdef WDCDEBUG
	if (chp->wdc->sc_dev.dv_cfdata->cf_flags & WDC_OPTION_PROBE_VERBOSE)
		wdcdebug_mask |= DEBUG_PROBE;

	if ((chp->ch_drive[0].drive_flags & DRIVE_ATAPI) ||
	    (chp->ch_drive[1].drive_flags & DRIVE_ATAPI)) {
		wdcdebug_mask = DEBUG_PROBE;
	}
#endif /* WDCDEBUG */

	for (i = 0; i < 2; i++) {
		struct ata_drive_datas *drvp = &chp->ch_drive[i];

		/* If controller can't do 16bit flag the drives as 32bit */
		if ((chp->wdc->cap &
		    (WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32)) ==
		    WDC_CAPABILITY_DATA32)
			drvp->drive_flags |= DRIVE_CAP32;

		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		if (i == 1 && ((chp->ch_drive[0].drive_flags & DRIVE) == 0))
			chp->ch_flags |= WDCF_ONESLAVE;
		/*
		 * Wait a bit, some devices are weird just after a reset.
		 * Then issue a IDENTIFY command, to try to detect slave ghost.
		 */
		delay(5000);
		if (ata_get_params(&chp->ch_drive[i], at_poll, &drvp->id) ==
		    CMD_OK) {
			/* If IDENTIFY succeeded, this is not an OLD ctrl */
			drvp->drive_flags &= ~DRIVE_OLD;
		} else {
			bzero(&drvp->id, sizeof(struct ataparams));
			drvp->drive_flags &=
			    ~(DRIVE_ATA | DRIVE_ATAPI);
			WDCDEBUG_PRINT(("%s:%d:%d: IDENTIFY failed\n",
			    chp->wdc->sc_dev.dv_xname,
			    chp->channel, i), DEBUG_PROBE);

			if ((drvp->drive_flags & DRIVE_OLD) &&
			    !wdc_preata_drive(chp, i))
				drvp->drive_flags &= ~DRIVE_OLD;
		}
	}

	WDCDEBUG_PRINT(("wdcattach: ch_drive_flags 0x%x 0x%x\n",
	    chp->ch_drive[0].drive_flags, chp->ch_drive[1].drive_flags),
	    DEBUG_PROBE);

	/* If no drives, abort here */
	if ((chp->ch_drive[0].drive_flags & DRIVE) == 0 &&
	    (chp->ch_drive[1].drive_flags & DRIVE) == 0)
		goto exit;

	for (i = 0; i < 2; i++) {
		if ((chp->ch_drive[i].drive_flags & DRIVE) == 0) {
			continue;
		}
		bzero(&aa_link, sizeof(struct ata_atapi_attach));
		if (chp->ch_drive[i].drive_flags & DRIVE_ATAPI)
			aa_link.aa_type = T_ATAPI;
		else
			aa_link.aa_type = T_ATA;
		aa_link.aa_channel = chp->channel;
		aa_link.aa_openings = 1;
		aa_link.aa_drv_data = &chp->ch_drive[i];
		config_found(&chp->wdc->sc_dev, (void *)&aa_link, wdprint);
	}

	/*
	 * reset drive_flags for unattached devices, reset state for attached
	 *  ones
	 */
	for (i = 0; i < 2; i++) {
		if (chp->ch_drive[i].drive_name[0] == 0)
			chp->ch_drive[i].drive_flags = 0;
	}

exit:
#ifdef WDCDEBUG
	wdcdebug_mask = savedmask;
#endif
	return;	/* for the ``exit'' label above */
}

/*
 * Start I/O on a controller, for the given channel.
 * The first xfer may be not for our channel if the channel queues
 * are shared.
 */
void
wdcstart(struct channel_softc *chp)
{
	struct wdc_xfer *xfer;

	splassert(IPL_BIO);

	/* is there a xfer ? */
	if ((xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer)) == NULL) {
		return;
	}

	/* adjust chp, in case we have a shared queue */
	chp = xfer->chp;

	if ((chp->ch_flags & WDCF_ACTIVE) != 0 ) {
		return; /* channel already active */
	}
#ifdef DIAGNOSTIC
	if ((chp->ch_flags & WDCF_IRQ_WAIT) != 0)
		panic("wdcstart: channel waiting for irq");
#endif /* DIAGNOSTIC */

	WDCDEBUG_PRINT(("wdcstart: xfer %p channel %d drive %d\n", xfer,
	    chp->channel, xfer->drive), DEBUG_XFERS);
	chp->ch_flags |= WDCF_ACTIVE;
	if (chp->ch_drive[xfer->drive].drive_flags & DRIVE_RESET) {
		chp->ch_drive[xfer->drive].drive_flags &= ~DRIVE_RESET;
		chp->ch_drive[xfer->drive].state = 0;
	}
	xfer->c_start(chp, xfer);
}

int
wdcdetach(struct channel_softc *chp, int flags)
{
	int s, rv;

	s = splbio();
	chp->dying = 1;

	wdc_kill_pending(chp);
	timeout_del(&chp->ch_timo);

	rv = config_detach_children((struct device *)chp->wdc, flags);
	splx(s);

	return (rv);
}

/*
 * Interrupt routine for the controller.  Acknowledge the interrupt, check for
 * errors on the current operation, mark it done if necessary, and start the
 * next request.  Also check for a partially done transfer, and continue with
 * the next chunk if so.
 */
int
wdcintr(void *arg)
{
	struct channel_softc *chp = arg;
	struct wdc_xfer *xfer;
	u_int8_t st = 0;
	int ret = 0;

	if ((chp->ch_flags & WDCF_IRQ_WAIT) == 0) {
		/* Acknowledge interrupt by reading status */
		if (chp->_vtbl == 0)
			st = bus_space_read_1(chp->cmd_iot, chp->cmd_ioh,
			    wdr_status & _WDC_REGMASK);
		else
			st = CHP_READ_REG(chp, wdr_status);
		if (st == 0xff)
			return (-1);

		WDCDEBUG_PRINT(("wdcintr: inactive controller\n"), DEBUG_INTR);
		return ret;
	}

	WDCDEBUG_PRINT(("wdcintr\n"), DEBUG_INTR);
	xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer);
	if (chp->ch_flags & WDCF_DMA_WAIT) {
		chp->wdc->dma_status =
		    (*chp->wdc->dma_finish)(chp->wdc->dma_arg, chp->channel,
		    xfer->drive, 0);
		if (chp->wdc->dma_status == 0xff)
			return (-1);
		if (chp->wdc->dma_status & WDC_DMAST_NOIRQ) {
			/* IRQ not for us, not detected by DMA engine */
			return 0;
		}
		chp->ch_flags &= ~WDCF_DMA_WAIT;
	}
		
	chp->ch_flags &= ~WDCF_IRQ_WAIT;
	ret = xfer->c_intr(chp, xfer, 1);
	if (ret == 0)	/* irq was not for us, still waiting for irq */
		chp->ch_flags |= WDCF_IRQ_WAIT;
	return (ret);
}

/* Put all disk in RESET state */
void
wdc_reset_channel(struct ata_drive_datas *drvp, int nowait)
{
	struct channel_softc *chp = drvp->chnl_softc;
	int drive;

	WDCDEBUG_PRINT(("ata_reset_channel %s:%d for drive %d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drvp->drive),
	    DEBUG_FUNCS);
	(void) wdcreset(chp, nowait ? NOWAIT : VERBOSE);
	for (drive = 0; drive < 2; drive++) {
		chp->ch_drive[drive].state = 0;
	}
}

int
wdcreset(struct channel_softc *chp, int flags)
{
	int drv_mask1, drv_mask2;

	if (!chp->_vtbl)
		chp->_vtbl = &wdc_default_vtbl;

	chp->wdc->reset(chp);

	if (flags & NOWAIT)
		return 0;

	drv_mask1 = (chp->ch_drive[0].drive_flags & DRIVE) ? 0x01:0x00;
	drv_mask1 |= (chp->ch_drive[1].drive_flags & DRIVE) ? 0x02:0x00;
	drv_mask2 = __wdcwait_reset(chp, drv_mask1);

	if ((flags & VERBOSE) && drv_mask2 != drv_mask1) {
		printf("%s channel %d: reset failed for",
		    chp->wdc->sc_dev.dv_xname, chp->channel);
		if ((drv_mask1 & 0x01) != 0 && (drv_mask2 & 0x01) == 0)
			printf(" drive 0");
		if ((drv_mask1 & 0x02) != 0 && (drv_mask2 & 0x02) == 0)
			printf(" drive 1");
		printf("\n");
	}

	return (drv_mask1 != drv_mask2) ? 1 : 0;
}

void
wdc_do_reset(struct channel_softc *chp)
{
	wdc_set_drive(chp, 0);
	DELAY(10);
	CHP_WRITE_REG(chp, wdr_ctlr, WDCTL_4BIT | WDCTL_RST);
	delay(10000);
	CHP_WRITE_REG(chp, wdr_ctlr, WDCTL_4BIT);
	delay(10000);
}

int
__wdcwait_reset(struct channel_softc *chp, int drv_mask)
{
	int timeout;
	u_int8_t st0, er0, st1, er1;

	/* wait for BSY to deassert */
	for (timeout = 0; timeout < WDCNDELAY_RST; timeout++) {
		wdc_set_drive(chp, 0);
		delay(10);
		st0 = CHP_READ_REG(chp, wdr_status);
		er0 = CHP_READ_REG(chp, wdr_error);
		wdc_set_drive(chp, 1);
		delay(10);
		st1 = CHP_READ_REG(chp, wdr_status);
		er1 = CHP_READ_REG(chp, wdr_error);

		if ((drv_mask & 0x01) == 0) {
			/* no master */
			if ((drv_mask & 0x02) != 0 && (st1 & WDCS_BSY) == 0) {
				/* No master, slave is ready, it's done */
				goto end;
			}
		} else if ((drv_mask & 0x02) == 0) {
			/* no slave */
			if ((drv_mask & 0x01) != 0 && (st0 & WDCS_BSY) == 0) {
				/* No slave, master is ready, it's done */
				goto end;
			}
		} else {
			/* Wait for both master and slave to be ready */
			if ((st0 & WDCS_BSY) == 0 && (st1 & WDCS_BSY) == 0) {
				goto end;
			}
		}
		delay(WDCDELAY);
	}
	/* Reset timed out. Maybe it's because drv_mask was not right */
	if (st0 & WDCS_BSY)
		drv_mask &= ~0x01;
	if (st1 & WDCS_BSY)
		drv_mask &= ~0x02;
end:
	WDCDEBUG_PRINT(("%s:%d: wdcwait_reset() end, st0=0x%b, er0=0x%x, "
	    "st1=0x%b, er1=0x%x, reset time=%d msec\n",
	    chp->wdc ? chp->wdc->sc_dev.dv_xname : "wdcprobe", chp->channel,
	    st0, WDCS_BITS, er0, st1, WDCS_BITS, er1,
	    timeout * WDCDELAY / 1000), DEBUG_PROBE);

	return drv_mask;
}

/*
 * Wait for a drive to be !BSY, and have mask in its status register.
 * return -1 for a timeout after "timeout" ms.
 */
int
wdc_wait_for_status(struct channel_softc *chp, int mask, int bits, int timeout)
{
	u_char status;
	int time = 0;

	WDCDEBUG_PRINT(("wdcwait %s:%d\n", chp->wdc ?chp->wdc->sc_dev.dv_xname
	    :"none", chp->channel), DEBUG_STATUS);
	chp->ch_error = 0;

	timeout = timeout * 1000 / WDCDELAY; /* delay uses microseconds */

	for (;;) {
		chp->ch_status = status = CHP_READ_REG(chp, wdr_status);
		WDC_LOG_STATUS(chp, chp->ch_status);

		if (status == 0xff) {
			if ((chp->ch_flags & WDCF_ONESLAVE)) {
				wdc_set_drive(chp, 1);
				chp->ch_status = status =
				    CHP_READ_REG(chp, wdr_status);
				WDC_LOG_STATUS(chp, chp->ch_status);
			}
		}
		if ((status & WDCS_BSY) == 0 && (status & mask) == bits)
			break;
		if (++time > timeout) {
			WDCDEBUG_PRINT(("wdcwait: timeout, status 0x%b "
			    "error 0x%x\n", status, WDCS_BITS,
			    CHP_READ_REG(chp, wdr_error)),
			    DEBUG_STATUSX | DEBUG_STATUS);
			return -1;
		}
		delay(WDCDELAY);
	}
	if (status & WDCS_ERR) {
		chp->ch_error = CHP_READ_REG(chp, wdr_error);
		WDC_LOG_ERROR(chp, chp->ch_error);

		WDCDEBUG_PRINT(("wdcwait: error %x\n", chp->ch_error),
			       DEBUG_STATUSX | DEBUG_STATUS);
	}

#ifdef WDCNDELAY_DEBUG
	/* After autoconfig, there should be no long delays. */
	if (!cold && time > WDCNDELAY_DEBUG) {
		struct wdc_xfer *xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer);
		if (xfer == NULL)
			printf("%s channel %d: warning: busy-wait took %dus\n",
			    chp->wdc->sc_dev.dv_xname, chp->channel,
			    WDCDELAY * time);
		else
			printf("%s:%d:%d: warning: busy-wait took %dus\n",
			    chp->wdc->sc_dev.dv_xname, chp->channel,
			    xfer->drive,
			    WDCDELAY * time);
	}
#endif /* WDCNDELAY_DEBUG */
	return time;
}

/*
 * Busy-wait for DMA to complete
 */
int
wdc_dmawait(struct channel_softc *chp, struct wdc_xfer *xfer, int timeout)
{
	int time;
	for (time = 0; time < timeout * 1000 / WDCDELAY; time++) {
		chp->wdc->dma_status =
		    (*chp->wdc->dma_finish)(chp->wdc->dma_arg,
		    chp->channel, xfer->drive, 0);
		if ((chp->wdc->dma_status & WDC_DMAST_NOIRQ) == 0)
			return 0;
		if (chp->wdc->dma_status == 0xff) {
			chp->dying = 1;
			return -1;
		}
		delay(WDCDELAY);
	}
	/* timeout, force a DMA halt */
	chp->wdc->dma_status = (*chp->wdc->dma_finish)(chp->wdc->dma_arg,
	    chp->channel, xfer->drive, 1);
	return 1;
}

void
wdctimeout(void *arg)
{
	struct channel_softc *chp = (struct channel_softc *)arg;
	struct wdc_xfer *xfer;
	int s;

	WDCDEBUG_PRINT(("wdctimeout\n"), DEBUG_FUNCS);

	s = splbio();
	xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer);

	/* Did we lose a race with the interrupt? */
	if (xfer == NULL ||
	    !timeout_triggered(&chp->ch_timo)) {
		splx(s);
		return;
	}
	if ((chp->ch_flags & WDCF_IRQ_WAIT) != 0) {
		__wdcerror(chp, "timeout");
		printf("\ttype: %s\n", (xfer->c_flags & C_ATAPI) ?
		    "atapi":"ata");
		printf("\tc_bcount: %d\n", xfer->c_bcount);
		printf("\tc_skip: %d\n", xfer->c_skip);
		if (chp->ch_flags & WDCF_DMA_WAIT) {
			chp->wdc->dma_status =
			    (*chp->wdc->dma_finish)(chp->wdc->dma_arg,
			    chp->channel, xfer->drive, 1);
			chp->ch_flags &= ~WDCF_DMA_WAIT;
		}
		/*
		 * Call the interrupt routine. If we just missed and interrupt,
		 * it will do what's needed. Else, it will take the needed
		 * action (reset the device).
		 */
		xfer->c_flags |= C_TIMEOU;
		chp->ch_flags &= ~WDCF_IRQ_WAIT;
		xfer->c_intr(chp, xfer, 1);
	} else
		__wdcerror(chp, "missing untimeout");
	splx(s);
}

/*
 * Probe drive's capabilities, for use by the controller later.
 * Assumes drvp points to an existing drive.
 * XXX this should be a controller-indep function
 */
void
wdc_probe_caps(struct ata_drive_datas *drvp, struct ataparams *params)
{
	struct channel_softc *chp = drvp->chnl_softc;
	struct wdc_softc *wdc = chp->wdc;
	int i, valid_mode_found;
	int cf_flags = drvp->cf_flags;

	if ((wdc->cap & (WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32)) ==
	    (WDC_CAPABILITY_DATA16 | WDC_CAPABILITY_DATA32)) {
		struct ataparams params2;

		/*
		 * Controller claims 16 and 32 bit transfers.
		 * Re-do an IDENTIFY with 32-bit transfers,
		 * and compare results.
		 */
		drvp->drive_flags |= DRIVE_CAP32;
		ata_get_params(drvp, at_poll, &params2);
		if (bcmp(params, &params2, sizeof(struct ataparams)) != 0) {
			/* Not good. fall back to 16bits */
			drvp->drive_flags &= ~DRIVE_CAP32;
		}
	}
#if 0 /* Some ultra-DMA drives claims to only support ATA-3. sigh */
	if (params->atap_ata_major > 0x01 &&
	    params->atap_ata_major != 0xffff) {
		for (i = 14; i > 0; i--) {
			if (params->atap_ata_major & (1 << i)) {
				printf("%sATA version %d\n", sep, i);
				drvp->ata_vers = i;
				break;
			}
		}
	} else
#endif /* 0 */
	/* Use PIO mode 3 as a default value for ATAPI devices */
	if (drvp->drive_flags & DRIVE_ATAPI)
		drvp->PIO_mode = 3;

	WDCDEBUG_PRINT(("wdc_probe_caps: wdc_cap 0x%x cf_flags 0x%x\n",
	    wdc->cap, cf_flags), DEBUG_PROBE);

	valid_mode_found = 0;

	WDCDEBUG_PRINT(("%s: atap_oldpiotiming=%d\n", __func__,
	    params->atap_oldpiotiming), DEBUG_PROBE);
	/*
	 * ATA-4 compliant devices contain PIO mode
	 * number in atap_oldpiotiming.
	 */
	if (params->atap_oldpiotiming <= 2) {
		drvp->PIO_cap = params->atap_oldpiotiming;
		valid_mode_found = 1;
		drvp->drive_flags |= DRIVE_MODE;
	} else if (params->atap_oldpiotiming > 180) {
		/*
		 * ATA-2 compliant devices contain cycle
		 * time in atap_oldpiotiming.
		 * A device with a cycle time of 180ns
		 * or less is at least PIO mode 3 and
		 * should be reporting that in
		 * atap_piomode_supp, so ignore it here.
		 */
		if (params->atap_oldpiotiming <= 240) {
			drvp->PIO_cap = 2;
		} else {
			drvp->PIO_cap = 1;
		}
		valid_mode_found = 1;
		drvp->drive_flags |= DRIVE_MODE;
	}
	if (valid_mode_found)
		drvp->PIO_mode = drvp->PIO_cap;

	WDCDEBUG_PRINT(("%s: atap_extensions=0x%x, atap_piomode_supp=0x%x, "
	    "atap_dmamode_supp=0x%x, atap_udmamode_supp=0x%x\n",
	    __func__, params->atap_extensions, params->atap_piomode_supp,
	    params->atap_dmamode_supp, params->atap_udmamode_supp),
	    DEBUG_PROBE);

	/*
	 * It's not in the specs, but it seems that some drive
	 * returns 0xffff in atap_extensions when this field is invalid
	 */
	if (params->atap_extensions != 0xffff &&
	    (params->atap_extensions & WDC_EXT_MODES)) {
		/*
		 * XXX some drives report something wrong here (they claim to
		 * support PIO mode 8 !). As mode is coded on 3 bits in
		 * SET FEATURE, limit it to 7 (so limit i to 4).
		 * If higher mode than 7 is found, abort.
		 */
		for (i = 7; i >= 0; i--) {
			if ((params->atap_piomode_supp & (1 << i)) == 0)
				continue;
			if (i > 4)
				return;

			valid_mode_found = 1;

			if ((wdc->cap & WDC_CAPABILITY_MODE) == 0) {
				drvp->PIO_cap = i + 3;
				continue;
			}

			/*
			 * See if mode is accepted.
			 * If the controller can't set its PIO mode,
			 * assume the BIOS set it up correctly
			 */
			if (ata_set_mode(drvp, 0x08 | (i + 3),
			    at_poll) != CMD_OK)
				continue;

			/*
			 * If controller's driver can't set its PIO mode,
			 * set the highest one the controller supports
			 */
			if (wdc->PIO_cap >= i + 3) {
				drvp->PIO_mode = i + 3;
				drvp->PIO_cap = i + 3;
				break;
			}
		}
		if (!valid_mode_found) {
			/*
			 * We didn't find a valid PIO mode.
			 * Assume the values returned for DMA are buggy too
			 */
			return;
		}
		drvp->drive_flags |= DRIVE_MODE;

		/* Some controllers don't support ATAPI DMA */
		if ((drvp->drive_flags & DRIVE_ATAPI) &&
		    (wdc->cap & WDC_CAPABILITY_NO_ATAPI_DMA))
			return;

		valid_mode_found = 0;
		for (i = 7; i >= 0; i--) {
			if ((params->atap_dmamode_supp & (1 << i)) == 0)
				continue;
			if ((wdc->cap & WDC_CAPABILITY_DMA) &&
			    (wdc->cap & WDC_CAPABILITY_MODE))
				if (ata_set_mode(drvp, 0x20 | i, at_poll)
				    != CMD_OK)
					continue;

			valid_mode_found = 1;

			if (wdc->cap & WDC_CAPABILITY_DMA) {
				if ((wdc->cap & WDC_CAPABILITY_MODE) &&
				    wdc->DMA_cap < i)
					continue;
				drvp->DMA_mode = i;
				drvp->DMA_cap = i;
				drvp->drive_flags |= DRIVE_DMA;
			}
			break;
		}
		if (params->atap_extensions & WDC_EXT_UDMA_MODES) {
			for (i = 7; i >= 0; i--) {
				if ((params->atap_udmamode_supp & (1 << i))
				    == 0)
					continue;
				if ((wdc->cap & WDC_CAPABILITY_MODE) &&
				    (wdc->cap & WDC_CAPABILITY_UDMA))
					if (ata_set_mode(drvp, 0x40 | i,
					    at_poll) != CMD_OK)
						continue;
				if (wdc->cap & WDC_CAPABILITY_UDMA) {
					if ((wdc->cap & WDC_CAPABILITY_MODE) &&
					    wdc->UDMA_cap < i)
						continue;
					drvp->UDMA_mode = i;
					drvp->UDMA_cap = i;
					drvp->drive_flags |= DRIVE_UDMA;
				}
				break;
			}
		}
	}

	/* Try to guess ATA version here, if it didn't get reported */
	if (drvp->ata_vers == 0) {
		if (drvp->drive_flags & DRIVE_UDMA)
			drvp->ata_vers = 4; /* should be at last ATA-4 */
		else if (drvp->PIO_cap > 2)
			drvp->ata_vers = 2; /* should be at last ATA-2 */
	}
	if (cf_flags & ATA_CONFIG_PIO_SET) {
		drvp->PIO_mode =
		    (cf_flags & ATA_CONFIG_PIO_MODES) >> ATA_CONFIG_PIO_OFF;
		drvp->drive_flags |= DRIVE_MODE;
	}
	if ((wdc->cap & WDC_CAPABILITY_DMA) == 0) {
		/* don't care about DMA modes */
		return;
	}
	if (cf_flags & ATA_CONFIG_DMA_SET) {
		if ((cf_flags & ATA_CONFIG_DMA_MODES) ==
		    ATA_CONFIG_DMA_DISABLE) {
			drvp->drive_flags &= ~DRIVE_DMA;
		} else {
			drvp->DMA_mode = (cf_flags & ATA_CONFIG_DMA_MODES) >>
			    ATA_CONFIG_DMA_OFF;
			drvp->drive_flags |= DRIVE_DMA | DRIVE_MODE;
		}
	}
	if ((wdc->cap & WDC_CAPABILITY_UDMA) == 0) {
		/* don't care about UDMA modes */
		return;
	}
	if (cf_flags & ATA_CONFIG_UDMA_SET) {
		if ((cf_flags & ATA_CONFIG_UDMA_MODES) ==
		    ATA_CONFIG_UDMA_DISABLE) {
			drvp->drive_flags &= ~DRIVE_UDMA;
		} else {
			drvp->UDMA_mode = (cf_flags & ATA_CONFIG_UDMA_MODES) >>
			    ATA_CONFIG_UDMA_OFF;
			drvp->drive_flags |= DRIVE_UDMA | DRIVE_MODE;
		}
	}
}

void
wdc_output_bytes(struct ata_drive_datas *drvp, void *bytes, unsigned int buflen)
{
	struct channel_softc *chp = drvp->chnl_softc;
	unsigned int off = 0;
	unsigned int len = buflen, roundlen;

	if (drvp->drive_flags & DRIVE_CAP32) {
		roundlen = len & ~3;

		CHP_WRITE_RAW_MULTI_4(chp,
		    (void *)((u_int8_t *)bytes + off), roundlen);

		off += roundlen;
		len -= roundlen;
	}

	if (len > 0) {
		roundlen = (len + 1) & ~0x1;

		CHP_WRITE_RAW_MULTI_2(chp,
		    (void *)((u_int8_t *)bytes + off), roundlen);
	}
}

void
wdc_input_bytes(struct ata_drive_datas *drvp, void *bytes, unsigned int buflen)
{
	struct channel_softc *chp = drvp->chnl_softc;
	unsigned int off = 0;
	unsigned int len = buflen, roundlen;

	if (drvp->drive_flags & DRIVE_CAP32) {
		roundlen = len & ~3;

		CHP_READ_RAW_MULTI_4(chp,
		    (void *)((u_int8_t *)bytes + off), roundlen);

		off += roundlen;
		len -= roundlen;
	}

	if (len > 0) {
		roundlen = (len + 1) & ~0x1;

		CHP_READ_RAW_MULTI_2(chp,
		    (void *)((u_int8_t *)bytes + off), roundlen);
	}
}

void
wdc_print_caps(struct ata_drive_datas *drvp)
{
	/* This is actually a lie until we fix the _probe_caps
	   algorithm. Don't print out lies */
#if 0
 	printf("%s: can use ", drvp->drive_name);

	if (drvp->drive_flags & DRIVE_CAP32) {
		printf("32-bit");
	} else
		printf("16-bit");

	printf(", PIO mode %d", drvp->PIO_cap);

	if (drvp->drive_flags & DRIVE_DMA) {
		printf(", DMA mode %d", drvp->DMA_cap);
	}

	if (drvp->drive_flags & DRIVE_UDMA) {
		printf(", Ultra-DMA mode %d", drvp->UDMA_cap);
	}

	printf("\n");
#endif /* 0 */
}

void
wdc_print_current_modes(struct channel_softc *chp)
{
	int drive;
	struct ata_drive_datas *drvp;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;

		printf("%s(%s:%d:%d):",
 		    drvp->drive_name,
		    chp->wdc->sc_dev.dv_xname, chp->channel, drive);

		if ((chp->wdc->cap & WDC_CAPABILITY_MODE) == 0 &&
		    !(drvp->cf_flags & ATA_CONFIG_PIO_SET))
			printf(" using BIOS timings");
		else
			printf(" using PIO mode %d", drvp->PIO_mode);
		if (drvp->drive_flags & DRIVE_DMA)
			printf(", DMA mode %d", drvp->DMA_mode);
		if (drvp->drive_flags & DRIVE_UDMA)
			printf(", Ultra-DMA mode %d", drvp->UDMA_mode);
		printf("\n");
	}
}

/*
 * downgrade the transfer mode of a drive after an error. return 1 if
 * downgrade was possible, 0 otherwise.
 */
int
wdc_downgrade_mode(struct ata_drive_datas *drvp)
{
	struct channel_softc *chp = drvp->chnl_softc;
	struct wdc_softc *wdc = chp->wdc;
	int cf_flags = drvp->cf_flags;

	/* if drive or controller don't know its mode, we can't do much */
	if ((drvp->drive_flags & DRIVE_MODE) == 0 ||
	    (wdc->cap & WDC_CAPABILITY_MODE) == 0)
		return 0;
	/* current drive mode was set by a config flag, let it this way */
	if ((cf_flags & ATA_CONFIG_PIO_SET) ||
	    (cf_flags & ATA_CONFIG_DMA_SET) ||
	    (cf_flags & ATA_CONFIG_UDMA_SET))
		return 0;

	/*
	 * We'd ideally like to use an Ultra DMA mode since they have the
	 * protection of a CRC. So we try each Ultra DMA mode and see if
	 * we can find any working combo
	 */
	if ((drvp->drive_flags & DRIVE_UDMA) && drvp->UDMA_mode > 0) {
		drvp->UDMA_mode = drvp->UDMA_mode - 1;
		printf("%s: transfer error, downgrading to Ultra-DMA mode %d\n",
		    drvp->drive_name, drvp->UDMA_mode);
	} else 	if ((drvp->drive_flags & DRIVE_UDMA) &&
	    (drvp->drive_flags & DRIVE_DMAERR) == 0) {
		/*
		 * If we were using ultra-DMA, don't downgrade to
		 * multiword DMA if we noticed a CRC error. It has
		 * been noticed that CRC errors in ultra-DMA lead to
		 * silent data corruption in multiword DMA.  Data
		 * corruption is less likely to occur in PIO mode.
		 */
		drvp->drive_flags &= ~DRIVE_UDMA;
		drvp->drive_flags |= DRIVE_DMA;
		drvp->DMA_mode = drvp->DMA_cap;
		printf("%s: transfer error, downgrading to DMA mode %d\n",
		    drvp->drive_name, drvp->DMA_mode);
	} else if (drvp->drive_flags & (DRIVE_DMA | DRIVE_UDMA)) {
		drvp->drive_flags &= ~(DRIVE_DMA | DRIVE_UDMA);
		drvp->PIO_mode = drvp->PIO_cap;
		printf("%s: transfer error, downgrading to PIO mode %d\n",
		    drvp->drive_name, drvp->PIO_mode);
	} else /* already using PIO, can't downgrade */
		return 0;

	wdc->set_modes(chp);
	/* reset the channel, which will schedule all drives for setup */
	wdc_reset_channel(drvp, 0);
	return 1;
}

int
wdc_exec_command(struct ata_drive_datas *drvp, struct wdc_command *wdc_c)
{
	struct channel_softc *chp = drvp->chnl_softc;
	struct wdc_xfer *xfer;
	int s, ret;

	WDCDEBUG_PRINT(("wdc_exec_command %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drvp->drive),
	    DEBUG_FUNCS);

	/* set up an xfer and queue. Wait for completion */
	xfer = wdc_get_xfer(wdc_c->flags & AT_WAIT ? WDC_CANSLEEP :
	    WDC_NOSLEEP);
	if (xfer == NULL) {
		return WDC_TRY_AGAIN;
	}

	if (wdc_c->flags & AT_POLL)
		xfer->c_flags |= C_POLL;
	xfer->drive = drvp->drive;
	xfer->databuf = wdc_c->data;
	xfer->c_bcount = wdc_c->bcount;
	xfer->cmd = wdc_c;
	xfer->c_start = __wdccommand_start;
	xfer->c_intr = __wdccommand_intr;
	xfer->c_kill_xfer = __wdccommand_done;

	s = splbio();
	wdc_exec_xfer(chp, xfer);
#ifdef DIAGNOSTIC
	if ((wdc_c->flags & AT_POLL) != 0 &&
	    (wdc_c->flags & AT_DONE) == 0)
		panic("wdc_exec_command: polled command not done");
#endif /* DIAGNOSTIC */
	if (wdc_c->flags & AT_DONE) {
		ret = WDC_COMPLETE;
	} else {
		if (wdc_c->flags & AT_WAIT) {
			WDCDEBUG_PRINT(("wdc_exec_command sleeping\n"),
				       DEBUG_FUNCS);

			while ((wdc_c->flags & AT_DONE) == 0) {
				tsleep_nsec(wdc_c, PRIBIO, "wdccmd", INFSLP);
			}
			ret = WDC_COMPLETE;
		} else {
			ret = WDC_QUEUED;
		}
	}
	splx(s);
	return ret;
}

void
__wdccommand_start(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	int drive = xfer->drive;
	struct wdc_command *wdc_c = xfer->cmd;

	WDCDEBUG_PRINT(("__wdccommand_start %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive),
	    DEBUG_FUNCS);

	/*
	 * Disable interrupts if we're polling
	 */
	if (xfer->c_flags & C_POLL) {
		wdc_disable_intr(chp);
	}

	wdc_set_drive(chp, drive);
	DELAY(1);

	/*
	 * For resets, we don't really care to make sure that
	 * the bus is free
	 */
	if (wdc_c->r_command != ATAPI_SOFT_RESET) {
		if (wdcwait(chp, wdc_c->r_st_bmask | WDCS_DRQ,
		    wdc_c->r_st_bmask, wdc_c->timeout) != 0) {
			goto timeout;
		}
	} else
		DELAY(10);

	wdccommand(chp, drive, wdc_c->r_command, wdc_c->r_cyl, wdc_c->r_head,
	    wdc_c->r_sector, wdc_c->r_count, wdc_c->r_features);

	if ((wdc_c->flags & AT_WRITE) == AT_WRITE) {
		/* wait at least 400ns before reading status register */
		DELAY(10);
		if (wait_for_unbusy(chp, wdc_c->timeout) != 0)
			goto timeout;

		if ((chp->ch_status & (WDCS_DRQ | WDCS_ERR)) == WDCS_ERR) {
			__wdccommand_done(chp, xfer);
			return;
		}

		if (wait_for_drq(chp, wdc_c->timeout) != 0)
			goto timeout;

		wdc_output_bytes(&chp->ch_drive[drive],
		    wdc_c->data, wdc_c->bcount);
	}

	if ((wdc_c->flags & AT_POLL) == 0) {
		chp->ch_flags |= WDCF_IRQ_WAIT; /* wait for interrupt */
		timeout_add_msec(&chp->ch_timo, wdc_c->timeout);
		return;
	}

	/*
	 * Polled command. Wait for drive ready or drq. Done in intr().
	 * Wait for at last 400ns for status bit to be valid.
	 */
	delay(10);
	__wdccommand_intr(chp, xfer, 0);
	return;

timeout:	
	wdc_c->flags |= AT_TIMEOU;
	__wdccommand_done(chp, xfer);
}

int
__wdccommand_intr(struct channel_softc *chp, struct wdc_xfer *xfer, int irq)
{
	struct ata_drive_datas *drvp = &chp->ch_drive[xfer->drive];
	struct wdc_command *wdc_c = xfer->cmd;
	int bcount = wdc_c->bcount;
	char *data = wdc_c->data;

	WDCDEBUG_PRINT(("__wdccommand_intr %s:%d:%d\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive), DEBUG_INTR);
	if (wdcwait(chp, wdc_c->r_st_pmask, wdc_c->r_st_pmask,
	    (irq == 0) ? wdc_c->timeout : 0)) {
		if (chp->dying) {
			__wdccommand_done(chp, xfer);
			return -1;
		}
		if (irq && (xfer->c_flags & C_TIMEOU) == 0)
			return 0; /* IRQ was not for us */
		wdc_c->flags |= AT_TIMEOU;
		goto out;
	}
	if (chp->wdc->cap & WDC_CAPABILITY_IRQACK)
		chp->wdc->irqack(chp);
	if (wdc_c->flags & AT_READ) {
		if ((chp->ch_status & WDCS_DRQ) == 0) {
			wdc_c->flags |= AT_TIMEOU;
			goto out;
		}
		wdc_input_bytes(drvp, data, bcount);
		/* Should we wait for device to indicate idle? */
	}
out:
	__wdccommand_done(chp, xfer);
	WDCDEBUG_PRINT(("__wdccommand_intr returned\n"), DEBUG_INTR);
	return 1;
}

void
__wdccommand_done(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	struct wdc_command *wdc_c = xfer->cmd;

	WDCDEBUG_PRINT(("__wdccommand_done %s:%d:%d %02x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, xfer->drive,
	    chp->ch_status), DEBUG_FUNCS);
	if (chp->dying)
		goto killit;
	if (chp->ch_status & WDCS_DWF)
		wdc_c->flags |= AT_DF;
	if (chp->ch_status & WDCS_ERR) {
		wdc_c->flags |= AT_ERROR;
		wdc_c->r_error = chp->ch_error;
	}
	wdc_c->flags |= AT_DONE;
	if ((wdc_c->flags & AT_READREG) != 0 &&
	    (wdc_c->flags & (AT_ERROR | AT_DF)) == 0) {
		wdc_c->r_head = CHP_READ_REG(chp, wdr_sdh);
		wdc_c->r_cyl = CHP_READ_REG(chp, wdr_cyl_hi) << 8;
		wdc_c->r_cyl |= CHP_READ_REG(chp, wdr_cyl_lo);
		wdc_c->r_sector = CHP_READ_REG(chp, wdr_sector);
		wdc_c->r_count = CHP_READ_REG(chp, wdr_seccnt);
		wdc_c->r_error = CHP_READ_REG(chp, wdr_error);
		wdc_c->r_features = wdc_c->r_error;
	}

killit:
	if (xfer->c_flags & C_POLL) {
		wdc_enable_intr(chp);
	} else
		timeout_del(&chp->ch_timo);

	wdc_free_xfer(chp, xfer);
	WDCDEBUG_PRINT(("__wdccommand_done before callback\n"), DEBUG_INTR);

	if (chp->dying)
		return;

	if (wdc_c->flags & AT_WAIT)
		wakeup(wdc_c);
	else
		if (wdc_c->callback)
			wdc_c->callback(wdc_c->callback_arg);
	wdcstart(chp);
	WDCDEBUG_PRINT(("__wdccommand_done returned\n"), DEBUG_INTR);
}

/*
 * Send a command. The drive should be ready.
 * Assumes interrupts are blocked.
 */
void
wdccommand(struct channel_softc *chp, u_int8_t drive, u_int8_t command,
    u_int16_t cylin, u_int8_t head, u_int8_t sector, u_int8_t count,
    u_int8_t features)
{
	WDCDEBUG_PRINT(("wdccommand %s:%d:%d: command=0x%x cylin=%d head=%d "
	    "sector=%d count=%d features=%d\n", chp->wdc->sc_dev.dv_xname,
	    chp->channel, drive, command, cylin, head, sector, count, features),
	    DEBUG_FUNCS);
	WDC_LOG_ATA_CMDLONG(chp, head, features, cylin, cylin >> 8, sector,
	    count, command);

	/* Select drive, head, and addressing mode. */
	CHP_WRITE_REG(chp, wdr_sdh, WDSD_IBM | (drive << 4) | head);

	/* Load parameters. */
	CHP_WRITE_REG(chp, wdr_features, features);
	CHP_WRITE_REG(chp, wdr_cyl_lo, cylin);
	CHP_WRITE_REG(chp, wdr_cyl_hi, cylin >> 8);
	CHP_WRITE_REG(chp, wdr_sector, sector);
	CHP_WRITE_REG(chp, wdr_seccnt, count);

	/* Send command. */
	CHP_WRITE_REG(chp, wdr_command, command);
}

/*
 * Send a 48-bit addressing command. The drive should be ready.
 * Assumes interrupts are blocked.
 */
void
wdccommandext(struct channel_softc *chp, u_int8_t drive, u_int8_t command,
    u_int64_t blkno, u_int16_t count)
{
	WDCDEBUG_PRINT(("wdccommandext %s:%d:%d: command=0x%x blkno=%llu "
	    "count=%d\n", chp->wdc->sc_dev.dv_xname,
	    chp->channel, drive, command, blkno, count),
	    DEBUG_FUNCS);
	WDC_LOG_ATA_CMDEXT(chp, blkno >> 40, blkno >> 16, blkno >> 32,
	    blkno >> 8, blkno >> 24, blkno, count >> 8, count, command);

	/* Select drive and LBA mode. */
	CHP_WRITE_REG(chp, wdr_sdh, (drive << 4) | WDSD_LBA);

	/* Load parameters. */
	CHP_LBA48_WRITE_REG(chp, wdr_lba_hi,
	    ((blkno >> 32) & 0xff00) | ((blkno >> 16) & 0xff));
	CHP_LBA48_WRITE_REG(chp, wdr_lba_mi,
	    ((blkno >> 24) & 0xff00) | ((blkno >> 8) & 0xff));
	CHP_LBA48_WRITE_REG(chp, wdr_lba_lo,
	    ((blkno >> 16) & 0xff00) | (blkno & 0xff));
	CHP_LBA48_WRITE_REG(chp, wdr_seccnt, count);

	/* Send command. */
	CHP_WRITE_REG(chp, wdr_command, command);
}

/*
 * Simplified version of wdccommand().  Unbusy/ready/drq must be
 * tested by the caller.
 */
void
wdccommandshort(struct channel_softc *chp, int drive, int command)
{

	WDCDEBUG_PRINT(("wdccommandshort %s:%d:%d command 0x%x\n",
	    chp->wdc->sc_dev.dv_xname, chp->channel, drive, command),
	    DEBUG_FUNCS);
	WDC_LOG_ATA_CMDSHORT(chp, command);

	/* Select drive. */
	CHP_WRITE_REG(chp, wdr_sdh, WDSD_IBM | (drive << 4));
	CHP_WRITE_REG(chp, wdr_command, command);
}

/* Add a command to the queue and start controller. Must be called at splbio */

void
wdc_exec_xfer(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	WDCDEBUG_PRINT(("wdc_exec_xfer %p flags 0x%x channel %d drive %d\n",
	    xfer, xfer->c_flags, chp->channel, xfer->drive), DEBUG_XFERS);

	/* complete xfer setup */
	xfer->chp = chp;

	/*
	 * If we are a polled command, and the list is not empty,
	 * we are doing a dump. Drop the list to allow the polled command
	 * to complete, we're going to reboot soon anyway.
	 */
	if ((xfer->c_flags & C_POLL) != 0 &&
	    !TAILQ_EMPTY(&chp->ch_queue->sc_xfer)) {
		TAILQ_INIT(&chp->ch_queue->sc_xfer);
	}
	/* insert at the end of command list */
	TAILQ_INSERT_TAIL(&chp->ch_queue->sc_xfer,xfer , c_xferchain);
	WDCDEBUG_PRINT(("wdcstart from wdc_exec_xfer, flags 0x%x\n",
	    chp->ch_flags), DEBUG_XFERS);
	wdcstart(chp);
}

void *
wdc_xfer_get(void *null)
{
	return (pool_get(&wdc_xfer_pool, PR_NOWAIT | PR_ZERO));
}

void
wdc_scrub_xfer(struct wdc_xfer *xfer)
{
	memset(xfer, 0, sizeof(*xfer));
	xfer->c_flags = C_SCSIXFER;
}

void
wdc_xfer_put(void *null, void *xxfer)
{
	struct wdc_xfer *xfer = xxfer;
	int put = 0;
	int s;

	s = splbio();
	if (ISSET(xfer->c_flags, C_SCSIXFER))
		CLR(xfer->c_flags, C_SCSIXFER);
	else
		put = 1;
	splx(s);

	if (put)
		pool_put(&wdc_xfer_pool, xfer);
}

struct wdc_xfer *
wdc_get_xfer(int flags)
{
	return (scsi_io_get(&wdc_xfer_iopool,
	    ISSET(flags, WDC_NOSLEEP) ? SCSI_NOSLEEP : 0));
}

void
wdc_free_xfer(struct channel_softc *chp, struct wdc_xfer *xfer)
{
	int put = 0;
	int s;

	if (xfer->c_flags & C_PRIVATEXFER) {
		chp->ch_flags &= ~WDCF_ACTIVE;
		TAILQ_REMOVE(&chp->ch_queue->sc_xfer, xfer, c_xferchain);
		return;
	}

	s = splbio();
	chp->ch_flags &= ~WDCF_ACTIVE;
	TAILQ_REMOVE(&chp->ch_queue->sc_xfer, xfer, c_xferchain);
	if (ISSET(xfer->c_flags, C_SCSIXFER))
		CLR(xfer->c_flags, C_SCSIXFER);
	else
		put = 1;
	splx(s);

	if (put)
		scsi_io_put(&wdc_xfer_iopool, xfer);
}


/*
 * Kill off all pending xfers for a channel_softc.
 *
 * Must be called at splbio().
 */
void
wdc_kill_pending(struct channel_softc *chp)
{
	struct wdc_xfer *xfer;

	while ((xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer)) != NULL) {
		chp = xfer->chp;
		(*xfer->c_kill_xfer)(chp, xfer);
	}
}

void
__wdcerror(struct channel_softc *chp, char *msg)
{
	struct wdc_xfer *xfer = TAILQ_FIRST(&chp->ch_queue->sc_xfer);
	if (xfer == NULL)
		printf("%s:%d: %s\n", chp->wdc->sc_dev.dv_xname, chp->channel,
		    msg);
	else
		printf("%s(%s:%d:%d): %s\n",
		    chp->ch_drive[xfer->drive].drive_name,
		    chp->wdc->sc_dev.dv_xname,
		    chp->channel, xfer->drive, msg);
}

/*
 * the bit bucket
 */
void
wdcbit_bucket(struct channel_softc *chp, int size)
{
	CHP_READ_RAW_MULTI_2(chp, NULL, size);
}


#include <sys/ataio.h>
#include <sys/fcntl.h>

int wdc_ioc_ata_cmd(struct ata_drive_datas *, atareq_t *);

int
wdc_ioc_ata_cmd(struct ata_drive_datas *drvp, atareq_t *atareq)
{
	struct wdc_command wdc_c;
	int err = 0;

	/*
	 * Make sure a timeout was supplied in the ioctl request
	 */
	if (atareq->timeout == 0)
		return (EINVAL);

	if (atareq->datalen > MAXPHYS)
		return (EINVAL);

	bzero(&wdc_c, sizeof(wdc_c));

	if (atareq->datalen > 0) {
		wdc_c.data = dma_alloc(atareq->datalen, PR_NOWAIT | PR_ZERO);
		if (wdc_c.data == NULL) {
			err = ENOMEM;
			goto err;
		}
		wdc_c.bcount = atareq->datalen;
	}

	wdc_c.flags = AT_WAIT;
	if (atareq->flags & ATACMD_READ)
		wdc_c.flags |= AT_READ;
	if (atareq->flags & ATACMD_WRITE) {
		if (atareq->datalen > 0) {
			err = copyin(atareq->databuf, wdc_c.data,
			    atareq->datalen);
			if (err != 0)
				goto err;
		}
		wdc_c.flags |= AT_WRITE;
	}
	if (atareq->flags & ATACMD_READREG)
		wdc_c.flags |= AT_READREG;

	wdc_c.timeout = atareq->timeout;
	wdc_c.r_command = atareq->command;
	wdc_c.r_head = atareq->head & 0x0f;
	wdc_c.r_cyl = atareq->cylinder;
	wdc_c.r_sector = atareq->sec_num;
	wdc_c.r_count = atareq->sec_count;
	wdc_c.r_features = atareq->features;
	if (drvp->drive_flags & DRIVE_ATAPI) {
		if (wdc_c.r_command == WDCC_IDENTIFY)
			wdc_c.r_command = ATAPI_IDENTIFY_DEVICE;
	} else {
		wdc_c.r_st_bmask = WDCS_DRDY;
		wdc_c.r_st_pmask = WDCS_DRDY;
	}

	if (wdc_exec_command(drvp, &wdc_c) != WDC_COMPLETE) {
		atareq->retsts = ATACMD_ERROR;
		goto copyout;
	}

	if (wdc_c.flags & (AT_ERROR | AT_TIMEOU | AT_DF)) {
		if (wdc_c.flags & AT_ERROR) {
			atareq->retsts = ATACMD_ERROR;
			atareq->error = wdc_c.r_error;
		} else if (wdc_c.flags & AT_DF)
			atareq->retsts = ATACMD_DF;
		else
			atareq->retsts = ATACMD_TIMEOUT;
	} else {
		atareq->retsts = ATACMD_OK;
		if (atareq->flags & ATACMD_READREG) {
			atareq->head = wdc_c.r_head;
			atareq->cylinder = wdc_c.r_cyl;
			atareq->sec_num = wdc_c.r_sector;
			atareq->sec_count = wdc_c.r_count;
			atareq->features = wdc_c.r_features;
			atareq->error = wdc_c.r_error;
		}
	}

copyout:
	if (atareq->datalen > 0 && atareq->flags & ATACMD_READ) {
		err = copyout(wdc_c.data, atareq->databuf, atareq->datalen);
		if (err != 0)
			goto err;
	}

err:
	if (wdc_c.data)
		dma_free(wdc_c.data, atareq->datalen);
	return (err);
}

int
wdc_ioctl(struct ata_drive_datas *drvp, u_long xfer, caddr_t addr, int flag,
    struct proc *p)
{
	int error = 0;

	switch (xfer) {
#ifdef WDCDEBUG
	case ATAIOGETTRACE: {
		atagettrace_t *agt = (atagettrace_t *)addr;
		unsigned int size = 0;
		char *log_to_copy;

		size = agt->buf_size;
		if (size > 65536) {
			size = 65536;
		}

		log_to_copy = wdc_get_log(&size, &agt->bytes_left);

		if (log_to_copy != NULL) {
			error = copyout(log_to_copy, agt->buf, size);
			free(log_to_copy, M_TEMP, 0);
		}

		agt->bytes_copied = size;
		break;
	}
#endif /* WDCDEBUG */

	case ATAIOCCOMMAND: {
		atareq_t *atareq = (atareq_t *)addr;

		/*
		 * Make sure this command is (relatively) safe first
		 */
		if ((flag & FWRITE) == 0 && atareq->flags & ATACMD_WRITE)
			error = EPERM;
		else
			error = wdc_ioc_ata_cmd(drvp, atareq);
		break;
	}

	default:
		error = ENOTTY;
		goto exit;
	}

exit:
	return (error);
}
