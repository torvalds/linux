/*
 *  linux/drivers/tdkphy.h
 *
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __TDKPHY_H
#define __TDKPHY_H

/*
 * Register definitions for the TDK 78Q2120 PHY 
 * which is on the Camelot board
 */

/*
 * Copyright (c) Altera Corporation 2000.
 * All rights reserved.
 */
#define PHY_CONTROL                                                                                     (0)
#define PHY_CONTROL_COLT_MSK                                                            (0x80)
#define PHY_CONTROL_COLT_OFST                                                           (7)
#define PHY_CONTROL_DUPLEX_MSK                                                          (0x100)
#define PHY_CONTROL_DUPLEX_OFST                                                         (8)
#define PHY_CONTROL_RANEG_MSK                                                           (0x200)
#define PHY_CONTROL_RANEG_OFST                                                          (9)
#define PHY_CONTROL_ISO_MSK                                                                     (0x400)
#define PHY_CONTROL_ISO_OFST                                                            (10)
#define PHY_CONTROL_PWRDN_MSK                                                           (0x800)
#define PHY_CONTROL_PWRDN_OFST                                                          (11)
#define PHY_CONTROL_ANEGEN_MSK                                                          (0x1000)
#define PHY_CONTROL_ANEGEN_OFST                                                         (12)
#define PHY_CONTROL_SPEEDSL_MSK                                                         (0x2000)
#define PHY_CONTROL_SPEEDSL_OFST                                                        (13)
#define PHY_CONTROL_LOOPBK_MSK                                                          (0x4000)
#define PHY_CONTROL_LOOPBK_OFST                                                         (14)
#define PHY_CONTROL_RESET_MSK                                                           (0x8000)
#define PHY_CONTROL_RESET_OFST                                                          (15)

#define PHY_STATUS                                                                                      (1)
#define PHY_STATUS_ETXD_MSK                                                                     (0x1)
#define PHY_STATUS_EXTD_OFST                                                            (0)
#define PHY_STATUS_JAB_MSK                                                                      (0x2)
#define PHY_STATUS_JAB_OFST                                                                     (1)
#define PHY_STATUS_LINK_MSK                                                                     (0x4)
#define PHY_STATUS_LINK_OFST                                                            (2)
#define PHY_STATUS_ANEGA_MSK                                                            (0x8)
#define PHY_STATUS_ANEGA_OFST                                                           (3)
#define PHY_STATUS_RFAULT_MSK                                                           (0x10)
#define PHY_STATUS_RFAULT_OFST                                                          (4)
#define PHY_STATUS_ANEGC_MSK                                                            (0x20)
#define PHY_STATUS_ANEGC_OFST                                                           (5)
#define PHY_STATUS_10T_H_MSK                                                            (0x800)
#define PHY_STATUS_10T_H_OFST                                                           (11)
#define PHY_STATUS_10T_F_MSK                                                            (0x1000)
#define PHY_STATUS_10T_F_OFST                                                           (12)
#define PHY_STATUS_100_X_H_MSK                                                          (0x2000)
#define PHY_STATUS_100_X_H_OFST                                                         (13)
#define PHY_STATUS_100_X_F_MSK                                                          (0x4000)
#define PHY_STATUS_100_X_F_OFST                                                         (14)
#define PHY_STATUS_100T4_MSK                                                            (0x8000)
#define PHY_STATUS_100T4_OFST                                                           (15)

#define PHY_ID1                                                                                         (2)
#define PHY_ID1_OUI_MSK                                                                         (0xFFFF)
#define PHY_ID1_OUI_OFST                                                                        (0)

#define PHY_ID2                                                                                         (3)
#define PHY_ID2_RN_MSK                                                                          (0xF)
#define PHY_ID2_RN_OFST                                                                         (0)
#define PHY_ID2_MN_MSK                                                                          (0x3F0)
#define PHY_ID2_MN_OFST                                                                         (4)
#define PHY_ID2_OUI_MSK                                                                         (0xFC00)
#define PHY_ID2_OUI_OFST                                                                        (10)

