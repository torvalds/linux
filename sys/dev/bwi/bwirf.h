/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/bwi/bwirf.h,v 1.3 2007/10/03 04:53:19 sephe Exp $
 * $FreeBSD$
 */

#ifndef _BWI_RF_H
#define _BWI_RF_H

int		bwi_rf_attach(struct bwi_mac *);
void		bwi_rf_clear_state(struct bwi_rf *);

int		bwi_rf_map_txpower(struct bwi_mac *);
void		bwi_rf_lo_adjust(struct bwi_mac *, const struct bwi_tpctl *);
void		bwi_rf_set_chan(struct bwi_mac *, u_int, int);
void		bwi_rf_get_gains(struct bwi_mac *);
void		bwi_rf_init(struct bwi_mac *);
void		bwi_rf_init_bcm2050(struct bwi_mac *);
void		bwi_rf_init_hw_nrssi_table(struct bwi_mac *, uint16_t);
void		bwi_rf_set_ant_mode(struct bwi_mac *, int);

void		bwi_rf_clear_tssi(struct bwi_mac *);
int		bwi_rf_get_latest_tssi(struct bwi_mac *, int8_t[], uint16_t);
int		bwi_rf_tssi2dbm(struct bwi_mac *, int8_t, int8_t *);

void		bwi_rf_write(struct bwi_mac *, uint16_t, uint16_t);
uint16_t	bwi_rf_read(struct bwi_mac *, uint16_t);

static __inline void
bwi_rf_off(struct bwi_mac *_mac)
{
	_mac->mac_rf.rf_off(_mac);
	/* TODO:LED */

	_mac->mac_rf.rf_flags &= ~BWI_RF_F_ON;
}

static __inline void
bwi_rf_on(struct bwi_mac *_mac)
{
	if (_mac->mac_rf.rf_flags & BWI_RF_F_ON)
		return;

	_mac->mac_rf.rf_on(_mac);
	/* TODO: LED */

	_mac->mac_rf.rf_flags |= BWI_RF_F_ON;
}

static __inline void
bwi_rf_calc_nrssi_slope(struct bwi_mac *_mac)
{
	_mac->mac_rf.rf_calc_nrssi_slope(_mac);
}

static __inline void
bwi_rf_set_nrssi_thr(struct bwi_mac *_mac)
{
	_mac->mac_rf.rf_set_nrssi_thr(_mac);
}

static __inline int
bwi_rf_calc_rssi(struct bwi_mac *_mac, const struct bwi_rxbuf_hdr *_hdr)
{
	return _mac->mac_rf.rf_calc_rssi(_mac, _hdr);
}

static __inline int
bwi_rf_calc_noise(struct bwi_mac *_mac)
{
	return _mac->mac_rf.rf_calc_noise(_mac);
}

static __inline void
bwi_rf_lo_update(struct bwi_mac *_mac)
{
	return _mac->mac_rf.rf_lo_update(_mac);
}

#define RF_WRITE(mac, ofs, val)		bwi_rf_write((mac), (ofs), (val))
#define RF_READ(mac, ofs)		bwi_rf_read((mac), (ofs))

#define RF_SETBITS(mac, ofs, bits)		\
	RF_WRITE((mac), (ofs), RF_READ((mac), (ofs)) | (bits))
#define RF_CLRBITS(mac, ofs, bits)		\
	RF_WRITE((mac), (ofs), RF_READ((mac), (ofs)) & ~(bits))
#define RF_FILT_SETBITS(mac, ofs, filt, bits)	\
	RF_WRITE((mac), (ofs), (RF_READ((mac), (ofs)) & (filt)) | (bits))

#define BWI_RFR_ATTEN			0x43

#define BWI_RFR_TXPWR			0x52
#define BWI_RFR_TXPWR1_MASK		__BITS(6, 4)

#define BWI_RFR_BBP_ATTEN		0x60
#define BWI_RFR_BBP_ATTEN_CALIB_BIT	__BIT(0)
#define BWI_RFR_BBP_ATTEN_CALIB_IDX	__BITS(4, 1)

/*
 * TSSI -- TX power maps
 */
/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * B PHY
 */
#define BWI_TXPOWER_MAP_11B \
	0x4d,	0x4c,	0x4b,	0x4a,	0x4a,	0x49,	0x48,	0x47,	\
	0x47,	0x46,	0x45,	0x45,	0x44,	0x43,	0x42,	0x42,	\
	0x41,	0x40,	0x3f,	0x3e,	0x3d,	0x3c,	0x3b,	0x3a,	\
	0x39,	0x38,	0x37,	0x36,	0x35,	0x34,	0x32,	0x31,	\
	0x30,	0x2f,	0x2d,	0x2c,	0x2b,	0x29,	0x28,	0x26,	\
	0x25,	0x23,	0x21,	0x1f,	0x1d,	0x1a,	0x17,	0x14,	\
	0x10,	0x0c,	0x06,	0x00,	-7,	-7,	-7,	-7, 	\
	-7,	-7,	-7,	-7,	-7,	-7,	-7,	-7
/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * G PHY
 */
#define BWI_TXPOWER_MAP_11G \
	77,	77,	77,	76,	76,	76,	75,	75,	\
	74,	74,	73,	73,	73,	72,	72,	71,	\
	71,	70,	70,	69,	68,	68,	67,	67,	\
	66,	65,	65,	64,	63,	63,	62,	61,	\
	60,	59,	58,	57,	56,	55,	54,	53,	\
	52,	50,	49,	47,	45,	43,	40,	37,	\
	33,	28,	22,	14,	5,	-7,	-20,	-20,	\
	-20,	-20,	-20,	-20,	-20,	-20,	-20,	-20

#endif	/* !_BWI_RF_H */
