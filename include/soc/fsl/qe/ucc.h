/*
 * Copyright (C) 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * Description:
 * Internal header file for UCC unit routines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __UCC_H__
#define __UCC_H__

#include <soc/fsl/qe/immap_qe.h>
#include <soc/fsl/qe/qe.h>

#define STATISTICS

#define UCC_MAX_NUM	8

/* Slow or fast type for UCCs.
*/
enum ucc_speed_type {
	UCC_SPEED_TYPE_FAST = UCC_GUEMR_MODE_FAST_RX | UCC_GUEMR_MODE_FAST_TX,
	UCC_SPEED_TYPE_SLOW = UCC_GUEMR_MODE_SLOW_RX | UCC_GUEMR_MODE_SLOW_TX
};

/* ucc_set_type
 * Sets UCC to slow or fast mode.
 *
 * ucc_num - (In) number of UCC (0-7).
 * speed   - (In) slow or fast mode for UCC.
 */
int ucc_set_type(unsigned int ucc_num, enum ucc_speed_type speed);

int ucc_set_qe_mux_mii_mng(unsigned int ucc_num);

int ucc_set_qe_mux_rxtx(unsigned int ucc_num, enum qe_clock clock,
	enum comm_dir mode);

int ucc_mux_set_grant_tsa_bkpt(unsigned int ucc_num, int set, u32 mask);

/* QE MUX clock routing for UCC
*/
static inline int ucc_set_qe_mux_grant(unsigned int ucc_num, int set)
{
	return ucc_mux_set_grant_tsa_bkpt(ucc_num, set, QE_CMXUCR_GRANT);
}

static inline int ucc_set_qe_mux_tsa(unsigned int ucc_num, int set)
{
	return ucc_mux_set_grant_tsa_bkpt(ucc_num, set, QE_CMXUCR_TSA);
}

static inline int ucc_set_qe_mux_bkpt(unsigned int ucc_num, int set)
{
	return ucc_mux_set_grant_tsa_bkpt(ucc_num, set, QE_CMXUCR_BKPT);
}

#endif				/* __UCC_H__ */
