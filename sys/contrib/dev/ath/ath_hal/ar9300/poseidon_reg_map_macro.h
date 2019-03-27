/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*                                                                           */
/* File:       /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/poseidon_reg_map_macro.h*/
/* Creator:    kcwo                                                          */
/* Time:       Tuesday Nov 2, 2010 [5:38:25 pm]                              */
/*                                                                           */
/* Path:       /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top   */
/* Arguments:  /cad/denali/blueprint/3.7.3//Linux-64bit/blueprint -codegen   */
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/env/blueprint/ath_ansic.codegen*/
/*             -ath_ansic -Wdesc -I                                          */
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top -I*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint -I    */
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/env/blueprint -I*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig*/
/*             -odir                                                         */
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top   */
/*             -eval {$INCLUDE_SYSCONFIG_FILES=1} -eval                      */
/*             $WAR_EV58615_for_ansic_codegen=1 poseidon_reg.rdl             */
/*                                                                           */
/* Sources:    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/rtc/blueprint/rtc_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_pcu_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/host_intf/rtl/blueprint/host_intf_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/merlin2_0_radio_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/rtc_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dma_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/poseidon_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/efuse_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/merlin2_0_radio_reg_map.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dcu_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_dma_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_pcu/blueprint/mac_pcu_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/bb/blueprint/bb_reg_map.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_qcu_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/apb_analog/analog_intf_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/emulation_misc.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/pcie_phy_reg_csr.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/analog_intf_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/svd_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/poseidon_radio_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/host_intf/rtl/blueprint/efuse_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/amba_mac/svd/blueprint/svd_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_qcu_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/radio_65_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/rtc_sync_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/amba_mac/blueprint/rtc_sync_reg.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/pcie_phy_reg_csr_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/bb_reg_map_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_dcu_reg_sysconfig.rdl*/
/*             /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/env/blueprint/ath_ansic.pm*/
/*             /cad/local/lib/perl/Pinfo.pm                                  */
/*                                                                           */
/* Blueprint:   3.7.3 (Fri Aug 29 12:39:16 PDT 2008)                         */
/* Machine:    zydasc19                                                      */
/* OS:         Linux 2.6.9-78.0.8.ELsmp                                      */
/* Description:                                                              */
/*                                                                           */
/*This Register Map contains the complete register set for Poseidon.         */
/*                                                                           */
/* Copyright (C) 2010 Denali Software Inc.  All rights reserved              */
/* THIS FILE IS AUTOMATICALLY GENERATED BY DENALI BLUEPRINT, DO NOT EDIT     */
/*                                                                           */


#ifndef __REG_POSEIDON_REG_MAP_MACRO_H__
#define __REG_POSEIDON_REG_MAP_MACRO_H__

/* macros for BlueprintGlobalNameSpace::AXI_INTERCONNECT_CTRL */
#ifndef __AXI_INTERCONNECT_CTRL_MACRO__
#define __AXI_INTERCONNECT_CTRL_MACRO__

/* macros for field FORCE_SEL_ON */
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__SHIFT                            0
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__WIDTH                            1
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__MASK                   0x00000001U
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__READ(src) \
                    (u_int32_t)(src)\
                    & 0x00000001U
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x00000001U)
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | ((u_int32_t)(src) &\
                    0x00000001U)
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x00000001U)))
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(1)
#define AXI_INTERCONNECT_CTRL__FORCE_SEL_ON__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(0)

/* macros for field SELECT_SLV_PCIE */
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__SHIFT                         1
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__WIDTH                         1
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__MASK                0x00000002U
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000002U) >> 1)
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 1) & 0x00000002U)
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | (((u_int32_t)(src) <<\
                    1) & 0x00000002U)
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 1) & ~0x00000002U)))
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | ((u_int32_t)(1) << 1)
#define AXI_INTERCONNECT_CTRL__SELECT_SLV_PCIE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | ((u_int32_t)(0) << 1)

/* macros for field SW_WOW_ENABLE */
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__SHIFT                           2
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__WIDTH                           1
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__MASK                  0x00000004U
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000004U) >> 2)
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 2) & 0x00000004U)
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000004U) | (((u_int32_t)(src) <<\
                    2) & 0x00000004U)
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 2) & ~0x00000004U)))
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000004U) | ((u_int32_t)(1) << 2)
#define AXI_INTERCONNECT_CTRL__SW_WOW_ENABLE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000004U) | ((u_int32_t)(0) << 2)
#define AXI_INTERCONNECT_CTRL__TYPE                                   u_int32_t
#define AXI_INTERCONNECT_CTRL__READ                                 0x00000007U
#define AXI_INTERCONNECT_CTRL__WRITE                                0x00000007U

#endif /* __AXI_INTERCONNECT_CTRL_MACRO__ */


/* macros for host_intf_reg_block.AXI_INTERCONNECT_CTRL */
#define INST_HOST_INTF_REG_BLOCK__AXI_INTERCONNECT_CTRL__NUM                  1

/* macros for BlueprintGlobalNameSpace::green_tx_control_1 */
#ifndef __GREEN_TX_CONTROL_1_MACRO__
#define __GREEN_TX_CONTROL_1_MACRO__

/* macros for field green_tx_enable */
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__SHIFT                            0
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__WIDTH                            1
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__MASK                   0x00000001U
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__READ(src) \
                    (u_int32_t)(src)\
                    & 0x00000001U
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x00000001U)
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | ((u_int32_t)(src) &\
                    0x00000001U)
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x00000001U)))
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(1)
#define GREEN_TX_CONTROL_1__GREEN_TX_ENABLE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(0)

/* macros for field green_cases */
#define GREEN_TX_CONTROL_1__GREEN_CASES__SHIFT                                1
#define GREEN_TX_CONTROL_1__GREEN_CASES__WIDTH                                1
#define GREEN_TX_CONTROL_1__GREEN_CASES__MASK                       0x00000002U
#define GREEN_TX_CONTROL_1__GREEN_CASES__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000002U) >> 1)
#define GREEN_TX_CONTROL_1__GREEN_CASES__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 1) & 0x00000002U)
#define GREEN_TX_CONTROL_1__GREEN_CASES__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | (((u_int32_t)(src) <<\
                    1) & 0x00000002U)
#define GREEN_TX_CONTROL_1__GREEN_CASES__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 1) & ~0x00000002U)))
#define GREEN_TX_CONTROL_1__GREEN_CASES__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | ((u_int32_t)(1) << 1)
#define GREEN_TX_CONTROL_1__GREEN_CASES__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000002U) | ((u_int32_t)(0) << 1)
#define GREEN_TX_CONTROL_1__TYPE                                      u_int32_t
#define GREEN_TX_CONTROL_1__READ                                    0x00000003U
#define GREEN_TX_CONTROL_1__WRITE                                   0x00000003U

#endif /* __GREEN_TX_CONTROL_1_MACRO__ */

/* macros for BlueprintGlobalNameSpace::bb_reg_page_control */
#ifndef __BB_REG_PAGE_CONTROL_MACRO__
#define __BB_REG_PAGE_CONTROL_MACRO__

/* macros for field disable_bb_reg_page */
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__SHIFT                       0
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__WIDTH                       1
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__MASK              0x00000001U
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__READ(src) \
                    (u_int32_t)(src)\
                    & 0x00000001U
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x00000001U)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | ((u_int32_t)(src) &\
                    0x00000001U)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x00000001U)))
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(1)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(0)

/* macros for field bb_register_page */
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__SHIFT                          1
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__WIDTH                          3
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__MASK                 0x0000000eU
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000000eU) >> 1)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 1) & 0x0000000eU)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000000eU) | (((u_int32_t)(src) <<\
                    1) & 0x0000000eU)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 1) & ~0x0000000eU)))

/* macros for field direct_access_page */
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__SHIFT                        4
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__WIDTH                        1
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__MASK               0x00000010U
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000010U) >> 4)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 4) & 0x00000010U)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | (((u_int32_t)(src) <<\
                    4) & 0x00000010U)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 4) & ~0x00000010U)))
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | ((u_int32_t)(1) << 4)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | ((u_int32_t)(0) << 4)
#define BB_REG_PAGE_CONTROL__TYPE                                     u_int32_t
#define BB_REG_PAGE_CONTROL__READ                                   0x0000001fU
#define BB_REG_PAGE_CONTROL__WRITE                                  0x0000001fU

#endif /* __BB_REG_PAGE_CONTROL_MACRO__ */


/* macros for bb_reg_block.bb_bbb_reg_map.BB_bb_reg_page_control */
#define INST_BB_REG_BLOCK__BB_BBB_REG_MAP__BB_BB_REG_PAGE_CONTROL__NUM        1

/* macros for BlueprintGlobalNameSpace::peak_det_ctrl_1 */

/* macros for field peak_det_tally_thr_low_0 */
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__SHIFT                      8
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__WIDTH                      5
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__MASK             0x00001f00U
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00001f00U) >> 8)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 8) & 0x00001f00U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00001f00U) | (((u_int32_t)(src) <<\
                    8) & 0x00001f00U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_LOW_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 8) & ~0x00001f00U)))

/* macros for field peak_det_tally_thr_med_0 */
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__SHIFT                     13
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__WIDTH                      5
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__MASK             0x0003e000U
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0003e000U) >> 13)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 13) & 0x0003e000U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003e000U) | (((u_int32_t)(src) <<\
                    13) & 0x0003e000U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_MED_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 13) & ~0x0003e000U)))

/* macros for field peak_det_tally_thr_high_0 */
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__SHIFT                    18
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__WIDTH                     5
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__MASK            0x007c0000U
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x007c0000U) >> 18)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 18) & 0x007c0000U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x007c0000U) | (((u_int32_t)(src) <<\
                    18) & 0x007c0000U)
#define PEAK_DET_CTRL_1__PEAK_DET_TALLY_THR_HIGH_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 18) & ~0x007c0000U)))

/* macros for bb_reg_block.bb_agc_reg_map.BB_peak_det_ctrl_1 */


/* macros for BlueprintGlobalNameSpace::peak_det_ctrl_2 */

/* macros for field rf_gain_drop_db_low_0 */
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__SHIFT                        10
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__WIDTH                         5
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__MASK                0x00007c00U
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00007c00U) >> 10)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 10) & 0x00007c00U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00007c00U) | (((u_int32_t)(src) <<\
                    10) & 0x00007c00U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_LOW_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 10) & ~0x00007c00U)))

/* macros for field rf_gain_drop_db_med_0 */
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__SHIFT                        15
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__WIDTH                         5
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__MASK                0x000f8000U
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000f8000U) >> 15)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 15) & 0x000f8000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000f8000U) | (((u_int32_t)(src) <<\
                    15) & 0x000f8000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_MED_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 15) & ~0x000f8000U)))

/* macros for field rf_gain_drop_db_high_0 */
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__SHIFT                       20
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__WIDTH                        5
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__MASK               0x01f00000U
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x01f00000U) >> 20)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 20) & 0x01f00000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x01f00000U) | (((u_int32_t)(src) <<\
                    20) & 0x01f00000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_HIGH_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x01f00000U)))

/* macros for field rf_gain_drop_db_non_0 */
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__SHIFT                        25
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__WIDTH                         5
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__MASK                0x3e000000U
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x3e000000U) >> 25)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 25) & 0x3e000000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x3e000000U) | (((u_int32_t)(src) <<\
                    25) & 0x3e000000U)
#define PEAK_DET_CTRL_2__RF_GAIN_DROP_DB_NON_0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 25) & ~0x3e000000U)))

