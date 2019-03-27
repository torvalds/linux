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

#ifndef	__IF_BWN_PHY_N_REGS_H__
#define	__IF_BWN_PHY_N_REGS_H__

/* N-PHY registers. */

#define	BWN_NPHY_BBCFG				BWN_PHY_N(0x001) /* BB config */
#define	 BWN_NPHY_BBCFG_RSTCCA			0x4000 /* Reset CCA */
#define	 BWN_NPHY_BBCFG_RSTRX			0x8000 /* Reset RX */
#define	BWN_NPHY_CHANNEL			BWN_PHY_N(0x005) /* Channel */
#define	BWN_NPHY_TXERR				BWN_PHY_N(0x007) /* TX error */
#define	BWN_NPHY_BANDCTL			BWN_PHY_N(0x009) /* Band control */
#define	 BWN_NPHY_BANDCTL_5GHZ			0x0001 /* Use the 5GHz band */
#define	BWN_NPHY_4WI_ADDR			BWN_PHY_N(0x00B) /* Four-wire bus address */
#define	BWN_NPHY_4WI_DATAHI			BWN_PHY_N(0x00C) /* Four-wire bus data high */
#define	BWN_NPHY_4WI_DATALO			BWN_PHY_N(0x00D) /* Four-wire bus data low */
#define	BWN_NPHY_BIST_STAT0			BWN_PHY_N(0x00E) /* Built-in self test status 0 */
#define	BWN_NPHY_BIST_STAT1			BWN_PHY_N(0x00F) /* Built-in self test status 1 */

#define	BWN_NPHY_C1_DESPWR			BWN_PHY_N(0x018) /* Core 1 desired power */
#define	BWN_NPHY_C1_CCK_DESPWR			BWN_PHY_N(0x019) /* Core 1 CCK desired power */
#define	BWN_NPHY_C1_BCLIPBKOFF			BWN_PHY_N(0x01A) /* Core 1 barely clip backoff */
#define	BWN_NPHY_C1_CCK_BCLIPBKOFF		BWN_PHY_N(0x01B) /* Core 1 CCK barely clip backoff */
#define	BWN_NPHY_C1_CGAINI			BWN_PHY_N(0x01C) /* Core 1 compute gain info */
#define	 BWN_NPHY_C1_CGAINI_GAINBKOFF		0x001F /* Gain backoff */
#define	 BWN_NPHY_C1_CGAINI_GAINBKOFF_SHIFT	0
#define	 BWN_NPHY_C1_CGAINI_CLIPGBKOFF		0x03E0 /* Clip gain backoff */
#define	 BWN_NPHY_C1_CGAINI_CLIPGBKOFF_SHIFT	5
#define	 BWN_NPHY_C1_CGAINI_GAINSTEP		0x1C00 /* Gain step */
#define	 BWN_NPHY_C1_CGAINI_GAINSTEP_SHIFT	10
#define	 BWN_NPHY_C1_CGAINI_CL2DETECT		0x2000 /* Clip 2 detect mask */
#define	BWN_NPHY_C1_CCK_CGAINI			BWN_PHY_N(0x01D) /* Core 1 CCK compute gain info */
#define	 BWN_NPHY_C1_CCK_CGAINI_GAINBKOFF	0x001F /* Gain backoff */
#define	 BWN_NPHY_C1_CCK_CGAINI_CLIPGBKOFF	0x01E0 /* CCK barely clip gain backoff */
#define	BWN_NPHY_C1_MINMAX_GAIN			BWN_PHY_N(0x01E) /* Core 1 min/max gain */
#define	 BWN_NPHY_C1_MINGAIN			0x00FF /* Minimum gain */
#define	 BWN_NPHY_C1_MINGAIN_SHIFT		0
#define	 BWN_NPHY_C1_MAXGAIN			0xFF00 /* Maximum gain */
#define	 BWN_NPHY_C1_MAXGAIN_SHIFT		8
#define	BWN_NPHY_C1_CCK_MINMAX_GAIN		BWN_PHY_N(0x01F) /* Core 1 CCK min/max gain */
#define	 BWN_NPHY_C1_CCK_MINGAIN		0x00FF /* Minimum gain */
#define	 BWN_NPHY_C1_CCK_MINGAIN_SHIFT		0
#define	 BWN_NPHY_C1_CCK_MAXGAIN		0xFF00 /* Maximum gain */
#define	 BWN_NPHY_C1_CCK_MAXGAIN_SHIFT		8
#define	BWN_NPHY_C1_INITGAIN			BWN_PHY_N(0x020) /* Core 1 initial gain code */
#define	 BWN_NPHY_C1_INITGAIN_EXTLNA		0x0001 /* External LNA index */
#define	 BWN_NPHY_C1_INITGAIN_LNA		0x0006 /* LNA index */
#define	 BWN_NPHY_C1_INITGAIN_LNAIDX_SHIFT	1
#define	 BWN_NPHY_C1_INITGAIN_HPVGA1		0x0078 /* HPVGA1 index */
#define	 BWN_NPHY_C1_INITGAIN_HPVGA1_SHIFT	3
#define	 BWN_NPHY_C1_INITGAIN_HPVGA2		0x0F80 /* HPVGA2 index */
#define	 BWN_NPHY_C1_INITGAIN_HPVGA2_SHIFT	7
#define	 BWN_NPHY_C1_INITGAIN_TRRX		0x1000 /* TR RX index */
#define	 BWN_NPHY_C1_INITGAIN_TRTX		0x2000 /* TR TX index */
#define	BWN_NPHY_REV3_C1_INITGAIN_A		BWN_PHY_N(0x020)
#define	BWN_NPHY_C1_CLIP1_HIGAIN		BWN_PHY_N(0x021) /* Core 1 clip1 high gain code */
#define	BWN_NPHY_REV3_C1_INITGAIN_B		BWN_PHY_N(0x021)
#define	BWN_NPHY_C1_CLIP1_MEDGAIN		BWN_PHY_N(0x022) /* Core 1 clip1 medium gain code */
#define	BWN_NPHY_REV3_C1_CLIP_HIGAIN_A		BWN_PHY_N(0x022)
#define	BWN_NPHY_C1_CLIP1_LOGAIN		BWN_PHY_N(0x023) /* Core 1 clip1 low gain code */
#define	BWN_NPHY_REV3_C1_CLIP_HIGAIN_B		BWN_PHY_N(0x023)
#define	BWN_NPHY_C1_CLIP2_GAIN			BWN_PHY_N(0x024) /* Core 1 clip2 gain code */
#define	BWN_NPHY_REV3_C1_CLIP_MEDGAIN_A		BWN_PHY_N(0x024)
#define	BWN_NPHY_C1_FILTERGAIN			BWN_PHY_N(0x025) /* Core 1 filter gain */
#define	BWN_NPHY_C1_LPF_QHPF_BW			BWN_PHY_N(0x026) /* Core 1 LPF Q HP F bandwidth */
#define	BWN_NPHY_C1_CLIPWBTHRES			BWN_PHY_N(0x027) /* Core 1 clip wideband threshold */
#define	 BWN_NPHY_C1_CLIPWBTHRES_CLIP2		0x003F /* Clip 2 */
#define	 BWN_NPHY_C1_CLIPWBTHRES_CLIP2_SHIFT	0
#define	 BWN_NPHY_C1_CLIPWBTHRES_CLIP1		0x0FC0 /* Clip 1 */
#define	 BWN_NPHY_C1_CLIPWBTHRES_CLIP1_SHIFT	6
#define	BWN_NPHY_C1_W1THRES			BWN_PHY_N(0x028) /* Core 1 W1 threshold */
#define	BWN_NPHY_C1_EDTHRES			BWN_PHY_N(0x029) /* Core 1 ED threshold */
#define	BWN_NPHY_C1_SMSIGTHRES			BWN_PHY_N(0x02A) /* Core 1 small sig threshold */
#define	BWN_NPHY_C1_NBCLIPTHRES			BWN_PHY_N(0x02B) /* Core 1 NB clip threshold */
#define	BWN_NPHY_C1_CLIP1THRES			BWN_PHY_N(0x02C) /* Core 1 clip1 threshold */
#define	BWN_NPHY_C1_CLIP2THRES			BWN_PHY_N(0x02D) /* Core 1 clip2 threshold */

#define	BWN_NPHY_C2_DESPWR			BWN_PHY_N(0x02E) /* Core 2 desired power */
#define	BWN_NPHY_C2_CCK_DESPWR			BWN_PHY_N(0x02F) /* Core 2 CCK desired power */
#define	BWN_NPHY_C2_BCLIPBKOFF			BWN_PHY_N(0x030) /* Core 2 barely clip backoff */
#define	BWN_NPHY_C2_CCK_BCLIPBKOFF		BWN_PHY_N(0x031) /* Core 2 CCK barely clip backoff */
#define	BWN_NPHY_C2_CGAINI			BWN_PHY_N(0x032) /* Core 2 compute gain info */
#define	 BWN_NPHY_C2_CGAINI_GAINBKOFF		0x001F /* Gain backoff */
#define	 BWN_NPHY_C2_CGAINI_GAINBKOFF_SHIFT	0
#define	 BWN_NPHY_C2_CGAINI_CLIPGBKOFF		0x03E0 /* Clip gain backoff */
#define	 BWN_NPHY_C2_CGAINI_CLIPGBKOFF_SHIFT	5
#define	 BWN_NPHY_C2_CGAINI_GAINSTEP		0x1C00 /* Gain step */
#define	 BWN_NPHY_C2_CGAINI_GAINSTEP_SHIFT	10
#define	 BWN_NPHY_C2_CGAINI_CL2DETECT		0x2000 /* Clip 2 detect mask */
#define	BWN_NPHY_C2_CCK_CGAINI			BWN_PHY_N(0x033) /* Core 2 CCK compute gain info */
#define	 BWN_NPHY_C2_CCK_CGAINI_GAINBKOFF	0x001F /* Gain backoff */
#define	 BWN_NPHY_C2_CCK_CGAINI_CLIPGBKOFF	0x01E0 /* CCK barely clip gain backoff */
#define	BWN_NPHY_C2_MINMAX_GAIN			BWN_PHY_N(0x034) /* Core 2 min/max gain */
#define	 BWN_NPHY_C2_MINGAIN			0x00FF /* Minimum gain */
#define	 BWN_NPHY_C2_MINGAIN_SHIFT		0
#define	 BWN_NPHY_C2_MAXGAIN			0xFF00 /* Maximum gain */
#define	 BWN_NPHY_C2_MAXGAIN_SHIFT		8
#define	BWN_NPHY_C2_CCK_MINMAX_GAIN		BWN_PHY_N(0x035) /* Core 2 CCK min/max gain */
#define	 BWN_NPHY_C2_CCK_MINGAIN		0x00FF /* Minimum gain */
#define	 BWN_NPHY_C2_CCK_MINGAIN_SHIFT		0
#define	 BWN_NPHY_C2_CCK_MAXGAIN		0xFF00 /* Maximum gain */
#define	 BWN_NPHY_C2_CCK_MAXGAIN_SHIFT		8
#define	BWN_NPHY_C2_INITGAIN			BWN_PHY_N(0x036) /* Core 2 initial gain code */
#define	 BWN_NPHY_C2_INITGAIN_EXTLNA		0x0001 /* External LNA index */
#define	 BWN_NPHY_C2_INITGAIN_LNA		0x0006 /* LNA index */
#define	 BWN_NPHY_C2_INITGAIN_LNAIDX_SHIFT	1
#define	 BWN_NPHY_C2_INITGAIN_HPVGA1		0x0078 /* HPVGA1 index */
#define	 BWN_NPHY_C2_INITGAIN_HPVGA1_SHIFT	3
#define	 BWN_NPHY_C2_INITGAIN_HPVGA2		0x0F80 /* HPVGA2 index */
#define	 BWN_NPHY_C2_INITGAIN_HPVGA2_SHIFT	7
#define	 BWN_NPHY_C2_INITGAIN_TRRX		0x1000 /* TR RX index */
#define	 BWN_NPHY_C2_INITGAIN_TRTX		0x2000 /* TR TX index */
#define	BWN_NPHY_REV3_C1_CLIP_MEDGAIN_B		BWN_PHY_N(0x036)
#define	BWN_NPHY_C2_CLIP1_HIGAIN		BWN_PHY_N(0x037) /* Core 2 clip1 high gain code */
#define	BWN_NPHY_REV3_C1_CLIP_LOGAIN_A		BWN_PHY_N(0x037)
#define	BWN_NPHY_C2_CLIP1_MEDGAIN		BWN_PHY_N(0x038) /* Core 2 clip1 medium gain code */
#define	BWN_NPHY_REV3_C1_CLIP_LOGAIN_B		BWN_PHY_N(0x038)
#define	BWN_NPHY_C2_CLIP1_LOGAIN		BWN_PHY_N(0x039) /* Core 2 clip1 low gain code */
#define	BWN_NPHY_REV3_C1_CLIP2_GAIN_A		BWN_PHY_N(0x039)
#define	BWN_NPHY_C2_CLIP2_GAIN			BWN_PHY_N(0x03A) /* Core 2 clip2 gain code */
#define	BWN_NPHY_REV3_C1_CLIP2_GAIN_B		BWN_PHY_N(0x03A)
#define	BWN_NPHY_C2_FILTERGAIN			BWN_PHY_N(0x03B) /* Core 2 filter gain */
#define	BWN_NPHY_C2_LPF_QHPF_BW			BWN_PHY_N(0x03C) /* Core 2 LPF Q HP F bandwidth */
#define	BWN_NPHY_C2_CLIPWBTHRES			BWN_PHY_N(0x03D) /* Core 2 clip wideband threshold */
#define	 BWN_NPHY_C2_CLIPWBTHRES_CLIP2		0x003F /* Clip 2 */
#define	 BWN_NPHY_C2_CLIPWBTHRES_CLIP2_SHIFT	0
#define	 BWN_NPHY_C2_CLIPWBTHRES_CLIP1		0x0FC0 /* Clip 1 */
#define	 BWN_NPHY_C2_CLIPWBTHRES_CLIP1_SHIFT	6
#define	BWN_NPHY_C2_W1THRES			BWN_PHY_N(0x03E) /* Core 2 W1 threshold */
#define	BWN_NPHY_C2_EDTHRES			BWN_PHY_N(0x03F) /* Core 2 ED threshold */
#define	BWN_NPHY_C2_SMSIGTHRES			BWN_PHY_N(0x040) /* Core 2 small sig threshold */
#define	BWN_NPHY_C2_NBCLIPTHRES			BWN_PHY_N(0x041) /* Core 2 NB clip threshold */
#define	BWN_NPHY_C2_CLIP1THRES			BWN_PHY_N(0x042) /* Core 2 clip1 threshold */
#define	BWN_NPHY_C2_CLIP2THRES			BWN_PHY_N(0x043) /* Core 2 clip2 threshold */

