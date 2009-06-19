/*
 * Copyright (C) 2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * vpss - video processing subsystem module header file.
 *
 * Include this header file if a driver needs to configure vpss system
 * module. It exports a set of library functions  for video drivers to
 * configure vpss system module functions such as clock enable/disable,
 * vpss interrupt mux to arm, and other common vpss system module
 * functions.
 */
#ifndef _VPSS_H
#define _VPSS_H

/* selector for ccdc input selection on DM355 */
enum vpss_ccdc_source_sel {
	VPSS_CCDCIN,
	VPSS_HSSIIN
};

/* Used for enable/diable VPSS Clock */
enum vpss_clock_sel {
	/* DM355/DM365 */
	VPSS_CCDC_CLOCK,
	VPSS_IPIPE_CLOCK,
	VPSS_H3A_CLOCK,
	VPSS_CFALD_CLOCK,
	/*
	 * When using VPSS_VENC_CLOCK_SEL in vpss_enable_clock() api
	 * following applies:-
	 * en = 0 selects ENC_CLK
	 * en = 1 selects ENC_CLK/2
	 */
	VPSS_VENC_CLOCK_SEL,
	VPSS_VPBE_CLOCK,
};

/* select input to ccdc on dm355 */
int vpss_select_ccdc_source(enum vpss_ccdc_source_sel src_sel);
/* enable/disable a vpss clock, 0 - success, -1 - failure */
int vpss_enable_clock(enum vpss_clock_sel clock_sel, int en);

/* wbl reset for dm644x */
enum vpss_wbl_sel {
	VPSS_PCR_AEW_WBL_0 = 16,
	VPSS_PCR_AF_WBL_0,
	VPSS_PCR_RSZ4_WBL_0,
	VPSS_PCR_RSZ3_WBL_0,
	VPSS_PCR_RSZ2_WBL_0,
	VPSS_PCR_RSZ1_WBL_0,
	VPSS_PCR_PREV_WBL_0,
	VPSS_PCR_CCDC_WBL_O,
};
int vpss_clear_wbl_overflow(enum vpss_wbl_sel wbl_sel);
#endif