/* macros for field enable_rfsat_restart */
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__SHIFT                         30
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__WIDTH                          1
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__MASK                 0x40000000U
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x40000000U) >> 30)
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 30) & 0x40000000U)
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | (((u_int32_t)(src) <<\
                    30) & 0x40000000U)
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 30) & ~0x40000000U)))
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | ((u_int32_t)(1) << 30)
#define PEAK_DET_CTRL_2__ENABLE_RFSAT_RESTART__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | ((u_int32_t)(0) << 30)
#define PEAK_DET_CTRL_2__TYPE                                         u_int32_t
#define PEAK_DET_CTRL_2__READ                                       0x7fffffffU
#define PEAK_DET_CTRL_2__WRITE                                      0x7fffffffU

/* macros for bb_reg_block.bb_agc_reg_map.BB_peak_det_ctrl_2 */

/* macros for BlueprintGlobalNameSpace::bt_coex_1 */
#ifndef __BT_COEX_1_MACRO__
#define __BT_COEX_1_MACRO__

/* macros for field peak_det_tally_thr_low_1 */
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__SHIFT                            0
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__WIDTH                            5
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__MASK                   0x0000001fU
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000001fU
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000001fU)
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000001fU) | ((u_int32_t)(src) &\
                    0x0000001fU)
#define BT_COEX_1__PEAK_DET_TALLY_THR_LOW_1__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000001fU)))

/* macros for field peak_det_tally_thr_med_1 */
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__SHIFT                            5
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__WIDTH                            5
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__MASK                   0x000003e0U
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000003e0U) >> 5)
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 5) & 0x000003e0U)
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000003e0U) | (((u_int32_t)(src) <<\
                    5) & 0x000003e0U)
#define BT_COEX_1__PEAK_DET_TALLY_THR_MED_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 5) & ~0x000003e0U)))

/* macros for field peak_det_tally_thr_high_1 */
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__SHIFT                          10
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__WIDTH                           5
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__MASK                  0x00007c00U
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00007c00U) >> 10)
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 10) & 0x00007c00U)
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00007c00U) | (((u_int32_t)(src) <<\
                    10) & 0x00007c00U)
#define BT_COEX_1__PEAK_DET_TALLY_THR_HIGH_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 10) & ~0x00007c00U)))

/* macros for field rf_gain_drop_db_low_1 */
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__SHIFT                              15
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__WIDTH                               5
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__MASK                      0x000f8000U
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000f8000U) >> 15)
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 15) & 0x000f8000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000f8000U) | (((u_int32_t)(src) <<\
                    15) & 0x000f8000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_LOW_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 15) & ~0x000f8000U)))

/* macros for field rf_gain_drop_db_med_1 */
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__SHIFT                              20
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__WIDTH                               5
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__MASK                      0x01f00000U
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x01f00000U) >> 20)
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 20) & 0x01f00000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x01f00000U) | (((u_int32_t)(src) <<\
                    20) & 0x01f00000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_MED_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x01f00000U)))

/* macros for field rf_gain_drop_db_high_1 */
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__SHIFT                             25
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__WIDTH                              5
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__MASK                     0x3e000000U
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x3e000000U) >> 25)
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 25) & 0x3e000000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x3e000000U) | (((u_int32_t)(src) <<\
                    25) & 0x3e000000U)
#define BT_COEX_1__RF_GAIN_DROP_DB_HIGH_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 25) & ~0x3e000000U)))

/* macros for field bt_tx_disable_NF_cal */
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__SHIFT                               30
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__WIDTH                                1
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__MASK                       0x40000000U
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x40000000U) >> 30)
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 30) & 0x40000000U)
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | (((u_int32_t)(src) <<\
                    30) & 0x40000000U)
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 30) & ~0x40000000U)))
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | ((u_int32_t)(1) << 30)
#define BT_COEX_1__BT_TX_DISABLE_NF_CAL__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x40000000U) | ((u_int32_t)(0) << 30)
#define BT_COEX_1__TYPE                                               u_int32_t
#define BT_COEX_1__READ                                             0x7fffffffU
#define BT_COEX_1__WRITE                                            0x7fffffffU

#endif /* __BT_COEX_1_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_bt_coex_1 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_BT_COEX_1__NUM                  1

/* macros for BlueprintGlobalNameSpace::bt_coex_2 */
#ifndef __BT_COEX_2_MACRO__
#define __BT_COEX_2_MACRO__

/* macros for field peak_det_tally_thr_low_2 */
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__SHIFT                            0
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__WIDTH                            5
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__MASK                   0x0000001fU
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000001fU
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000001fU)
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000001fU) | ((u_int32_t)(src) &\
                    0x0000001fU)
#define BT_COEX_2__PEAK_DET_TALLY_THR_LOW_2__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000001fU)))

/* macros for field peak_det_tally_thr_med_2 */
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__SHIFT                            5
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__WIDTH                            5
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__MASK                   0x000003e0U
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000003e0U) >> 5)
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 5) & 0x000003e0U)
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000003e0U) | (((u_int32_t)(src) <<\
                    5) & 0x000003e0U)
#define BT_COEX_2__PEAK_DET_TALLY_THR_MED_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 5) & ~0x000003e0U)))

/* macros for field peak_det_tally_thr_high_2 */
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__SHIFT                          10
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__WIDTH                           5
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__MASK                  0x00007c00U
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00007c00U) >> 10)
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 10) & 0x00007c00U)
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00007c00U) | (((u_int32_t)(src) <<\
                    10) & 0x00007c00U)
#define BT_COEX_2__PEAK_DET_TALLY_THR_HIGH_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 10) & ~0x00007c00U)))

/* macros for field rf_gain_drop_db_low_2 */
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__SHIFT                              15
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__WIDTH                               5
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__MASK                      0x000f8000U
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000f8000U) >> 15)
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 15) & 0x000f8000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000f8000U) | (((u_int32_t)(src) <<\
                    15) & 0x000f8000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_LOW_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 15) & ~0x000f8000U)))

/* macros for field rf_gain_drop_db_med_2 */
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__SHIFT                              20
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__WIDTH                               5
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__MASK                      0x01f00000U
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x01f00000U) >> 20)
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 20) & 0x01f00000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x01f00000U) | (((u_int32_t)(src) <<\
                    20) & 0x01f00000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_MED_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x01f00000U)))

/* macros for field rf_gain_drop_db_high_2 */
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__SHIFT                             25
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__WIDTH                              5
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__MASK                     0x3e000000U
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x3e000000U) >> 25)
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 25) & 0x3e000000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x3e000000U) | (((u_int32_t)(src) <<\
                    25) & 0x3e000000U)
#define BT_COEX_2__RF_GAIN_DROP_DB_HIGH_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 25) & ~0x3e000000U)))

/* macros for field rfsat_rx_rx */
#define BT_COEX_2__RFSAT_RX_RX__SHIFT                                        30
#define BT_COEX_2__RFSAT_RX_RX__WIDTH                                         2
#define BT_COEX_2__RFSAT_RX_RX__MASK                                0xc0000000U
#define BT_COEX_2__RFSAT_RX_RX__READ(src) \
                    (((u_int32_t)(src)\
                    & 0xc0000000U) >> 30)
#define BT_COEX_2__RFSAT_RX_RX__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 30) & 0xc0000000U)
#define BT_COEX_2__RFSAT_RX_RX__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xc0000000U) | (((u_int32_t)(src) <<\
                    30) & 0xc0000000U)
#define BT_COEX_2__RFSAT_RX_RX__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 30) & ~0xc0000000U)))
#define BT_COEX_2__TYPE                                               u_int32_t
#define BT_COEX_2__READ                                             0xffffffffU
#define BT_COEX_2__WRITE                                            0xffffffffU

#endif /* __BT_COEX_2_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_bt_coex_2 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_BT_COEX_2__NUM                  1

/* macros for BlueprintGlobalNameSpace::bt_coex_3 */
#ifndef __BT_COEX_3_MACRO__
#define __BT_COEX_3_MACRO__

/* macros for field rfsat_bt_srch_srch */
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__SHIFT                                  0
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__WIDTH                                  2
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__MASK                         0x00000003U
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__READ(src) (u_int32_t)(src) & 0x00000003U
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x00000003U)
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000003U) | ((u_int32_t)(src) &\
                    0x00000003U)
#define BT_COEX_3__RFSAT_BT_SRCH_SRCH__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x00000003U)))

/* macros for field rfsat_bt_rx_srch */
#define BT_COEX_3__RFSAT_BT_RX_SRCH__SHIFT                                    2
#define BT_COEX_3__RFSAT_BT_RX_SRCH__WIDTH                                    2
#define BT_COEX_3__RFSAT_BT_RX_SRCH__MASK                           0x0000000cU
#define BT_COEX_3__RFSAT_BT_RX_SRCH__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000000cU) >> 2)
#define BT_COEX_3__RFSAT_BT_RX_SRCH__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 2) & 0x0000000cU)
#define BT_COEX_3__RFSAT_BT_RX_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000000cU) | (((u_int32_t)(src) <<\
                    2) & 0x0000000cU)
#define BT_COEX_3__RFSAT_BT_RX_SRCH__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 2) & ~0x0000000cU)))

/* macros for field rfsat_bt_srch_rx */
#define BT_COEX_3__RFSAT_BT_SRCH_RX__SHIFT                                    4
#define BT_COEX_3__RFSAT_BT_SRCH_RX__WIDTH                                    2
#define BT_COEX_3__RFSAT_BT_SRCH_RX__MASK                           0x00000030U
#define BT_COEX_3__RFSAT_BT_SRCH_RX__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000030U) >> 4)
#define BT_COEX_3__RFSAT_BT_SRCH_RX__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 4) & 0x00000030U)
#define BT_COEX_3__RFSAT_BT_SRCH_RX__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000030U) | (((u_int32_t)(src) <<\
                    4) & 0x00000030U)
#define BT_COEX_3__RFSAT_BT_SRCH_RX__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 4) & ~0x00000030U)))

/* macros for field rfsat_wlan_srch_srch */
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__SHIFT                                6
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__WIDTH                                2
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__MASK                       0x000000c0U
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000000c0U) >> 6)
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 6) & 0x000000c0U)
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000000c0U) | (((u_int32_t)(src) <<\
                    6) & 0x000000c0U)
#define BT_COEX_3__RFSAT_WLAN_SRCH_SRCH__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 6) & ~0x000000c0U)))

/* macros for field rfsat_wlan_rx_srch */
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__SHIFT                                  8
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__WIDTH                                  2
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__MASK                         0x00000300U
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000300U) >> 8)
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 8) & 0x00000300U)
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000300U) | (((u_int32_t)(src) <<\
                    8) & 0x00000300U)
#define BT_COEX_3__RFSAT_WLAN_RX_SRCH__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 8) & ~0x00000300U)))

/* macros for field rfsat_wlan_srch_rx */
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__SHIFT                                 10
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__WIDTH                                  2
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__MASK                         0x00000c00U
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000c00U) >> 10)
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 10) & 0x00000c00U)
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000c00U) | (((u_int32_t)(src) <<\
                    10) & 0x00000c00U)
#define BT_COEX_3__RFSAT_WLAN_SRCH_RX__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 10) & ~0x00000c00U)))

/* macros for field rfsat_eq_srch_srch */
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__SHIFT                                 12
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__WIDTH                                  2
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__MASK                         0x00003000U
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00003000U) >> 12)
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 12) & 0x00003000U)
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00003000U) | (((u_int32_t)(src) <<\
                    12) & 0x00003000U)
#define BT_COEX_3__RFSAT_EQ_SRCH_SRCH__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 12) & ~0x00003000U)))

/* macros for field rfsat_eq_rx_srch */
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__SHIFT                                   14
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__WIDTH                                    2
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__MASK                           0x0000c000U
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000c000U) >> 14)
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 14) & 0x0000c000U)
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000c000U) | (((u_int32_t)(src) <<\
                    14) & 0x0000c000U)
