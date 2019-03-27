/*

  Broadcom B43 wireless driver
  IEEE 802.11n PHY data tables

  Copyright (c) 2008 Michael Buesch <m@bues.ch>
  Copyright (c) 2010 Rafał Miłecki <zajec5@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

/*
 * $FreeBSD$
 */

#ifndef	__IF_BWN_TABLES_NPHY_H__
#define	__IF_BWN_TABLES_NPHY_H__

struct bwn_phy_n_sfo_cfg {
	uint16_t phy_bw1a;
	uint16_t phy_bw2;
	uint16_t phy_bw3;
	uint16_t phy_bw4;
	uint16_t phy_bw5;
	uint16_t phy_bw6;
};

struct bwn_mac;

struct bwn_nphy_txiqcal_ladder {
	uint8_t percent;
	uint8_t g_env;
};

struct bwn_nphy_rf_control_override_rev2 {
	uint8_t addr0;
	uint8_t addr1;
	uint16_t bmask;
	uint8_t shift;
};

struct bwn_nphy_rf_control_override_rev3 {
	uint16_t val_mask;
	uint8_t val_shift;
	uint8_t en_addr0;
	uint8_t val_addr0;
	uint8_t en_addr1;
	uint8_t val_addr1;
};

struct bwn_nphy_rf_control_override_rev7 {
	uint16_t field;
	uint16_t val_addr_core0;
	uint16_t val_addr_core1;
	uint16_t val_mask;
	uint8_t val_shift;
};

struct bwn_nphy_gain_ctl_workaround_entry {
	int8_t lna1_gain[4];
	int8_t lna2_gain[4];
	uint8_t gain_db[10];
	uint8_t gain_bits[10];

	uint16_t init_gain;
	uint16_t rfseq_init[4];

	uint16_t cliphi_gain;
	uint16_t clipmd_gain;
	uint16_t cliplo_gain;

	uint16_t crsmin;
	uint16_t crsminl;
	uint16_t crsminu;

	uint16_t nbclip;
	uint16_t wlclip;
};

/* Get entry with workaround values for gain ctl. Does not return NULL. */
struct bwn_nphy_gain_ctl_workaround_entry *bwn_nphy_get_gain_ctl_workaround_ent(
	struct bwn_mac *mac, bool ghz5, bool ext_lna);


/* The N-PHY tables. */
#define BWN_NTAB_TYPEMASK		0xF0000000
#define BWN_NTAB_8BIT			0x10000000
#define BWN_NTAB_16BIT			0x20000000
#define BWN_NTAB_32BIT			0x30000000
#define BWN_NTAB8(table, offset)	(((table) << 10) | (offset) | BWN_NTAB_8BIT)
#define BWN_NTAB16(table, offset)	(((table) << 10) | (offset) | BWN_NTAB_16BIT)
#define BWN_NTAB32(table, offset)	(((table) << 10) | (offset) | BWN_NTAB_32BIT)

