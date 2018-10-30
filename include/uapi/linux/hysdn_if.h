/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* $Id: hysdn_if.h,v 1.1.8.3 2001/09/23 22:25:05 kai Exp $
 *
 * Linux driver for HYSDN cards
 * ioctl definitions shared by hynetmgr and driver.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/****************/
/* error values */
/****************/
#define ERR_NONE             0 /* no error occurred */
#define ERR_ALREADY_BOOT  1000 /* we are already booting */
#define EPOF_BAD_MAGIC    1001 /* bad magic in POF header */
#define ERR_BOARD_DPRAM   1002 /* board DPRAM failed */
#define EPOF_INTERNAL     1003 /* internal POF handler error */
#define EPOF_BAD_IMG_SIZE 1004 /* POF boot image size invalid */
#define ERR_BOOTIMG_FAIL  1005 /* 1. stage boot image did not start */
#define ERR_BOOTSEQ_FAIL  1006 /* 2. stage boot seq handshake timeout */
#define ERR_POF_TIMEOUT   1007 /* timeout waiting for card pof ready */
#define ERR_NOT_BOOTED    1008 /* operation only allowed when booted */
#define ERR_CONF_LONG     1009 /* conf line is too long */ 
#define ERR_INV_CHAN      1010 /* invalid channel number */ 
#define ERR_ASYNC_TIME    1011 /* timeout sending async data */ 