#define BT_COEX_3__RFSAT_EQ_RX_SRCH__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 14) & ~0x0000c000U)))

/* macros for field rfsat_eq_srch_rx */
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__SHIFT                                   16
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__WIDTH                                    2
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__MASK                           0x00030000U
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00030000U) >> 16)
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 16) & 0x00030000U)
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00030000U) | (((u_int32_t)(src) <<\
                    16) & 0x00030000U)
#define BT_COEX_3__RFSAT_EQ_SRCH_RX__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 16) & ~0x00030000U)))

/* macros for field rf_gain_drop_db_non_1 */
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__SHIFT                              18
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__WIDTH                               5
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__MASK                      0x007c0000U
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x007c0000U) >> 18)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 18) & 0x007c0000U)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x007c0000U) | (((u_int32_t)(src) <<\
                    18) & 0x007c0000U)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 18) & ~0x007c0000U)))

/* macros for field rf_gain_drop_db_non_2 */
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__SHIFT                              23
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__WIDTH                               5
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__MASK                      0x0f800000U
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0f800000U) >> 23)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 23) & 0x0f800000U)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0f800000U) | (((u_int32_t)(src) <<\
                    23) & 0x0f800000U)
#define BT_COEX_3__RF_GAIN_DROP_DB_NON_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 23) & ~0x0f800000U)))

/* macros for field bt_rx_firpwr_incr */
#define BT_COEX_3__BT_RX_FIRPWR_INCR__SHIFT                                  28
#define BT_COEX_3__BT_RX_FIRPWR_INCR__WIDTH                                   4
#define BT_COEX_3__BT_RX_FIRPWR_INCR__MASK                          0xf0000000U
#define BT_COEX_3__BT_RX_FIRPWR_INCR__READ(src) \
                    (((u_int32_t)(src)\
                    & 0xf0000000U) >> 28)
#define BT_COEX_3__BT_RX_FIRPWR_INCR__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 28) & 0xf0000000U)
#define BT_COEX_3__BT_RX_FIRPWR_INCR__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xf0000000U) | (((u_int32_t)(src) <<\
                    28) & 0xf0000000U)
#define BT_COEX_3__BT_RX_FIRPWR_INCR__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 28) & ~0xf0000000U)))
#define BT_COEX_3__TYPE                                               u_int32_t
#define BT_COEX_3__READ                                             0xffffffffU
#define BT_COEX_3__WRITE                                            0xffffffffU

#endif /* __BT_COEX_3_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_bt_coex_3 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_BT_COEX_3__NUM                  1

/* macros for BlueprintGlobalNameSpace::bt_coex_4 */
#ifndef __BT_COEX_4_MACRO__
#define __BT_COEX_4_MACRO__

/* macros for field rfgain_eqv_lna_0 */
#define BT_COEX_4__RFGAIN_EQV_LNA_0__SHIFT                                    0
#define BT_COEX_4__RFGAIN_EQV_LNA_0__WIDTH                                    8
#define BT_COEX_4__RFGAIN_EQV_LNA_0__MASK                           0x000000ffU
#define BT_COEX_4__RFGAIN_EQV_LNA_0__READ(src)   (u_int32_t)(src) & 0x000000ffU
#define BT_COEX_4__RFGAIN_EQV_LNA_0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x000000ffU)
#define BT_COEX_4__RFGAIN_EQV_LNA_0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000000ffU) | ((u_int32_t)(src) &\
                    0x000000ffU)
#define BT_COEX_4__RFGAIN_EQV_LNA_0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x000000ffU)))

/* macros for field rfgain_eqv_lna_1 */
#define BT_COEX_4__RFGAIN_EQV_LNA_1__SHIFT                                    8
#define BT_COEX_4__RFGAIN_EQV_LNA_1__WIDTH                                    8
#define BT_COEX_4__RFGAIN_EQV_LNA_1__MASK                           0x0000ff00U
#define BT_COEX_4__RFGAIN_EQV_LNA_1__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000ff00U) >> 8)
#define BT_COEX_4__RFGAIN_EQV_LNA_1__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 8) & 0x0000ff00U)
#define BT_COEX_4__RFGAIN_EQV_LNA_1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000ff00U) | (((u_int32_t)(src) <<\
                    8) & 0x0000ff00U)
#define BT_COEX_4__RFGAIN_EQV_LNA_1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 8) & ~0x0000ff00U)))

/* macros for field rfgain_eqv_lna_2 */
#define BT_COEX_4__RFGAIN_EQV_LNA_2__SHIFT                                   16
#define BT_COEX_4__RFGAIN_EQV_LNA_2__WIDTH                                    8
#define BT_COEX_4__RFGAIN_EQV_LNA_2__MASK                           0x00ff0000U
#define BT_COEX_4__RFGAIN_EQV_LNA_2__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00ff0000U) >> 16)
#define BT_COEX_4__RFGAIN_EQV_LNA_2__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 16) & 0x00ff0000U)
#define BT_COEX_4__RFGAIN_EQV_LNA_2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00ff0000U) | (((u_int32_t)(src) <<\
                    16) & 0x00ff0000U)
#define BT_COEX_4__RFGAIN_EQV_LNA_2__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 16) & ~0x00ff0000U)))

/* macros for field rfgain_eqv_lna_3 */
#define BT_COEX_4__RFGAIN_EQV_LNA_3__SHIFT                                   24
#define BT_COEX_4__RFGAIN_EQV_LNA_3__WIDTH                                    8
#define BT_COEX_4__RFGAIN_EQV_LNA_3__MASK                           0xff000000U
#define BT_COEX_4__RFGAIN_EQV_LNA_3__READ(src) \
                    (((u_int32_t)(src)\
                    & 0xff000000U) >> 24)
#define BT_COEX_4__RFGAIN_EQV_LNA_3__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 24) & 0xff000000U)
#define BT_COEX_4__RFGAIN_EQV_LNA_3__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xff000000U) | (((u_int32_t)(src) <<\
                    24) & 0xff000000U)
#define BT_COEX_4__RFGAIN_EQV_LNA_3__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 24) & ~0xff000000U)))
#define BT_COEX_4__TYPE                                               u_int32_t
#define BT_COEX_4__READ                                             0xffffffffU
#define BT_COEX_4__WRITE                                            0xffffffffU

#endif /* __BT_COEX_4_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_bt_coex_4 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_BT_COEX_4__NUM                  1

/* macros for BlueprintGlobalNameSpace::bt_coex_5 */
#ifndef __BT_COEX_5_MACRO__
#define __BT_COEX_5_MACRO__

/* macros for field rfgain_eqv_lna_4 */
#define BT_COEX_5__RFGAIN_EQV_LNA_4__SHIFT                                    0
#define BT_COEX_5__RFGAIN_EQV_LNA_4__WIDTH                                    8
#define BT_COEX_5__RFGAIN_EQV_LNA_4__MASK                           0x000000ffU
#define BT_COEX_5__RFGAIN_EQV_LNA_4__READ(src)   (u_int32_t)(src) & 0x000000ffU
#define BT_COEX_5__RFGAIN_EQV_LNA_4__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x000000ffU)
#define BT_COEX_5__RFGAIN_EQV_LNA_4__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000000ffU) | ((u_int32_t)(src) &\
                    0x000000ffU)
#define BT_COEX_5__RFGAIN_EQV_LNA_4__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x000000ffU)))

/* macros for field rfgain_eqv_lna_5 */
#define BT_COEX_5__RFGAIN_EQV_LNA_5__SHIFT                                    8
#define BT_COEX_5__RFGAIN_EQV_LNA_5__WIDTH                                    8
#define BT_COEX_5__RFGAIN_EQV_LNA_5__MASK                           0x0000ff00U
#define BT_COEX_5__RFGAIN_EQV_LNA_5__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000ff00U) >> 8)
#define BT_COEX_5__RFGAIN_EQV_LNA_5__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 8) & 0x0000ff00U)
#define BT_COEX_5__RFGAIN_EQV_LNA_5__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000ff00U) | (((u_int32_t)(src) <<\
                    8) & 0x0000ff00U)
#define BT_COEX_5__RFGAIN_EQV_LNA_5__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 8) & ~0x0000ff00U)))

/* macros for field rfgain_eqv_lna_6 */
#define BT_COEX_5__RFGAIN_EQV_LNA_6__SHIFT                                   16
#define BT_COEX_5__RFGAIN_EQV_LNA_6__WIDTH                                    8
#define BT_COEX_5__RFGAIN_EQV_LNA_6__MASK                           0x00ff0000U
#define BT_COEX_5__RFGAIN_EQV_LNA_6__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00ff0000U) >> 16)
#define BT_COEX_5__RFGAIN_EQV_LNA_6__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 16) & 0x00ff0000U)
#define BT_COEX_5__RFGAIN_EQV_LNA_6__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00ff0000U) | (((u_int32_t)(src) <<\
                    16) & 0x00ff0000U)
#define BT_COEX_5__RFGAIN_EQV_LNA_6__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 16) & ~0x00ff0000U)))

/* macros for field rfgain_eqv_lna_7 */
#define BT_COEX_5__RFGAIN_EQV_LNA_7__SHIFT                                   24
#define BT_COEX_5__RFGAIN_EQV_LNA_7__WIDTH                                    8
#define BT_COEX_5__RFGAIN_EQV_LNA_7__MASK                           0xff000000U
#define BT_COEX_5__RFGAIN_EQV_LNA_7__READ(src) \
                    (((u_int32_t)(src)\
                    & 0xff000000U) >> 24)
#define BT_COEX_5__RFGAIN_EQV_LNA_7__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 24) & 0xff000000U)
#define BT_COEX_5__RFGAIN_EQV_LNA_7__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xff000000U) | (((u_int32_t)(src) <<\
                    24) & 0xff000000U)
#define BT_COEX_5__RFGAIN_EQV_LNA_7__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 24) & ~0xff000000U)))
#define BT_COEX_5__TYPE                                               u_int32_t
#define BT_COEX_5__READ                                             0xffffffffU
#define BT_COEX_5__WRITE                                            0xffffffffU

#endif /* __BT_COEX_5_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_bt_coex_5 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_BT_COEX_5__NUM                  1

/* macros for BlueprintGlobalNameSpace::dc_cal_status_b0 */
#ifndef __DC_CAL_STATUS_B0_MACRO__
#define __DC_CAL_STATUS_B0_MACRO__

/* macros for field offsetC1I_0 */
#define DC_CAL_STATUS_B0__OFFSETC1I_0__SHIFT                                  0
#define DC_CAL_STATUS_B0__OFFSETC1I_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC1I_0__MASK                         0x0000001fU
#define DC_CAL_STATUS_B0__OFFSETC1I_0__READ(src) (u_int32_t)(src) & 0x0000001fU

/* macros for field offsetC1Q_0 */
#define DC_CAL_STATUS_B0__OFFSETC1Q_0__SHIFT                                  5
#define DC_CAL_STATUS_B0__OFFSETC1Q_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC1Q_0__MASK                         0x000003e0U
#define DC_CAL_STATUS_B0__OFFSETC1Q_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000003e0U) >> 5)

/* macros for field offsetC2I_0 */
#define DC_CAL_STATUS_B0__OFFSETC2I_0__SHIFT                                 10
#define DC_CAL_STATUS_B0__OFFSETC2I_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC2I_0__MASK                         0x00007c00U
#define DC_CAL_STATUS_B0__OFFSETC2I_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00007c00U) >> 10)

