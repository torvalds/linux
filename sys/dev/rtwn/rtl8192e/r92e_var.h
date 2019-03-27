/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef R92E_VAR_H
#define R92E_VAR_H

#include <dev/rtwn/rtl8192e/r92e_rom_defs.h>

struct r92e_softc {
	uint8_t	chip;
	uint8_t	rs_flags;

	uint8_t	regulatory;
	uint8_t	crystalcap;

	int8_t	cck_tx_pwr[R92E_MAX_RF_PATH][R92E_GROUP_2G];
	int8_t	ht40_tx_pwr_2g[R92E_MAX_RF_PATH][R92E_GROUP_2G];

	int8_t	cck_tx_pwr_diff_2g[R92E_MAX_RF_PATH][R92E_MAX_TX_COUNT];
	int8_t	ofdm_tx_pwr_diff_2g[R92E_MAX_RF_PATH][R92E_MAX_TX_COUNT];
	int8_t	bw20_tx_pwr_diff_2g[R92E_MAX_RF_PATH][R92E_MAX_TX_COUNT];
	int8_t	bw40_tx_pwr_diff_2g[R92E_MAX_RF_PATH][R92E_MAX_TX_COUNT];

	int		ac_usb_dma_size;
	int		ac_usb_dma_time;
	uint32_t	rf_chnlbw[R92E_MAX_RF_PATH];
};

#endif	/* R92E_VAR_H */
