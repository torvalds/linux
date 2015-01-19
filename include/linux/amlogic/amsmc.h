/*
 * AMLOGIC Smart card driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _AMSMC_H
#define _AMSMC_H

#include <asm/types.h>

#define AMSMC_MAX_ATR_LEN    33

enum {
	AMSMC_CARDOUT = 0,
	AMSMC_CARDIN = 1
};

struct am_smc_atr {
	char    atr[AMSMC_MAX_ATR_LEN];
	int     atr_len;
};

struct am_smc_param {
	int     f;
	int     d;
	int     n;
	int     bwi;
	int     cwi;
	int     bgt;
	int     freq;
	int     recv_invert;
	int     recv_lsb_msb;
	int     recv_no_parity;
	int     xmit_invert;
	int     xmit_lsb_msb;
	int     xmit_retries;
	int     xmit_repeat_dis;
};

#define AMSMC_IOC_MAGIC  'C'

#define AMSMC_IOC_RESET        _IOR(AMSMC_IOC_MAGIC, 0x00, struct am_smc_atr)
#define AMSMC_IOC_GET_STATUS   _IOR(AMSMC_IOC_MAGIC, 0x01, int)
#define AMSMC_IOC_ACTIVE       _IO(AMSMC_IOC_MAGIC, 0x02)
#define AMSMC_IOC_DEACTIVE     _IO(AMSMC_IOC_MAGIC, 0x03)
#define AMSMC_IOC_GET_PARAM    _IOR(AMSMC_IOC_MAGIC, 0x04, struct am_smc_param)
#define AMSMC_IOC_SET_PARAM    _IOW(AMSMC_IOC_MAGIC, 0x05, struct am_smc_param)

#endif