/* Static N-PHY tables */
#define BWN_NTAB_FRAMESTRUCT		BWN_NTAB32(0x0A, 0x000) /* Frame Struct Table */
#define BWN_NTAB_FRAMESTRUCT_SIZE	832
#define BWN_NTAB_FRAMELT		BWN_NTAB8 (0x18, 0x000) /* Frame Lookup Table */
#define BWN_NTAB_FRAMELT_SIZE		32
#define BWN_NTAB_TMAP			BWN_NTAB32(0x0C, 0x000) /* T Map Table */
#define BWN_NTAB_TMAP_SIZE		448
#define BWN_NTAB_TDTRN			BWN_NTAB32(0x0E, 0x000) /* TDTRN Table */
#define BWN_NTAB_TDTRN_SIZE		704
#define BWN_NTAB_INTLEVEL		BWN_NTAB32(0x0D, 0x000) /* Int Level Table */
#define BWN_NTAB_INTLEVEL_SIZE		7
#define BWN_NTAB_PILOT			BWN_NTAB16(0x0B, 0x000) /* Pilot Table */
#define BWN_NTAB_PILOT_SIZE		88
#define BWN_NTAB_PILOTLT		BWN_NTAB32(0x14, 0x000) /* Pilot Lookup Table */
#define BWN_NTAB_PILOTLT_SIZE		6
#define BWN_NTAB_TDI20A0		BWN_NTAB32(0x13, 0x080) /* TDI Table 20 Antenna 0 */
#define BWN_NTAB_TDI20A0_SIZE		55
#define BWN_NTAB_TDI20A1		BWN_NTAB32(0x13, 0x100) /* TDI Table 20 Antenna 1 */
#define BWN_NTAB_TDI20A1_SIZE		55
#define BWN_NTAB_TDI40A0		BWN_NTAB32(0x13, 0x280) /* TDI Table 40 Antenna 0 */
#define BWN_NTAB_TDI40A0_SIZE		110
#define BWN_NTAB_TDI40A1		BWN_NTAB32(0x13, 0x300) /* TDI Table 40 Antenna 1 */
#define BWN_NTAB_TDI40A1_SIZE		110
#define BWN_NTAB_BDI			BWN_NTAB16(0x15, 0x000) /* BDI Table */
#define BWN_NTAB_BDI_SIZE		6
#define BWN_NTAB_CHANEST		BWN_NTAB32(0x16, 0x000) /* Channel Estimate Table */
#define BWN_NTAB_CHANEST_SIZE		96
#define BWN_NTAB_MCS			BWN_NTAB8 (0x12, 0x000) /* MCS Table */
#define BWN_NTAB_MCS_SIZE		128

/* Volatile N-PHY tables */
#define BWN_NTAB_NOISEVAR10		BWN_NTAB32(0x10, 0x000) /* Noise Var Table 10 */
#define BWN_NTAB_NOISEVAR10_SIZE	256
#define BWN_NTAB_NOISEVAR11		BWN_NTAB32(0x10, 0x080) /* Noise Var Table 11 */
#define BWN_NTAB_NOISEVAR11_SIZE	256
#define BWN_NTAB_C0_ESTPLT		BWN_NTAB8 (0x1A, 0x000) /* Estimate Power Lookup Table Core 0 */
#define BWN_NTAB_C0_ESTPLT_SIZE		64
#define BWN_NTAB_C0_ADJPLT		BWN_NTAB8 (0x1A, 0x040) /* Adjust Power Lookup Table Core 0 */
#define BWN_NTAB_C0_ADJPLT_SIZE		128
#define BWN_NTAB_C0_GAINCTL		BWN_NTAB32(0x1A, 0x0C0) /* Gain Control Lookup Table Core 0 */
#define BWN_NTAB_C0_GAINCTL_SIZE	128
#define BWN_NTAB_C0_IQLT		BWN_NTAB32(0x1A, 0x140) /* IQ Lookup Table Core 0 */
#define BWN_NTAB_C0_IQLT_SIZE		128
#define BWN_NTAB_C0_LOFEEDTH		BWN_NTAB16(0x1A, 0x1C0) /* Local Oscillator Feed Through Lookup Table Core 0 */
#define BWN_NTAB_C0_LOFEEDTH_SIZE	128
#define BWN_NTAB_C1_ESTPLT		BWN_NTAB8 (0x1B, 0x000) /* Estimate Power Lookup Table Core 1 */
#define BWN_NTAB_C1_ESTPLT_SIZE		64
#define BWN_NTAB_C1_ADJPLT		BWN_NTAB8 (0x1B, 0x040) /* Adjust Power Lookup Table Core 1 */
#define BWN_NTAB_C1_ADJPLT_SIZE		128
#define BWN_NTAB_C1_GAINCTL		BWN_NTAB32(0x1B, 0x0C0) /* Gain Control Lookup Table Core 1 */
#define BWN_NTAB_C1_GAINCTL_SIZE	128
#define BWN_NTAB_C1_IQLT		BWN_NTAB32(0x1B, 0x140) /* IQ Lookup Table Core 1 */
#define BWN_NTAB_C1_IQLT_SIZE		128
#define BWN_NTAB_C1_LOFEEDTH		BWN_NTAB16(0x1B, 0x1C0) /* Local Oscillator Feed Through Lookup Table Core 1 */
#define BWN_NTAB_C1_LOFEEDTH_SIZE	128

