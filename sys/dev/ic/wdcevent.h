/*	$OpenBSD: wdcevent.h,v 1.8 2015/08/17 15:36:29 krw Exp $	*/
/*
 * Copyright (c) 2001 Constantine Sapuntzakis
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef WDCEVENT_H
#define WDCEVENT_H

enum wdcevent_type {
	WDCEVENT_STATUS = 1,
	WDCEVENT_ERROR,
	WDCEVENT_ATAPI_CMD,
	WDCEVENT_ATAPI_DONE,
	WDCEVENT_ATA_SHORT,
	WDCEVENT_ATA_LONG,
	WDCEVENT_SET_DRIVE1,
	WDCEVENT_SET_DRIVE0,
	WDCEVENT_REG,
	WDCEVENT_ATA_EXT
};

#ifdef _KERNEL

#ifdef WDCDEBUG
void wdc_log(struct channel_softc *chp, enum wdcevent_type type,
    unsigned int size, char  val[]);

static __inline void WDC_LOG_STATUS(struct channel_softc *chp,
    u_int8_t status) {
	if (chp->ch_prev_log_status == status)
		return;

	chp->ch_prev_log_status = status;
	wdc_log(chp, WDCEVENT_STATUS, 1, &status);
}

static __inline void WDC_LOG_ERROR(struct channel_softc *chp,
    u_int8_t error) {
	wdc_log(chp, WDCEVENT_ERROR, 1, &error);
}

static __inline void WDC_LOG_ATAPI_CMD(struct channel_softc *chp, int drive,
    int flags, int len, void *cmd) {
	u_int8_t record[20];

	record[0] = (flags >> 8);
	record[1] = flags & 0xff;
	memcpy(&record[2], cmd, len);

	wdc_log(chp, WDCEVENT_ATAPI_CMD, len + 2, record);
}

static __inline void WDC_LOG_ATAPI_DONE(struct channel_softc *chp, int drive,
    int flags, u_int8_t error) {
	char record[3] = {flags >> 8, flags & 0xff, error};
	wdc_log(chp, WDCEVENT_ATAPI_DONE, 3, record);
}

static __inline void WDC_LOG_ATA_CMDSHORT(struct channel_softc *chp, u_int8_t cmd) {
	wdc_log(chp, WDCEVENT_ATA_SHORT, 1, &cmd);
}

static __inline void WDC_LOG_ATA_CMDLONG(struct channel_softc *chp,
    u_int8_t head, u_int8_t features, u_int8_t cylinhi, u_int8_t cylinlo,
    u_int8_t sector, u_int8_t count, u_int8_t command) {
	char record[8] = { head, features, cylinhi, cylinlo,
			   sector, count, command };

	wdc_log(chp, WDCEVENT_ATA_LONG, 7, record);
}

static __inline void WDC_LOG_SET_DRIVE(struct channel_softc *chp,
    u_int8_t drive) {
	wdc_log(chp, drive ? WDCEVENT_SET_DRIVE1 : WDCEVENT_SET_DRIVE0,
	    0, NULL);
}

static __inline void WDC_LOG_REG(struct channel_softc *chp,
    enum wdc_regs reg, u_int16_t val) {
	char record[3];

	record[0] = reg;
	record[1] = (val >> 8);
	record[2] = val & 0xff;

	wdc_log(chp, WDCEVENT_REG, 3, record);
}

static __inline void WDC_LOG_ATA_CMDEXT(struct channel_softc *chp,
    u_int8_t lba_hi1, u_int8_t lba_hi2, u_int8_t lba_mi1, u_int8_t lba_mi2,
    u_int8_t lba_lo1, u_int8_t lba_lo2, u_int8_t count1, u_int8_t count2,
    u_int8_t command) {
	char record[9] = { lba_hi1, lba_hi2, lba_mi1, lba_mi2,
			   lba_lo1, lba_lo2, count1, count2, command };

	wdc_log(chp, WDCEVENT_ATA_EXT, 9, record);
}
#else
#define WDC_LOG_STATUS(chp, status)
#define WDC_LOG_ERROR(chp, error)
#define WDC_LOG_ATAPI_CMD(chp, drive, flags, len, cmd)
#define WDC_LOG_ATAPI_DONE(chp, drive, flags, error)
#define WDC_LOG_ATA_CMDSHORT(chp, cmd)
#define WDC_LOG_ATA_CMDLONG(chp, head, features, cylinhi, cylinlo, \
    sector, count, command)
#define WDC_LOG_SET_DRIVE(chp, drive)
#define WDC_LOG_REG(chp, reg, val)
#define WDC_LOG_ATA_CMDEXT(chp, lba_hi1, lba_hi2, lba_mi1, lba_mi2, \
    lba_lo1, lba_lo2, count1, count2, command)
#endif /* WDCDEBUG */

#endif	/* _KERNEL */

#endif	/* WDCEVENT_H */
