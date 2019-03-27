/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * Interface to the EBH-30xx specific devices
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#include <time.h>
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-cn3010-evb-hs5.h"
#include "cvmx-twsi.h"


static inline uint8_t bin2bcd(uint8_t bin)
{
    return (bin / 10) << 4 | (bin % 10);
}

static inline uint8_t bcd2bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0xf);
}

#define TM_CHECK(_expr, _msg) \
        do { \
            if (_expr) { \
                cvmx_dprintf("Warning: RTC has invalid %s field\n", (_msg)); \
                rc = -1; \
            } \
        } while(0);

static int validate_tm_struct(struct tm * tms)
{
    int rc = 0;

    if (!tms)
	return -1;

    TM_CHECK(tms->tm_sec < 0  || tms->tm_sec > 60,  "second"); /* + Leap sec */
    TM_CHECK(tms->tm_min < 0  || tms->tm_min > 59,  "minute");
    TM_CHECK(tms->tm_hour < 0 || tms->tm_hour > 23, "hour");
    TM_CHECK(tms->tm_mday < 1 || tms->tm_mday > 31, "day");
    TM_CHECK(tms->tm_wday < 0 || tms->tm_wday > 6,  "day of week");
    TM_CHECK(tms->tm_mon < 0  || tms->tm_mon > 11,  "month");
    TM_CHECK(tms->tm_year < 0 || tms->tm_year > 200,"year");

    return rc;
}

/*
 * Board-specifc RTC read
 * Time is expressed in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 * and converted internally to calendar format.
 */
uint32_t cvmx_rtc_ds1337_read(void)
{
    int       i, retry;
    uint32_t  time;
    uint8_t   reg[8];
    uint8_t   sec;
    struct tm tms;


    memset(&reg, 0, sizeof(reg));
    memset(&tms, 0, sizeof(struct tm));

    for(retry=0; retry<2; retry++)
    {
	/* Lockless read: detects the infrequent roll-over and retries */
	reg[0] = cvmx_twsi_read8(CVMX_RTC_DS1337_ADDR, 0x0);
	for(i=1; i<7; i++)
	    reg[i] = cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1337_ADDR);

	sec = cvmx_twsi_read8(CVMX_RTC_DS1337_ADDR, 0x0);
	if ((sec & 0xf) == (reg[0] & 0xf))
	    break; /* Time did not roll-over, value is correct */
    }

    tms.tm_sec  = bcd2bin(reg[0] & 0x7f);
    tms.tm_min  = bcd2bin(reg[1] & 0x7f);
    tms.tm_hour = bcd2bin(reg[2] & 0x3f);
    if ((reg[2] & 0x40) && (reg[2] & 0x20))   /* AM/PM format and is PM time */
    {
	tms.tm_hour = (tms.tm_hour + 12) % 24;
    }
    tms.tm_wday = (reg[3] & 0x7) - 1;         /* Day of week field is 0..6 */
    tms.tm_mday = bcd2bin(reg[4] & 0x3f);
    tms.tm_mon  = bcd2bin(reg[5] & 0x1f) - 1; /* Month field is 0..11 */
    tms.tm_year = ((reg[5] & 0x80) ? 100 : 0) + bcd2bin(reg[6]);


    if (validate_tm_struct(&tms))
	cvmx_dprintf("Warning: RTC calendar is not configured properly\n");

    time = mktime(&tms);

    return time;
}

/*
 * Board-specific RTC write
 * Time returned is in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 */
int cvmx_rtc_ds1337_write(uint32_t time)
{
    int       i, rc, retry;
    struct tm tms;
    uint8_t   reg[8];
    uint8_t   sec;
    time_t    time_from_epoch = time;


    localtime_r(&time_from_epoch, &tms);

    if (validate_tm_struct(&tms))
    {
	cvmx_dprintf("Error: RTC was passed wrong calendar values, write failed\n");
	goto tm_invalid;
    }

    reg[0] = bin2bcd(tms.tm_sec);
    reg[1] = bin2bcd(tms.tm_min);
    reg[2] = bin2bcd(tms.tm_hour);      /* Force 0..23 format even if using AM/PM */
    reg[3] = bin2bcd(tms.tm_wday + 1);
    reg[4] = bin2bcd(tms.tm_mday);
    reg[5] = bin2bcd(tms.tm_mon + 1);
    if (tms.tm_year >= 100)             /* Set century bit*/
    {
	reg[5] |= 0x80;
    }
    reg[6] = bin2bcd(tms.tm_year % 100);

    /* Lockless write: detects the infrequent roll-over and retries */
    for(retry=0; retry<2; retry++)
    {
	rc = 0;
	for(i=0; i<7; i++)
	{
	    rc |= cvmx_twsi_write8(CVMX_RTC_DS1337_ADDR, i, reg[i]);
	}

	sec = cvmx_twsi_read8(CVMX_RTC_DS1337_ADDR, 0x0);
	if ((sec & 0xf) == (reg[0] & 0xf))
	    break; /* Time did not roll-over, value is correct */
    }

    return (rc ? -1 : 0);

 tm_invalid:
    return -1;
}

#ifdef CVMX_RTC_DEBUG

void cvmx_rtc_ds1337_dump_state(void)
{
    int i = 0;

    printf("RTC:\n");
    printf("%d : %02X ", i, cvmx_twsi_read8(CVMX_RTC_DS1337_ADDR, 0x0));
    for(i=1; i<16; i++) {
	printf("%02X ", cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1337_ADDR));
    }
    printf("\n");
}

#endif /* CVMX_RTC_DEBUG */