#define	BWN_NPHY_CRS_THRES1			BWN_PHY_N(0x044) /* CRS threshold 1 */
#define	BWN_NPHY_CRS_THRES2			BWN_PHY_N(0x045) /* CRS threshold 2 */
#define	BWN_NPHY_CRS_THRES3			BWN_PHY_N(0x046) /* CRS threshold 3 */
#define	BWN_NPHY_CRSCTL				BWN_PHY_N(0x047) /* CRS control */
#define	BWN_NPHY_DCFADDR			BWN_PHY_N(0x048) /* DC filter address */
#define	BWN_NPHY_RXF20_NUM0			BWN_PHY_N(0x049) /* RX filter 20 numerator 0 */
#define	BWN_NPHY_RXF20_NUM1			BWN_PHY_N(0x04A) /* RX filter 20 numerator 1 */
#define	BWN_NPHY_RXF20_NUM2			BWN_PHY_N(0x04B) /* RX filter 20 numerator 2 */
#define	BWN_NPHY_RXF20_DENOM0			BWN_PHY_N(0x04C) /* RX filter 20 denominator 0 */
#define	BWN_NPHY_RXF20_DENOM1			BWN_PHY_N(0x04D) /* RX filter 20 denominator 1 */
#define	BWN_NPHY_RXF20_NUM10			BWN_PHY_N(0x04E) /* RX filter 20 numerator 10 */
#define	BWN_NPHY_RXF20_NUM11			BWN_PHY_N(0x04F) /* RX filter 20 numerator 11 */
#define	BWN_NPHY_RXF20_NUM12			BWN_PHY_N(0x050) /* RX filter 20 numerator 12 */
#define	BWN_NPHY_RXF20_DENOM10			BWN_PHY_N(0x051) /* RX filter 20 denominator 10 */
#define	BWN_NPHY_RXF20_DENOM11			BWN_PHY_N(0x052) /* RX filter 20 denominator 11 */
#define	BWN_NPHY_RXF40_NUM0			BWN_PHY_N(0x053) /* RX filter 40 numerator 0 */
#define	BWN_NPHY_RXF40_NUM1			BWN_PHY_N(0x054) /* RX filter 40 numerator 1 */
#define	BWN_NPHY_RXF40_NUM2			BWN_PHY_N(0x055) /* RX filter 40 numerator 2 */
#define	BWN_NPHY_RXF40_DENOM0			BWN_PHY_N(0x056) /* RX filter 40 denominator 0 */
#define	BWN_NPHY_RXF40_DENOM1			BWN_PHY_N(0x057) /* RX filter 40 denominator 1 */
#define	BWN_NPHY_RXF40_NUM10			BWN_PHY_N(0x058) /* RX filter 40 numerator 10 */
#define	BWN_NPHY_RXF40_NUM11			BWN_PHY_N(0x059) /* RX filter 40 numerator 11 */
#define	BWN_NPHY_RXF40_NUM12			BWN_PHY_N(0x05A) /* RX filter 40 numerator 12 */
#define	BWN_NPHY_RXF40_DENOM10			BWN_PHY_N(0x05B) /* RX filter 40 denominator 10 */
#define	BWN_NPHY_RXF40_DENOM11			BWN_PHY_N(0x05C) /* RX filter 40 denominator 11 */
#define	BWN_NPHY_PPROC_RSTLEN			BWN_PHY_N(0x060) /* Packet processing reset length */
#define	BWN_NPHY_INITCARR_DLEN			BWN_PHY_N(0x061) /* Initial carrier detection length */
#define	BWN_NPHY_CLIP1CARR_DLEN			BWN_PHY_N(0x062) /* Clip1 carrier detection length */
#define	BWN_NPHY_CLIP2CARR_DLEN			BWN_PHY_N(0x063) /* Clip2 carrier detection length */
#define	BWN_NPHY_INITGAIN_SLEN			BWN_PHY_N(0x064) /* Initial gain settle length */
#define	BWN_NPHY_CLIP1GAIN_SLEN			BWN_PHY_N(0x065) /* Clip1 gain settle length */
#define	BWN_NPHY_CLIP2GAIN_SLEN			BWN_PHY_N(0x066) /* Clip2 gain settle length */
#define	BWN_NPHY_PACKGAIN_SLEN			BWN_PHY_N(0x067) /* Packet gain settle length */
#define	BWN_NPHY_CARRSRC_TLEN			BWN_PHY_N(0x068) /* Carrier search timeout length */
#define	BWN_NPHY_TISRC_TLEN			BWN_PHY_N(0x069) /* Timing search timeout length */
#define	BWN_NPHY_ENDROP_TLEN			BWN_PHY_N(0x06A) /* Energy drop timeout length */
#define	BWN_NPHY_CLIP1_NBDWELL_LEN		BWN_PHY_N(0x06B) /* Clip1 NB dwell length */
#define	BWN_NPHY_CLIP2_NBDWELL_LEN		BWN_PHY_N(0x06C) /* Clip2 NB dwell length */
#define	BWN_NPHY_W1CLIP1_DWELL_LEN		BWN_PHY_N(0x06D) /* W1 clip1 dwell length */
#define	BWN_NPHY_W1CLIP2_DWELL_LEN		BWN_PHY_N(0x06E) /* W1 clip2 dwell length */
#define	BWN_NPHY_W2CLIP1_DWELL_LEN		BWN_PHY_N(0x06F) /* W2 clip1 dwell length */
#define	BWN_NPHY_PLOAD_CSENSE_EXTLEN		BWN_PHY_N(0x070) /* Payload carrier sense extension length */
#define	BWN_NPHY_EDROP_CSENSE_EXTLEN		BWN_PHY_N(0x071) /* Energy drop carrier sense extension length */
#define	BWN_NPHY_TABLE_ADDR			BWN_PHY_N(0x072) /* Table address */
#define	BWN_NPHY_TABLE_DATALO			BWN_PHY_N(0x073) /* Table data low */
#define	BWN_NPHY_TABLE_DATAHI			BWN_PHY_N(0x074) /* Table data high */
#define	BWN_NPHY_WWISE_LENIDX			BWN_PHY_N(0x075) /* WWiSE length index */
#define	BWN_NPHY_TGNSYNC_LENIDX			BWN_PHY_N(0x076) /* TGNsync length index */
#define	BWN_NPHY_TXMACIF_HOLDOFF		BWN_PHY_N(0x077) /* TX MAC IF Hold off */
#define	BWN_NPHY_RFCTL_CMD			BWN_PHY_N(0x078) /* RF control (command) */
#define	 BWN_NPHY_RFCTL_CMD_START		0x0001 /* Start sequence */
#define	 BWN_NPHY_RFCTL_CMD_RXTX		0x0002 /* RX/TX */
#define	 BWN_NPHY_RFCTL_CMD_CORESEL		0x0038 /* Core select */
#define	 BWN_NPHY_RFCTL_CMD_CORESEL_SHIFT	3
#define	 BWN_NPHY_RFCTL_CMD_PORFORCE		0x0040 /* POR force */
#define	 BWN_NPHY_RFCTL_CMD_OEPORFORCE		0x0080 /* OE POR force */
#define	 BWN_NPHY_RFCTL_CMD_RXEN		0x0100 /* RX enable */
#define	 BWN_NPHY_RFCTL_CMD_TXEN		0x0200 /* TX enable */
#define	 BWN_NPHY_RFCTL_CMD_CHIP0PU		0x0400 /* Chip0 PU */
#define	 BWN_NPHY_RFCTL_CMD_EN			0x0800 /* Radio enabled */
#define	 BWN_NPHY_RFCTL_CMD_SEQENCORE		0xF000 /* Seq en core */
#define	 BWN_NPHY_RFCTL_CMD_SEQENCORE_SHIFT	12
#define	BWN_NPHY_RFCTL_RSSIO1			BWN_PHY_N(0x07A) /* RF control (RSSI others 1) */
#define	 BWN_NPHY_RFCTL_RSSIO1_RXPD		0x0001 /* RX PD */
#define	 BWN_NPHY_RFCTL_RSSIO1_TXPD		0x0002 /* TX PD */
#define	 BWN_NPHY_RFCTL_RSSIO1_PAPD		0x0004 /* PA PD */
#define	 BWN_NPHY_RFCTL_RSSIO1_RSSICTL		0x0030 /* RSSI control */
#define	 BWN_NPHY_RFCTL_RSSIO1_LPFBW		0x00C0 /* LPF bandwidth */
#define	 BWN_NPHY_RFCTL_RSSIO1_HPFBWHI		0x0100 /* HPF bandwidth high */
#define	 BWN_NPHY_RFCTL_RSSIO1_HIQDISCO		0x0200 /* HIQ dis core */
#define	BWN_NPHY_RFCTL_RXG1			BWN_PHY_N(0x07B) /* RF control (RX gain 1) */
#define	BWN_NPHY_RFCTL_TXG1			BWN_PHY_N(0x07C) /* RF control (TX gain 1) */
#define	BWN_NPHY_RFCTL_RSSIO2			BWN_PHY_N(0x07D) /* RF control (RSSI others 2) */
#define	 BWN_NPHY_RFCTL_RSSIO2_RXPD		0x0001 /* RX PD */
#define	 BWN_NPHY_RFCTL_RSSIO2_TXPD		0x0002 /* TX PD */
#define	 BWN_NPHY_RFCTL_RSSIO2_PAPD		0x0004 /* PA PD */
#define	 BWN_NPHY_RFCTL_RSSIO2_RSSICTL		0x0030 /* RSSI control */
#define	 BWN_NPHY_RFCTL_RSSIO2_LPFBW		0x00C0 /* LPF bandwidth */
#define	 BWN_NPHY_RFCTL_RSSIO2_HPFBWHI		0x0100 /* HPF bandwidth high */
#define	 BWN_NPHY_RFCTL_RSSIO2_HIQDISCO		0x0200 /* HIQ dis core */
#define	BWN_NPHY_RFCTL_RXG2			BWN_PHY_N(0x07E) /* RF control (RX gain 2) */
#define	BWN_NPHY_RFCTL_TXG2			BWN_PHY_N(0x07F) /* RF control (TX gain 2) */
#define	BWN_NPHY_RFCTL_RSSIO3			BWN_PHY_N(0x080) /* RF control (RSSI others 3) */
#define	 BWN_NPHY_RFCTL_RSSIO3_RXPD		0x0001 /* RX PD */
#define	 BWN_NPHY_RFCTL_RSSIO3_TXPD		0x0002 /* TX PD */
#define	 BWN_NPHY_RFCTL_RSSIO3_PAPD		0x0004 /* PA PD */
#define	 BWN_NPHY_RFCTL_RSSIO3_RSSICTL		0x0030 /* RSSI control */
#define	 BWN_NPHY_RFCTL_RSSIO3_LPFBW		0x00C0 /* LPF bandwidth */
#define	 BWN_NPHY_RFCTL_RSSIO3_HPFBWHI		0x0100 /* HPF bandwidth high */
#define	 BWN_NPHY_RFCTL_RSSIO3_HIQDISCO		0x0200 /* HIQ dis core */
#define	BWN_NPHY_RFCTL_RXG3			BWN_PHY_N(0x081) /* RF control (RX gain 3) */
#define	BWN_NPHY_RFCTL_TXG3			BWN_PHY_N(0x082) /* RF control (TX gain 3) */
#define	BWN_NPHY_RFCTL_RSSIO4			BWN_PHY_N(0x083) /* RF control (RSSI others 4) */
#define	 BWN_NPHY_RFCTL_RSSIO4_RXPD		0x0001 /* RX PD */
#define	 BWN_NPHY_RFCTL_RSSIO4_TXPD		0x0002 /* TX PD */
#define	 BWN_NPHY_RFCTL_RSSIO4_PAPD		0x0004 /* PA PD */
#define	 BWN_NPHY_RFCTL_RSSIO4_RSSICTL		0x0030 /* RSSI control */
#define	 BWN_NPHY_RFCTL_RSSIO4_LPFBW		0x00C0 /* LPF bandwidth */
#define	 BWN_NPHY_RFCTL_RSSIO4_HPFBWHI		0x0100 /* HPF bandwidth high */
#define	 BWN_NPHY_RFCTL_RSSIO4_HIQDISCO		0x0200 /* HIQ dis core */
#define	BWN_NPHY_RFCTL_RXG4			BWN_PHY_N(0x084) /* RF control (RX gain 4) */
#define	BWN_NPHY_RFCTL_TXG4			BWN_PHY_N(0x085) /* RF control (TX gain 4) */
#define	BWN_NPHY_C1_TXIQ_COMP_OFF		BWN_PHY_N(0x087) /* Core 1 TX I/Q comp offset */
#define	BWN_NPHY_C2_TXIQ_COMP_OFF		BWN_PHY_N(0x088) /* Core 2 TX I/Q comp offset */
#define	BWN_NPHY_C1_TXCTL			BWN_PHY_N(0x08B) /* Core 1 TX control */
#define	BWN_NPHY_C2_TXCTL			BWN_PHY_N(0x08C) /* Core 2 TX control */
#define	BWN_NPHY_AFECTL_OVER1			BWN_PHY_N(0x08F) /* AFE control override 1 */
#define	BWN_NPHY_SCRAM_SIGCTL			BWN_PHY_N(0x090) /* Scram signal control */
#define	 BWN_NPHY_SCRAM_SIGCTL_INITST		0x007F /* Initial state value */
#define	 BWN_NPHY_SCRAM_SIGCTL_INITST_SHIFT	0
#define	 BWN_NPHY_SCRAM_SIGCTL_SCM		0x0080 /* Scram control mode */
#define	 BWN_NPHY_SCRAM_SIGCTL_SICE		0x0100 /* Scram index control enable */
#define	 BWN_NPHY_SCRAM_SIGCTL_START		0xFE00 /* Scram start bit */
#define	 BWN_NPHY_SCRAM_SIGCTL_START_SHIFT	9
#define	BWN_NPHY_RFCTL_INTC1			BWN_PHY_N(0x091) /* RF control (intc 1) */
#define	BWN_NPHY_RFCTL_INTC2			BWN_PHY_N(0x092) /* RF control (intc 2) */
#define	BWN_NPHY_RFCTL_INTC3			BWN_PHY_N(0x093) /* RF control (intc 3) */
#define	BWN_NPHY_RFCTL_INTC4			BWN_PHY_N(0x094) /* RF control (intc 4) */
#define	BWN_NPHY_NRDTO_WWISE			BWN_PHY_N(0x095) /* # datatones WWiSE */
#define	BWN_NPHY_NRDTO_TGNSYNC			BWN_PHY_N(0x096) /* # datatones TGNsync */
#define	BWN_NPHY_SIGFMOD_WWISE			BWN_PHY_N(0x097) /* Signal field mod WWiSE */
#define	BWN_NPHY_LEG_SIGFMOD_11N		BWN_PHY_N(0x098) /* Legacy signal field mod 11n */
#define	BWN_NPHY_HT_SIGFMOD_11N			BWN_PHY_N(0x099) /* HT signal field mod 11n */
#define	BWN_NPHY_C1_RXIQ_COMPA0			BWN_PHY_N(0x09A) /* Core 1 RX I/Q comp A0 */
#define	BWN_NPHY_C1_RXIQ_COMPB0			BWN_PHY_N(0x09B) /* Core 1 RX I/Q comp B0 */
#define	BWN_NPHY_C2_RXIQ_COMPA1			BWN_PHY_N(0x09C) /* Core 2 RX I/Q comp A1 */
#define	BWN_NPHY_C2_RXIQ_COMPB1			BWN_PHY_N(0x09D) /* Core 2 RX I/Q comp B1 */
#define	BWN_NPHY_RXCTL				BWN_PHY_N(0x0A0) /* RX control */
#define	 BWN_NPHY_RXCTL_BSELU20			0x0010 /* Band select upper 20 */
#define	 BWN_NPHY_RXCTL_RIFSEN			0x0080 /* RIFS enable */
#define	BWN_NPHY_RFSEQMODE			BWN_PHY_N(0x0A1) /* RF seq mode */
#define	 BWN_NPHY_RFSEQMODE_CAOVER		0x0001 /* Core active override */
#define	 BWN_NPHY_RFSEQMODE_TROVER		0x0002 /* Trigger override */
#define	BWN_NPHY_RFSEQCA			BWN_PHY_N(0x0A2) /* RF seq core active */
#define	 BWN_NPHY_RFSEQCA_TXEN			0x000F /* TX enable */
#define	 BWN_NPHY_RFSEQCA_TXEN_SHIFT		0
#define	 BWN_NPHY_RFSEQCA_RXEN			0x00F0 /* RX enable */
#define	 BWN_NPHY_RFSEQCA_RXEN_SHIFT		4
#define	 BWN_NPHY_RFSEQCA_TXDIS			0x0F00 /* TX disable */
#define	 BWN_NPHY_RFSEQCA_TXDIS_SHIFT		8
#define	 BWN_NPHY_RFSEQCA_RXDIS			0xF000 /* RX disable */
#define	 BWN_NPHY_RFSEQCA_RXDIS_SHIFT		12
#define	BWN_NPHY_RFSEQTR			BWN_PHY_N(0x0A3) /* RF seq trigger */
#define	 BWN_NPHY_RFSEQTR_RX2TX			0x0001 /* RX2TX */
#define	 BWN_NPHY_RFSEQTR_TX2RX			0x0002 /* TX2RX */
#define	 BWN_NPHY_RFSEQTR_UPGH			0x0004 /* Update gain H */
#define	 BWN_NPHY_RFSEQTR_UPGL			0x0008 /* Update gain L */
#define	 BWN_NPHY_RFSEQTR_UPGU			0x0010 /* Update gain U */
#define	 BWN_NPHY_RFSEQTR_RST2RX		0x0020 /* Reset to RX */
#define	BWN_NPHY_RFSEQST			BWN_PHY_N(0x0A4) /* RF seq status. Values same as trigger. */
#define	BWN_NPHY_AFECTL_OVER			BWN_PHY_N(0x0A5) /* AFE control override */
#define	BWN_NPHY_AFECTL_C1			BWN_PHY_N(0x0A6) /* AFE control core 1 */
#define	BWN_NPHY_AFECTL_C2			BWN_PHY_N(0x0A7) /* AFE control core 2 */
#define	BWN_NPHY_AFECTL_C3			BWN_PHY_N(0x0A8) /* AFE control core 3 */
#define	BWN_NPHY_AFECTL_C4			BWN_PHY_N(0x0A9) /* AFE control core 4 */
#define	BWN_NPHY_AFECTL_DACGAIN1		BWN_PHY_N(0x0AA) /* AFE control DAC gain 1 */
#define	BWN_NPHY_AFECTL_DACGAIN2		BWN_PHY_N(0x0AB) /* AFE control DAC gain 2 */
#define	BWN_NPHY_AFECTL_DACGAIN3		BWN_PHY_N(0x0AC) /* AFE control DAC gain 3 */
#define	BWN_NPHY_AFECTL_DACGAIN4		BWN_PHY_N(0x0AD) /* AFE control DAC gain 4 */
#define	BWN_NPHY_STR_ADDR1			BWN_PHY_N(0x0AE) /* STR address 1 */
#define	BWN_NPHY_STR_ADDR2			BWN_PHY_N(0x0AF) /* STR address 2 */
#define	BWN_NPHY_CLASSCTL			BWN_PHY_N(0x0B0) /* Classifier control */
#define	 BWN_NPHY_CLASSCTL_CCKEN		0x0001 /* CCK enable */
#define	 BWN_NPHY_CLASSCTL_OFDMEN		0x0002 /* OFDM enable */
#define	 BWN_NPHY_CLASSCTL_WAITEDEN		0x0004 /* Waited enable */
#define	BWN_NPHY_IQFLIP				BWN_PHY_N(0x0B1) /* I/Q flip */
#define	 BWN_NPHY_IQFLIP_ADC1			0x0001 /* ADC1 */
#define	 BWN_NPHY_IQFLIP_ADC2			0x0010 /* ADC2 */
#define	BWN_NPHY_SISO_SNR_THRES			BWN_PHY_N(0x0B2) /* SISO SNR threshold */
#define	BWN_NPHY_SIGMA_N_MULT			BWN_PHY_N(0x0B3) /* Sigma N multiplier */
#define	BWN_NPHY_TXMACDELAY			BWN_PHY_N(0x0B4) /* TX MAC delay */
#define	BWN_NPHY_TXFRAMEDELAY			BWN_PHY_N(0x0B5) /* TX frame delay */
#define	BWN_NPHY_MLPARM				BWN_PHY_N(0x0B6) /* ML parameters */
#define	BWN_NPHY_MLCTL				BWN_PHY_N(0x0B7) /* ML control */
#define	BWN_NPHY_WWISE_20NCYCDAT		BWN_PHY_N(0x0B8) /* WWiSE 20 N cyc data */
#define	BWN_NPHY_WWISE_40NCYCDAT		BWN_PHY_N(0x0B9) /* WWiSE 40 N cyc data */
#define	BWN_NPHY_TGNSYNC_20NCYCDAT		BWN_PHY_N(0x0BA) /* TGNsync 20 N cyc data */
#define	BWN_NPHY_TGNSYNC_40NCYCDAT		BWN_PHY_N(0x0BB) /* TGNsync 40 N cyc data */
#define	BWN_NPHY_INITSWIZP			BWN_PHY_N(0x0BC) /* Initial swizzle pattern */
#define	BWN_NPHY_TXTAILCNT			BWN_PHY_N(0x0BD) /* TX tail count value */
#define	BWN_NPHY_BPHY_CTL1			BWN_PHY_N(0x0BE) /* B PHY control 1 */
#define	BWN_NPHY_BPHY_CTL2			BWN_PHY_N(0x0BF) /* B PHY control 2 */
#define	 BWN_NPHY_BPHY_CTL2_LUT			0x001F /* LUT index */
#define	 BWN_NPHY_BPHY_CTL2_LUT_SHIFT		0
#define	 BWN_NPHY_BPHY_CTL2_MACDEL		0x7FE0 /* MAC delay */
#define	 BWN_NPHY_BPHY_CTL2_MACDEL_SHIFT	5
#define	BWN_NPHY_IQLOCAL_CMD			BWN_PHY_N(0x0C0) /* I/Q LO cal command */
#define	 BWN_NPHY_IQLOCAL_CMD_EN		0x8000
#define	BWN_NPHY_IQLOCAL_CMDNNUM		BWN_PHY_N(0x0C1) /* I/Q LO cal command N num */
#define	BWN_NPHY_IQLOCAL_CMDGCTL		BWN_PHY_N(0x0C2) /* I/Q LO cal command G control */
#define	BWN_NPHY_SAMP_CMD			BWN_PHY_N(0x0C3) /* Sample command */
#define	 BWN_NPHY_SAMP_CMD_STOP			0x0002 /* Stop */
#define	BWN_NPHY_SAMP_LOOPCNT			BWN_PHY_N(0x0C4) /* Sample loop count */
#define	BWN_NPHY_SAMP_WAITCNT			BWN_PHY_N(0x0C5) /* Sample wait count */
#define	BWN_NPHY_SAMP_DEPCNT			BWN_PHY_N(0x0C6) /* Sample depth count */
#define	BWN_NPHY_SAMP_STAT			BWN_PHY_N(0x0C7) /* Sample status */
#define	BWN_NPHY_GPIO_LOOEN			BWN_PHY_N(0x0C8) /* GPIO low out enable */
#define	BWN_NPHY_GPIO_HIOEN			BWN_PHY_N(0x0C9) /* GPIO high out enable */
#define	BWN_NPHY_GPIO_SEL			BWN_PHY_N(0x0CA) /* GPIO select */
#define	BWN_NPHY_GPIO_CLKCTL			BWN_PHY_N(0x0CB) /* GPIO clock control */
#define	BWN_NPHY_TXF_20CO_AS0			BWN_PHY_N(0x0CC) /* TX filter 20 coeff A stage 0 */
#define	BWN_NPHY_TXF_20CO_AS1			BWN_PHY_N(0x0CD) /* TX filter 20 coeff A stage 1 */
#define	BWN_NPHY_TXF_20CO_AS2			BWN_PHY_N(0x0CE) /* TX filter 20 coeff A stage 2 */
#define	BWN_NPHY_TXF_20CO_B32S0			BWN_PHY_N(0x0CF) /* TX filter 20 coeff B32 stage 0 */
#define	BWN_NPHY_TXF_20CO_B1S0			BWN_PHY_N(0x0D0) /* TX filter 20 coeff B1 stage 0 */
#define	BWN_NPHY_TXF_20CO_B32S1			BWN_PHY_N(0x0D1) /* TX filter 20 coeff B32 stage 1 */
#define	BWN_NPHY_TXF_20CO_B1S1			BWN_PHY_N(0x0D2) /* TX filter 20 coeff B1 stage 1 */
#define	BWN_NPHY_TXF_20CO_B32S2			BWN_PHY_N(0x0D3) /* TX filter 20 coeff B32 stage 2 */
#define	BWN_NPHY_TXF_20CO_B1S2			BWN_PHY_N(0x0D4) /* TX filter 20 coeff B1 stage 2 */
#define	BWN_NPHY_SIGFLDTOL			BWN_PHY_N(0x0D5) /* Signal fld tolerance */
#define	BWN_NPHY_TXSERFLD			BWN_PHY_N(0x0D6) /* TX service field */
#define	BWN_NPHY_AFESEQ_RX2TX_PUD		BWN_PHY_N(0x0D7) /* AFE seq RX2TX power up/down delay */
#define	BWN_NPHY_AFESEQ_TX2RX_PUD		BWN_PHY_N(0x0D8) /* AFE seq TX2RX power up/down delay */
#define	BWN_NPHY_TGNSYNC_SCRAMI0		BWN_PHY_N(0x0D9) /* TGNsync scram init 0 */
#define	BWN_NPHY_TGNSYNC_SCRAMI1		BWN_PHY_N(0x0DA) /* TGNsync scram init 1 */
#define	BWN_NPHY_INITSWIZPATTLEG		BWN_PHY_N(0x0DB) /* Initial swizzle pattern leg */
#define	BWN_NPHY_BPHY_CTL3			BWN_PHY_N(0x0DC) /* B PHY control 3 */
#define	 BWN_NPHY_BPHY_CTL3_SCALE		0x00FF /* Scale */
#define	 BWN_NPHY_BPHY_CTL3_SCALE_SHIFT		0
#define	 BWN_NPHY_BPHY_CTL3_FSC			0xFF00 /* Frame start count value */
#define	 BWN_NPHY_BPHY_CTL3_FSC_SHIFT		8
#define	BWN_NPHY_BPHY_CTL4			BWN_PHY_N(0x0DD) /* B PHY control 4 */
#define	BWN_NPHY_C1_TXBBMULT			BWN_PHY_N(0x0DE) /* Core 1 TX BB multiplier */
#define	BWN_NPHY_C2_TXBBMULT			BWN_PHY_N(0x0DF) /* Core 2 TX BB multiplier */
#define	BWN_NPHY_TXF_40CO_AS0			BWN_PHY_N(0x0E1) /* TX filter 40 coeff A stage 0 */
#define	BWN_NPHY_TXF_40CO_AS1			BWN_PHY_N(0x0E2) /* TX filter 40 coeff A stage 1 */
#define	BWN_NPHY_TXF_40CO_AS2			BWN_PHY_N(0x0E3) /* TX filter 40 coeff A stage 2 */
#define	BWN_NPHY_TXF_40CO_B32S0			BWN_PHY_N(0x0E4) /* TX filter 40 coeff B32 stage 0 */
#define	BWN_NPHY_TXF_40CO_B1S0			BWN_PHY_N(0x0E5) /* TX filter 40 coeff B1 stage 0 */
#define	BWN_NPHY_TXF_40CO_B32S1			BWN_PHY_N(0x0E6) /* TX filter 40 coeff B32 stage 1 */
#define	BWN_NPHY_TXF_40CO_B1S1			BWN_PHY_N(0x0E7) /* TX filter 40 coeff B1 stage 1 */
#define	BWN_NPHY_REV3_RFCTL_OVER0		BWN_PHY_N(0x0E7)
#define	BWN_NPHY_TXF_40CO_B32S2			BWN_PHY_N(0x0E8) /* TX filter 40 coeff B32 stage 2 */
#define	BWN_NPHY_TXF_40CO_B1S2			BWN_PHY_N(0x0E9) /* TX filter 40 coeff B1 stage 2 */
#define	BWN_NPHY_BIST_STAT2			BWN_PHY_N(0x0EA) /* BIST status 2 */
#define	BWN_NPHY_BIST_STAT3			BWN_PHY_N(0x0EB) /* BIST status 3 */
#define	BWN_NPHY_RFCTL_OVER			BWN_PHY_N(0x0EC) /* RF control override */
#define	BWN_NPHY_REV3_RFCTL_OVER1		BWN_PHY_N(0x0EC)
#define	BWN_NPHY_MIMOCFG			BWN_PHY_N(0x0ED) /* MIMO config */
#define	 BWN_NPHY_MIMOCFG_GFMIX			0x0004 /* Greenfield or mixed mode */
#define	 BWN_NPHY_MIMOCFG_AUTO			0x0100 /* Greenfield/mixed mode auto */
#define	BWN_NPHY_RADAR_BLNKCTL			BWN_PHY_N(0x0EE) /* Radar blank control */
#define	BWN_NPHY_A0RADAR_FIFOCTL		BWN_PHY_N(0x0EF) /* Antenna 0 radar FIFO control */
#define	BWN_NPHY_A1RADAR_FIFOCTL		BWN_PHY_N(0x0F0) /* Antenna 1 radar FIFO control */
#define	BWN_NPHY_A0RADAR_FIFODAT		BWN_PHY_N(0x0F1) /* Antenna 0 radar FIFO data */
#define	BWN_NPHY_A1RADAR_FIFODAT		BWN_PHY_N(0x0F2) /* Antenna 1 radar FIFO data */
#define	BWN_NPHY_RADAR_THRES0			BWN_PHY_N(0x0F3) /* Radar threshold 0 */
#define	BWN_NPHY_RADAR_THRES1			BWN_PHY_N(0x0F4) /* Radar threshold 1 */
#define	BWN_NPHY_RADAR_THRES0R			BWN_PHY_N(0x0F5) /* Radar threshold 0R */
#define	BWN_NPHY_RADAR_THRES1R			BWN_PHY_N(0x0F6) /* Radar threshold 1R */
#define	BWN_NPHY_CSEN_20IN40_DLEN		BWN_PHY_N(0x0F7) /* Carrier sense 20 in 40 dwell length */
#define	BWN_NPHY_RFCTL_LUT_TRSW_LO1		BWN_PHY_N(0x0F8) /* RF control LUT TRSW lower 1 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_UP1		BWN_PHY_N(0x0F9) /* RF control LUT TRSW upper 1 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_LO2		BWN_PHY_N(0x0FA) /* RF control LUT TRSW lower 2 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_UP2		BWN_PHY_N(0x0FB) /* RF control LUT TRSW upper 2 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_LO3		BWN_PHY_N(0x0FC) /* RF control LUT TRSW lower 3 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_UP3		BWN_PHY_N(0x0FD) /* RF control LUT TRSW upper 3 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_LO4		BWN_PHY_N(0x0FE) /* RF control LUT TRSW lower 4 */
#define	BWN_NPHY_RFCTL_LUT_TRSW_UP4		BWN_PHY_N(0x0FF) /* RF control LUT TRSW upper 4 */
#define	BWN_NPHY_RFCTL_LUT_LNAPA1		BWN_PHY_N(0x100) /* RF control LUT LNA PA 1 */
#define	BWN_NPHY_RFCTL_LUT_LNAPA2		BWN_PHY_N(0x101) /* RF control LUT LNA PA 2 */
#define	BWN_NPHY_RFCTL_LUT_LNAPA3		BWN_PHY_N(0x102) /* RF control LUT LNA PA 3 */
#define	BWN_NPHY_RFCTL_LUT_LNAPA4		BWN_PHY_N(0x103) /* RF control LUT LNA PA 4 */
#define	BWN_NPHY_TGNSYNC_CRCM0			BWN_PHY_N(0x104) /* TGNsync CRC mask 0 */
#define	BWN_NPHY_TGNSYNC_CRCM1			BWN_PHY_N(0x105) /* TGNsync CRC mask 1 */
#define	BWN_NPHY_TGNSYNC_CRCM2			BWN_PHY_N(0x106) /* TGNsync CRC mask 2 */
#define	BWN_NPHY_TGNSYNC_CRCM3			BWN_PHY_N(0x107) /* TGNsync CRC mask 3 */
#define	BWN_NPHY_TGNSYNC_CRCM4			BWN_PHY_N(0x108) /* TGNsync CRC mask 4 */
#define	BWN_NPHY_CRCPOLY			BWN_PHY_N(0x109) /* CRC polynomial */
#define	BWN_NPHY_SIGCNT				BWN_PHY_N(0x10A) /* # sig count */
#define	BWN_NPHY_SIGSTARTBIT_CTL		BWN_PHY_N(0x10B) /* Sig start bit control */
#define	BWN_NPHY_CRCPOLY_ORDER			BWN_PHY_N(0x10C) /* CRC polynomial order */
#define	BWN_NPHY_RFCTL_CST0			BWN_PHY_N(0x10D) /* RF control core swap table 0 */
#define	BWN_NPHY_RFCTL_CST1			BWN_PHY_N(0x10E) /* RF control core swap table 1 */
#define	BWN_NPHY_RFCTL_CST2O			BWN_PHY_N(0x10F) /* RF control core swap table 2 + others */
#define	BWN_NPHY_BPHY_CTL5			BWN_PHY_N(0x111) /* B PHY control 5 */
#define	BWN_NPHY_RFSEQ_LPFBW			BWN_PHY_N(0x112) /* RF seq LPF bandwidth */
#define	BWN_NPHY_TSSIBIAS1			BWN_PHY_N(0x114) /* TSSI bias val 1 */
#define	BWN_NPHY_TSSIBIAS2			BWN_PHY_N(0x115) /* TSSI bias val 2 */
#define	 BWN_NPHY_TSSIBIAS_BIAS			0x00FF /* Bias */
#define	 BWN_NPHY_TSSIBIAS_BIAS_SHIFT		0
#define	 BWN_NPHY_TSSIBIAS_VAL			0xFF00 /* Value */
#define	 BWN_NPHY_TSSIBIAS_VAL_SHIFT		8
#define	BWN_NPHY_ESTPWR1			BWN_PHY_N(0x118) /* Estimated power 1 */
#define	BWN_NPHY_ESTPWR2			BWN_PHY_N(0x119) /* Estimated power 2 */
#define	 BWN_NPHY_ESTPWR_PWR			0x00FF /* Estimated power */
#define	 BWN_NPHY_ESTPWR_PWR_SHIFT		0
#define	 BWN_NPHY_ESTPWR_VALID			0x0100 /* Estimated power valid */
#define	BWN_NPHY_TSSI_MAXTXFDT			BWN_PHY_N(0x11C) /* TSSI max TX frame delay time */
#define	 BWN_NPHY_TSSI_MAXTXFDT_VAL		0x00FF /* max TX frame delay time */
#define	 BWN_NPHY_TSSI_MAXTXFDT_VAL_SHIFT	0
#define	BWN_NPHY_TSSI_MAXTDT			BWN_PHY_N(0x11D) /* TSSI max TSSI delay time */
#define	 BWN_NPHY_TSSI_MAXTDT_VAL		0x00FF /* max TSSI delay time */
#define	 BWN_NPHY_TSSI_MAXTDT_VAL_SHIFT		0
#define	BWN_NPHY_ITSSI1				BWN_PHY_N(0x11E) /* TSSI idle 1 */
#define	BWN_NPHY_ITSSI2				BWN_PHY_N(0x11F) /* TSSI idle 2 */
#define	 BWN_NPHY_ITSSI_VAL			0x00FF /* Idle TSSI */
#define	 BWN_NPHY_ITSSI_VAL_SHIFT		0
#define	BWN_NPHY_TSSIMODE			BWN_PHY_N(0x122) /* TSSI mode */
#define	 BWN_NPHY_TSSIMODE_EN			0x0001 /* TSSI enable */
#define	 BWN_NPHY_TSSIMODE_PDEN			0x0002 /* Power det enable */
#define	BWN_NPHY_RXMACIFM			BWN_PHY_N(0x123) /* RX Macif mode */
#define	BWN_NPHY_CRSIT_COCNT_LO			BWN_PHY_N(0x124) /* CRS idle time CRS-on count (low) */
#define	BWN_NPHY_CRSIT_COCNT_HI			BWN_PHY_N(0x125) /* CRS idle time CRS-on count (high) */
#define	BWN_NPHY_CRSIT_MTCNT_LO			BWN_PHY_N(0x126) /* CRS idle time measure time count (low) */
#define	BWN_NPHY_CRSIT_MTCNT_HI			BWN_PHY_N(0x127) /* CRS idle time measure time count (high) */
#define	BWN_NPHY_SAMTWC				BWN_PHY_N(0x128) /* Sample tail wait count */
#define	BWN_NPHY_IQEST_CMD			BWN_PHY_N(0x129) /* I/Q estimate command */
#define	 BWN_NPHY_IQEST_CMD_START		0x0001 /* Start */
#define	 BWN_NPHY_IQEST_CMD_MODE		0x0002 /* Mode */
#define	BWN_NPHY_IQEST_WT			BWN_PHY_N(0x12A) /* I/Q estimate wait time */
#define	 BWN_NPHY_IQEST_WT_VAL			0x00FF /* Wait time */
#define	 BWN_NPHY_IQEST_WT_VAL_SHIFT		0
#define	BWN_NPHY_IQEST_SAMCNT			BWN_PHY_N(0x12B) /* I/Q estimate sample count */
#define	BWN_NPHY_IQEST_IQACC_LO0		BWN_PHY_N(0x12C) /* I/Q estimate I/Q acc lo 0 */
#define	BWN_NPHY_IQEST_IQACC_HI0		BWN_PHY_N(0x12D) /* I/Q estimate I/Q acc hi 0 */
#define	BWN_NPHY_IQEST_IPACC_LO0		BWN_PHY_N(0x12E) /* I/Q estimate I power acc lo 0 */
#define	BWN_NPHY_IQEST_IPACC_HI0		BWN_PHY_N(0x12F) /* I/Q estimate I power acc hi 0 */
#define	BWN_NPHY_IQEST_QPACC_LO0		BWN_PHY_N(0x130) /* I/Q estimate Q power acc lo 0 */
#define	BWN_NPHY_IQEST_QPACC_HI0		BWN_PHY_N(0x131) /* I/Q estimate Q power acc hi 0 */
#define	BWN_NPHY_IQEST_IQACC_LO1		BWN_PHY_N(0x134) /* I/Q estimate I/Q acc lo 1 */
#define	BWN_NPHY_IQEST_IQACC_HI1		BWN_PHY_N(0x135) /* I/Q estimate I/Q acc hi 1 */
#define	BWN_NPHY_IQEST_IPACC_LO1		BWN_PHY_N(0x136) /* I/Q estimate I power acc lo 1 */
#define	BWN_NPHY_IQEST_IPACC_HI1		BWN_PHY_N(0x137) /* I/Q estimate I power acc hi 1 */
#define	BWN_NPHY_IQEST_QPACC_LO1		BWN_PHY_N(0x138) /* I/Q estimate Q power acc lo 1 */
#define	BWN_NPHY_IQEST_QPACC_HI1		BWN_PHY_N(0x139) /* I/Q estimate Q power acc hi 1 */
#define	BWN_NPHY_MIMO_CRSTXEXT			BWN_PHY_N(0x13A) /* MIMO PHY CRS TX extension */
#define	BWN_NPHY_PWRDET1			BWN_PHY_N(0x13B) /* Power det 1 */
#define	BWN_NPHY_PWRDET2			BWN_PHY_N(0x13C) /* Power det 2 */
#define	BWN_NPHY_MAXRSSI_DTIME			BWN_PHY_N(0x13F) /* RSSI max RSSI delay time */
#define	BWN_NPHY_PIL_DW0			BWN_PHY_N(0x141) /* Pilot data weight 0 */
#define	BWN_NPHY_PIL_DW1			BWN_PHY_N(0x142) /* Pilot data weight 1 */
#define	BWN_NPHY_PIL_DW2			BWN_PHY_N(0x143) /* Pilot data weight 2 */
#define	 BWN_NPHY_PIL_DW_BPSK			0x000F /* BPSK */
#define	 BWN_NPHY_PIL_DW_BPSK_SHIFT		0
#define	 BWN_NPHY_PIL_DW_QPSK			0x00F0 /* QPSK */
#define	 BWN_NPHY_PIL_DW_QPSK_SHIFT		4
#define	 BWN_NPHY_PIL_DW_16QAM			0x0F00 /* 16-QAM */
#define	 BWN_NPHY_PIL_DW_16QAM_SHIFT		8
#define	 BWN_NPHY_PIL_DW_64QAM			0xF000 /* 64-QAM */
#define	 BWN_NPHY_PIL_DW_64QAM_SHIFT		12
#define	BWN_NPHY_FMDEM_CFG			BWN_PHY_N(0x144) /* FM demodulation config */
#define	BWN_NPHY_PHASETR_A0			BWN_PHY_N(0x145) /* Phase track alpha 0 */
#define	BWN_NPHY_PHASETR_A1			BWN_PHY_N(0x146) /* Phase track alpha 1 */
#define	BWN_NPHY_PHASETR_A2			BWN_PHY_N(0x147) /* Phase track alpha 2 */
#define	BWN_NPHY_PHASETR_B0			BWN_PHY_N(0x148) /* Phase track beta 0 */
#define	BWN_NPHY_PHASETR_B1			BWN_PHY_N(0x149) /* Phase track beta 1 */
#define	BWN_NPHY_PHASETR_B2			BWN_PHY_N(0x14A) /* Phase track beta 2 */
#define	BWN_NPHY_PHASETR_CHG0			BWN_PHY_N(0x14B) /* Phase track change 0 */
#define	BWN_NPHY_PHASETR_CHG1			BWN_PHY_N(0x14C) /* Phase track change 1 */
#define	BWN_NPHY_PHASETW_OFF			BWN_PHY_N(0x14D) /* Phase track offset */
#define	BWN_NPHY_RFCTL_DBG			BWN_PHY_N(0x14E) /* RF control debug */
#define	BWN_NPHY_CCK_SHIFTB_REF			BWN_PHY_N(0x150) /* CCK shiftbits reference var */
#define	BWN_NPHY_OVER_DGAIN0			BWN_PHY_N(0x152) /* Override digital gain 0 */
#define	BWN_NPHY_OVER_DGAIN1			BWN_PHY_N(0x153) /* Override digital gain 1 */
#define	 BWN_NPHY_OVER_DGAIN_FDGV		0x0007 /* Force digital gain value */
#define	 BWN_NPHY_OVER_DGAIN_FDGV_SHIFT		0
#define	 BWN_NPHY_OVER_DGAIN_FDGEN		0x0008 /* Force digital gain enable */
#define	 BWN_NPHY_OVER_DGAIN_CCKDGECV		0xFF00 /* CCK digital gain enable count value */
#define	 BWN_NPHY_OVER_DGAIN_CCKDGECV_SHIFT	8
#define	BWN_NPHY_BIST_STAT4			BWN_PHY_N(0x156) /* BIST status 4 */
#define	BWN_NPHY_RADAR_MAL			BWN_PHY_N(0x157) /* Radar MA length */
#define	BWN_NPHY_RADAR_SRCCTL			BWN_PHY_N(0x158) /* Radar search control */
#define	BWN_NPHY_VLD_DTSIG			BWN_PHY_N(0x159) /* VLD data tones sig */
#define	BWN_NPHY_VLD_DTDAT			BWN_PHY_N(0x15A) /* VLD data tones data */
#define	BWN_NPHY_C1_BPHY_RXIQCA0		BWN_PHY_N(0x15B) /* Core 1 B PHY RX I/Q comp A0 */
#define	BWN_NPHY_C1_BPHY_RXIQCB0		BWN_PHY_N(0x15C) /* Core 1 B PHY RX I/Q comp B0 */
#define	BWN_NPHY_C2_BPHY_RXIQCA1		BWN_PHY_N(0x15D) /* Core 2 B PHY RX I/Q comp A1 */
#define	BWN_NPHY_C2_BPHY_RXIQCB1		BWN_PHY_N(0x15E) /* Core 2 B PHY RX I/Q comp B1 */
#define	BWN_NPHY_FREQGAIN0			BWN_PHY_N(0x160) /* Frequency gain 0 */
#define	BWN_NPHY_FREQGAIN1			BWN_PHY_N(0x161) /* Frequency gain 1 */
#define	BWN_NPHY_FREQGAIN2			BWN_PHY_N(0x162) /* Frequency gain 2 */
#define	BWN_NPHY_FREQGAIN3			BWN_PHY_N(0x163) /* Frequency gain 3 */
#define	BWN_NPHY_FREQGAIN4			BWN_PHY_N(0x164) /* Frequency gain 4 */
#define	BWN_NPHY_FREQGAIN5			BWN_PHY_N(0x165) /* Frequency gain 5 */
#define	BWN_NPHY_FREQGAIN6			BWN_PHY_N(0x166) /* Frequency gain 6 */
#define	BWN_NPHY_FREQGAIN7			BWN_PHY_N(0x167) /* Frequency gain 7 */
#define	BWN_NPHY_FREQGAIN_BYPASS		BWN_PHY_N(0x168) /* Frequency gain bypass */
#define	BWN_NPHY_TRLOSS				BWN_PHY_N(0x169) /* TR loss value */
#define	BWN_NPHY_C1_ADCCLIP			BWN_PHY_N(0x16A) /* Core 1 ADC clip */
#define	BWN_NPHY_C2_ADCCLIP			BWN_PHY_N(0x16B) /* Core 2 ADC clip */
#define	BWN_NPHY_LTRN_OFFGAIN			BWN_PHY_N(0x16F) /* LTRN offset gain */
#define	BWN_NPHY_LTRN_OFF			BWN_PHY_N(0x170) /* LTRN offset */
#define	BWN_NPHY_NRDATAT_WWISE20SIG		BWN_PHY_N(0x171) /* # data tones WWiSE 20 sig */
#define	BWN_NPHY_NRDATAT_WWISE40SIG		BWN_PHY_N(0x172) /* # data tones WWiSE 40 sig */
#define	BWN_NPHY_NRDATAT_TGNSYNC20SIG		BWN_PHY_N(0x173) /* # data tones TGNsync 20 sig */
#define	BWN_NPHY_NRDATAT_TGNSYNC40SIG		BWN_PHY_N(0x174) /* # data tones TGNsync 40 sig */
#define	BWN_NPHY_WWISE_CRCM0			BWN_PHY_N(0x175) /* WWiSE CRC mask 0 */
#define	BWN_NPHY_WWISE_CRCM1			BWN_PHY_N(0x176) /* WWiSE CRC mask 1 */
#define	BWN_NPHY_WWISE_CRCM2			BWN_PHY_N(0x177) /* WWiSE CRC mask 2 */
#define	BWN_NPHY_WWISE_CRCM3			BWN_PHY_N(0x178) /* WWiSE CRC mask 3 */
#define	BWN_NPHY_WWISE_CRCM4			BWN_PHY_N(0x179) /* WWiSE CRC mask 4 */
#define	BWN_NPHY_CHANEST_CDDSH			BWN_PHY_N(0x17A) /* Channel estimate CDD shift */
#define	BWN_NPHY_HTAGC_WCNT			BWN_PHY_N(0x17B) /* HT ADC wait counters */
#define	BWN_NPHY_SQPARM				BWN_PHY_N(0x17C) /* SQ params */
#define	BWN_NPHY_MCSDUP6M			BWN_PHY_N(0x17D) /* MCS dup 6M */
#define	BWN_NPHY_NDATAT_DUP40			BWN_PHY_N(0x17E) /* # data tones dup 40 */
#define	BWN_NPHY_DUP40_TGNSYNC_CYCD		BWN_PHY_N(0x17F) /* Dup40 TGNsync cycle data */
#define	BWN_NPHY_DUP40_GFBL			BWN_PHY_N(0x180) /* Dup40 GF format BL address */
#define	BWN_NPHY_DUP40_BL			BWN_PHY_N(0x181) /* Dup40 format BL address */
#define	BWN_NPHY_LEGDUP_FTA			BWN_PHY_N(0x182) /* Legacy dup frm table address */
#define	BWN_NPHY_PACPROC_DBG			BWN_PHY_N(0x183) /* Packet processing debug */
#define	BWN_NPHY_PIL_CYC1			BWN_PHY_N(0x184) /* Pilot cycle counter 1 */
#define	BWN_NPHY_PIL_CYC2			BWN_PHY_N(0x185) /* Pilot cycle counter 2 */
#define	BWN_NPHY_TXF_20CO_S0A1			BWN_PHY_N(0x186) /* TX filter 20 coeff stage 0 A1 */
#define	BWN_NPHY_TXF_20CO_S0A2			BWN_PHY_N(0x187) /* TX filter 20 coeff stage 0 A2 */
#define	BWN_NPHY_TXF_20CO_S1A1			BWN_PHY_N(0x188) /* TX filter 20 coeff stage 1 A1 */
#define	BWN_NPHY_TXF_20CO_S1A2			BWN_PHY_N(0x189) /* TX filter 20 coeff stage 1 A2 */
#define	BWN_NPHY_TXF_20CO_S2A1			BWN_PHY_N(0x18A) /* TX filter 20 coeff stage 2 A1 */
#define	BWN_NPHY_TXF_20CO_S2A2			BWN_PHY_N(0x18B) /* TX filter 20 coeff stage 2 A2 */
#define	BWN_NPHY_TXF_20CO_S0B1			BWN_PHY_N(0x18C) /* TX filter 20 coeff stage 0 B1 */
#define	BWN_NPHY_TXF_20CO_S0B2			BWN_PHY_N(0x18D) /* TX filter 20 coeff stage 0 B2 */
#define	BWN_NPHY_TXF_20CO_S0B3			BWN_PHY_N(0x18E) /* TX filter 20 coeff stage 0 B3 */
#define	BWN_NPHY_TXF_20CO_S1B1			BWN_PHY_N(0x18F) /* TX filter 20 coeff stage 1 B1 */
#define	BWN_NPHY_TXF_20CO_S1B2			BWN_PHY_N(0x190) /* TX filter 20 coeff stage 1 B2 */
#define	BWN_NPHY_TXF_20CO_S1B3			BWN_PHY_N(0x191) /* TX filter 20 coeff stage 1 B3 */
#define	BWN_NPHY_TXF_20CO_S2B1			BWN_PHY_N(0x192) /* TX filter 20 coeff stage 2 B1 */
#define	BWN_NPHY_TXF_20CO_S2B2			BWN_PHY_N(0x193) /* TX filter 20 coeff stage 2 B2 */
#define	BWN_NPHY_TXF_20CO_S2B3			BWN_PHY_N(0x194) /* TX filter 20 coeff stage 2 B3 */
#define	BWN_NPHY_TXF_40CO_S0A1			BWN_PHY_N(0x195) /* TX filter 40 coeff stage 0 A1 */
#define	BWN_NPHY_TXF_40CO_S0A2			BWN_PHY_N(0x196) /* TX filter 40 coeff stage 0 A2 */
#define	BWN_NPHY_TXF_40CO_S1A1			BWN_PHY_N(0x197) /* TX filter 40 coeff stage 1 A1 */
#define	BWN_NPHY_TXF_40CO_S1A2			BWN_PHY_N(0x198) /* TX filter 40 coeff stage 1 A2 */
#define	BWN_NPHY_TXF_40CO_S2A1			BWN_PHY_N(0x199) /* TX filter 40 coeff stage 2 A1 */
#define	BWN_NPHY_TXF_40CO_S2A2			BWN_PHY_N(0x19A) /* TX filter 40 coeff stage 2 A2 */
#define	BWN_NPHY_TXF_40CO_S0B1			BWN_PHY_N(0x19B) /* TX filter 40 coeff stage 0 B1 */
#define	BWN_NPHY_TXF_40CO_S0B2			BWN_PHY_N(0x19C) /* TX filter 40 coeff stage 0 B2 */
#define	BWN_NPHY_TXF_40CO_S0B3			BWN_PHY_N(0x19D) /* TX filter 40 coeff stage 0 B3 */
#define	BWN_NPHY_TXF_40CO_S1B1			BWN_PHY_N(0x19E) /* TX filter 40 coeff stage 1 B1 */
#define	BWN_NPHY_TXF_40CO_S1B2			BWN_PHY_N(0x19F) /* TX filter 40 coeff stage 1 B2 */
#define	BWN_NPHY_TXF_40CO_S1B3			BWN_PHY_N(0x1A0) /* TX filter 40 coeff stage 1 B3 */
#define	BWN_NPHY_TXF_40CO_S2B1			BWN_PHY_N(0x1A1) /* TX filter 40 coeff stage 2 B1 */
#define	BWN_NPHY_TXF_40CO_S2B2			BWN_PHY_N(0x1A2) /* TX filter 40 coeff stage 2 B2 */
#define	BWN_NPHY_TXF_40CO_S2B3			BWN_PHY_N(0x1A3) /* TX filter 40 coeff stage 2 B3 */
#define	BWN_NPHY_RSSIMC_0I_RSSI_X		BWN_PHY_N(0x1A4) /* RSSI multiplication coefficient 0 I RSSI X */
#define	BWN_NPHY_RSSIMC_0I_RSSI_Y		BWN_PHY_N(0x1A5) /* RSSI multiplication coefficient 0 I RSSI Y */
#define	BWN_NPHY_RSSIMC_0I_RSSI_Z		BWN_PHY_N(0x1A6) /* RSSI multiplication coefficient 0 I RSSI Z */
#define	BWN_NPHY_RSSIMC_0I_TBD			BWN_PHY_N(0x1A7) /* RSSI multiplication coefficient 0 I TBD */
#define	BWN_NPHY_RSSIMC_0I_PWRDET		BWN_PHY_N(0x1A8) /* RSSI multiplication coefficient 0 I power det */
#define	BWN_NPHY_RSSIMC_0I_TSSI			BWN_PHY_N(0x1A9) /* RSSI multiplication coefficient 0 I TSSI */
#define	BWN_NPHY_RSSIMC_0Q_RSSI_X		BWN_PHY_N(0x1AA) /* RSSI multiplication coefficient 0 Q RSSI X */
#define	BWN_NPHY_RSSIMC_0Q_RSSI_Y		BWN_PHY_N(0x1AB) /* RSSI multiplication coefficient 0 Q RSSI Y */
#define	BWN_NPHY_RSSIMC_0Q_RSSI_Z		BWN_PHY_N(0x1AC) /* RSSI multiplication coefficient 0 Q RSSI Z */
#define	BWN_NPHY_RSSIMC_0Q_TBD			BWN_PHY_N(0x1AD) /* RSSI multiplication coefficient 0 Q TBD */
#define	BWN_NPHY_RSSIMC_0Q_PWRDET		BWN_PHY_N(0x1AE) /* RSSI multiplication coefficient 0 Q power det */
#define	BWN_NPHY_RSSIMC_0Q_TSSI			BWN_PHY_N(0x1AF) /* RSSI multiplication coefficient 0 Q TSSI */
#define	BWN_NPHY_RSSIMC_1I_RSSI_X		BWN_PHY_N(0x1B0) /* RSSI multiplication coefficient 1 I RSSI X */
#define	BWN_NPHY_RSSIMC_1I_RSSI_Y		BWN_PHY_N(0x1B1) /* RSSI multiplication coefficient 1 I RSSI Y */
#define	BWN_NPHY_RSSIMC_1I_RSSI_Z		BWN_PHY_N(0x1B2) /* RSSI multiplication coefficient 1 I RSSI Z */
#define	BWN_NPHY_RSSIMC_1I_TBD			BWN_PHY_N(0x1B3) /* RSSI multiplication coefficient 1 I TBD */
#define	BWN_NPHY_RSSIMC_1I_PWRDET		BWN_PHY_N(0x1B4) /* RSSI multiplication coefficient 1 I power det */
#define	BWN_NPHY_RSSIMC_1I_TSSI			BWN_PHY_N(0x1B5) /* RSSI multiplication coefficient 1 I TSSI */
#define	BWN_NPHY_RSSIMC_1Q_RSSI_X		BWN_PHY_N(0x1B6) /* RSSI multiplication coefficient 1 Q RSSI X */
#define	BWN_NPHY_RSSIMC_1Q_RSSI_Y		BWN_PHY_N(0x1B7) /* RSSI multiplication coefficient 1 Q RSSI Y */
#define	BWN_NPHY_RSSIMC_1Q_RSSI_Z		BWN_PHY_N(0x1B8) /* RSSI multiplication coefficient 1 Q RSSI Z */
#define	BWN_NPHY_RSSIMC_1Q_TBD			BWN_PHY_N(0x1B9) /* RSSI multiplication coefficient 1 Q TBD */
#define	BWN_NPHY_RSSIMC_1Q_PWRDET		BWN_PHY_N(0x1BA) /* RSSI multiplication coefficient 1 Q power det */
#define	BWN_NPHY_RSSIMC_1Q_TSSI			BWN_PHY_N(0x1BB) /* RSSI multiplication coefficient 1 Q TSSI */
#define	BWN_NPHY_SAMC_WCNT			BWN_PHY_N(0x1BC) /* Sample collect wait counter */
#define	BWN_NPHY_PTHROUGH_CNT			BWN_PHY_N(0x1BD) /* Pass-through counter */
#define	BWN_NPHY_LTRN_OFF_G20L			BWN_PHY_N(0x1C4) /* LTRN offset gain 20L */
#define	BWN_NPHY_LTRN_OFF_20L			BWN_PHY_N(0x1C5) /* LTRN offset 20L */
#define	BWN_NPHY_LTRN_OFF_G20U			BWN_PHY_N(0x1C6) /* LTRN offset gain 20U */
#define	BWN_NPHY_LTRN_OFF_20U			BWN_PHY_N(0x1C7) /* LTRN offset 20U */
#define	BWN_NPHY_DSSSCCK_GAINSL			BWN_PHY_N(0x1C8) /* DSSS/CCK gain settle length */
#define	BWN_NPHY_GPIO_LOOUT			BWN_PHY_N(0x1C9) /* GPIO low out */
#define	BWN_NPHY_GPIO_HIOUT			BWN_PHY_N(0x1CA) /* GPIO high out */
#define	BWN_NPHY_CRS_CHECK			BWN_PHY_N(0x1CB) /* CRS check */
#define	BWN_NPHY_ML_LOGSS_RAT			BWN_PHY_N(0x1CC) /* ML/logss ratio */
#define	BWN_NPHY_DUPSCALE			BWN_PHY_N(0x1CD) /* Dup scale */
#define	BWN_NPHY_BW1A				BWN_PHY_N(0x1CE) /* BW 1A */
#define	BWN_NPHY_BW2				BWN_PHY_N(0x1CF) /* BW 2 */
#define	BWN_NPHY_BW3				BWN_PHY_N(0x1D0) /* BW 3 */
#define	BWN_NPHY_BW4				BWN_PHY_N(0x1D1) /* BW 4 */
#define	BWN_NPHY_BW5				BWN_PHY_N(0x1D2) /* BW 5 */
#define	BWN_NPHY_BW6				BWN_PHY_N(0x1D3) /* BW 6 */
#define	BWN_NPHY_COALEN0			BWN_PHY_N(0x1D4) /* Coarse length 0 */
#define	BWN_NPHY_COALEN1			BWN_PHY_N(0x1D5) /* Coarse length 1 */
#define	BWN_NPHY_CRSTHRES_1U			BWN_PHY_N(0x1D6) /* CRS threshold 1 U */
#define	BWN_NPHY_CRSTHRES_2U			BWN_PHY_N(0x1D7) /* CRS threshold 2 U */
#define	BWN_NPHY_CRSTHRES_3U			BWN_PHY_N(0x1D8) /* CRS threshold 3 U */
#define	BWN_NPHY_CRSCTL_U			BWN_PHY_N(0x1D9) /* CRS control U */
#define	BWN_NPHY_CRSTHRES_1L			BWN_PHY_N(0x1DA) /* CRS threshold 1 L */
#define	BWN_NPHY_CRSTHRES_2L			BWN_PHY_N(0x1DB) /* CRS threshold 2 L */
#define	BWN_NPHY_CRSTHRES_3L			BWN_PHY_N(0x1DC) /* CRS threshold 3 L */
#define	BWN_NPHY_CRSCTL_L			BWN_PHY_N(0x1DD) /* CRS control L */
#define	BWN_NPHY_STRA_1U			BWN_PHY_N(0x1DE) /* STR address 1 U */
#define	BWN_NPHY_STRA_2U			BWN_PHY_N(0x1DF) /* STR address 2 U */
#define	BWN_NPHY_STRA_1L			BWN_PHY_N(0x1E0) /* STR address 1 L */
#define	BWN_NPHY_STRA_2L			BWN_PHY_N(0x1E1) /* STR address 2 L */
#define	BWN_NPHY_CRSCHECK1			BWN_PHY_N(0x1E2) /* CRS check 1 */
#define	BWN_NPHY_CRSCHECK2			BWN_PHY_N(0x1E3) /* CRS check 2 */
#define	BWN_NPHY_CRSCHECK3			BWN_PHY_N(0x1E4) /* CRS check 3 */
#define	BWN_NPHY_JMPSTP0			BWN_PHY_N(0x1E5) /* Jump step 0 */
#define	BWN_NPHY_JMPSTP1			BWN_PHY_N(0x1E6) /* Jump step 1 */
#define	BWN_NPHY_TXPCTL_CMD			BWN_PHY_N(0x1E7) /* TX power control command */
#define	 BWN_NPHY_TXPCTL_CMD_INIT		0x007F /* Init */
#define	 BWN_NPHY_TXPCTL_CMD_INIT_SHIFT		0
#define	 BWN_NPHY_TXPCTL_CMD_COEFF		0x2000 /* Power control coefficients */
#define	 BWN_NPHY_TXPCTL_CMD_HWPCTLEN		0x4000 /* Hardware TX power control enable */
#define	 BWN_NPHY_TXPCTL_CMD_PCTLEN		0x8000 /* TX power control enable */
#define	BWN_NPHY_TXPCTL_N			BWN_PHY_N(0x1E8) /* TX power control N num */
#define	 BWN_NPHY_TXPCTL_N_TSSID		0x00FF /* N TSSI delay */
#define	 BWN_NPHY_TXPCTL_N_TSSID_SHIFT		0
#define	 BWN_NPHY_TXPCTL_N_NPTIL2		0x0700 /* N PT integer log2 */
#define	 BWN_NPHY_TXPCTL_N_NPTIL2_SHIFT		8
#define	BWN_NPHY_TXPCTL_ITSSI			BWN_PHY_N(0x1E9) /* TX power control idle TSSI */
#define	 BWN_NPHY_TXPCTL_ITSSI_0		0x003F /* Idle TSSI 0 */
#define	 BWN_NPHY_TXPCTL_ITSSI_0_SHIFT		0
#define	 BWN_NPHY_TXPCTL_ITSSI_1		0x3F00 /* Idle TSSI 1 */
#define	 BWN_NPHY_TXPCTL_ITSSI_1_SHIFT		8
#define	 BWN_NPHY_TXPCTL_ITSSI_BINF		0x8000 /* Raw TSSI offset bin format */
#define	BWN_NPHY_TXPCTL_TPWR			BWN_PHY_N(0x1EA) /* TX power control target power */
#define	 BWN_NPHY_TXPCTL_TPWR_0			0x00FF /* Power 0 */
#define	 BWN_NPHY_TXPCTL_TPWR_0_SHIFT		0
#define	 BWN_NPHY_TXPCTL_TPWR_1			0xFF00 /* Power 1 */
#define	 BWN_NPHY_TXPCTL_TPWR_1_SHIFT		8
#define	BWN_NPHY_TXPCTL_BIDX			BWN_PHY_N(0x1EB) /* TX power control base index */
#define	 BWN_NPHY_TXPCTL_BIDX_0			0x007F /* uC base index 0 */
#define	 BWN_NPHY_TXPCTL_BIDX_0_SHIFT		0
#define	 BWN_NPHY_TXPCTL_BIDX_1			0x7F00 /* uC base index 1 */
#define	 BWN_NPHY_TXPCTL_BIDX_1_SHIFT		8
#define	 BWN_NPHY_TXPCTL_BIDX_LOAD		0x8000 /* Load base index */
#define	BWN_NPHY_TXPCTL_PIDX			BWN_PHY_N(0x1EC) /* TX power control power index */
#define	 BWN_NPHY_TXPCTL_PIDX_0			0x007F /* uC power index 0 */
#define	 BWN_NPHY_TXPCTL_PIDX_0_SHIFT		0
#define	 BWN_NPHY_TXPCTL_PIDX_1			0x7F00 /* uC power index 1 */
#define	 BWN_NPHY_TXPCTL_PIDX_1_SHIFT		8
#define	BWN_NPHY_C1_TXPCTL_STAT			BWN_PHY_N(0x1ED) /* Core 1 TX power control status */
#define	BWN_NPHY_C2_TXPCTL_STAT			BWN_PHY_N(0x1EE) /* Core 2 TX power control status */
#define	 BWN_NPHY_TXPCTL_STAT_EST		0x00FF /* Estimated power */
#define	 BWN_NPHY_TXPCTL_STAT_EST_SHIFT		0
#define	 BWN_NPHY_TXPCTL_STAT_BIDX		0x7F00 /* Base index */
#define	 BWN_NPHY_TXPCTL_STAT_BIDX_SHIFT	8
#define	 BWN_NPHY_TXPCTL_STAT_ESTVALID		0x8000 /* Estimated power valid */
#define	BWN_NPHY_SMALLSGS_LEN			BWN_PHY_N(0x1EF) /* Small sig gain settle length */
#define	BWN_NPHY_PHYSTAT_GAIN0			BWN_PHY_N(0x1F0) /* PHY stats gain info 0 */
#define	BWN_NPHY_PHYSTAT_GAIN1			BWN_PHY_N(0x1F1) /* PHY stats gain info 1 */
#define	BWN_NPHY_PHYSTAT_FREQEST		BWN_PHY_N(0x1F2) /* PHY stats frequency estimate */
#define	BWN_NPHY_PHYSTAT_ADVRET			BWN_PHY_N(0x1F3) /* PHY stats ADV retard */
#define	BWN_NPHY_PHYLB_MODE			BWN_PHY_N(0x1F4) /* PHY loopback mode */
#define	BWN_NPHY_TONE_MIDX20_1			BWN_PHY_N(0x1F5) /* Tone map index 20/1 */
#define	BWN_NPHY_TONE_MIDX20_2			BWN_PHY_N(0x1F6) /* Tone map index 20/2 */
#define	BWN_NPHY_TONE_MIDX20_3			BWN_PHY_N(0x1F7) /* Tone map index 20/3 */
#define	BWN_NPHY_TONE_MIDX40_1			BWN_PHY_N(0x1F8) /* Tone map index 40/1 */
#define	BWN_NPHY_TONE_MIDX40_2			BWN_PHY_N(0x1F9) /* Tone map index 40/2 */
#define	BWN_NPHY_TONE_MIDX40_3			BWN_PHY_N(0x1FA) /* Tone map index 40/3 */
#define	BWN_NPHY_TONE_MIDX40_4			BWN_PHY_N(0x1FB) /* Tone map index 40/4 */
#define	BWN_NPHY_PILTONE_MIDX1			BWN_PHY_N(0x1FC) /* Pilot tone map index 1 */
#define	BWN_NPHY_PILTONE_MIDX2			BWN_PHY_N(0x1FD) /* Pilot tone map index 2 */
#define	BWN_NPHY_PILTONE_MIDX3			BWN_PHY_N(0x1FE) /* Pilot tone map index 3 */
#define	BWN_NPHY_TXRIFS_FRDEL			BWN_PHY_N(0x1FF) /* TX RIFS frame delay */
#define	BWN_NPHY_AFESEQ_RX2TX_PUD_40M		BWN_PHY_N(0x200) /* AFE seq rx2tx power up/down delay 40M */
#define	BWN_NPHY_AFESEQ_TX2RX_PUD_40M		BWN_PHY_N(0x201) /* AFE seq tx2rx power up/down delay 40M */
#define	BWN_NPHY_AFESEQ_RX2TX_PUD_20M		BWN_PHY_N(0x202) /* AFE seq rx2tx power up/down delay 20M */
#define	BWN_NPHY_AFESEQ_TX2RX_PUD_20M		BWN_PHY_N(0x203) /* AFE seq tx2rx power up/down delay 20M */
#define	BWN_NPHY_RX_SIGCTL			BWN_PHY_N(0x204) /* RX signal control */
#define	BWN_NPHY_RXPIL_CYCNT0			BWN_PHY_N(0x205) /* RX pilot cycle counter 0 */
#define	BWN_NPHY_RXPIL_CYCNT1			BWN_PHY_N(0x206) /* RX pilot cycle counter 1 */
#define	BWN_NPHY_RXPIL_CYCNT2			BWN_PHY_N(0x207) /* RX pilot cycle counter 2 */
#define	BWN_NPHY_AFESEQ_RX2TX_PUD_10M		BWN_PHY_N(0x208) /* AFE seq rx2tx power up/down delay 10M */
#define	BWN_NPHY_AFESEQ_TX2RX_PUD_10M		BWN_PHY_N(0x209) /* AFE seq tx2rx power up/down delay 10M */
#define	BWN_NPHY_DSSSCCK_CRSEXTL		BWN_PHY_N(0x20A) /* DSSS/CCK CRS extension length */
#define	BWN_NPHY_ML_LOGSS_RATSLOPE		BWN_PHY_N(0x20B) /* ML/logss ratio slope */
#define	BWN_NPHY_RIFS_SRCTL			BWN_PHY_N(0x20C) /* RIFS search timeout length */
#define	BWN_NPHY_TXREALFD			BWN_PHY_N(0x20D) /* TX real frame delay */
#define	BWN_NPHY_HPANT_SWTHRES			BWN_PHY_N(0x20E) /* High power antenna switch threshold */
#define	BWN_NPHY_EDCRS_ASSTHRES0		BWN_PHY_N(0x210) /* ED CRS assert threshold 0 */
#define	BWN_NPHY_EDCRS_ASSTHRES1		BWN_PHY_N(0x211) /* ED CRS assert threshold 1 */
#define	BWN_NPHY_EDCRS_DEASSTHRES0		BWN_PHY_N(0x212) /* ED CRS deassert threshold 0 */
#define	BWN_NPHY_EDCRS_DEASSTHRES1		BWN_PHY_N(0x213) /* ED CRS deassert threshold 1 */
#define	BWN_NPHY_STR_WTIME20U			BWN_PHY_N(0x214) /* STR wait time 20U */
#define	BWN_NPHY_STR_WTIME20L			BWN_PHY_N(0x215) /* STR wait time 20L */
#define	BWN_NPHY_TONE_MIDX657M			BWN_PHY_N(0x216) /* Tone map index 657M */
#define	BWN_NPHY_HTSIGTONES			BWN_PHY_N(0x217) /* HT signal tones */
#define	BWN_NPHY_RSSI1				BWN_PHY_N(0x219) /* RSSI value 1 */
#define	BWN_NPHY_RSSI2				BWN_PHY_N(0x21A) /* RSSI value 2 */
#define	BWN_NPHY_CHAN_ESTHANG			BWN_PHY_N(0x21D) /* Channel estimate hang */
#define	BWN_NPHY_FINERX2_CGC			BWN_PHY_N(0x221) /* Fine RX 2 clock gate control */
#define	 BWN_NPHY_FINERX2_CGC_DECGC		0x0008 /* Decode gated clocks */
#define	BWN_NPHY_TXPCTL_INIT			BWN_PHY_N(0x222) /* TX power control init */
#define	 BWN_NPHY_TXPCTL_INIT_PIDXI1		0x00FF /* Power index init 1 */
#define	 BWN_NPHY_TXPCTL_INIT_PIDXI1_SHIFT	0
#define	BWN_NPHY_ED_CRSEN			BWN_PHY_N(0x223)
#define	BWN_NPHY_ED_CRS40ASSERTTHRESH0		BWN_PHY_N(0x224)
#define	BWN_NPHY_ED_CRS40ASSERTTHRESH1		BWN_PHY_N(0x225)
#define	BWN_NPHY_ED_CRS40DEASSERTTHRESH0	BWN_PHY_N(0x226)
#define	BWN_NPHY_ED_CRS40DEASSERTTHRESH1	BWN_PHY_N(0x227)
#define	BWN_NPHY_ED_CRS20LASSERTTHRESH0		BWN_PHY_N(0x228)
#define	BWN_NPHY_ED_CRS20LASSERTTHRESH1		BWN_PHY_N(0x229)
#define	BWN_NPHY_ED_CRS20LDEASSERTTHRESH0	BWN_PHY_N(0x22A)
#define	BWN_NPHY_ED_CRS20LDEASSERTTHRESH1	BWN_PHY_N(0x22B)
#define	BWN_NPHY_ED_CRS20UASSERTTHRESH0		BWN_PHY_N(0x22C)
#define	BWN_NPHY_ED_CRS20UASSERTTHRESH1		BWN_PHY_N(0x22D)
#define	BWN_NPHY_ED_CRS20UDEASSERTTHRESH0	BWN_PHY_N(0x22E)
#define	BWN_NPHY_ED_CRS20UDEASSERTTHRESH1	BWN_PHY_N(0x22F)
#define	BWN_NPHY_ED_CRS				BWN_PHY_N(0x230)
#define	BWN_NPHY_TIMEOUTEN			BWN_PHY_N(0x231)
#define	BWN_NPHY_OFDMPAYDECODETIMEOUTLEN	BWN_PHY_N(0x232)
#define	BWN_NPHY_CCKPAYDECODETIMEOUTLEN		BWN_PHY_N(0x233)
#define	BWN_NPHY_NONPAYDECODETIMEOUTLEN		BWN_PHY_N(0x234)
#define	BWN_NPHY_TIMEOUTSTATUS			BWN_PHY_N(0x235)
#define	BWN_NPHY_RFCTRLCORE0GPIO0		BWN_PHY_N(0x236)
#define	BWN_NPHY_RFCTRLCORE0GPIO1		BWN_PHY_N(0x237)
#define	BWN_NPHY_RFCTRLCORE0GPIO2		BWN_PHY_N(0x238)
#define	BWN_NPHY_RFCTRLCORE0GPIO3		BWN_PHY_N(0x239)
#define	BWN_NPHY_RFCTRLCORE1GPIO0		BWN_PHY_N(0x23A)
#define	BWN_NPHY_RFCTRLCORE1GPIO1		BWN_PHY_N(0x23B)
#define	BWN_NPHY_RFCTRLCORE1GPIO2		BWN_PHY_N(0x23C)
#define	BWN_NPHY_RFCTRLCORE1GPIO3		BWN_PHY_N(0x23D)
#define	BWN_NPHY_BPHYTESTCONTROL		BWN_PHY_N(0x23E)

