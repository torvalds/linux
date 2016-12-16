/*
 * Internal header file for QE TDM mode routines.
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors:	Zhao Qiang <qiang.zhao@nxp.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version
 */

#ifndef _QE_TDM_H_
#define _QE_TDM_H_

#include <linux/kernel.h>
#include <linux/list.h>

#include <soc/fsl/qe/immap_qe.h>
#include <soc/fsl/qe/qe.h>

#include <soc/fsl/qe/ucc.h>
#include <soc/fsl/qe/ucc_fast.h>

/* SI RAM entries */
#define SIR_LAST	0x0001
#define SIR_BYTE	0x0002
#define SIR_CNT(x)	((x) << 2)
#define SIR_CSEL(x)	((x) << 5)
#define SIR_SGS		0x0200
#define SIR_SWTR	0x4000
#define SIR_MCC		0x8000
#define SIR_IDLE	0

/* SIxMR fields */
#define SIMR_SAD(x) ((x) << 12)
#define SIMR_SDM_NORMAL	0x0000
#define SIMR_SDM_INTERNAL_LOOPBACK	0x0800
#define SIMR_SDM_MASK	0x0c00
#define SIMR_CRT	0x0040
#define SIMR_SL		0x0020
#define SIMR_CE		0x0010
#define SIMR_FE		0x0008
#define SIMR_GM		0x0004
#define SIMR_TFSD(n)	(n)
#define SIMR_RFSD(n)	((n) << 8)

enum tdm_ts_t {
	TDM_TX_TS,
	TDM_RX_TS
};

enum tdm_framer_t {
	TDM_FRAMER_T1,
	TDM_FRAMER_E1
};

enum tdm_mode_t {
	TDM_INTERNAL_LOOPBACK,
	TDM_NORMAL
};

struct si_mode_info {
	u8 simr_rfsd;
	u8 simr_tfsd;
	u8 simr_crt;
	u8 simr_sl;
	u8 simr_ce;
	u8 simr_fe;
	u8 simr_gm;
};

struct ucc_tdm_info {
	struct ucc_fast_info uf_info;
	struct si_mode_info si_info;
};

struct ucc_tdm {
	u16 tdm_port;		/* port for this tdm:TDMA,TDMB */
	u32 siram_entry_id;
	u16 __iomem *siram;
	struct si1 __iomem *si_regs;
	enum tdm_framer_t tdm_framer_type;
	enum tdm_mode_t tdm_mode;
	u8 num_of_ts;		/* the number of timeslots in this tdm frame */
	u32 tx_ts_mask;		/* tx time slot mask */
	u32 rx_ts_mask;		/* rx time slot mask */
};

int ucc_of_parse_tdm(struct device_node *np, struct ucc_tdm *utdm,
		     struct ucc_tdm_info *ut_info);
void ucc_tdm_init(struct ucc_tdm *utdm, struct ucc_tdm_info *ut_info);
#endif