#define PHY_AUTO_NEG_ADVERTISEMENT                                                      (4)
#define PHY_AUTO_NEG_ADVERTISEMENT_SELECTOR_MSK                         (0x1F)
#define PHY_AUTO_NEG_ADVERTISEMENT_SELECTOR_OFST                        (0)
#define PHY_AUTO_NEG_ADVERTISEMENT_A0_MSK                                       (0x20)
#define PHY_AUTO_NEG_ADVERTISEMENT_A0_OFST                                      (5)
#define PHY_AUTO_NEG_ADVERTISEMENT_A1_MSK                                       (0x40)
#define PHY_AUTO_NEG_ADVERTISEMENT_A1_OFST                                      (6)
#define PHY_AUTO_NEG_ADVERTISEMENT_A2_MSK                                       (0x80)
#define PHY_AUTO_NEG_ADVERTISEMENT_A2_OFST                                      (7)
#define PHY_AUTO_NEG_ADVERTISEMENT_A3_MSK                                       (0x100)
#define PHY_AUTO_NEG_ADVERTISEMENT_A3_OFST                                      (8)
#define PHY_AUTO_NEG_ADVERTISEMENT_A4_MSK                                       (0x200)
#define PHY_AUTO_NEG_ADVERTISEMENT_A4_OFST                                      (9)
#define PHY_AUTO_NEG_ADVERTISEMENT_TAF_MSK                                      (0x1FE0)
#define PHY_AUTO_NEG_ADVERTISEMENT_TAF_OFST                                     (5)
#define PHY_AUTO_NEG_ADVERTISEMENT_RF_MSK                                       (0x2000)
#define PHY_AUTO_NEG_ADVERTISEMENT_RF_OFST                                      (13)
#define PHY_AUTO_NEG_ADVERTISEMENT_RSVD_MSK                                     (0x4000)
#define PHY_AUTO_NEG_ADVERTISEMENT_RVSD_OFST                            (14)
#define PHY_AUTO_NEG_ADVERTISEMENT_NP_MSK                                       (0x8000)
#define PHY_AUTO_NEG_ADVERTISEMENT_NP_OFST                                      (15)

#define PHY_AUTO_NEG_LINK_PARTNER                                                       (5)
#define PHY_AUTO_NEG_LINK_PARTNER_S4_MSK                                        (0x1F)
#define PHY_AUTO_NEG_LINK_PARTNER_S4_OFST                                       (0)
#define PHY_AUTO_NEG_LINK_PARTNER_A7_MSK                                        (0x1FE0)
#define PHY_AUTO_NEG_LINK_PARTNER_A7_OFST                                       (5)
#define PHY_AUTO_NEG_LINK_PARTNER_RF_MSK                                        (0x2000)
#define PHY_AUTO_NEG_LINK_PARTNER_RF_OFST                                       (13)
#define PHY_AUTO_NEG_LINK_PARTNER_ACK_MSK                                       (0x4000)
#define PHY_AUTO_NEG_LINK_PARTNER_ACK_OFST                                      (14)
#define PHY_AUTO_NEG_LINK_PARTNER_NP_MSK                                        (0x8000)
#define PHY_AUTO_NEG_LINK_PARTNER_NP_OFST                                       (15)

#define PHY_AUTO_NEG_EXPANSION                                                          (6)
#define PHY_AUTO_NEG_EXPANSION_LPANEGA_MSK                                      (0x1)
#define PHY_AUTO_NEG_EXPANSION_LPANEGA_OFST                                     (0)
#define PHY_AUTO_NEG_EXPANSION_PRX_MSK                                          (0x2)
#define PHY_AUTO_NEG_EXPANSION_PRX_OFST                                         (1)
#define PHY_AUTO_NEG_EXPANSION_NPA_MSK                                          (0x4)
#define PHY_AUTO_NEG_EXPANSION_NPA_OFST                                         (2)
#define PHY_AUTO_NEG_EXPANSION_LPNPA_MSK                                        (0x8)
#define PHY_AUTO_NEG_EXPANSION_LPNPA_OFST                                       (3)
#define PHY_AUTO_NEG_EXPANSION_PDF_MSK                                          (0x10)
#define PHY_AUTO_NEG_EXPANSION_PDF_OFST                                         (4)

#define PHY_VENDOR_SPECIFIC                                                                     (16)
#define PHY_VENDOR_SPECIFIC_RXCC_MSK                                            (0x1)
#define PHY_VENDOR_SPECIFIC_RXCC_OFST                                           (0)
#define PHY_VENDOR_SPECIFIC_PCSBP_MSK                                           (0x2)
#define PHY_VENDOR_SPECIFIC_PCSBP_OFST                                          (1)
#define PHY_VENDOR_SPECIFIC_RVSPOL_MSK                                          (0x10)
#define PHY_VENDOR_SPECIFIC_RVSPOL_OFST                                         (4)
#define PHY_VENDOR_SPECIFIC_APOL_MSK                                            (0x20)  
#define PHY_VENDOR_SPECIFIC_APOL_OFST                                           (5)
#define PHY_VENDOR_SPECIFIC_GPIO0_DIR_MSK                                       (0x40)
#define PHY_VENDOR_SPECIFIC_GPIO0_DIR_OFST                                      (6)
#define PHY_VENDOR_SPECIFIC_GPIO0_DAT_MSK                                       (0x80)
#define PHY_VENDOR_SPECIFIC_GPIO0_DAT_OFST                                      (7)
#define PHY_VENDOR_SPECIFIC_GPIO1_DIR_MSK                                       (0x100)
#define PHY_VENDOR_SPECIFIC_GPIO1_DIR_OFST                                      (8)
#define PHY_VENDOR_SPECIFIC_GPIO1_DAT_MSK                                       (0x200)
#define PHY_VENDOR_SPECIFIC_GPIO1_DAT_OFST                                      (9)
#define PHY_VENDOR_SPECIFIC_10BT_NATURAL_LOOPBACK_DAT_MSK       (0x400)
#define PHY_VENDOR_SPECIFIC_10BT_NATURAL_LOOPBACK_DAT_OFST      (10)
#define PHY_VENDOR_SPECIFIC_10BT_SQE_TEST_INHIBIT_MSK           (0x800)
#define PHY_VENDOR_SPECIFIC_10BT_SQE_TEST_INHIBIT_OFST          (11)
#define PHY_VENDOR_SPECIFIC_TXHIM_MSK                                           (0x1000)
#define PHY_VENDOR_SPECIFIC_TXHIM_OFST                                          (12)
#define PHY_VENDOR_SPECIFIC_INT_LEVEL_MSK                                       (0x4000)
#define PHY_VENDOR_SPECIFIC_INT_LEVEL_OFST                                      (14)
#define PHY_VENDOR_SPECIFIC_RPTR_MSK                                            (0x8000)
#define PHY_VENDOR_SPECIFIC_RPTR_OFST                                           (15)