/* REV3+ */
#define	BWN_NPHY_FORCEFRONT0			BWN_PHY_N(0x23F)
#define	BWN_NPHY_FORCEFRONT1			BWN_PHY_N(0x240)
#define	BWN_NPHY_NORMVARHYSTTH			BWN_PHY_N(0x241)
#define	BWN_NPHY_TXCCKERROR			BWN_PHY_N(0x242)
#define	BWN_NPHY_AFESEQINITDACGAIN		BWN_PHY_N(0x243)
#define	BWN_NPHY_TXANTSWLUT			BWN_PHY_N(0x244)
#define	BWN_NPHY_CORECONFIG			BWN_PHY_N(0x245)
#define	BWN_NPHY_ANTENNADIVDWELLTIME		BWN_PHY_N(0x246)
#define	BWN_NPHY_ANTENNACCKDIVDWELLTIME		BWN_PHY_N(0x247)
#define	BWN_NPHY_ANTENNADIVBACKOFFGAIN		BWN_PHY_N(0x248)
#define	BWN_NPHY_ANTENNADIVMINGAIN		BWN_PHY_N(0x249)
#define	BWN_NPHY_BRDSEL_NORMVARHYSTTH		BWN_PHY_N(0x24A)
#define	BWN_NPHY_RXANTSWITCHCTRL		BWN_PHY_N(0x24B)
#define	BWN_NPHY_ENERGYDROPTIMEOUTLEN2		BWN_PHY_N(0x24C)
#define	BWN_NPHY_ML_LOG_TXEVM0			BWN_PHY_N(0x250)
#define	BWN_NPHY_ML_LOG_TXEVM1			BWN_PHY_N(0x251)
#define	BWN_NPHY_ML_LOG_TXEVM2			BWN_PHY_N(0x252)
#define	BWN_NPHY_ML_LOG_TXEVM3			BWN_PHY_N(0x253)
#define	BWN_NPHY_ML_LOG_TXEVM4			BWN_PHY_N(0x254)
#define	BWN_NPHY_ML_LOG_TXEVM5			BWN_PHY_N(0x255)
#define	BWN_NPHY_ML_LOG_TXEVM6			BWN_PHY_N(0x256)
#define	BWN_NPHY_ML_LOG_TXEVM7			BWN_PHY_N(0x257)
#define	BWN_NPHY_ML_SCALE_TWEAK			BWN_PHY_N(0x258)
#define	BWN_NPHY_MLUA				BWN_PHY_N(0x259)
#define	BWN_NPHY_ZFUA				BWN_PHY_N(0x25A)
#define	BWN_NPHY_CHANUPSYM01			BWN_PHY_N(0x25B)
#define	BWN_NPHY_CHANUPSYM2			BWN_PHY_N(0x25C)
#define	BWN_NPHY_RXSTRNFILT20NUM00		BWN_PHY_N(0x25D)
#define	BWN_NPHY_RXSTRNFILT20NUM01		BWN_PHY_N(0x25E)
#define	BWN_NPHY_RXSTRNFILT20NUM02		BWN_PHY_N(0x25F)
#define	BWN_NPHY_RXSTRNFILT20DEN00		BWN_PHY_N(0x260)
#define	BWN_NPHY_RXSTRNFILT20DEN01		BWN_PHY_N(0x261)
#define	BWN_NPHY_RXSTRNFILT20NUM10		BWN_PHY_N(0x262)
#define	BWN_NPHY_RXSTRNFILT20NUM11		BWN_PHY_N(0x263)
#define	BWN_NPHY_RXSTRNFILT20NUM12		BWN_PHY_N(0x264)
#define	BWN_NPHY_RXSTRNFILT20DEN10		BWN_PHY_N(0x265)
#define	BWN_NPHY_RXSTRNFILT20DEN11		BWN_PHY_N(0x266)
#define	BWN_NPHY_RXSTRNFILT40NUM00		BWN_PHY_N(0x267)
#define	BWN_NPHY_RXSTRNFILT40NUM01		BWN_PHY_N(0x268)
#define	BWN_NPHY_RXSTRNFILT40NUM02		BWN_PHY_N(0x269)
#define	BWN_NPHY_RXSTRNFILT40DEN00		BWN_PHY_N(0x26A)
#define	BWN_NPHY_RXSTRNFILT40DEN01		BWN_PHY_N(0x26B)
#define	BWN_NPHY_RXSTRNFILT40NUM10		BWN_PHY_N(0x26C)
#define	BWN_NPHY_RXSTRNFILT40NUM11		BWN_PHY_N(0x26D)
#define	BWN_NPHY_RXSTRNFILT40NUM12		BWN_PHY_N(0x26E)
#define	BWN_NPHY_RXSTRNFILT40DEN10		BWN_PHY_N(0x26F)
#define	BWN_NPHY_RXSTRNFILT40DEN11		BWN_PHY_N(0x270)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD1		BWN_PHY_N(0x271)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD2		BWN_PHY_N(0x272)
#define	BWN_NPHY_CRSHIGHLOWPOWTHRESHOLD		BWN_PHY_N(0x273)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD1L		BWN_PHY_N(0x274)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD2L		BWN_PHY_N(0x275)
#define	BWN_NPHY_CRSHIGHLOWPOWTHRESHOLDL	BWN_PHY_N(0x276)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD1U		BWN_PHY_N(0x277)
#define	BWN_NPHY_CRSHIGHPOWTHRESHOLD2U		BWN_PHY_N(0x278)
#define	BWN_NPHY_CRSHIGHLOWPOWTHRESHOLDU	BWN_PHY_N(0x279)
#define	BWN_NPHY_CRSACIDETECTTHRESH		BWN_PHY_N(0x27A)
#define	BWN_NPHY_CRSACIDETECTTHRESHL		BWN_PHY_N(0x27B)
#define	BWN_NPHY_CRSACIDETECTTHRESHU		BWN_PHY_N(0x27C)
#define	BWN_NPHY_CRSMINPOWER0			BWN_PHY_N(0x27D)
#define	BWN_NPHY_CRSMINPOWER1			BWN_PHY_N(0x27E)
#define	BWN_NPHY_CRSMINPOWER2			BWN_PHY_N(0x27F)
#define	BWN_NPHY_CRSMINPOWERL0			BWN_PHY_N(0x280)
#define	BWN_NPHY_CRSMINPOWERL1			BWN_PHY_N(0x281)
#define	BWN_NPHY_CRSMINPOWERL2			BWN_PHY_N(0x282)
#define	BWN_NPHY_CRSMINPOWERU0			BWN_PHY_N(0x283)
#define	BWN_NPHY_CRSMINPOWERU1			BWN_PHY_N(0x284)
#define	BWN_NPHY_CRSMINPOWERU2			BWN_PHY_N(0x285)
#define	BWN_NPHY_STRPARAM			BWN_PHY_N(0x286)
#define	BWN_NPHY_STRPARAML			BWN_PHY_N(0x287)
#define	BWN_NPHY_STRPARAMU			BWN_PHY_N(0x288)
#define	BWN_NPHY_BPHYCRSMINPOWER0		BWN_PHY_N(0x289)
#define	BWN_NPHY_BPHYCRSMINPOWER1		BWN_PHY_N(0x28A)
#define	BWN_NPHY_BPHYCRSMINPOWER2		BWN_PHY_N(0x28B)
#define	BWN_NPHY_BPHYFILTDEN0COEF		BWN_PHY_N(0x28C)
#define	BWN_NPHY_BPHYFILTDEN1COEF		BWN_PHY_N(0x28D)
#define	BWN_NPHY_BPHYFILTDEN2COEF		BWN_PHY_N(0x28E)
#define	BWN_NPHY_BPHYFILTNUM0COEF		BWN_PHY_N(0x28F)
#define	BWN_NPHY_BPHYFILTNUM1COEF		BWN_PHY_N(0x290)
#define	BWN_NPHY_BPHYFILTNUM2COEF		BWN_PHY_N(0x291)
#define	BWN_NPHY_BPHYFILTNUM01COEF2		BWN_PHY_N(0x292)
#define	BWN_NPHY_BPHYFILTBYPASS			BWN_PHY_N(0x293)
#define	BWN_NPHY_SGILTRNOFFSET			BWN_PHY_N(0x294)
#define	BWN_NPHY_RADAR_T2_MIN			BWN_PHY_N(0x295)
#define	BWN_NPHY_TXPWRCTRLDAMPING		BWN_PHY_N(0x296)
#define	BWN_NPHY_PAPD_EN0			BWN_PHY_N(0x297) /* PAPD Enable0 TBD */
#define	BWN_NPHY_EPS_TABLE_ADJ0			BWN_PHY_N(0x298) /* EPS Table Adj0 TBD */
#define	BWN_NPHY_EPS_OVERRIDEI_0		BWN_PHY_N(0x299)
#define	BWN_NPHY_EPS_OVERRIDEQ_0		BWN_PHY_N(0x29A)
#define	BWN_NPHY_PAPD_EN1			BWN_PHY_N(0x29B) /* PAPD Enable1 TBD */
#define	BWN_NPHY_EPS_TABLE_ADJ1			BWN_PHY_N(0x29C) /* EPS Table Adj1 TBD */
#define	BWN_NPHY_EPS_OVERRIDEI_1		BWN_PHY_N(0x29D)
#define	BWN_NPHY_EPS_OVERRIDEQ_1		BWN_PHY_N(0x29E)
#define	BWN_NPHY_PAPD_CAL_ADDRESS		BWN_PHY_N(0x29F)
#define	BWN_NPHY_PAPD_CAL_YREFEPSILON		BWN_PHY_N(0x2A0)
#define	BWN_NPHY_PAPD_CAL_SETTLE		BWN_PHY_N(0x2A1)
#define	BWN_NPHY_PAPD_CAL_CORRELATE		BWN_PHY_N(0x2A2)
#define	BWN_NPHY_PAPD_CAL_SHIFTS0		BWN_PHY_N(0x2A3)
#define	BWN_NPHY_PAPD_CAL_SHIFTS1		BWN_PHY_N(0x2A4)
#define	BWN_NPHY_SAMPLE_START_ADDR		BWN_PHY_N(0x2A5)
#define	BWN_NPHY_RADAR_ADC_TO_DBM		BWN_PHY_N(0x2A6)
#define	BWN_NPHY_REV3_C2_INITGAIN_A		BWN_PHY_N(0x2A7)
#define	BWN_NPHY_REV3_C2_INITGAIN_B		BWN_PHY_N(0x2A8)
#define	BWN_NPHY_REV3_C2_CLIP_HIGAIN_A		BWN_PHY_N(0x2A9)
#define	BWN_NPHY_REV3_C2_CLIP_HIGAIN_B		BWN_PHY_N(0x2AA)
#define	BWN_NPHY_REV3_C2_CLIP_MEDGAIN_A		BWN_PHY_N(0x2AB)
#define	BWN_NPHY_REV3_C2_CLIP_MEDGAIN_B		BWN_PHY_N(0x2AC)
#define	BWN_NPHY_REV3_C2_CLIP_LOGAIN_A		BWN_PHY_N(0x2AD)
#define	BWN_NPHY_REV3_C2_CLIP_LOGAIN_B		BWN_PHY_N(0x2AE)
#define	BWN_NPHY_REV3_C2_CLIP2_GAIN_A		BWN_PHY_N(0x2AF)
#define	BWN_NPHY_REV3_C2_CLIP2_GAIN_B		BWN_PHY_N(0x2B0)

