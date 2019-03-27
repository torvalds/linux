/***********************license start***************
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2003-2008 Cavium Networks (support@cavium.com). All rights
 *  reserved.
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials provided
 *        with the distribution.
 *
 *      * Neither the name of Cavium Networks nor the names of
 *        its contributors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 *  TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 *  AND WITH ALL FAULTS AND CAVIUM NETWORKS MAKES NO PROMISES, REPRESENTATIONS
 *  OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 *  RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 *  REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 *  DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES
 *  OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR
 *  PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET
 *  POSSESSION OR CORRESPONDENCE TO DESCRIPTION.  THE ENTIRE RISK ARISING OUT
 *  OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 *
 *
 *  For any questions regarding licensing please contact marketing@caviumnetworks.com
 *
 ***********************license end**************************************/






/**
 * @file
 *
 * Interface to the EBH-30xx specific devices
 *
 * <hr>$Revision: 41586 $<hr>
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/timespec.h>
#include <sys/clock.h>
#include <sys/libkern.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-cn3010-evb-hs5.h>
#include <contrib/octeon-sdk/cvmx-twsi.h>

#define CT_CHECK(_expr, _msg) \
        do { \
            if (_expr) { \
                cvmx_dprintf("Warning: RTC has invalid %s field\n", (_msg)); \
                rc = -1; \
            } \
        } while(0);

static int validate_ct_struct(struct clocktime *ct)
{
    int rc = 0;

    if (!ct)
	return -1;

    CT_CHECK(ct->sec < 0  || ct->sec > 60,  "second"); /* + Leap sec */
    CT_CHECK(ct->min < 0  || ct->min > 59,  "minute");
    CT_CHECK(ct->hour < 0 || ct->hour > 23, "hour");
    CT_CHECK(ct->day < 1 || ct->day > 31, "day");
    CT_CHECK(ct->dow < 0 || ct->dow > 6,  "day of week");
    CT_CHECK(ct->mon < 1  || ct->mon > 12,  "month");
    CT_CHECK(ct->year > 2037,"year");

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
    uint8_t   reg[8];
    uint8_t   sec;
    struct clocktime ct;
    struct timespec ts;


    memset(&reg, 0, sizeof(reg));
    memset(&ct, 0, sizeof(ct));

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

    ct.sec  = bcd2bin(reg[0] & 0x7f);
    ct.min  = bcd2bin(reg[1] & 0x7f);
    ct.hour = bcd2bin(reg[2] & 0x3f);
    if ((reg[2] & 0x40) && (reg[2] & 0x20))   /* AM/PM format and is PM time */
    {
	ct.hour = (ct.hour + 12) % 24;
    }
    ct.dow = (reg[3] & 0x7) - 1; /* Day of week field is 0..6 */
    ct.day = bcd2bin(reg[4] & 0x3f);
    ct.mon  = bcd2bin(reg[5] & 0x1f); /* Month field is 1..12 */
#if defined(OCTEON_BOARD_CAPK_0100ND)
    /*
     * CAPK-0100ND uses DS1307 that does not have century bit
     */
    ct.year = 2000 + bcd2bin(reg[6]);
#else
    ct.year = ((reg[5] & 0x80) ? 2000 : 1900) + bcd2bin(reg[6]);
#endif

    if (validate_ct_struct(&ct))
	cvmx_dprintf("Warning: RTC calendar is not configured properly\n");

    if (clock_ct_to_ts(&ct, &ts) != 0) {
	cvmx_dprintf("Warning: RTC calendar is not configured properly\n");
        return 0;
    }

    return ts.tv_sec;
}

/*
 * Board-specific RTC write
 * Time returned is in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 */
int cvmx_rtc_ds1337_write(uint32_t time)
{
    struct clocktime ct;
    struct timespec ts;
    int       i, rc, retry;
    uint8_t   reg[8];
    uint8_t   sec;

    ts.tv_sec = time;
    ts.tv_nsec = 0;

    clock_ts_to_ct(&ts, &ct);

    if (validate_ct_struct(&ct))
    {
	cvmx_dprintf("Error: RTC was passed wrong calendar values, write failed\n");
	goto ct_invalid;
    }

    reg[0] = bin2bcd(ct.sec);
    reg[1] = bin2bcd(ct.min);
    reg[2] = bin2bcd(ct.hour);       /* Force 0..23 format even if using AM/PM */
    reg[3] = bin2bcd(ct.dow + 1);
    reg[4] = bin2bcd(ct.day);
    reg[5] = bin2bcd(ct.mon);
#if !defined(OCTEON_BOARD_CAPK_0100ND)
    if (ct.year >= 2000)             /* Set century bit*/
    {
	reg[5] |= 0x80;
    }
#endif
    reg[6] = bin2bcd(ct.year % 100);

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

 ct_invalid:
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
