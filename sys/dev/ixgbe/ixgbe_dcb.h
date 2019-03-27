/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _IXGBE_DCB_H_
#define _IXGBE_DCB_H_

#include "ixgbe_type.h"

/* DCB defines */
/* DCB credit calculation defines */
#define IXGBE_DCB_CREDIT_QUANTUM	64
#define IXGBE_DCB_MAX_CREDIT_REFILL	200   /* 200 * 64B = 12800B */
#define IXGBE_DCB_MAX_TSO_SIZE		(32 * 1024) /* Max TSO pkt size in DCB*/
#define IXGBE_DCB_MAX_CREDIT		(2 * IXGBE_DCB_MAX_CREDIT_REFILL)

/* 513 for 32KB TSO packet */
#define IXGBE_DCB_MIN_TSO_CREDIT	\
	((IXGBE_DCB_MAX_TSO_SIZE / IXGBE_DCB_CREDIT_QUANTUM) + 1)

/* DCB configuration defines */
#define IXGBE_DCB_MAX_USER_PRIORITY	8
#define IXGBE_DCB_MAX_BW_GROUP		8
#define IXGBE_DCB_BW_PERCENT		100

#define IXGBE_DCB_TX_CONFIG		0
#define IXGBE_DCB_RX_CONFIG		1

/* DCB capability defines */
#define IXGBE_DCB_PG_SUPPORT	0x00000001
#define IXGBE_DCB_PFC_SUPPORT	0x00000002
#define IXGBE_DCB_BCN_SUPPORT	0x00000004
#define IXGBE_DCB_UP2TC_SUPPORT	0x00000008
#define IXGBE_DCB_GSP_SUPPORT	0x00000010

struct ixgbe_dcb_support {
	u32 capabilities; /* DCB capabilities */

	/* Each bit represents a number of TCs configurable in the hw.
	 * If 8 traffic classes can be configured, the value is 0x80. */
	u8 traffic_classes;
	u8 pfc_traffic_classes;
};

enum ixgbe_dcb_tsa {
	ixgbe_dcb_tsa_ets = 0,
	ixgbe_dcb_tsa_group_strict_cee,
	ixgbe_dcb_tsa_strict
};

/* Traffic class bandwidth allocation per direction */
struct ixgbe_dcb_tc_path {
	u8 bwg_id; /* Bandwidth Group (BWG) ID */
	u8 bwg_percent; /* % of BWG's bandwidth */
	u8 link_percent; /* % of link bandwidth */
	u8 up_to_tc_bitmap; /* User Priority to Traffic Class mapping */
	u16 data_credits_refill; /* Credit refill amount in 64B granularity */
	u16 data_credits_max; /* Max credits for a configured packet buffer
			       * in 64B granularity.*/
	enum ixgbe_dcb_tsa tsa; /* Link or Group Strict Priority */
};

enum ixgbe_dcb_pfc {
	ixgbe_dcb_pfc_disabled = 0,
	ixgbe_dcb_pfc_enabled,
	ixgbe_dcb_pfc_enabled_txonly,
	ixgbe_dcb_pfc_enabled_rxonly
};

/* Traffic class configuration */
struct ixgbe_dcb_tc_config {
	struct ixgbe_dcb_tc_path path[2]; /* One each for Tx/Rx */
	enum ixgbe_dcb_pfc pfc; /* Class based flow control setting */

	u16 desc_credits_max; /* For Tx Descriptor arbitration */
	u8 tc; /* Traffic class (TC) */
};

enum ixgbe_dcb_pba {
	/* PBA[0-7] each use 64KB FIFO */
	ixgbe_dcb_pba_equal = PBA_STRATEGY_EQUAL,
	/* PBA[0-3] each use 80KB, PBA[4-7] each use 48KB */
	ixgbe_dcb_pba_80_48 = PBA_STRATEGY_WEIGHTED
};

struct ixgbe_dcb_num_tcs {
	u8 pg_tcs;
	u8 pfc_tcs;
};

struct ixgbe_dcb_config {
	struct ixgbe_dcb_tc_config tc_config[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	struct ixgbe_dcb_support support;
	struct ixgbe_dcb_num_tcs num_tcs;
	u8 bw_percentage[2][IXGBE_DCB_MAX_BW_GROUP]; /* One each for Tx/Rx */
	bool pfc_mode_enable;
	bool round_robin_enable;

	enum ixgbe_dcb_pba rx_pba_cfg;

	u32 dcb_cfg_version; /* Not used...OS-specific? */
	u32 link_speed; /* For bandwidth allocation validation purpose */
	bool vt_mode;
};

/* DCB driver APIs */

/* DCB rule checking */
s32 ixgbe_dcb_check_config_cee(struct ixgbe_dcb_config *);

/* DCB credits calculation */
s32 ixgbe_dcb_calculate_tc_credits(u8 *, u16 *, u16 *, int);
s32 ixgbe_dcb_calculate_tc_credits_cee(struct ixgbe_hw *,
				       struct ixgbe_dcb_config *, u32, u8);

/* DCB PFC */
s32 ixgbe_dcb_config_pfc(struct ixgbe_hw *, u8, u8 *);
s32 ixgbe_dcb_config_pfc_cee(struct ixgbe_hw *, struct ixgbe_dcb_config *);

/* DCB stats */
s32 ixgbe_dcb_config_tc_stats(struct ixgbe_hw *);
s32 ixgbe_dcb_get_tc_stats(struct ixgbe_hw *, struct ixgbe_hw_stats *, u8);
s32 ixgbe_dcb_get_pfc_stats(struct ixgbe_hw *, struct ixgbe_hw_stats *, u8);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter_cee(struct ixgbe_hw *,
					 struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_tx_data_arbiter_cee(struct ixgbe_hw *,
					 struct ixgbe_dcb_config *);
s32 ixgbe_dcb_config_rx_arbiter_cee(struct ixgbe_hw *,
				    struct ixgbe_dcb_config *);

/* DCB unpack routines */
void ixgbe_dcb_unpack_pfc_cee(struct ixgbe_dcb_config *, u8 *, u8 *);
void ixgbe_dcb_unpack_refill_cee(struct ixgbe_dcb_config *, int, u16 *);
void ixgbe_dcb_unpack_max_cee(struct ixgbe_dcb_config *, u16 *);
void ixgbe_dcb_unpack_bwgid_cee(struct ixgbe_dcb_config *, int, u8 *);
void ixgbe_dcb_unpack_tsa_cee(struct ixgbe_dcb_config *, int, u8 *);
void ixgbe_dcb_unpack_map_cee(struct ixgbe_dcb_config *, int, u8 *);
u8 ixgbe_dcb_get_tc_from_up(struct ixgbe_dcb_config *, int, u8);

/* DCB initialization */
s32 ixgbe_dcb_hw_config(struct ixgbe_hw *, u16 *, u16 *, u8 *, u8 *, u8 *);
s32 ixgbe_dcb_hw_config_cee(struct ixgbe_hw *, struct ixgbe_dcb_config *);
#endif /* _IXGBE_DCB_H_ */
