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
 * Interface to the Thunder specific devices
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-thunder.h"
#include "cvmx-gpio.h"
#include "cvmx-twsi.h"


static const int BYPASS_STATUS = 1<<5; /* GPIO 5 */
static const int BYPASS_EN     = 1<<6; /* GPIO 6 */
static const int WDT_BP_CLR    = 1<<7; /* GPIO 7 */

static const int RTC_CTL_ADDR = 0x7;
static const int RTC_CTL_BIT_EOSC   = 0x80;
static const int RTC_CTL_BIT_WACE   = 0x40;
static const int RTC_CTL_BIT_WD_ALM = 0x20;
static const int RTC_CTL_BIT_WDSTR  = 0x8;
static const int RTC_CTL_BIT_AIE    = 0x1;
static const int RTC_WD_ALM_CNT_BYTE0_ADDR = 0x4;

#define CVMX_LAN_BYPASS_MSG(...)  do {} while(0)

/*
 * Board-specifc RTC read
 * Time is expressed in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 */
uint32_t cvmx_rtc_ds1374_read(void)
{
    int      retry;
    uint8_t  sec;
    uint32_t time;

    for(retry=0; retry<2; retry++)
    {
        time = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR, 0x0);
        time |= (cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1374_ADDR) & 0xff) << 8;
        time |= (cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1374_ADDR) & 0xff) << 16;
        time |= (cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1374_ADDR) & 0xff) << 24;

        sec = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR, 0x0);
        if (sec == (time & 0xff))
            break; /* Time did not roll-over, value is correct */
    }

    return time;
}

/*
 * Board-specific RTC write
 * Time is expressed in seconds from epoch (Jan 1 1970 at 00:00:00 UTC)
 */
int cvmx_rtc_ds1374_write(uint32_t time)
{
    int      rc;
    int      retry;
    uint8_t  sec;

    for(retry=0; retry<2; retry++)
    {
        rc  = cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR, 0x0, time & 0xff);
        rc |= cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR, 0x1, (time >> 8) & 0xff);
        rc |= cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR, 0x2, (time >> 16) & 0xff);
        rc |= cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR, 0x3, (time >> 24) & 0xff);
        sec = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR, 0x0);
        if (sec == (time & 0xff))
            break; /* Time did not roll-over, value is correct */
    }

    return (rc ? -1 : 0);
}

static int cvmx_rtc_ds1374_alarm_config(int WD, int WDSTR, int AIE)
{
    int val;

    val = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR);
    val = val & ~RTC_CTL_BIT_EOSC; /* Make sure that oscillator is running */
    WD?(val = val | RTC_CTL_BIT_WD_ALM):(val = val & ~RTC_CTL_BIT_WD_ALM);
    WDSTR?(val = val | RTC_CTL_BIT_WDSTR):(val = val & ~RTC_CTL_BIT_WDSTR);
    AIE?(val = val | RTC_CTL_BIT_AIE):(val = val & ~RTC_CTL_BIT_AIE);
    cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR, val);
    return 0;
}

static int cvmx_rtc_ds1374_alarm_set(int alarm_on)
{
    uint8_t val;

    if (alarm_on)
    {
        val = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR);
        cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR, val | RTC_CTL_BIT_WACE);
    }
    else
    {
        val = cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR);
        cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR,RTC_CTL_ADDR, val & ~RTC_CTL_BIT_WACE);
    }
    return 0;
}


static int cvmx_rtc_ds1374_alarm_counter_set(uint32_t interval)
{
    int i;
    int rc = 0;

    for(i=0;i<3;i++)
    {
        rc |= cvmx_twsi_write8(CVMX_RTC_DS1374_ADDR, RTC_WD_ALM_CNT_BYTE0_ADDR+i, interval & 0xFF);
        interval >>= 8;
    }
    return rc;
}

#if 0 /* XXX unused */
static uint32_t cvmx_rtc_ds1374_alarm_counter_get(void)
{
    int i;
    uint32_t interval = 0;

    for(i=0;i<3;i++)
    {
        interval |= ( cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR,RTC_WD_ALM_CNT_BYTE0_ADDR+i) & 0xff) << (i*8);
    }
    return interval;
}
#endif


#ifdef CVMX_RTC_DEBUG

void cvmx_rtc_ds1374_dump_state(void)
{
    int i = 0;

    cvmx_dprintf("RTC:\n");
    cvmx_dprintf("%d : %02X ", i, cvmx_twsi_read8(CVMX_RTC_DS1374_ADDR, 0x0));
    for(i=1; i<10; i++)
    {
        cvmx_dprintf("%02X ", cvmx_twsi_read8_cur_addr(CVMX_RTC_DS1374_ADDR));
    }
    cvmx_dprintf("\n");
}

