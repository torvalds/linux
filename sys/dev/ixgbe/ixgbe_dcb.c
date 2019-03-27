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


#include "ixgbe_type.h"
#include "ixgbe_dcb.h"
#include "ixgbe_dcb_82598.h"
#include "ixgbe_dcb_82599.h"

/**
 * ixgbe_dcb_calculate_tc_credits - This calculates the ieee traffic class
 * credits from the configured bandwidth percentages. Credits
 * are the smallest unit programmable into the underlying
 * hardware. The IEEE 802.1Qaz specification do not use bandwidth
 * groups so this is much simplified from the CEE case.
 * @bw: bandwidth index by traffic class
 * @refill: refill credits index by traffic class
 * @max: max credits by traffic class
 * @max_frame_size: maximum frame size
 */
s32 ixgbe_dcb_calculate_tc_credits(u8 *bw, u16 *refill, u16 *max,
				   int max_frame_size)
{
	int min_percent = 100;
	int min_credit, multiplier;
	int i;

	min_credit = ((max_frame_size / 2) + IXGBE_DCB_CREDIT_QUANTUM - 1) /
			IXGBE_DCB_CREDIT_QUANTUM;

	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		if (bw[i] < min_percent && bw[i])
			min_percent = bw[i];
	}

	multiplier = (min_credit / min_percent) + 1;

	/* Find out the hw credits for each TC */
	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		int val = min(bw[i] * multiplier, IXGBE_DCB_MAX_CREDIT_REFILL);

		if (val < min_credit)
			val = min_credit;
		refill[i] = (u16)val;

		max[i] = bw[i] ? (bw[i]*IXGBE_DCB_MAX_CREDIT)/100 : min_credit;
	}

	return 0;
}

/**
 * ixgbe_dcb_calculate_tc_credits_cee - Calculates traffic class credits
 * @hw: pointer to hardware structure
 * @dcb_config: Struct containing DCB settings
 * @max_frame_size: Maximum frame size
 * @direction: Configuring either Tx or Rx
 *
 * This function calculates the credits allocated to each traffic class.
 * It should be called only after the rules are checked by
 * ixgbe_dcb_check_config_cee().
 */
s32 ixgbe_dcb_calculate_tc_credits_cee(struct ixgbe_hw *hw,
				   struct ixgbe_dcb_config *dcb_config,
				   u32 max_frame_size, u8 direction)
{
	struct ixgbe_dcb_tc_path *p;
	u32 min_multiplier	= 0;
	u16 min_percent		= 100;
	s32 ret_val =		IXGBE_SUCCESS;
	/* Initialization values default for Tx settings */
	u32 min_credit		= 0;
	u32 credit_refill	= 0;
	u32 credit_max		= 0;
	u16 link_percentage	= 0;
	u8  bw_percent		= 0;
	u8  i;

	if (dcb_config == NULL) {
		ret_val = IXGBE_ERR_CONFIG;
		goto out;
	}

	min_credit = ((max_frame_size / 2) + IXGBE_DCB_CREDIT_QUANTUM - 1) /
		     IXGBE_DCB_CREDIT_QUANTUM;

	/* Find smallest link percentage */
	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[direction];
		bw_percent = dcb_config->bw_percentage[direction][p->bwg_id];
		link_percentage = p->bwg_percent;

		link_percentage = (link_percentage * bw_percent) / 100;

		if (link_percentage && link_percentage < min_percent)
			min_percent = link_percentage;
	}

	/*
	 * The ratio between traffic classes will control the bandwidth
	 * percentages seen on the wire. To calculate this ratio we use
	 * a multiplier. It is required that the refill credits must be
	 * larger than the max frame size so here we find the smallest
	 * multiplier that will allow all bandwidth percentages to be
	 * greater than the max frame size.
	 */
	min_multiplier = (min_credit / min_percent) + 1;

	/* Find out the link percentage for each TC first */
	for (i = 0; i < IXGBE_DCB_MAX_TRAFFIC_CLASS; i++) {
		p = &dcb_config->tc_config[i].path[direction];
		bw_percent = dcb_config->bw_percentage[direction][p->bwg_id];

		link_percentage = p->bwg_percent;
		/* Must be careful of integer division for very small nums */
		link_percentage = (link_percentage * bw_percent) / 100;
		if (p->bwg_percent > 0 && link_percentage == 0)
			link_percentage = 1;

		/* Save link_percentage for reference */
		p->link_percent = (u8)link_percentage;

		/* Calculate credit refill ratio using multiplier */
		credit_refill = min(link_percentage * min_multiplier,
				    (u32)IXGBE_DCB_MAX_CREDIT_REFILL);

		/* Refill at least minimum credit */
		if (credit_refill < min_credit)
			credit_refill = min_credit;

		p->data_credits_refill = (u16)credit_refill;

		/* Calculate maximum credit for the TC */
		credit_max = (link_percentage * IXGBE_DCB_MAX_CREDIT) / 100;

		/*
		 * Adjustment based on rule checking, if the percentage
		 * of a TC is too small, the maximum credit may not be
		 * enough to send out a jumbo frame in data plane arbitration.
		 */
		if (credit_max < min_credit)
			credit_max = min_credit;

		if (direction == IXGBE_DCB_TX_CONFIG) {
			/*
			 * Adjustment based on rule checking, if the
			 * percentage of a TC is too small, the maximum
			 * credit may not be enough to send out a TSO
			 * packet in descriptor plane arbitration.
			 */
			if (credit_max && (credit_max <
			    IXGBE_DCB_MIN_TSO_CREDIT)
			    && (hw->mac.type == ixgbe_mac_82598EB))
				credit_max = IXGBE_DCB_MIN_TSO_CREDIT;

			dcb_config->tc_config[i].desc_credits_max =
								(u16)credit_max;
		}

		p->data_credits_max = (u16)credit_max;
	}

