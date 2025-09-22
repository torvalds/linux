/*	$OpenBSD: igc_nvm.h,v 1.2 2024/06/09 05:18:12 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_NVM_H_
#define _IGC_NVM_H_

void	igc_init_nvm_ops_generic(struct igc_hw *);
int	igc_null_read_nvm(struct igc_hw *, uint16_t, uint16_t, uint16_t *);
void	igc_null_nvm_generic(struct igc_hw *);
int	igc_null_write_nvm(struct igc_hw *, uint16_t, uint16_t, uint16_t *);
int	igc_poll_eerd_eewr_done(struct igc_hw *, int);
int	igc_read_mac_addr_generic(struct igc_hw *);
int	igc_read_nvm_eerd(struct igc_hw *, uint16_t, uint16_t, uint16_t *);
int	igc_validate_nvm_checksum_generic(struct igc_hw *);
void	igc_reload_nvm_generic(struct igc_hw *);

#endif	/* _IGC_NVM_H_ */