/* macros for field offsetC2Q_0 */
#define DC_CAL_STATUS_B0__OFFSETC2Q_0__SHIFT                                 15
#define DC_CAL_STATUS_B0__OFFSETC2Q_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC2Q_0__MASK                         0x000f8000U
#define DC_CAL_STATUS_B0__OFFSETC2Q_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x000f8000U) >> 15)

/* macros for field offsetC3I_0 */
#define DC_CAL_STATUS_B0__OFFSETC3I_0__SHIFT                                 20
#define DC_CAL_STATUS_B0__OFFSETC3I_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC3I_0__MASK                         0x01f00000U
#define DC_CAL_STATUS_B0__OFFSETC3I_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x01f00000U) >> 20)

/* macros for field offsetC3Q_0 */
#define DC_CAL_STATUS_B0__OFFSETC3Q_0__SHIFT                                 25
#define DC_CAL_STATUS_B0__OFFSETC3Q_0__WIDTH                                  5
#define DC_CAL_STATUS_B0__OFFSETC3Q_0__MASK                         0x3e000000U
#define DC_CAL_STATUS_B0__OFFSETC3Q_0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x3e000000U) >> 25)
#define DC_CAL_STATUS_B0__TYPE                                        u_int32_t
#define DC_CAL_STATUS_B0__READ                                      0x3fffffffU

#endif /* __DC_CAL_STATUS_B0_MACRO__ */


/* macros for bb_reg_block.bb_agc_reg_map.BB_dc_cal_status_b0 */
#define INST_BB_REG_BLOCK__BB_AGC_REG_MAP__BB_DC_CAL_STATUS_B0__NUM           1

/* macros for BlueprintGlobalNameSpace::bbb_sig_detect */

/* macros for field bbb_mrc_off_no_swap */
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__SHIFT                           23
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__WIDTH                            1
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__MASK                   0x00800000U
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00800000U) >> 23)
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 23) & 0x00800000U)
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00800000U) | (((u_int32_t)(src) <<\
                    23) & 0x00800000U)
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 23) & ~0x00800000U)))
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00800000U) | ((u_int32_t)(1) << 23)
#define BBB_SIG_DETECT__BBB_MRC_OFF_NO_SWAP__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00800000U) | ((u_int32_t)(0) << 23)

#define BBB_SIG_DETECT__TYPE                                          u_int32_t
#define BBB_SIG_DETECT__READ                                        0x80ffffffU
#define BBB_SIG_DETECT__WRITE                                       0x80ffffffU

/* macros for BlueprintGlobalNameSpace::gen_controls */

/* macros for field enable_dac_async_fifo */
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__SHIFT                           11
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__WIDTH                            1
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__MASK                   0x00000800U
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000800U) >> 11)
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 11) & 0x00000800U)
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000800U) | (((u_int32_t)(src) <<\
                    11) & 0x00000800U)
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 11) & ~0x00000800U)))
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000800U) | ((u_int32_t)(1) << 11)
#define GEN_CONTROLS__ENABLE_DAC_ASYNC_FIFO__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000800U) | ((u_int32_t)(0) << 11)


/* macros for field static20_mode_ht40_packet_handling */
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__SHIFT              15
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__WIDTH               1
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__MASK      0x00008000U
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00008000U) >> 15)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 15) & 0x00008000U)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | (((u_int32_t)(src) <<\
                    15) & 0x00008000U)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 15) & ~0x00008000U)))
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | ((u_int32_t)(1) << 15)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_HANDLING__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | ((u_int32_t)(0) << 15)

/* macros for field static20_mode_ht40_packet_error_rpt */
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__SHIFT             16
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__WIDTH              1
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__MASK     0x00010000U
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00010000U) >> 16)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 16) & 0x00010000U)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00010000U) | (((u_int32_t)(src) <<\
                    16) & 0x00010000U)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 16) & ~0x00010000U)))
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00010000U) | ((u_int32_t)(1) << 16)
#define GEN_CONTROLS__STATIC20_MODE_HT40_PACKET_ERROR_RPT__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00010000U) | ((u_int32_t)(0) << 16)

/* macros for field unsupp_ht_rate_threshold */
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__SHIFT                        18
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__WIDTH                         7
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__MASK                0x01fc0000U
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x01fc0000U) >> 18)
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 18) & 0x01fc0000U)
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x01fc0000U) | (((u_int32_t)(src) <<\
                    18) & 0x01fc0000U)
#define GEN_CONTROLS__UNSUPP_HT_RATE_THRESHOLD__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 18) & ~0x01fc0000U)))
#define GEN_CONTROLS__TYPE                                            u_int32_t
#define GEN_CONTROLS__READ                                          0x01fdffffU
#define GEN_CONTROLS__WRITE                                         0x01fdffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_gen_controls */

/* macros for BlueprintGlobalNameSpace::bb_reg_page_control */
#ifndef __BB_REG_PAGE_CONTROL_MACRO__
#define __BB_REG_PAGE_CONTROL_MACRO__

/* macros for field disable_bb_reg_page */
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__SHIFT                       0
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__WIDTH                       1
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__MASK              0x00000001U
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__READ(src) \
                    (u_int32_t)(src)\
                    & 0x00000001U
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x00000001U)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | ((u_int32_t)(src) &\
                    0x00000001U)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x00000001U)))
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(1)
#define BB_REG_PAGE_CONTROL__DISABLE_BB_REG_PAGE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000001U) | (u_int32_t)(0)

/* macros for field bb_register_page */
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__SHIFT                          1
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__WIDTH                          3
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__MASK                 0x0000000eU
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0000000eU) >> 1)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 1) & 0x0000000eU)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000000eU) | (((u_int32_t)(src) <<\
                    1) & 0x0000000eU)
#define BB_REG_PAGE_CONTROL__BB_REGISTER_PAGE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 1) & ~0x0000000eU)))

/* macros for field direct_access_page */
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__SHIFT                        4
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__WIDTH                        1
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__MASK               0x00000010U
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000010U) >> 4)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 4) & 0x00000010U)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | (((u_int32_t)(src) <<\
                    4) & 0x00000010U)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 4) & ~0x00000010U)))
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | ((u_int32_t)(1) << 4)
#define BB_REG_PAGE_CONTROL__DIRECT_ACCESS_PAGE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000010U) | ((u_int32_t)(0) << 4)
#define BB_REG_PAGE_CONTROL__TYPE                                     u_int32_t
#define BB_REG_PAGE_CONTROL__READ                                   0x0000001fU
#define BB_REG_PAGE_CONTROL__WRITE                                  0x0000001fU

#endif /* __BB_REG_PAGE_CONTROL_MACRO__ */


/* macros for bb_reg_block.bb_bbb_reg_map.BB_bb_reg_page_control */
#define INST_BB_REG_BLOCK__BB_BBB_REG_MAP__BB_BB_REG_PAGE_CONTROL__NUM        1

/* macros for BlueprintGlobalNameSpace::spectral_scan */


/* macros for field spectral_scan_compressed_rpt */
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__SHIFT                   31
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__WIDTH                    1
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__MASK           0x80000000U
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x80000000U) >> 31)
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 31) & 0x80000000U)
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | (((u_int32_t)(src) <<\
                    31) & 0x80000000U)
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 31) & ~0x80000000U)))
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(1) << 31)
#define SPECTRAL_SCAN__SPECTRAL_SCAN_COMPRESSED_RPT__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(0) << 31)
#define SPECTRAL_SCAN__TYPE                                           u_int32_t
#define SPECTRAL_SCAN__READ                                         0xffffffffU
#define SPECTRAL_SCAN__WRITE                                        0xffffffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_spectral_scan */

/* macros for BlueprintGlobalNameSpace::search_start_delay */


/* macros for field rx_sounding_enable */
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__SHIFT                        14
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__WIDTH                         1
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__MASK                0x00004000U
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00004000U) >> 14)
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 14) & 0x00004000U)
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00004000U) | (((u_int32_t)(src) <<\
                    14) & 0x00004000U)
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 14) & ~0x00004000U)))
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00004000U) | ((u_int32_t)(1) << 14)
#define SEARCH_START_DELAY__RX_SOUNDING_ENABLE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00004000U) | ((u_int32_t)(0) << 14)

/* macros for field rm_hcsd4svd */
#define SEARCH_START_DELAY__RM_HCSD4SVD__SHIFT                               15
#define SEARCH_START_DELAY__RM_HCSD4SVD__WIDTH                                1
#define SEARCH_START_DELAY__RM_HCSD4SVD__MASK                       0x00008000U
#define SEARCH_START_DELAY__RM_HCSD4SVD__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00008000U) >> 15)
#define SEARCH_START_DELAY__RM_HCSD4SVD__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 15) & 0x00008000U)
#define SEARCH_START_DELAY__RM_HCSD4SVD__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | (((u_int32_t)(src) <<\
                    15) & 0x00008000U)
#define SEARCH_START_DELAY__RM_HCSD4SVD__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 15) & ~0x00008000U)))
#define SEARCH_START_DELAY__RM_HCSD4SVD__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | ((u_int32_t)(1) << 15)
#define SEARCH_START_DELAY__RM_HCSD4SVD__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00008000U) | ((u_int32_t)(0) << 15)
#define SEARCH_START_DELAY__TYPE                                      u_int32_t
#define SEARCH_START_DELAY__READ                                    0x0000ffffU
#define SEARCH_START_DELAY__WRITE                                   0x0000ffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_search_start_delay */

/* macros for BlueprintGlobalNameSpace::frame_control */

/* macros for field en_err_static20_mode_ht40_packet */
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__SHIFT               19
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__WIDTH                1
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__MASK       0x00080000U
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00080000U) >> 19)
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 19) & 0x00080000U)
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | (((u_int32_t)(src) <<\
                    19) & 0x00080000U)
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 19) & ~0x00080000U)))
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | ((u_int32_t)(1) << 19)
#define FRAME_CONTROL__EN_ERR_STATIC20_MODE_HT40_PACKET__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | ((u_int32_t)(0) << 19)

/* macros for bb_reg_block.bb_sm_reg_map.BB_frame_control */

/* macros for BlueprintGlobalNameSpace::switch_table_com1 */

/* macros for field switch_table_com_spdt */
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__SHIFT                      20
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__WIDTH                       4
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__MASK              0x00f00000U
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00f00000U) >> 20)
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 20) & 0x00f00000U)
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00f00000U) | (((u_int32_t)(src) <<\
                    20) & 0x00f00000U)
#define SWITCH_TABLE_COM1__SWITCH_TABLE_COM_SPDT__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x00f00000U)))
#define SWITCH_TABLE_COM1__TYPE                                       u_int32_t
#define SWITCH_TABLE_COM1__READ                                     0x00ffffffU
#define SWITCH_TABLE_COM1__WRITE                                    0x00ffffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_switch_table_com1 */

/* macros for bb_reg_block.bb_sm_reg_map.BB_powertx_rate12 */

/* macros for field use_per_packet_olpc_gain_delta_adj */
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__SHIFT                7
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__WIDTH                1
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__MASK       0x00000080U
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000080U) >> 7)
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 7) & 0x00000080U)
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000080U) | (((u_int32_t)(src) <<\
                    7) & 0x00000080U)
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 7) & ~0x00000080U)))
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000080U) | ((u_int32_t)(1) << 7)
#define POWERTX_MAX__USE_PER_PACKET_OLPC_GAIN_DELTA_ADJ__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000080U) | ((u_int32_t)(0) << 7)
#define POWERTX_MAX__TYPE                                             u_int32_t
#define POWERTX_MAX__READ                                           0x000000c0U
#define POWERTX_MAX__WRITE                                          0x000000c0U

/* macros for bb_reg_block.bb_sm_reg_map.BB_powertx_max */