out:
	return ret_val;
}

/**
 * ixgbe_dcb_unpack_pfc_cee - Unpack dcb_config PFC info
 * @cfg: dcb configuration to unpack into hardware consumable fields
 * @map: user priority to traffic class map
 * @pfc_up: u8 to store user priority PFC bitmask
 *
 * This unpacks the dcb configuration PFC info which is stored per
 * traffic class into a 8bit user priority bitmask that can be
 * consumed by hardware routines. The priority to tc map must be
 * updated before calling this routine to use current up-to maps.
 */
void ixgbe_dcb_unpack_pfc_cee(struct ixgbe_dcb_config *cfg, u8 *map, u8 *pfc_up)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	int up;

	/*
	 * If the TC for this user priority has PFC enabled then set the
	 * matching bit in 'pfc_up' to reflect that PFC is enabled.
	 */
	for (*pfc_up = 0, up = 0; up < IXGBE_DCB_MAX_USER_PRIORITY; up++) {
		if (tc_config[map[up]].pfc != ixgbe_dcb_pfc_disabled)
			*pfc_up |= 1 << up;
	}
}

void ixgbe_dcb_unpack_refill_cee(struct ixgbe_dcb_config *cfg, int direction,
			     u16 *refill)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	int tc;

	for (tc = 0; tc < IXGBE_DCB_MAX_TRAFFIC_CLASS; tc++)
		refill[tc] = tc_config[tc].path[direction].data_credits_refill;
}

void ixgbe_dcb_unpack_max_cee(struct ixgbe_dcb_config *cfg, u16 *max)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	int tc;

	for (tc = 0; tc < IXGBE_DCB_MAX_TRAFFIC_CLASS; tc++)
		max[tc] = tc_config[tc].desc_credits_max;
}

void ixgbe_dcb_unpack_bwgid_cee(struct ixgbe_dcb_config *cfg, int direction,
			    u8 *bwgid)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	int tc;

	for (tc = 0; tc < IXGBE_DCB_MAX_TRAFFIC_CLASS; tc++)
		bwgid[tc] = tc_config[tc].path[direction].bwg_id;
}

void ixgbe_dcb_unpack_tsa_cee(struct ixgbe_dcb_config *cfg, int direction,
			   u8 *tsa)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	int tc;

	for (tc = 0; tc < IXGBE_DCB_MAX_TRAFFIC_CLASS; tc++)
		tsa[tc] = tc_config[tc].path[direction].tsa;
}

u8 ixgbe_dcb_get_tc_from_up(struct ixgbe_dcb_config *cfg, int direction, u8 up)
{
	struct ixgbe_dcb_tc_config *tc_config = &cfg->tc_config[0];
	u8 prio_mask = 1 << up;
	u8 tc = cfg->num_tcs.pg_tcs;

	/* If tc is 0 then DCB is likely not enabled or supported */
	if (!tc)
		goto out;

	/*
	 * Test from maximum TC to 1 and report the first match we find.  If
	 * we find no match we can assume that the TC is 0 since the TC must
	 * be set for all user priorities
	 */
	for (tc--; tc; tc--) {
		if (prio_mask & tc_config[tc].path[direction].up_to_tc_bitmap)
			break;
	}
out:
	return tc;
}

