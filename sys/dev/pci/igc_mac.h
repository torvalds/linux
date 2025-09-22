/*	$OpenBSD: igc_mac.h,v 1.2 2024/06/09 05:18:12 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_MAC_H_
#define _IGC_MAC_H_

void	igc_init_mac_ops_generic(struct igc_hw *);
int	igc_null_ops_generic(struct igc_hw *);
int	igc_config_fc_after_link_up_generic(struct igc_hw *);
int	igc_disable_pcie_master_generic(struct igc_hw *);
int	igc_force_mac_fc_generic(struct igc_hw *);
int	igc_get_auto_rd_done_generic(struct igc_hw *);
int	igc_get_speed_and_duplex_copper_generic(struct igc_hw *, uint16_t *,
 	    uint16_t *);
void	igc_update_mc_addr_list_generic(struct igc_hw *, uint8_t *, uint32_t);
int	igc_rar_set_generic(struct igc_hw *, uint8_t *, uint32_t);
int	igc_set_fc_watermarks_generic(struct igc_hw *);
int	igc_setup_link_generic(struct igc_hw *);

int	igc_hash_mc_addr_generic(struct igc_hw *, uint8_t *);

void	igc_clear_hw_cntrs_base_generic(struct igc_hw *);
void	igc_init_rx_addrs_generic(struct igc_hw *, uint16_t);
void	igc_put_hw_semaphore_generic(struct igc_hw *);
int	igc_check_alt_mac_addr_generic(struct igc_hw *);
void	igc_write_vfta_generic(struct igc_hw *, uint32_t, uint32_t);
void	igc_config_collision_dist_generic(struct igc_hw *);

#endif	/* _IGC_MAC_H_ */
