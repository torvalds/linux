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
	VPSS_HSSIIN,
	VPSS_PGLPBK,	/* for DM365 only */
	VPSS_CCDCPG	/* for DM365 only */
};

struct vpss_sync_pol {
	unsigned int ccdpg_hdpol:1;
	unsigned int ccdpg_vdpol:1;
};

struct vpss_pg_frame_size {
	short hlpfr;
	short pplen;
};

/* Used for enable/disable VPSS Clock */
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
	/* DM365 only clocks */
	VPSS_IPIPEIF_CLOCK,
	VPSS_RSZ_CLOCK,
	VPSS_BL_CLOCK,
	/*
	 * When using VPSS_PCLK_INTERNAL in vpss_enable_clock() api
	 * following applies:-
	 * en = 0 disable internal PCLK
	 * en = 1 enables internal PCLK
	 */
	VPSS_PCLK_INTERNAL,
	/*
	 * When using VPSS_PSYNC_CLOCK_SEL in vpss_enable_clock() api
	 * following applies:-
	 * en = 0 enables MMR clock
	 * en = 1 enables VPSS clock
	 */
	VPSS_PSYNC_CLOCK_SEL,
	VPSS_LDC_CLOCK_SEL,
	VPSS_OSD_CLOCK_SEL,
	VPSS_FDIF_CLOCK,
	VPSS_LDC_CLOCK
};

/* select input to ccdc on dm355 */
int vpss_select_ccdc_source(enum vpss_ccdc_source_sel src_sel);
/* enable/disable a vpss clock, 0 - success, -1 - failure */
int vpss_enable_clock(enum vpss_clock_sel clock_sel, int en);
/* set sync polarity, only for DM365*/
void dm365_vpss_set_sync_pol(struct vpss_sync_pol);
/* set the PG_FRAME_SIZE register, only for DM365 */
void dm365_vpss_set_pg_frame_size(struct vpss_pg_frame_size);

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
/* clear wbl overflow flag for DM6446 */
int vpss_clear_wbl_overflow(enum vpss_wbl_sel wbl_sel);

/* set sync polarity*/
void vpss_set_sync_pol(struct vpss_sync_pol sync);
/* set the PG_FRAME_SIZE register */
void vpss_set_pg_frame_size(struct vpss_pg_frame_size frame_size);
/*
 * vpss_check_and_clear_interrupt - check and clear interrupt
 * @irq - common enumerator for IRQ
 *
 * Following return values used:-
 * 0 - interrupt occurred and cleared
 * 1 - interrupt not occurred
 * 2 - interrupt status not available
 */
int vpss_dma_complete_interrupt(void);

#endif