void ixgbe_dcb_unpack_map_cee(struct ixgbe_dcb_config *cfg, int direction,
			      u8 *map)
{
	u8 up;

	for (up = 0; up < IXGBE_DCB_MAX_USER_PRIORITY; up++)
		map[up] = ixgbe_dcb_get_tc_from_up(cfg, direction, up);
}

/**
 * ixgbe_dcb_config - Struct containing DCB settings.
 * @dcb_config: Pointer to DCB config structure
 *
 * This function checks DCB rules for DCB settings.
 * The following rules are checked:
 * 1. The sum of bandwidth percentages of all Bandwidth Groups must total 100%.
 * 2. The sum of bandwidth percentages of all Traffic Classes within a Bandwidth
 *    Group must total 100.
 * 3. A Traffic Class should not be set to both Link Strict Priority
 *    and Group Strict Priority.
 * 4. Link strict Bandwidth Groups can only have link strict traffic classes
 *    with zero bandwidth.
 */
s32 ixgbe_dcb_check_config_cee(struct ixgbe_dcb_config *dcb_config)
{
	struct ixgbe_dcb_tc_path *p;
	s32 ret_val = IXGBE_SUCCESS;
	u8 i, j, bw = 0, bw_id;
	u8 bw_sum[2][IXGBE_DCB_MAX_BW_GROUP];
	bool link_strict[2][IXGBE_DCB_MAX_BW_GROUP];

	memset(bw_sum, 0, sizeof(bw_sum));
	memset(link_strict, 0, sizeof(link_strict));

	/* First Tx, then Rx */
	for (i = 0; i < 2; i++) {
		/* Check each traffic class for rule violation */
		for (j = 0; j < IXGBE_DCB_MAX_TRAFFIC_CLASS; j++) {
			p = &dcb_config->tc_config[j].path[i];

			bw = p->bwg_percent;
			bw_id = p->bwg_id;

			if (bw_id >= IXGBE_DCB_MAX_BW_GROUP) {
				ret_val = IXGBE_ERR_CONFIG;
				goto err_config;
			}
			if (p->tsa == ixgbe_dcb_tsa_strict) {
				link_strict[i][bw_id] = TRUE;
				/* Link strict should have zero bandwidth */
				if (bw) {
					ret_val = IXGBE_ERR_CONFIG;
					goto err_config;
				}
			} else if (!bw) {
				/*
				 * Traffic classes without link strict
				 * should have non-zero bandwidth.
				 */
				ret_val = IXGBE_ERR_CONFIG;
				goto err_config;
			}
			bw_sum[i][bw_id] += bw;
		}

		bw = 0;

		/* Check each bandwidth group for rule violation */
		for (j = 0; j < IXGBE_DCB_MAX_BW_GROUP; j++) {
			bw += dcb_config->bw_percentage[i][j];
			/*
			 * Sum of bandwidth percentages of all traffic classes
			 * within a Bandwidth Group must total 100 except for
			 * link strict group (zero bandwidth).
			 */
			if (link_strict[i][j]) {
				if (bw_sum[i][j]) {
					/*
					 * Link strict group should have zero
					 * bandwidth.
					 */
					ret_val = IXGBE_ERR_CONFIG;
					goto err_config;
				}
			} else if (bw_sum[i][j] != IXGBE_DCB_BW_PERCENT &&
				   bw_sum[i][j] != 0) {
				ret_val = IXGBE_ERR_CONFIG;
				goto err_config;
			}
		}

		if (bw != IXGBE_DCB_BW_PERCENT) {
			ret_val = IXGBE_ERR_CONFIG;
			goto err_config;
		}
	}

err_config:
	DEBUGOUT2("DCB error code %d while checking %s settings.\n",
		  ret_val, (i == IXGBE_DCB_TX_CONFIG) ? "Tx" : "Rx");

	return ret_val;
}

/**
 * ixgbe_dcb_get_tc_stats - Returns status of each traffic class
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the status data for each of the Traffic Classes in use.
 */
s32 ixgbe_dcb_get_tc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
			   u8 tc_count)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_get_tc_stats_82598(hw, stats, tc_count);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_get_tc_stats_82599(hw, stats, tc_count);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_get_pfc_stats - Returns CBFC status of each traffic class
 * @hw: pointer to hardware structure
 * @stats: pointer to statistics structure
 * @tc_count:  Number of elements in bwg_array.
 *
 * This function returns the CBFC status data for each of the Traffic Classes.
 */