/* macros for BlueprintGlobalNameSpace::tx_forced_gain */


/* macros for field forced_ob2G */
#define TX_FORCED_GAIN__FORCED_OB2G__SHIFT                                   25
#define TX_FORCED_GAIN__FORCED_OB2G__WIDTH                                    3
#define TX_FORCED_GAIN__FORCED_OB2G__MASK                           0x0e000000U
#define TX_FORCED_GAIN__FORCED_OB2G__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0e000000U) >> 25)
#define TX_FORCED_GAIN__FORCED_OB2G__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 25) & 0x0e000000U)
#define TX_FORCED_GAIN__FORCED_OB2G__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0e000000U) | (((u_int32_t)(src) <<\
                    25) & 0x0e000000U)
#define TX_FORCED_GAIN__FORCED_OB2G__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 25) & ~0x0e000000U)))

/* macros for field forced_db2G */
#define TX_FORCED_GAIN__FORCED_DB2G__SHIFT                                   28
#define TX_FORCED_GAIN__FORCED_DB2G__WIDTH                                    3
#define TX_FORCED_GAIN__FORCED_DB2G__MASK                           0x70000000U
#define TX_FORCED_GAIN__FORCED_DB2G__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x70000000U) >> 28)
#define TX_FORCED_GAIN__FORCED_DB2G__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 28) & 0x70000000U)
#define TX_FORCED_GAIN__FORCED_DB2G__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x70000000U) | (((u_int32_t)(src) <<\
                    28) & 0x70000000U)
#define TX_FORCED_GAIN__FORCED_DB2G__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 28) & ~0x70000000U)))

/* macros for field forced_green_paprd_enable */
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__SHIFT                     31
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__WIDTH                      1
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__MASK             0x80000000U
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x80000000U) >> 31)
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 31) & 0x80000000U)
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | (((u_int32_t)(src) <<\
                    31) & 0x80000000U)
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 31) & ~0x80000000U)))
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(1) << 31)
#define TX_FORCED_GAIN__FORCED_GREEN_PAPRD_ENABLE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(0) << 31)
#define TX_FORCED_GAIN__TYPE                                          u_int32_t
#define TX_FORCED_GAIN__READ                                        0xffffffffU
#define TX_FORCED_GAIN__WRITE                                       0xffffffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_tx_forced_gain */

/* macros for BlueprintGlobalNameSpace::txiqcal_control_0 */


/* macros for field enable_txiq_calibrate */
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__SHIFT                      31
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__WIDTH                       1
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__MASK              0x80000000U
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x80000000U) >> 31)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 31) & 0x80000000U)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | (((u_int32_t)(src) <<\
                    31) & 0x80000000U)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 31) & ~0x80000000U)))
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(1) << 31)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(0) << 31)
#define TXIQCAL_CONTROL_0__TYPE                                       u_int32_t
#define TXIQCAL_CONTROL_0__READ                                     0xffffffffU
#define TXIQCAL_CONTROL_0__WRITE                                    0xffffffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_txiqcal_control_0 */

/* macros for BlueprintGlobalNameSpace::txiqcal_control_0 */

/* macros for field enable_txiq_calibrate */
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__SHIFT                      31
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__WIDTH                       1
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__MASK              0x80000000U
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x80000000U) >> 31)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 31) & 0x80000000U)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | (((u_int32_t)(src) <<\
                    31) & 0x80000000U)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 31) & ~0x80000000U)))
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(1) << 31)
#define TXIQCAL_CONTROL_0__ENABLE_TXIQ_CALIBRATE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x80000000U) | ((u_int32_t)(0) << 31)
#define TXIQCAL_CONTROL_0__TYPE                                       u_int32_t
#define TXIQCAL_CONTROL_0__READ                                     0xffffffffU
#define TXIQCAL_CONTROL_0__WRITE                                    0xffffffffU

/* macros for bb_reg_block.bb_sm_reg_map.BB_txiqcal_control_0 */

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_0_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_0_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_0_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_0_1_b0 */
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_1_B0__PAPRD_PRE_POST_SCALING_0_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_0_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_0_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_0_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_0_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_0_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_1_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_1_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_1_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_1_1_b0 */
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_1_B0__PAPRD_PRE_POST_SCALING_1_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_1_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_1_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_1_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_1_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_1_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_2_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_2_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_2_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_2_1_b0 */
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_1_B0__PAPRD_PRE_POST_SCALING_2_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_2_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_2_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_2_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_2_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_2_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_3_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_3_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_3_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_3_1_b0 */
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_1_B0__PAPRD_PRE_POST_SCALING_3_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_3_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_3_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_3_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_3_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_3_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_4_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_4_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_4_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_4_1_b0 */
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_1_B0__PAPRD_PRE_POST_SCALING_4_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_4_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_4_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_4_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_4_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_4_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_5_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_5_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_5_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_5_1_b0 */
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_1_B0__PAPRD_PRE_POST_SCALING_5_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_5_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_5_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_5_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_5_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_5_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_6_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_6_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_6_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_6_1_b0 */
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_1_B0__PAPRD_PRE_POST_SCALING_6_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_6_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_6_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_6_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_6_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_6_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_7_1_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_7_1_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_7_1_B0_MACRO__

/* macros for field paprd_pre_post_scaling_7_1_b0 */
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_1_B0__PAPRD_PRE_POST_SCALING_7_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_7_1_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_7_1_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_1_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_7_1_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_7_1_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_7_1_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_0_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_0_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_0_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_0_2_b0 */
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_2_B0__PAPRD_PRE_POST_SCALING_0_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_0_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_0_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_0_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_0_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_0_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_1_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_1_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_1_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_1_2_b0 */
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_2_B0__PAPRD_PRE_POST_SCALING_1_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_1_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_1_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_1_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_1_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_1_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_2_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_2_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_2_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_2_2_b0 */
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_2_B0__PAPRD_PRE_POST_SCALING_2_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_2_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_2_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_2_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_2_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_2_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_3_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_3_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_3_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_3_2_b0 */
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_2_B0__PAPRD_PRE_POST_SCALING_3_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_3_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_3_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_3_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_3_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_3_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_4_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_4_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_4_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_4_2_b0 */
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_2_B0__PAPRD_PRE_POST_SCALING_4_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_4_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_4_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_4_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_4_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_4_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_5_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_5_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_5_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_5_2_b0 */
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_2_B0__PAPRD_PRE_POST_SCALING_5_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_5_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_5_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_5_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_5_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_5_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_6_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_6_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_6_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_6_2_b0 */
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_2_B0__PAPRD_PRE_POST_SCALING_6_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_6_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_6_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_6_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_6_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_6_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_7_2_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_7_2_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_7_2_B0_MACRO__

/* macros for field paprd_pre_post_scaling_7_2_b0 */
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_2_B0__PAPRD_PRE_POST_SCALING_7_2_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_7_2_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_7_2_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_2_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_7_2_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_7_2_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_7_2_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_0_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_0_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_0_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_0_3_b0 */
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_3_B0__PAPRD_PRE_POST_SCALING_0_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_0_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_0_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_0_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_0_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_0_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_1_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_1_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_1_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_1_3_b0 */
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_3_B0__PAPRD_PRE_POST_SCALING_1_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_1_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_1_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_1_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_1_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_1_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_2_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_2_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_2_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_2_3_b0 */
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_3_B0__PAPRD_PRE_POST_SCALING_2_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_2_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_2_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_2_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_2_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_2_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_3_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_3_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_3_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_3_3_b0 */
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_3_B0__PAPRD_PRE_POST_SCALING_3_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_3_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_3_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_3_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_3_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_3_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_4_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_4_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_4_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_4_3_b0 */
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_3_B0__PAPRD_PRE_POST_SCALING_4_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_4_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_4_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_4_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_4_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_4_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_5_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_5_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_5_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_5_3_b0 */
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_3_B0__PAPRD_PRE_POST_SCALING_5_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_5_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_5_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_5_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_5_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_5_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_6_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_6_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_6_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_6_3_b0 */
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_3_B0__PAPRD_PRE_POST_SCALING_6_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_6_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_6_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_6_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_6_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_6_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_7_3_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_7_3_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_7_3_B0_MACRO__

/* macros for field paprd_pre_post_scaling_7_3_b0 */
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_3_B0__PAPRD_PRE_POST_SCALING_7_3_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_7_3_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_7_3_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_3_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_7_3_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_7_3_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_7_3_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_0_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_0_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_0_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_0_4_b0 */
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_0_4_B0__PAPRD_PRE_POST_SCALING_0_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_0_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_0_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_0_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_0_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_0_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_0_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_1_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_1_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_1_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_1_4_b0 */
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_1_4_B0__PAPRD_PRE_POST_SCALING_1_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_1_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_1_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_1_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_1_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_1_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_1_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_2_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_2_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_2_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_2_4_b0 */
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_2_4_B0__PAPRD_PRE_POST_SCALING_2_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_2_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_2_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_2_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_2_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_2_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_2_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_3_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_3_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_3_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_3_4_b0 */
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_3_4_B0__PAPRD_PRE_POST_SCALING_3_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_3_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_3_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_3_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_3_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_3_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_3_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_4_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_4_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_4_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_4_4_b0 */
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_4_4_B0__PAPRD_PRE_POST_SCALING_4_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_4_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_4_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_4_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_4_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_4_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_4_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_5_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_5_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_5_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_5_4_b0 */
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_5_4_B0__PAPRD_PRE_POST_SCALING_5_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_5_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_5_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_5_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_5_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_5_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_5_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_6_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_6_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_6_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_6_4_b0 */
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_6_4_B0__PAPRD_PRE_POST_SCALING_6_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_6_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_6_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_6_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_6_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_6_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_6_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_pre_post_scale_7_4_b0 */
#ifndef __PAPRD_PRE_POST_SCALE_7_4_B0_MACRO__
#define __PAPRD_PRE_POST_SCALE_7_4_B0_MACRO__

/* macros for field paprd_pre_post_scaling_7_4_b0 */
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__SHIFT     0
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__WIDTH    18
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__MASK \
                    0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003ffffU) | ((u_int32_t)(src) &\
                    0x0003ffffU)
#define PAPRD_PRE_POST_SCALE_7_4_B0__PAPRD_PRE_POST_SCALING_7_4_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0003ffffU)))
#define PAPRD_PRE_POST_SCALE_7_4_B0__TYPE                             u_int32_t
#define PAPRD_PRE_POST_SCALE_7_4_B0__READ                           0x0003ffffU
#define PAPRD_PRE_POST_SCALE_7_4_B0__WRITE                          0x0003ffffU

#endif /* __PAPRD_PRE_POST_SCALE_7_4_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_pre_post_scale_7_4_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_PRE_POST_SCALE_7_4_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_power_at_am2am_cal_b0 */
#ifndef __PAPRD_POWER_AT_AM2AM_CAL_B0_MACRO__
#define __PAPRD_POWER_AT_AM2AM_CAL_B0_MACRO__

/* macros for field paprd_power_at_am2am_cal_1_b0 */
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__SHIFT     0
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__WIDTH     6
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__MASK \
                    0x0000003fU
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000003fU
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000003fU)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000003fU) | ((u_int32_t)(src) &\
                    0x0000003fU)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_1_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000003fU)))

/* macros for field paprd_power_at_am2am_cal_2_b0 */
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__SHIFT     6
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__WIDTH     6
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__MASK \
                    0x00000fc0U
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000fc0U) >> 6)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 6) & 0x00000fc0U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000fc0U) | (((u_int32_t)(src) <<\
                    6) & 0x00000fc0U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_2_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 6) & ~0x00000fc0U)))

