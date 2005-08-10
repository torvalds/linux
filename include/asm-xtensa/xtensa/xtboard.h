#ifndef _xtboard_h_included_
#define _xtboard_h_included_

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 *  xtboard.h  --  Routines for getting useful information from the board.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


#include <xtensa/xt2000.h>

#define	XTBOARD_RTC_ERROR	-1
#define	XTBOARD_RTC_STOPPED	-2


/*  xt2000-i2cdev.c:  */
typedef void XtboardDelayFunc( unsigned );
extern XtboardDelayFunc* xtboard_set_nsdelay_func( XtboardDelayFunc *delay_fn );
extern int xtboard_i2c_read (unsigned id, unsigned char *buf, unsigned addr, unsigned size);
extern int xtboard_i2c_write(unsigned id, unsigned char *buf, unsigned addr, unsigned size);
extern int xtboard_i2c_wait_nvram_ack(unsigned id, unsigned swtimer);

/*  xtboard.c:  */
extern int xtboard_nvram_read (unsigned addr, unsigned len, unsigned char *buf);
extern int xtboard_nvram_write(unsigned addr, unsigned len, unsigned char *buf);
extern int xtboard_nvram_binfo_read (xt2000_nvram_binfo *buf);
extern int xtboard_nvram_binfo_write(xt2000_nvram_binfo *buf);
extern int xtboard_nvram_binfo_valid(xt2000_nvram_binfo *buf);
extern int xtboard_ethermac_get(unsigned char *buf);
extern int xtboard_ethermac_set(unsigned char *buf);

/*+*----------------------------------------------------------------------------
/ Function: xtboard_get_rtc_time
/
/ Description:  Get time stored in real-time clock.
/
/ Returns: time in seconds stored in real-time clock.
/-**----------------------------------------------------------------------------*/

extern unsigned xtboard_get_rtc_time(void);

/*+*----------------------------------------------------------------------------
/ Function: xtboard_set_rtc_time
/
/ Description:  Set time stored in real-time clock.
/
/ Parameters: 	time -- time in seconds to store to real-time clock
/
/ Returns: 0 on success, xtboard_i2c_write() error code otherwise.
/-**----------------------------------------------------------------------------*/

extern int xtboard_set_rtc_time(unsigned time);


/*  xtfreq.c:  */
/*+*----------------------------------------------------------------------------
/ Function: xtboard_measure_sys_clk
/
/ Description:  Get frequency of system clock.
/
/ Parameters:	none
/
/ Returns: frequency of system clock.
/-**----------------------------------------------------------------------------*/

extern unsigned xtboard_measure_sys_clk(void);


#if 0	/* old stuff from xtboard.c: */

/*+*----------------------------------------------------------------------------
/ Function: xtboard_nvram valid
/
/ Description:  Determines if data in NVRAM is valid.
/
/ Parameters:	delay -- 10us delay function
/
/ Returns: 1 if NVRAM is valid, 0 otherwise
/-**----------------------------------------------------------------------------*/

extern unsigned xtboard_nvram_valid(void (*delay)( void ));

/*+*----------------------------------------------------------------------------
/ Function: xtboard_get_nvram_contents
/
/ Description:  Returns contents of NVRAM.
/
/ Parameters: 	buf -- buffer to NVRAM contents.
/		delay -- 10us delay function
/
/ Returns: 1 if NVRAM is valid, 0 otherwise
/-**----------------------------------------------------------------------------*/

extern unsigned xtboard_get_nvram_contents(unsigned char *buf, void (*delay)( void ));

/*+*----------------------------------------------------------------------------
/ Function: xtboard_get_ether_addr
/
/ Description:  Returns ethernet address of board.
/
/ Parameters: 	buf -- buffer to store ethernet address
/		delay -- 10us delay function
/
/ Returns: nothing.
/-**----------------------------------------------------------------------------*/

extern void xtboard_get_ether_addr(unsigned char *buf, void (*delay)( void ));

#endif /*0*/


#endif /*_xtboard_h_included_*/