#define	BWN_NPHY_REV7_RF_CTL_MISC_REG3		BWN_PHY_N(0x340)
#define	BWN_NPHY_REV7_RF_CTL_MISC_REG4		BWN_PHY_N(0x341)
#define	BWN_NPHY_REV7_RF_CTL_OVER3		BWN_PHY_N(0x342)
#define	BWN_NPHY_REV7_RF_CTL_OVER4		BWN_PHY_N(0x343)
#define	BWN_NPHY_REV7_RF_CTL_MISC_REG5		BWN_PHY_N(0x344)
#define	BWN_NPHY_REV7_RF_CTL_MISC_REG6		BWN_PHY_N(0x345)
#define	BWN_NPHY_REV7_RF_CTL_OVER5		BWN_PHY_N(0x346)
#define	BWN_NPHY_REV7_RF_CTL_OVER6		BWN_PHY_N(0x347)

#define	BWN_PHY_B_BBCFG				BWN_PHY_N_BMODE(0x001) /* BB config */
#define	 BWN_PHY_B_BBCFG_RSTCCA			0x4000 /* Reset CCA */
#define	 BWN_PHY_B_BBCFG_RSTRX			0x8000 /* Reset RX */
#define	BWN_PHY_B_TEST				BWN_PHY_N_BMODE(0x00A)

#endif	/* __IF_BWN_PHY_N_REGS_H__ */