/* macros for field paprd_power_at_am2am_cal_3_b0 */
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__SHIFT    12
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__WIDTH     6
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__MASK \
                    0x0003f000U
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0003f000U) >> 12)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 12) & 0x0003f000U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003f000U) | (((u_int32_t)(src) <<\
                    12) & 0x0003f000U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_3_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 12) & ~0x0003f000U)))

/* macros for field paprd_power_at_am2am_cal_4_b0 */
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__SHIFT    18
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__WIDTH     6
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__MASK \
                    0x00fc0000U
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00fc0000U) >> 18)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 18) & 0x00fc0000U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00fc0000U) | (((u_int32_t)(src) <<\
                    18) & 0x00fc0000U)
#define PAPRD_POWER_AT_AM2AM_CAL_B0__PAPRD_POWER_AT_AM2AM_CAL_4_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 18) & ~0x00fc0000U)))
#define PAPRD_POWER_AT_AM2AM_CAL_B0__TYPE                             u_int32_t
#define PAPRD_POWER_AT_AM2AM_CAL_B0__READ                           0x00ffffffU
#define PAPRD_POWER_AT_AM2AM_CAL_B0__WRITE                          0x00ffffffU

#endif /* __PAPRD_POWER_AT_AM2AM_CAL_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_power_at_am2am_cal_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_POWER_AT_AM2AM_CAL_B0__NUM \
                    1

/* macros for BlueprintGlobalNameSpace::paprd_valid_obdb_b0 */
#ifndef __PAPRD_VALID_OBDB_B0_MACRO__
#define __PAPRD_VALID_OBDB_B0_MACRO__

/* macros for field paprd_valid_obdb_0_b0 */
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__SHIFT                     0
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__WIDTH                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__MASK            0x0000003fU
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000003fU
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000003fU)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000003fU) | ((u_int32_t)(src) &\
                    0x0000003fU)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_0_B0__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000003fU)))

/* macros for field paprd_valid_obdb_1_b0 */
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__SHIFT                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__WIDTH                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__MASK            0x00000fc0U
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00000fc0U) >> 6)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 6) & 0x00000fc0U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000fc0U) | (((u_int32_t)(src) <<\
                    6) & 0x00000fc0U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_1_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 6) & ~0x00000fc0U)))

/* macros for field paprd_valid_obdb_2_b0 */
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__SHIFT                    12
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__WIDTH                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__MASK            0x0003f000U
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x0003f000U) >> 12)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 12) & 0x0003f000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0003f000U) | (((u_int32_t)(src) <<\
                    12) & 0x0003f000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_2_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 12) & ~0x0003f000U)))

/* macros for field paprd_valid_obdb_3_b0 */
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__SHIFT                    18
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__WIDTH                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__MASK            0x00fc0000U
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x00fc0000U) >> 18)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 18) & 0x00fc0000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00fc0000U) | (((u_int32_t)(src) <<\
                    18) & 0x00fc0000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_3_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 18) & ~0x00fc0000U)))

/* macros for field paprd_valid_obdb_4_b0 */
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__SHIFT                    24
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__WIDTH                     6
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__MASK            0x3f000000U
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x3f000000U) >> 24)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 24) & 0x3f000000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x3f000000U) | (((u_int32_t)(src) <<\
                    24) & 0x3f000000U)
#define PAPRD_VALID_OBDB_B0__PAPRD_VALID_OBDB_4_B0__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 24) & ~0x3f000000U)))
#define PAPRD_VALID_OBDB_B0__TYPE                                     u_int32_t
#define PAPRD_VALID_OBDB_B0__READ                                   0x3fffffffU
#define PAPRD_VALID_OBDB_B0__WRITE                                  0x3fffffffU

#endif /* __PAPRD_VALID_OBDB_B0_MACRO__ */


/* macros for bb_reg_block.bb_chn_ext_reg_map.BB_paprd_valid_obdb_b0 */
#define INST_BB_REG_BLOCK__BB_CHN_EXT_REG_MAP__BB_PAPRD_VALID_OBDB_B0__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_1 */
#ifndef __GREEN_TX_GAIN_TAB_1_MACRO__
#define __GREEN_TX_GAIN_TAB_1_MACRO__

/* macros for field green_tg_table1 */
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__SHIFT                           0
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__WIDTH                           7
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_1__GREEN_TG_TABLE1__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_1__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_1__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_1__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_1_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_1 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_1__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_2 */
#ifndef __GREEN_TX_GAIN_TAB_2_MACRO__
#define __GREEN_TX_GAIN_TAB_2_MACRO__

/* macros for field green_tg_table2 */
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__SHIFT                           0
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__WIDTH                           7
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_2__GREEN_TG_TABLE2__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_2__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_2__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_2__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_2_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_2 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_2__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_3 */
#ifndef __GREEN_TX_GAIN_TAB_3_MACRO__
#define __GREEN_TX_GAIN_TAB_3_MACRO__

/* macros for field green_tg_table3 */
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__SHIFT                           0
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__WIDTH                           7
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_3__GREEN_TG_TABLE3__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_3__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_3__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_3__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_3_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_3 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_3__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_4 */
#ifndef __GREEN_TX_GAIN_TAB_4_MACRO__
#define __GREEN_TX_GAIN_TAB_4_MACRO__

/* macros for field green_tg_table4 */
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__SHIFT                           0
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__WIDTH                           7
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_4__GREEN_TG_TABLE4__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_4__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_4__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_4__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_4_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_4 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_4__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_5 */
#ifndef __GREEN_TX_GAIN_TAB_5_MACRO__
#define __GREEN_TX_GAIN_TAB_5_MACRO__

/* macros for field green_tg_table5 */
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__SHIFT                           0
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__WIDTH                           7
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_5__GREEN_TG_TABLE5__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_5__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_5__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_5__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_5_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_5 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_5__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_6 */
#ifndef __GREEN_TX_GAIN_TAB_6_MACRO__
#define __GREEN_TX_GAIN_TAB_6_MACRO__

/* macros for field green_tg_table6 */
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__SHIFT                           0
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__WIDTH                           7
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_6__GREEN_TG_TABLE6__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_6__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_6__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_6__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_6_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_6 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_6__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_7 */
#ifndef __GREEN_TX_GAIN_TAB_7_MACRO__
#define __GREEN_TX_GAIN_TAB_7_MACRO__

/* macros for field green_tg_table7 */
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__SHIFT                           0
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__WIDTH                           7
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_7__GREEN_TG_TABLE7__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_7__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_7__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_7__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_7_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_7 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_7__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_8 */
#ifndef __GREEN_TX_GAIN_TAB_8_MACRO__
#define __GREEN_TX_GAIN_TAB_8_MACRO__

/* macros for field green_tg_table8 */
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__SHIFT                           0
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__WIDTH                           7
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_8__GREEN_TG_TABLE8__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_8__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_8__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_8__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_8_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_8 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_8__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_9 */
#ifndef __GREEN_TX_GAIN_TAB_9_MACRO__
#define __GREEN_TX_GAIN_TAB_9_MACRO__

/* macros for field green_tg_table9 */
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__SHIFT                           0
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__WIDTH                           7
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__MASK                  0x0000007fU
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_9__GREEN_TG_TABLE9__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_9__TYPE                                     u_int32_t
#define GREEN_TX_GAIN_TAB_9__READ                                   0x0000007fU
#define GREEN_TX_GAIN_TAB_9__WRITE                                  0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_9_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_9 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_9__NUM     1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_10 */
#ifndef __GREEN_TX_GAIN_TAB_10_MACRO__
#define __GREEN_TX_GAIN_TAB_10_MACRO__

/* macros for field green_tg_table10 */
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__SHIFT                         0
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__WIDTH                         7
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_10__GREEN_TG_TABLE10__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_10__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_10__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_10__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_10_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_10 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_10__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_11 */
#ifndef __GREEN_TX_GAIN_TAB_11_MACRO__
#define __GREEN_TX_GAIN_TAB_11_MACRO__

/* macros for field green_tg_table11 */
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__SHIFT                         0
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__WIDTH                         7
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_11__GREEN_TG_TABLE11__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_11__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_11__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_11__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_11_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_11 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_11__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_12 */
#ifndef __GREEN_TX_GAIN_TAB_12_MACRO__
#define __GREEN_TX_GAIN_TAB_12_MACRO__

/* macros for field green_tg_table12 */
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__SHIFT                         0
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__WIDTH                         7
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_12__GREEN_TG_TABLE12__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_12__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_12__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_12__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_12_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_12 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_12__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_13 */
#ifndef __GREEN_TX_GAIN_TAB_13_MACRO__
#define __GREEN_TX_GAIN_TAB_13_MACRO__

/* macros for field green_tg_table13 */
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__SHIFT                         0
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__WIDTH                         7
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_13__GREEN_TG_TABLE13__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_13__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_13__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_13__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_13_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_13 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_13__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_14 */
#ifndef __GREEN_TX_GAIN_TAB_14_MACRO__
#define __GREEN_TX_GAIN_TAB_14_MACRO__

/* macros for field green_tg_table14 */
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__SHIFT                         0
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__WIDTH                         7
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_14__GREEN_TG_TABLE14__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_14__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_14__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_14__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_14_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_14 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_14__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_15 */
#ifndef __GREEN_TX_GAIN_TAB_15_MACRO__
#define __GREEN_TX_GAIN_TAB_15_MACRO__

/* macros for field green_tg_table15 */
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__SHIFT                         0
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__WIDTH                         7
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_15__GREEN_TG_TABLE15__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_15__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_15__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_15__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_15_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_15 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_15__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_16 */
#ifndef __GREEN_TX_GAIN_TAB_16_MACRO__
#define __GREEN_TX_GAIN_TAB_16_MACRO__

/* macros for field green_tg_table16 */
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__SHIFT                         0
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__WIDTH                         7
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_16__GREEN_TG_TABLE16__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_16__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_16__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_16__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_16_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_16 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_16__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_17 */
#ifndef __GREEN_TX_GAIN_TAB_17_MACRO__
#define __GREEN_TX_GAIN_TAB_17_MACRO__

/* macros for field green_tg_table17 */
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__SHIFT                         0
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__WIDTH                         7
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_17__GREEN_TG_TABLE17__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_17__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_17__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_17__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_17_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_17 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_17__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_18 */
#ifndef __GREEN_TX_GAIN_TAB_18_MACRO__
#define __GREEN_TX_GAIN_TAB_18_MACRO__

/* macros for field green_tg_table18 */
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__SHIFT                         0
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__WIDTH                         7
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_18__GREEN_TG_TABLE18__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_18__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_18__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_18__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_18_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_18 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_18__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_19 */
#ifndef __GREEN_TX_GAIN_TAB_19_MACRO__
#define __GREEN_TX_GAIN_TAB_19_MACRO__

/* macros for field green_tg_table19 */
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__SHIFT                         0
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__WIDTH                         7
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_19__GREEN_TG_TABLE19__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_19__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_19__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_19__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_19_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_19 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_19__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_20 */
#ifndef __GREEN_TX_GAIN_TAB_20_MACRO__
#define __GREEN_TX_GAIN_TAB_20_MACRO__

/* macros for field green_tg_table20 */
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__SHIFT                         0
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__WIDTH                         7
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_20__GREEN_TG_TABLE20__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_20__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_20__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_20__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_20_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_20 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_20__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_21 */
#ifndef __GREEN_TX_GAIN_TAB_21_MACRO__
#define __GREEN_TX_GAIN_TAB_21_MACRO__

/* macros for field green_tg_table21 */
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__SHIFT                         0
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__WIDTH                         7
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_21__GREEN_TG_TABLE21__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_21__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_21__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_21__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_21_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_21 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_21__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_22 */
#ifndef __GREEN_TX_GAIN_TAB_22_MACRO__
#define __GREEN_TX_GAIN_TAB_22_MACRO__