/* Volatile N-PHY tables, PHY revision >= 3 */
#define BWN_NTAB_ANT_SW_CTL_R3		BWN_NTAB16( 9,   0) /* antenna software control */

/* Static N-PHY tables, PHY revision >= 3 */
#define BWN_NTAB_FRAMESTRUCT_R3		BWN_NTAB32(10,   0) /* frame struct  */
#define BWN_NTAB_PILOT_R3		BWN_NTAB16(11,   0) /* pilot  */
#define BWN_NTAB_TMAP_R3		BWN_NTAB32(12,   0) /* TM AP  */
#define BWN_NTAB_INTLEVEL_R3		BWN_NTAB32(13,   0) /* INT LV  */
#define BWN_NTAB_TDTRN_R3		BWN_NTAB32(14,   0) /* TD TRN  */
#define BWN_NTAB_NOISEVAR_R3		BWN_NTAB32(16,   0) /* noise variance */
#define BWN_NTAB_MCS_R3			BWN_NTAB16(18,   0) /* MCS  */
#define BWN_NTAB_TDI20A0_R3		BWN_NTAB32(19, 128) /* TDI 20/0  */
#define BWN_NTAB_TDI20A1_R3		BWN_NTAB32(19, 256) /* TDI 20/1  */
#define BWN_NTAB_TDI40A0_R3		BWN_NTAB32(19, 640) /* TDI 40/0  */
#define BWN_NTAB_TDI40A1_R3		BWN_NTAB32(19, 768) /* TDI 40/1  */
#define BWN_NTAB_PILOTLT_R3		BWN_NTAB32(20,   0) /* PLT lookup  */
#define BWN_NTAB_CHANEST_R3		BWN_NTAB32(22,   0) /* channel estimate  */
#define BWN_NTAB_FRAMELT_R3		 BWN_NTAB8(24,   0) /* frame lookup  */
#define BWN_NTAB_C0_ESTPLT_R3		 BWN_NTAB8(26,   0) /* estimated power lookup 0  */
#define BWN_NTAB_C0_ADJPLT_R3		 BWN_NTAB8(26,  64) /* adjusted power lookup 0  */
#define BWN_NTAB_C0_GAINCTL_R3		BWN_NTAB32(26, 192) /* gain control lookup 0  */
#define BWN_NTAB_C0_IQLT_R3		BWN_NTAB32(26, 320) /* I/Q lookup 0  */
#define BWN_NTAB_C0_LOFEEDTH_R3		BWN_NTAB16(26, 448) /* Local Oscillator Feed Through lookup 0  */
#define BWN_NTAB_C0_PAPD_COMP_R3	BWN_NTAB16(26, 576)
#define BWN_NTAB_C1_ESTPLT_R3		 BWN_NTAB8(27,   0) /* estimated power lookup 1  */
#define BWN_NTAB_C1_ADJPLT_R3		 BWN_NTAB8(27,  64) /* adjusted power lookup 1  */
#define BWN_NTAB_C1_GAINCTL_R3		BWN_NTAB32(27, 192) /* gain control lookup 1  */
#define BWN_NTAB_C1_IQLT_R3		BWN_NTAB32(27, 320) /* I/Q lookup 1  */
#define BWN_NTAB_C1_LOFEEDTH_R3		BWN_NTAB16(27, 448) /* Local Oscillator Feed Through lookup 1 */
#define BWN_NTAB_C1_PAPD_COMP_R3	BWN_NTAB16(27, 576)

