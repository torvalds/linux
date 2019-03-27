/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * Copyright (c) 2016 Landon J. Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2007 Bruce M. Simpson.
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

#ifndef _IF_BWN_PHY_N_SPROM_H_
#define _IF_BWN_PHY_N_SPROM_H_

struct bwn_mac;

#define	BWN_NPHY_NUM_CORE_PWR	4

struct bwn_phy_n_core_pwr_info {
    uint8_t itssi_2g;
    uint8_t itssi_5g;
    uint8_t maxpwr_2g;
    uint8_t maxpwr_5gl;
    uint8_t maxpwr_5g;
    uint8_t maxpwr_5gh;
    int16_t pa_2g[3];
    int16_t pa_5gl[4];
    int16_t pa_5g[4];
    int16_t pa_5gh[4];
};

int	bwn_nphy_get_core_power_info(struct bwn_mac *mac, int core,
	    struct bwn_phy_n_core_pwr_info *c);

#endif /* _IF_BWN_PHY_N_SPROM_H_ */