/* macros for field green_tg_table22 */
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__SHIFT                         0
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__WIDTH                         7
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_22__GREEN_TG_TABLE22__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_22__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_22__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_22__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_22_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_22 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_22__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_23 */
#ifndef __GREEN_TX_GAIN_TAB_23_MACRO__
#define __GREEN_TX_GAIN_TAB_23_MACRO__

/* macros for field green_tg_table23 */
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__SHIFT                         0
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__WIDTH                         7
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_23__GREEN_TG_TABLE23__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_23__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_23__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_23__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_23_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_23 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_23__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_24 */
#ifndef __GREEN_TX_GAIN_TAB_24_MACRO__
#define __GREEN_TX_GAIN_TAB_24_MACRO__

/* macros for field green_tg_table24 */
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__SHIFT                         0
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__WIDTH                         7
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_24__GREEN_TG_TABLE24__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_24__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_24__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_24__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_24_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_24 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_24__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_25 */
#ifndef __GREEN_TX_GAIN_TAB_25_MACRO__
#define __GREEN_TX_GAIN_TAB_25_MACRO__

/* macros for field green_tg_table25 */
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__SHIFT                         0
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__WIDTH                         7
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_25__GREEN_TG_TABLE25__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_25__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_25__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_25__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_25_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_25 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_25__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_26 */
#ifndef __GREEN_TX_GAIN_TAB_26_MACRO__
#define __GREEN_TX_GAIN_TAB_26_MACRO__

/* macros for field green_tg_table26 */
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__SHIFT                         0
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__WIDTH                         7
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_26__GREEN_TG_TABLE26__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_26__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_26__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_26__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_26_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_26 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_26__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_27 */
#ifndef __GREEN_TX_GAIN_TAB_27_MACRO__
#define __GREEN_TX_GAIN_TAB_27_MACRO__

/* macros for field green_tg_table27 */
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__SHIFT                         0
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__WIDTH                         7
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_27__GREEN_TG_TABLE27__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_27__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_27__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_27__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_27_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_27 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_27__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_28 */
#ifndef __GREEN_TX_GAIN_TAB_28_MACRO__
#define __GREEN_TX_GAIN_TAB_28_MACRO__

/* macros for field green_tg_table28 */
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__SHIFT                         0
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__WIDTH                         7
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_28__GREEN_TG_TABLE28__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_28__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_28__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_28__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_28_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_28 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_28__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_29 */
#ifndef __GREEN_TX_GAIN_TAB_29_MACRO__
#define __GREEN_TX_GAIN_TAB_29_MACRO__

/* macros for field green_tg_table29 */
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__SHIFT                         0
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__WIDTH                         7
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_29__GREEN_TG_TABLE29__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_29__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_29__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_29__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_29_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_29 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_29__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_30 */
#ifndef __GREEN_TX_GAIN_TAB_30_MACRO__
#define __GREEN_TX_GAIN_TAB_30_MACRO__

/* macros for field green_tg_table30 */
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__SHIFT                         0
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__WIDTH                         7
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_30__GREEN_TG_TABLE30__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_30__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_30__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_30__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_30_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_30 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_30__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_31 */
#ifndef __GREEN_TX_GAIN_TAB_31_MACRO__
#define __GREEN_TX_GAIN_TAB_31_MACRO__

/* macros for field green_tg_table31 */
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__SHIFT                         0
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__WIDTH                         7
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_31__GREEN_TG_TABLE31__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_31__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_31__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_31__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_31_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_31 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_31__NUM    1

/* macros for BlueprintGlobalNameSpace::green_tx_gain_tab_32 */
#ifndef __GREEN_TX_GAIN_TAB_32_MACRO__
#define __GREEN_TX_GAIN_TAB_32_MACRO__

/* macros for field green_tg_table32 */
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__SHIFT                         0
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__WIDTH                         7
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__MASK                0x0000007fU
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__READ(src) \
                    (u_int32_t)(src)\
                    & 0x0000007fU
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__WRITE(src) \
                    ((u_int32_t)(src)\
                    & 0x0000007fU)
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0000007fU) | ((u_int32_t)(src) &\
                    0x0000007fU)
#define GREEN_TX_GAIN_TAB_32__GREEN_TG_TABLE32__VERIFY(src) \
                    (!(((u_int32_t)(src)\
                    & ~0x0000007fU)))
#define GREEN_TX_GAIN_TAB_32__TYPE                                    u_int32_t
#define GREEN_TX_GAIN_TAB_32__READ                                  0x0000007fU
#define GREEN_TX_GAIN_TAB_32__WRITE                                 0x0000007fU

#endif /* __GREEN_TX_GAIN_TAB_32_MACRO__ */


/* macros for bb_reg_block.bb_sm_ext_reg_map.BB_green_tx_gain_tab_32 */
#define INST_BB_REG_BLOCK__BB_SM_EXT_REG_MAP__BB_GREEN_TX_GAIN_TAB_32__NUM    1


/* macros for BlueprintGlobalNameSpace::PMU1 */
#ifndef __PMU1_MACRO__
#define __PMU1_MACRO__

/* macros for field pwd */
#define PMU1__PWD__SHIFT                                                      0
#define PMU1__PWD__WIDTH                                                      3
#define PMU1__PWD__MASK                                             0x00000007U
#define PMU1__PWD__READ(src)                     (u_int32_t)(src) & 0x00000007U
#define PMU1__PWD__WRITE(src)                  ((u_int32_t)(src) & 0x00000007U)
#define PMU1__PWD__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000007U) | ((u_int32_t)(src) &\
                    0x00000007U)
#define PMU1__PWD__VERIFY(src)           (!(((u_int32_t)(src) & ~0x00000007U)))

/* macros for field Nfdiv */
#define PMU1__NFDIV__SHIFT                                                    3
#define PMU1__NFDIV__WIDTH                                                    1
#define PMU1__NFDIV__MASK                                           0x00000008U
#define PMU1__NFDIV__READ(src)          (((u_int32_t)(src) & 0x00000008U) >> 3)
#define PMU1__NFDIV__WRITE(src)         (((u_int32_t)(src) << 3) & 0x00000008U)
#define PMU1__NFDIV__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000008U) | (((u_int32_t)(src) <<\
                    3) & 0x00000008U)
#define PMU1__NFDIV__VERIFY(src)  (!((((u_int32_t)(src) << 3) & ~0x00000008U)))
#define PMU1__NFDIV__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00000008U) | ((u_int32_t)(1) << 3)
#define PMU1__NFDIV__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00000008U) | ((u_int32_t)(0) << 3)

/* macros for field Refv */
#define PMU1__REFV__SHIFT                                                     4
#define PMU1__REFV__WIDTH                                                     4
#define PMU1__REFV__MASK                                            0x000000f0U
#define PMU1__REFV__READ(src)           (((u_int32_t)(src) & 0x000000f0U) >> 4)
#define PMU1__REFV__WRITE(src)          (((u_int32_t)(src) << 4) & 0x000000f0U)
#define PMU1__REFV__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000000f0U) | (((u_int32_t)(src) <<\
                    4) & 0x000000f0U)
#define PMU1__REFV__VERIFY(src)   (!((((u_int32_t)(src) << 4) & ~0x000000f0U)))

/* macros for field Gm1 */
#define PMU1__GM1__SHIFT                                                      8
#define PMU1__GM1__WIDTH                                                      3
#define PMU1__GM1__MASK                                             0x00000700U
#define PMU1__GM1__READ(src)            (((u_int32_t)(src) & 0x00000700U) >> 8)
#define PMU1__GM1__WRITE(src)           (((u_int32_t)(src) << 8) & 0x00000700U)
#define PMU1__GM1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00000700U) | (((u_int32_t)(src) <<\
                    8) & 0x00000700U)
#define PMU1__GM1__VERIFY(src)    (!((((u_int32_t)(src) << 8) & ~0x00000700U)))

/* macros for field Classb */
#define PMU1__CLASSB__SHIFT                                                  11
#define PMU1__CLASSB__WIDTH                                                   3
#define PMU1__CLASSB__MASK                                          0x00003800U
#define PMU1__CLASSB__READ(src)        (((u_int32_t)(src) & 0x00003800U) >> 11)
#define PMU1__CLASSB__WRITE(src)       (((u_int32_t)(src) << 11) & 0x00003800U)
#define PMU1__CLASSB__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00003800U) | (((u_int32_t)(src) <<\
                    11) & 0x00003800U)
#define PMU1__CLASSB__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 11) & ~0x00003800U)))

/* macros for field Cc */
#define PMU1__CC__SHIFT                                                      14
#define PMU1__CC__WIDTH                                                       3
#define PMU1__CC__MASK                                              0x0001c000U
#define PMU1__CC__READ(src)            (((u_int32_t)(src) & 0x0001c000U) >> 14)
#define PMU1__CC__WRITE(src)           (((u_int32_t)(src) << 14) & 0x0001c000U)
#define PMU1__CC__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0001c000U) | (((u_int32_t)(src) <<\
                    14) & 0x0001c000U)
#define PMU1__CC__VERIFY(src)    (!((((u_int32_t)(src) << 14) & ~0x0001c000U)))

/* macros for field Rc */
#define PMU1__RC__SHIFT                                                      17
#define PMU1__RC__WIDTH                                                       3
#define PMU1__RC__MASK                                              0x000e0000U
#define PMU1__RC__READ(src)            (((u_int32_t)(src) & 0x000e0000U) >> 17)
#define PMU1__RC__WRITE(src)           (((u_int32_t)(src) << 17) & 0x000e0000U)
#define PMU1__RC__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x000e0000U) | (((u_int32_t)(src) <<\
                    17) & 0x000e0000U)
#define PMU1__RC__VERIFY(src)    (!((((u_int32_t)(src) << 17) & ~0x000e0000U)))

/* macros for field Rampslope */
#define PMU1__RAMPSLOPE__SHIFT                                               20
#define PMU1__RAMPSLOPE__WIDTH                                                4
#define PMU1__RAMPSLOPE__MASK                                       0x00f00000U
#define PMU1__RAMPSLOPE__READ(src)     (((u_int32_t)(src) & 0x00f00000U) >> 20)
#define PMU1__RAMPSLOPE__WRITE(src)    (((u_int32_t)(src) << 20) & 0x00f00000U)
#define PMU1__RAMPSLOPE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00f00000U) | (((u_int32_t)(src) <<\
                    20) & 0x00f00000U)
#define PMU1__RAMPSLOPE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x00f00000U)))

/* macros for field Segm */
#define PMU1__SEGM__SHIFT                                                    24
#define PMU1__SEGM__WIDTH                                                     2
#define PMU1__SEGM__MASK                                            0x03000000U
#define PMU1__SEGM__READ(src)          (((u_int32_t)(src) & 0x03000000U) >> 24)
#define PMU1__SEGM__WRITE(src)         (((u_int32_t)(src) << 24) & 0x03000000U)
#define PMU1__SEGM__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x03000000U) | (((u_int32_t)(src) <<\
                    24) & 0x03000000U)
#define PMU1__SEGM__VERIFY(src)  (!((((u_int32_t)(src) << 24) & ~0x03000000U)))

/* macros for field UseLocalOsc */
#define PMU1__USELOCALOSC__SHIFT                                             26
#define PMU1__USELOCALOSC__WIDTH                                              1
#define PMU1__USELOCALOSC__MASK                                     0x04000000U
#define PMU1__USELOCALOSC__READ(src)   (((u_int32_t)(src) & 0x04000000U) >> 26)
#define PMU1__USELOCALOSC__WRITE(src)  (((u_int32_t)(src) << 26) & 0x04000000U)
#define PMU1__USELOCALOSC__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x04000000U) | (((u_int32_t)(src) <<\
                    26) & 0x04000000U)