#endif /* CVMX_RTC_DEBUG */


/*
 *  LAN bypass functionality
 */

/* Private initialization function */
static int cvmx_lan_bypass_init(void)
{
    const int CLR_PULSE = 100;  /* Longer than 100 ns (on CPUs up to 1 GHz) */

    //Clear GPIO 6
    cvmx_gpio_clear(BYPASS_EN);

    //Disable WDT
    cvmx_rtc_ds1374_alarm_set(0);

    //GPIO(7) Send a low pulse
    cvmx_gpio_clear(WDT_BP_CLR);
    cvmx_wait(CLR_PULSE);
    cvmx_gpio_set(WDT_BP_CLR);
    return 0;
}

/**
 * Set LAN bypass mode.
 *
 * Supported modes are:
 * - CVMX_LAN_BYPASS_OFF
 *     <br>LAN ports are connected ( port 0 <--> Octeon <--> port 1 )
 *
 * - CVMX_LAN_BYPASS_GPIO
 *     <br>LAN bypass is controlled by software using cvmx_lan_bypass_force() function.
 *     When transitioning to this mode, default is LAN bypass enabled
 *     ( port 0 <--> port 1, -- Octeon ).
 *
 * - CVMX_LAN_BYPASS_WATCHDOG
 *     <br>LAN bypass is inactive as long as a watchdog is kept alive.
 *     The default expiration time is 1 second and the function to
 *     call periodically to prevent watchdog expiration is
 *     cvmx_lan_bypass_keep_alive().
 *
 * @param mode           LAN bypass mode
 *
 * @return Error code, or 0 in case of success
 */
int cvmx_lan_bypass_mode_set(cvmx_lan_bypass_mode_t mode)
{
    switch(mode)
    {
    case CVMX_LAN_BYPASS_GPIO:
        /* make lan bypass enable */
        cvmx_lan_bypass_init();
        cvmx_gpio_set(BYPASS_EN);
        CVMX_LAN_BYPASS_MSG("Enable LAN bypass by GPIO. \n");
        break;

    case CVMX_LAN_BYPASS_WATCHDOG:
        /* make lan bypass enable */
        cvmx_lan_bypass_init();
        /* Set WDT parameters and turn it on */
        cvmx_rtc_ds1374_alarm_counter_set(0x1000);    /* 4096 ticks = 1 sec */
        cvmx_rtc_ds1374_alarm_config(1,1,1);
        cvmx_rtc_ds1374_alarm_set(1);
        CVMX_LAN_BYPASS_MSG("Enable LAN bypass by WDT. \n");
        break;

    case CVMX_LAN_BYPASS_OFF:
        /* make lan bypass disable */
        cvmx_lan_bypass_init();
        CVMX_LAN_BYPASS_MSG("Disable LAN bypass. \n");
        break;

    default:
        CVMX_LAN_BYPASS_MSG("%s: LAN bypass mode %d not supported\n", __FUNCTION__, mode);
        break;
    }
    return 0;
}

/**
 * Refresh watchdog timer.
 *
 * Call periodically (less than 1 second) to prevent triggering LAN bypass.
 * The alternative cvmx_lan_bypass_keep_alive_ms() is provided for cases
 * where a variable interval is required.
 */
void cvmx_lan_bypass_keep_alive(void)
{
    cvmx_rtc_ds1374_alarm_counter_set(0x1000);    /* 4096 ticks = 1 second */
}

/**
 * Refresh watchdog timer, setting a specific expiration interval.
 *
 * @param interval_ms     Interval, in milliseconds, to next watchdog expiration.
 */
void cvmx_lan_bypass_keep_alive_ms(uint32_t interval_ms)
{
    cvmx_rtc_ds1374_alarm_counter_set((interval_ms * 0x1000) / 1000);
}

/**
 * Control LAN bypass via software.
 *
 * @param force_bypass   Force LAN bypass to active (1) or inactive (0)
 *
 * @return Error code, or 0 in case of success
 */
int cvmx_lan_bypass_force(int force_bypass)
{
    if (force_bypass)
    {
        //Set GPIO 6
        cvmx_gpio_set(BYPASS_EN);
    }
    else
    {
        cvmx_lan_bypass_init();
    }
    return 0;
}

/**
 * Return status of LAN bypass circuit.
 *
 * @return 1 if ports are in LAN bypass, or 0 if normally connected
 */
int cvmx_lan_bypass_is_active(void)
{
    return !!(cvmx_gpio_read() & BYPASS_STATUS);
}
