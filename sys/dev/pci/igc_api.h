/*	$OpenBSD: igc_api.h,v 1.3 2024/06/09 05:18:12 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_API_H_
#define _IGC_API_H_

#include <dev/pci/if_igc.h>
#include <dev/pci/igc_hw.h>

extern void	igc_init_function_pointers_i225(struct igc_hw *);

int		igc_set_mac_type(struct igc_hw *);
int		igc_setup_init_funcs(struct igc_hw *, bool);
int		igc_init_mac_params(struct igc_hw *);
int		igc_init_nvm_params(struct igc_hw *);
int		igc_init_phy_params(struct igc_hw *);
int		igc_check_for_link(struct igc_hw *);
int		igc_reset_hw(struct igc_hw *);
int		igc_init_hw(struct igc_hw *);
int		igc_get_speed_and_duplex(struct igc_hw *, uint16_t *,
		    uint16_t *);
int		igc_rar_set(struct igc_hw *, uint8_t *, uint32_t);
void		igc_update_mc_addr_list(struct igc_hw *, uint8_t *, uint32_t);
int		igc_check_reset_block(struct igc_hw *);
int		igc_get_phy_info(struct igc_hw *);
int		igc_phy_hw_reset(struct igc_hw *);
int		igc_read_mac_addr(struct igc_hw *);
int		igc_validate_nvm_checksum(struct igc_hw *);

#endif /* _IGC_API_H_ */