#define PMU1__USELOCALOSC__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 26) & ~0x04000000U)))
#define PMU1__USELOCALOSC__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x04000000U) | ((u_int32_t)(1) << 26)
#define PMU1__USELOCALOSC__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x04000000U) | ((u_int32_t)(0) << 26)

/* macros for field ForceXoscStable */
#define PMU1__FORCEXOSCSTABLE__SHIFT                                         27
#define PMU1__FORCEXOSCSTABLE__WIDTH                                          1
#define PMU1__FORCEXOSCSTABLE__MASK                                 0x08000000U
#define PMU1__FORCEXOSCSTABLE__READ(src) \
                    (((u_int32_t)(src)\
                    & 0x08000000U) >> 27)
#define PMU1__FORCEXOSCSTABLE__WRITE(src) \
                    (((u_int32_t)(src)\
                    << 27) & 0x08000000U)
#define PMU1__FORCEXOSCSTABLE__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x08000000U) | (((u_int32_t)(src) <<\
                    27) & 0x08000000U)
#define PMU1__FORCEXOSCSTABLE__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 27) & ~0x08000000U)))
#define PMU1__FORCEXOSCSTABLE__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x08000000U) | ((u_int32_t)(1) << 27)
#define PMU1__FORCEXOSCSTABLE__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x08000000U) | ((u_int32_t)(0) << 27)

/* macros for field SelFb */
#define PMU1__SELFB__SHIFT                                                   28
#define PMU1__SELFB__WIDTH                                                    1
#define PMU1__SELFB__MASK                                           0x10000000U
#define PMU1__SELFB__READ(src)         (((u_int32_t)(src) & 0x10000000U) >> 28)
#define PMU1__SELFB__WRITE(src)        (((u_int32_t)(src) << 28) & 0x10000000U)
#define PMU1__SELFB__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x10000000U) | (((u_int32_t)(src) <<\
                    28) & 0x10000000U)
#define PMU1__SELFB__VERIFY(src) (!((((u_int32_t)(src) << 28) & ~0x10000000U)))
#define PMU1__SELFB__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x10000000U) | ((u_int32_t)(1) << 28)
#define PMU1__SELFB__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x10000000U) | ((u_int32_t)(0) << 28)

/* macros for field FilterFb */
#define PMU1__FILTERFB__SHIFT                                                29
#define PMU1__FILTERFB__WIDTH                                                 3
#define PMU1__FILTERFB__MASK                                        0xe0000000U
#define PMU1__FILTERFB__READ(src)      (((u_int32_t)(src) & 0xe0000000U) >> 29)
#define PMU1__FILTERFB__WRITE(src)     (((u_int32_t)(src) << 29) & 0xe0000000U)
#define PMU1__FILTERFB__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xe0000000U) | (((u_int32_t)(src) <<\
                    29) & 0xe0000000U)
#define PMU1__FILTERFB__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 29) & ~0xe0000000U)))
#define PMU1__TYPE                                                    u_int32_t
#define PMU1__READ                                                  0xffffffffU
#define PMU1__WRITE                                                 0xffffffffU

#endif /* __PMU1_MACRO__ */


/* macros for radio65_reg_block.ch0_PMU1 */
#define INST_RADIO65_REG_BLOCK__CH0_PMU1__NUM                                 1

/* macros for BlueprintGlobalNameSpace::PMU2 */
#ifndef __PMU2_MACRO__
#define __PMU2_MACRO__

/* macros for field SPARE2 */
#define PMU2__SPARE2__SHIFT                                                   0
#define PMU2__SPARE2__WIDTH                                                  19
#define PMU2__SPARE2__MASK                                          0x0007ffffU
#define PMU2__SPARE2__READ(src)                  (u_int32_t)(src) & 0x0007ffffU
#define PMU2__SPARE2__WRITE(src)               ((u_int32_t)(src) & 0x0007ffffU)
#define PMU2__SPARE2__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x0007ffffU) | ((u_int32_t)(src) &\
                    0x0007ffffU)
#define PMU2__SPARE2__VERIFY(src)        (!(((u_int32_t)(src) & ~0x0007ffffU)))

/* macros for field pwdlpo_pwd */
#define PMU2__PWDLPO_PWD__SHIFT                                              19
#define PMU2__PWDLPO_PWD__WIDTH                                               1
#define PMU2__PWDLPO_PWD__MASK                                      0x00080000U
#define PMU2__PWDLPO_PWD__READ(src)    (((u_int32_t)(src) & 0x00080000U) >> 19)
#define PMU2__PWDLPO_PWD__WRITE(src)   (((u_int32_t)(src) << 19) & 0x00080000U)
#define PMU2__PWDLPO_PWD__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | (((u_int32_t)(src) <<\
                    19) & 0x00080000U)
#define PMU2__PWDLPO_PWD__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 19) & ~0x00080000U)))
#define PMU2__PWDLPO_PWD__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | ((u_int32_t)(1) << 19)
#define PMU2__PWDLPO_PWD__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00080000U) | ((u_int32_t)(0) << 19)

/* macros for field disc_ovr */
#define PMU2__DISC_OVR__SHIFT                                                20
#define PMU2__DISC_OVR__WIDTH                                                 1
#define PMU2__DISC_OVR__MASK                                        0x00100000U
#define PMU2__DISC_OVR__READ(src)      (((u_int32_t)(src) & 0x00100000U) >> 20)
#define PMU2__DISC_OVR__WRITE(src)     (((u_int32_t)(src) << 20) & 0x00100000U)
#define PMU2__DISC_OVR__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00100000U) | (((u_int32_t)(src) <<\
                    20) & 0x00100000U)
#define PMU2__DISC_OVR__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 20) & ~0x00100000U)))
#define PMU2__DISC_OVR__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00100000U) | ((u_int32_t)(1) << 20)
#define PMU2__DISC_OVR__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00100000U) | ((u_int32_t)(0) << 20)

/* macros for field pgm */
#define PMU2__PGM__SHIFT                                                     21
#define PMU2__PGM__WIDTH                                                      1
#define PMU2__PGM__MASK                                             0x00200000U
#define PMU2__PGM__READ(src)           (((u_int32_t)(src) & 0x00200000U) >> 21)
#define PMU2__PGM__WRITE(src)          (((u_int32_t)(src) << 21) & 0x00200000U)
#define PMU2__PGM__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x00200000U) | (((u_int32_t)(src) <<\
                    21) & 0x00200000U)
#define PMU2__PGM__VERIFY(src)   (!((((u_int32_t)(src) << 21) & ~0x00200000U)))
#define PMU2__PGM__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x00200000U) | ((u_int32_t)(1) << 21)
#define PMU2__PGM__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x00200000U) | ((u_int32_t)(0) << 21)

/* macros for field FilterVc */
#define PMU2__FILTERVC__SHIFT                                                22
#define PMU2__FILTERVC__WIDTH                                                 3
#define PMU2__FILTERVC__MASK                                        0x01c00000U
#define PMU2__FILTERVC__READ(src)      (((u_int32_t)(src) & 0x01c00000U) >> 22)
#define PMU2__FILTERVC__WRITE(src)     (((u_int32_t)(src) << 22) & 0x01c00000U)
#define PMU2__FILTERVC__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x01c00000U) | (((u_int32_t)(src) <<\
                    22) & 0x01c00000U)
#define PMU2__FILTERVC__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 22) & ~0x01c00000U)))

/* macros for field Disc */
#define PMU2__DISC__SHIFT                                                    25
#define PMU2__DISC__WIDTH                                                     1
#define PMU2__DISC__MASK                                            0x02000000U
#define PMU2__DISC__READ(src)          (((u_int32_t)(src) & 0x02000000U) >> 25)
#define PMU2__DISC__WRITE(src)         (((u_int32_t)(src) << 25) & 0x02000000U)
#define PMU2__DISC__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x02000000U) | (((u_int32_t)(src) <<\
                    25) & 0x02000000U)
#define PMU2__DISC__VERIFY(src)  (!((((u_int32_t)(src) << 25) & ~0x02000000U)))
#define PMU2__DISC__SET(dst) \
                    (dst) = ((dst) &\
                    ~0x02000000U) | ((u_int32_t)(1) << 25)
#define PMU2__DISC__CLR(dst) \
                    (dst) = ((dst) &\
                    ~0x02000000U) | ((u_int32_t)(0) << 25)

/* macros for field DiscDel */
#define PMU2__DISCDEL__SHIFT                                                 26
#define PMU2__DISCDEL__WIDTH                                                  3
#define PMU2__DISCDEL__MASK                                         0x1c000000U
#define PMU2__DISCDEL__READ(src)       (((u_int32_t)(src) & 0x1c000000U) >> 26)
#define PMU2__DISCDEL__WRITE(src)      (((u_int32_t)(src) << 26) & 0x1c000000U)
#define PMU2__DISCDEL__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0x1c000000U) | (((u_int32_t)(src) <<\
                    26) & 0x1c000000U)
#define PMU2__DISCDEL__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 26) & ~0x1c000000U)))

/* macros for field SPARE1 */
#define PMU2__SPARE1__SHIFT                                                  29
#define PMU2__SPARE1__WIDTH                                                   3
#define PMU2__SPARE1__MASK                                          0xe0000000U
#define PMU2__SPARE1__READ(src)        (((u_int32_t)(src) & 0xe0000000U) >> 29)
#define PMU2__SPARE1__WRITE(src)       (((u_int32_t)(src) << 29) & 0xe0000000U)
#define PMU2__SPARE1__MODIFY(dst, src) \
                    (dst) = ((dst) &\
                    ~0xe0000000U) | (((u_int32_t)(src) <<\
                    29) & 0xe0000000U)
#define PMU2__SPARE1__VERIFY(src) \
                    (!((((u_int32_t)(src)\
                    << 29) & ~0xe0000000U)))
#define PMU2__TYPE                                                    u_int32_t
#define PMU2__READ                                                  0xffffffffU
#define PMU2__WRITE                                                 0xffffffffU

#endif /* __PMU2_MACRO__ */


/* macros for radio65_reg_block.ch0_PMU2 */
#define INST_RADIO65_REG_BLOCK__CH0_PMU2__NUM                                 1

#define POSEIDON_REG_MAP__VERSION \
                    "/cad/local/lib/perl/Pinfo.pm\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/analog_intf_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/bb_reg_map_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/efuse_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_dcu_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_dma_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_pcu_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/mac_qcu_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/merlin2_0_radio_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/pcie_phy_reg_csr_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/radio_65_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/rtc_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/rtc_sync_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/sysconfig/svd_reg_sysconfig.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/emulation_misc.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/merlin2_0_radio_reg_map.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/pcie_phy_reg_csr.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/poseidon_radio_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/blueprint/top/poseidon_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/env/blueprint/ath_ansic.pm\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/amba_mac/blueprint/rtc_sync_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/amba_mac/svd/blueprint/svd_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/apb_analog/analog_intf_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/bb/blueprint/bb_reg_map.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/host_intf/rtl/blueprint/efuse_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/host_intf/rtl/blueprint/host_intf_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dcu_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_dma_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_dma/blueprint/mac_qcu_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/mac/rtl/mac_pcu/blueprint/mac_pcu_reg.rdl\n\
                    /trees/kcwo/kcwo-dev/depot/chips/poseidon/1.0/rtl/rtc/blueprint/rtc_reg.rdl"
#endif /* __REG_POSEIDON_REG_MAP_MACRO_H__ */