#define PHY_IRQ_CONTROL                                                                         (17)
#define PHY_IRQ_CONTROL_ANEG_COMP_INT_MSK                                       (0x1)
#define PHY_IRQ_CONTROL_ANEG_COMP_INT_OFST                                      (0)
#define PHY_IRQ_CONTROL_RFAULT_INT_MSK                                          (0x2)
#define PHY_IRQ_CONTROL_RFAULT_INT_OFST                                         (1)
#define PHY_IRQ_CONTROL_LS_CHG_INT_MSK                                          (0x4)
#define PHY_IRQ_CONTROL_LS_CHG_INT_OFST                                         (2)
#define PHY_IRQ_CONTROL_LP_ACK_INT_MSK                                          (0x8)
#define PHY_IRQ_CONTROL_LP_ACK_INT_OFST                                         (3)
#define PHY_IRQ_CONTROL_PDF_INT_MSK                                                     (0x10)
#define PHY_IRQ_CONTROL_PDF_INT_OFST                                            (4)
#define PHY_IRQ_CONTROL_PRX_INT_MSK                                                     (0x20)
#define PHY_IRQ_CONTROL_PRX_INT_OFST                                            (5)
#define PHY_IRQ_CONTROL_RXER_INT_MSK                                            (0x40)
#define PHY_IRQ_CONTROL_RXER_INT_OFST                                           (6)
#define PHY_IRQ_CONTROL_JABBER_INT_MSK                                          (0x80)
#define PHY_IRQ_CONTROL_JABBER_INT_OFST                                         (7)
#define PHY_IRQ_CONTROL_ANEG_COMP_IE_MSK                                        (0x100)
#define PHY_IRQ_CONTROL_ANEG_COMP_IE_OFST                                       (8)
#define PHY_IRQ_CONTROL_RFAULT_IE_MSK                                           (0x200)
#define PHY_IRQ_CONTROL_RFAULT_IE_OFST                                          (9)
#define PHY_IRQ_CONTROL_LS_CHG_IE_MSK                                           (0x400)
#define PHY_IRQ_CONTROL_LS_CHG_IE_OFST                                          (10)
#define PHY_IRQ_CONTROL_LP_ACK_IE_MSK                                           (0x800)
#define PHY_IRQ_CONTROL_LP_ACK_IE_OFST                                          (11)
#define PHY_IRQ_CONTROL_PDF_IE_MSK                                                      (0x1000)
#define PHY_IRQ_CONTROL_PDF_IE_OFST                                                     (12)
#define PHY_IRQ_CONTROL_PRX_IE_MSK                                                      (0x2000)
#define PHY_IRQ_CONTROL_PRX_IE_OFST                                                     (13)
#define PHY_IRQ_CONTROL_RXER_IE_MSK                                                     (0x4000)
#define PHY_IRQ_CONTROL_RXER_IE_OFST                                            (14)
#define PHY_IRQ_CONTROL_JABBER_IE_MSK                                           (0x8000)
#define PHY_IRQ_CONTROL_JABBER_IE_OFST                                          (15)

#define PHY_DIAGNOSTIC                                                                          (18)
#define PHY_DIAGNOSTIC_RX_LOCK_MSK                                                      (0x100)
#define PHY_DIAGNOSTIC_RX_LOCK_OFST                                                     (8)
#define PHY_DIAGNOSTIC_RX_PASS_MSK                                                      (0x200)
#define PHY_DIAGNOSTIC_RX_PASS_OFST                                                     (9)
#define PHY_DIAGNOSTIC_RATE_MSK                                                         (0x400)
#define PHY_DIAGNOSTIC_RATE_OFST                                                        (10)
#define PHY_DIAGNOSTIC_DPLX_MSK                                                         (0x800)
#define PHY_DIAGNOSTIC_DPLX_OFST                                                        (11)
#define PHY_DIAGNOSTIC_ANEGF_MSK                                                        (0x1000)
#define PHY_DIAGNOSTIC_ANEGF_OFST                                                       (12)

#endif /* __TDKPHY_H */