/* Static N-PHY tables, PHY revision >= 7 */
#define BWN_NTAB_TMAP_R7		BWN_NTAB32(12,   0) /* TM AP */
#define BWN_NTAB_NOISEVAR_R7		BWN_NTAB32(16,   0) /* noise variance */

#define BWN_NTAB_TX_IQLO_CAL_LOFT_LADDER_40_SIZE	18
#define BWN_NTAB_TX_IQLO_CAL_LOFT_LADDER_20_SIZE	18
#define BWN_NTAB_TX_IQLO_CAL_IQIMB_LADDER_40_SIZE	18
#define BWN_NTAB_TX_IQLO_CAL_IQIMB_LADDER_20_SIZE	18
#define BWN_NTAB_TX_IQLO_CAL_STARTCOEFS_REV3		11
#define BWN_NTAB_TX_IQLO_CAL_STARTCOEFS			9
#define BWN_NTAB_TX_IQLO_CAL_CMDS_RECAL_REV3		12
#define BWN_NTAB_TX_IQLO_CAL_CMDS_RECAL			10
#define BWN_NTAB_TX_IQLO_CAL_CMDS_FULLCAL		10
#define BWN_NTAB_TX_IQLO_CAL_CMDS_FULLCAL_REV3		12

uint32_t bwn_ntab_read(struct bwn_mac *mac, uint32_t offset);
void bwn_ntab_read_bulk(struct bwn_mac *mac, uint32_t offset,
			 unsigned int nr_elements, void *_data);
void bwn_ntab_write(struct bwn_mac *mac, uint32_t offset, uint32_t value);
void bwn_ntab_write_bulk(struct bwn_mac *mac, uint32_t offset,
			  unsigned int nr_elements, const void *_data);

void bwn_nphy_tables_init(struct bwn_mac *mac);

const uint32_t *bwn_nphy_get_tx_gain_table(struct bwn_mac *mac);

const int16_t *bwn_ntab_get_rf_pwr_offset_table(struct bwn_mac *mac);

extern const int8_t bwn_ntab_papd_pga_gain_delta_ipa_2g[];

extern const uint16_t tbl_iqcal_gainparams[2][9][8];
extern const struct bwn_nphy_txiqcal_ladder ladder_lo[];
extern const struct bwn_nphy_txiqcal_ladder ladder_iq[];
extern const uint16_t loscale[];

extern const uint16_t tbl_tx_iqlo_cal_loft_ladder_40[];
extern const uint16_t tbl_tx_iqlo_cal_loft_ladder_20[];
extern const uint16_t tbl_tx_iqlo_cal_iqimb_ladder_40[];
extern const uint16_t tbl_tx_iqlo_cal_iqimb_ladder_20[];
extern const uint16_t tbl_tx_iqlo_cal_startcoefs_nphyrev3[];
extern const uint16_t tbl_tx_iqlo_cal_startcoefs[];
extern const uint16_t tbl_tx_iqlo_cal_cmds_recal_nphyrev3[];
extern const uint16_t tbl_tx_iqlo_cal_cmds_recal[];
extern const uint16_t tbl_tx_iqlo_cal_cmds_fullcal[];
extern const uint16_t tbl_tx_iqlo_cal_cmds_fullcal_nphyrev3[];
extern const int16_t tbl_tx_filter_coef_rev4[7][15];

extern const struct bwn_nphy_rf_control_override_rev2
	tbl_rf_control_override_rev2[];
extern const struct bwn_nphy_rf_control_override_rev3
	tbl_rf_control_override_rev3[];
const struct bwn_nphy_rf_control_override_rev7 *bwn_nphy_get_rf_ctl_over_rev7(
	struct bwn_mac *mac, uint16_t field, uint8_t override);

#endif	/* __IF_BWN_PHY_TABLES_N_H__ */