s32 ixgbe_dcb_get_pfc_stats(struct ixgbe_hw *hw, struct ixgbe_hw_stats *stats,
			    u8 tc_count)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_get_pfc_stats_82598(hw, stats, tc_count);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_get_pfc_stats_82599(hw, stats, tc_count);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_config_rx_arbiter_cee - Config Rx arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Rx Data Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_rx_arbiter_cee(struct ixgbe_hw *hw,
				struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	u8 tsa[IXGBE_DCB_MAX_TRAFFIC_CLASS]	= { 0 };
	u8 bwgid[IXGBE_DCB_MAX_TRAFFIC_CLASS]	= { 0 };
	u8 map[IXGBE_DCB_MAX_USER_PRIORITY]	= { 0 };
	u16 refill[IXGBE_DCB_MAX_TRAFFIC_CLASS]	= { 0 };
	u16 max[IXGBE_DCB_MAX_TRAFFIC_CLASS]	= { 0 };

	ixgbe_dcb_unpack_refill_cee(dcb_config, IXGBE_DCB_TX_CONFIG, refill);
	ixgbe_dcb_unpack_max_cee(dcb_config, max);
	ixgbe_dcb_unpack_bwgid_cee(dcb_config, IXGBE_DCB_TX_CONFIG, bwgid);
	ixgbe_dcb_unpack_tsa_cee(dcb_config, IXGBE_DCB_TX_CONFIG, tsa);
	ixgbe_dcb_unpack_map_cee(dcb_config, IXGBE_DCB_TX_CONFIG, map);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_rx_arbiter_82598(hw, refill, max, tsa);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_rx_arbiter_82599(hw, refill, max, bwgid,
							tsa, map);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_config_tx_desc_arbiter_cee - Config Tx Desc arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Descriptor Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_tx_desc_arbiter_cee(struct ixgbe_hw *hw,
				     struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	u8 tsa[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u8 bwgid[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u16 refill[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u16 max[IXGBE_DCB_MAX_TRAFFIC_CLASS];

	ixgbe_dcb_unpack_refill_cee(dcb_config, IXGBE_DCB_TX_CONFIG, refill);
	ixgbe_dcb_unpack_max_cee(dcb_config, max);
	ixgbe_dcb_unpack_bwgid_cee(dcb_config, IXGBE_DCB_TX_CONFIG, bwgid);
	ixgbe_dcb_unpack_tsa_cee(dcb_config, IXGBE_DCB_TX_CONFIG, tsa);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tx_desc_arbiter_82598(hw, refill, max,
							     bwgid, tsa);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_tx_desc_arbiter_82599(hw, refill, max,
							     bwgid, tsa);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_config_tx_data_arbiter_cee - Config Tx data arbiter
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Tx Data Arbiter and credits for each traffic class.
 */
s32 ixgbe_dcb_config_tx_data_arbiter_cee(struct ixgbe_hw *hw,
				     struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	u8 tsa[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u8 bwgid[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u8 map[IXGBE_DCB_MAX_USER_PRIORITY] = { 0 };
	u16 refill[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u16 max[IXGBE_DCB_MAX_TRAFFIC_CLASS];

	ixgbe_dcb_unpack_refill_cee(dcb_config, IXGBE_DCB_TX_CONFIG, refill);
	ixgbe_dcb_unpack_max_cee(dcb_config, max);
	ixgbe_dcb_unpack_bwgid_cee(dcb_config, IXGBE_DCB_TX_CONFIG, bwgid);
	ixgbe_dcb_unpack_tsa_cee(dcb_config, IXGBE_DCB_TX_CONFIG, tsa);
	ixgbe_dcb_unpack_map_cee(dcb_config, IXGBE_DCB_TX_CONFIG, map);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tx_data_arbiter_82598(hw, refill, max,
							     bwgid, tsa);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_tx_data_arbiter_82599(hw, refill, max,
							     bwgid, tsa,
							     map);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_config_pfc_cee - Config priority flow control
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure Priority Flow Control for each traffic class.
 */
s32 ixgbe_dcb_config_pfc_cee(struct ixgbe_hw *hw,
			 struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	u8 pfc_en;
	u8 map[IXGBE_DCB_MAX_USER_PRIORITY] = { 0 };

	ixgbe_dcb_unpack_map_cee(dcb_config, IXGBE_DCB_TX_CONFIG, map);
	ixgbe_dcb_unpack_pfc_cee(dcb_config, map, &pfc_en);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_pfc_82598(hw, pfc_en);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_pfc_82599(hw, pfc_en, map);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_config_tc_stats - Config traffic class statistics
 * @hw: pointer to hardware structure
 *
 * Configure queue statistics registers, all queues belonging to same traffic
 * class uses a single set of queue statistics counters.
 */
s32 ixgbe_dcb_config_tc_stats(struct ixgbe_hw *hw)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_tc_stats_82598(hw);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_tc_stats_82599(hw, NULL);
		break;
#endif
	default:
		break;
	}
	return ret;
}

/**
 * ixgbe_dcb_hw_config_cee - Config and enable DCB
 * @hw: pointer to hardware structure
 * @dcb_config: pointer to ixgbe_dcb_config structure
 *
 * Configure dcb settings and enable dcb mode.
 */
s32 ixgbe_dcb_hw_config_cee(struct ixgbe_hw *hw,
			struct ixgbe_dcb_config *dcb_config)
{
	s32 ret = IXGBE_NOT_IMPLEMENTED;
	u8 pfc_en;
	u8 tsa[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u8 bwgid[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u8 map[IXGBE_DCB_MAX_USER_PRIORITY] = { 0 };
	u16 refill[IXGBE_DCB_MAX_TRAFFIC_CLASS];
	u16 max[IXGBE_DCB_MAX_TRAFFIC_CLASS];

	/* Unpack CEE standard containers */
	ixgbe_dcb_unpack_refill_cee(dcb_config, IXGBE_DCB_TX_CONFIG, refill);
	ixgbe_dcb_unpack_max_cee(dcb_config, max);
	ixgbe_dcb_unpack_bwgid_cee(dcb_config, IXGBE_DCB_TX_CONFIG, bwgid);
	ixgbe_dcb_unpack_tsa_cee(dcb_config, IXGBE_DCB_TX_CONFIG, tsa);
	ixgbe_dcb_unpack_map_cee(dcb_config, IXGBE_DCB_TX_CONFIG, map);

	hw->mac.ops.setup_rxpba(hw, dcb_config->num_tcs.pg_tcs,
				0, dcb_config->rx_pba_cfg);

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_hw_config_82598(hw, dcb_config->link_speed,
						refill, max, bwgid, tsa);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ixgbe_dcb_config_82599(hw, dcb_config);
		ret = ixgbe_dcb_hw_config_82599(hw, dcb_config->link_speed,
						refill, max, bwgid,
						tsa, map);

		ixgbe_dcb_config_tc_stats_82599(hw, dcb_config);
		break;
#endif
	default:
		break;
	}

	if (!ret && dcb_config->pfc_mode_enable) {
		ixgbe_dcb_unpack_pfc_cee(dcb_config, map, &pfc_en);
		ret = ixgbe_dcb_config_pfc(hw, pfc_en, map);
	}

	return ret;
}

/* Helper routines to abstract HW specifics from DCB netlink ops */
s32 ixgbe_dcb_config_pfc(struct ixgbe_hw *hw, u8 pfc_en, u8 *map)
{
	int ret = IXGBE_ERR_PARAM;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ret = ixgbe_dcb_config_pfc_82598(hw, pfc_en);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ret = ixgbe_dcb_config_pfc_82599(hw, pfc_en, map);
		break;
#endif
	default:
		break;
	}
	return ret;
}

s32 ixgbe_dcb_hw_config(struct ixgbe_hw *hw, u16 *refill, u16 *max,
			    u8 *bwg_id, u8 *tsa, u8 *map)
{
	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		ixgbe_dcb_config_rx_arbiter_82598(hw, refill, max, tsa);
		ixgbe_dcb_config_tx_desc_arbiter_82598(hw, refill, max, bwg_id,
						       tsa);
		ixgbe_dcb_config_tx_data_arbiter_82598(hw, refill, max, bwg_id,
						       tsa);
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_X550EM_a:
#if !defined(NO_82599_SUPPORT) || !defined(NO_X540_SUPPORT)
		ixgbe_dcb_config_rx_arbiter_82599(hw, refill, max, bwg_id,
						  tsa, map);
		ixgbe_dcb_config_tx_desc_arbiter_82599(hw, refill, max, bwg_id,
						       tsa);
		ixgbe_dcb_config_tx_data_arbiter_82599(hw, refill, max, bwg_id,
						       tsa, map);
		break;
#endif
	default:
		break;
	}
	return 0;
}
