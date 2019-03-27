/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * Configuration and status register (CSR) address and type definitions for
 * Octoen.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */
#ifndef __CVMX_CSR_H__
#define __CVMX_CSR_H__

#ifndef CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ENABLE_CSR_ADDRESS_CHECKING 0
#endif

#include "cvmx-platform.h"
#include "cvmx-csr-enums.h"
#include "cvmx-csr-typedefs.h"

/* Map the HW names to the SDK historical names */
typedef cvmx_ciu_intx_en1_t             cvmx_ciu_int1_t;
typedef cvmx_ciu_intx_sum0_t            cvmx_ciu_intx0_t;
typedef cvmx_ciu_mbox_setx_t            cvmx_ciu_mbox_t;
typedef cvmx_fpa_fpfx_marks_t           cvmx_fpa_fpf_marks_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que0_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que1_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que2_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que3_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que4_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que5_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que6_page_index_t;
typedef cvmx_fpa_quex_page_index_t      cvmx_fpa_que7_page_index_t;
typedef cvmx_ipd_1st_mbuff_skip_t       cvmx_ipd_mbuff_first_skip_t;
typedef cvmx_ipd_1st_next_ptr_back_t    cvmx_ipd_first_next_ptr_back_t;
typedef cvmx_ipd_packet_mbuff_size_t    cvmx_ipd_mbuff_size_t;
typedef cvmx_ipd_qosx_red_marks_t       cvmx_ipd_qos_red_marks_t;
typedef cvmx_ipd_wqe_fpa_queue_t        cvmx_ipd_wqe_fpa_pool_t;
typedef cvmx_l2c_pfcx_t                 cvmx_l2c_pfc0_t;
typedef cvmx_l2c_pfcx_t                 cvmx_l2c_pfc1_t;
typedef cvmx_l2c_pfcx_t                 cvmx_l2c_pfc2_t;
typedef cvmx_l2c_pfcx_t                 cvmx_l2c_pfc3_t;
typedef cvmx_lmcx_bist_ctl_t            cvmx_lmc_bist_ctl_t;
typedef cvmx_lmcx_bist_result_t         cvmx_lmc_bist_result_t;
typedef cvmx_lmcx_comp_ctl_t            cvmx_lmc_comp_ctl_t;
typedef cvmx_lmcx_ctl_t                 cvmx_lmc_ctl_t;
typedef cvmx_lmcx_ctl1_t                cvmx_lmc_ctl1_t;
typedef cvmx_lmcx_dclk_cnt_hi_t         cvmx_lmc_dclk_cnt_hi_t;
typedef cvmx_lmcx_dclk_cnt_lo_t         cvmx_lmc_dclk_cnt_lo_t;
typedef cvmx_lmcx_dclk_ctl_t            cvmx_lmc_dclk_ctl_t;
typedef cvmx_lmcx_ddr2_ctl_t            cvmx_lmc_ddr2_ctl_t;
typedef cvmx_lmcx_delay_cfg_t           cvmx_lmc_delay_cfg_t;
typedef cvmx_lmcx_dll_ctl_t             cvmx_lmc_dll_ctl_t;
typedef cvmx_lmcx_dual_memcfg_t         cvmx_lmc_dual_memcfg_t;
typedef cvmx_lmcx_ecc_synd_t            cvmx_lmc_ecc_synd_t;
typedef cvmx_lmcx_fadr_t                cvmx_lmc_fadr_t;
typedef cvmx_lmcx_ifb_cnt_hi_t          cvmx_lmc_ifb_cnt_hi_t;
typedef cvmx_lmcx_ifb_cnt_lo_t          cvmx_lmc_ifb_cnt_lo_t;
typedef cvmx_lmcx_mem_cfg0_t            cvmx_lmc_mem_cfg0_t;
typedef cvmx_lmcx_mem_cfg1_t            cvmx_lmc_mem_cfg1_t;
typedef cvmx_lmcx_wodt_ctl0_t           cvmx_lmc_odt_ctl_t;
typedef cvmx_lmcx_ops_cnt_hi_t          cvmx_lmc_ops_cnt_hi_t;
typedef cvmx_lmcx_ops_cnt_lo_t          cvmx_lmc_ops_cnt_lo_t;
typedef cvmx_lmcx_pll_bwctl_t           cvmx_lmc_pll_bwctl_t;
typedef cvmx_lmcx_pll_ctl_t             cvmx_lmc_pll_ctl_t;
typedef cvmx_lmcx_pll_status_t          cvmx_lmc_pll_status_t;
typedef cvmx_lmcx_read_level_ctl_t      cvmx_lmc_read_level_ctl_t;
typedef cvmx_lmcx_read_level_dbg_t      cvmx_lmc_read_level_dbg_t;
typedef cvmx_lmcx_read_level_rankx_t    cvmx_lmc_read_level_rankx_t;
typedef cvmx_lmcx_rodt_comp_ctl_t       cvmx_lmc_rodt_comp_ctl_t;
typedef cvmx_lmcx_rodt_ctl_t            cvmx_lmc_rodt_ctl_t;
typedef cvmx_lmcx_wodt_ctl0_t           cvmx_lmc_wodt_ctl_t;
typedef cvmx_lmcx_wodt_ctl0_t           cvmx_lmc_wodt_ctl0_t;
typedef cvmx_lmcx_wodt_ctl1_t           cvmx_lmc_wodt_ctl1_t;
typedef cvmx_mio_boot_reg_cfgx_t	cvmx_mio_boot_reg_cfg0_t;
typedef cvmx_mio_boot_reg_timx_t	cvmx_mio_boot_reg_tim0_t;
typedef cvmx_mio_twsx_int_t             cvmx_mio_tws_int_t;
typedef cvmx_mio_twsx_sw_twsi_t         cvmx_mio_tws_sw_twsi_t;
typedef cvmx_mio_twsx_sw_twsi_ext_t     cvmx_mio_tws_sw_twsi_ext_t;
typedef cvmx_mio_twsx_twsi_sw_t         cvmx_mio_tws_twsi_sw_t;
typedef cvmx_npi_base_addr_inputx_t     cvmx_npi_base_addr_input_t;
typedef cvmx_npi_base_addr_outputx_t    cvmx_npi_base_addr_output_t;
typedef cvmx_npi_buff_size_outputx_t    cvmx_npi_buff_size_output_t;
typedef cvmx_npi_dma_highp_counts_t     cvmx_npi_dma_counts_t;
typedef cvmx_npi_dma_highp_naddr_t      cvmx_npi_dma_naddr_t;
typedef cvmx_npi_highp_dbell_t          cvmx_npi_dbell_t;
typedef cvmx_npi_highp_ibuff_saddr_t    cvmx_npi_dma_ibuff_saddr_t;
typedef cvmx_npi_mem_access_subidx_t    cvmx_npi_mem_access_subid_t;
typedef cvmx_npi_num_desc_outputx_t     cvmx_npi_num_desc_output_t;
typedef cvmx_npi_px_dbpair_addr_t       cvmx_npi_dbpair_addr_t;
typedef cvmx_npi_px_instr_addr_t        cvmx_npi_instr_addr_t;
typedef cvmx_npi_px_instr_cnts_t        cvmx_npi_instr_cnts_t;
typedef cvmx_npi_px_pair_cnts_t         cvmx_npi_pair_cnts_t;
typedef cvmx_npi_size_inputx_t          cvmx_npi_size_input_t;
typedef cvmx_pci_dbellx_t               cvmx_pci_dbell_t;
typedef cvmx_pci_dma_cntx_t             cvmx_pci_dma_cnt_t;
typedef cvmx_pci_dma_int_levx_t         cvmx_pci_dma_int_lev_t;
typedef cvmx_pci_dma_timex_t            cvmx_pci_dma_time_t;
typedef cvmx_pci_instr_countx_t         cvmx_pci_instr_count_t;
typedef cvmx_pci_pkt_creditsx_t         cvmx_pci_pkt_credits_t;
typedef cvmx_pci_pkts_sent_int_levx_t   cvmx_pci_pkts_sent_int_lev_t;
typedef cvmx_pci_pkts_sent_timex_t      cvmx_pci_pkts_sent_time_t;
typedef cvmx_pci_pkts_sentx_t           cvmx_pci_pkts_sent_t;
typedef cvmx_pip_prt_cfgx_t             cvmx_pip_port_cfg_t;
typedef cvmx_pip_prt_tagx_t             cvmx_pip_port_tag_cfg_t;
typedef cvmx_pip_qos_watchx_t           cvmx_pip_port_watcher_cfg_t;
typedef cvmx_pko_mem_queue_ptrs_t       cvmx_pko_queue_cfg_t;
typedef cvmx_pko_reg_cmd_buf_t          cvmx_pko_pool_cfg_t;
typedef cvmx_smix_clk_t                 cvmx_smi_clk_t;
typedef cvmx_smix_cmd_t                 cvmx_smi_cmd_t;
typedef cvmx_smix_en_t                  cvmx_smi_en_t;
typedef cvmx_smix_rd_dat_t              cvmx_smi_rd_dat_t;
typedef cvmx_smix_wr_dat_t              cvmx_smi_wr_dat_t;
typedef cvmx_tim_reg_flags_t            cvmx_tim_control_t;

/* The CSRs for bootbus region zero used to be independent of the
    other 1-7. As of SDK 1.7.0 these were combined. These macros
    are for backwards compactability */
#define CVMX_MIO_BOOT_REG_CFG0		CVMX_MIO_BOOT_REG_CFGX(0)
#define CVMX_MIO_BOOT_REG_TIM0		CVMX_MIO_BOOT_REG_TIMX(0)

/* The CN3XXX and CN58XX chips used to not have a LMC number
    passed to the address macros. These are here to supply backwards
    compatability with old code. Code should really use the new addresses
    with bus arguments for support on other chips */
#define CVMX_LMC_BIST_CTL               CVMX_LMCX_BIST_CTL(0)
#define CVMX_LMC_BIST_RESULT            CVMX_LMCX_BIST_RESULT(0)
#define CVMX_LMC_COMP_CTL               CVMX_LMCX_COMP_CTL(0)
#define CVMX_LMC_CTL                    CVMX_LMCX_CTL(0)
#define CVMX_LMC_CTL1                   CVMX_LMCX_CTL1(0)
#define CVMX_LMC_DCLK_CNT_HI            CVMX_LMCX_DCLK_CNT_HI(0)
#define CVMX_LMC_DCLK_CNT_LO            CVMX_LMCX_DCLK_CNT_LO(0)
#define CVMX_LMC_DCLK_CTL               CVMX_LMCX_DCLK_CTL(0)
#define CVMX_LMC_DDR2_CTL               CVMX_LMCX_DDR2_CTL(0)
#define CVMX_LMC_DELAY_CFG              CVMX_LMCX_DELAY_CFG(0)
#define CVMX_LMC_DLL_CTL                CVMX_LMCX_DLL_CTL(0)
#define CVMX_LMC_DUAL_MEMCFG            CVMX_LMCX_DUAL_MEMCFG(0)
#define CVMX_LMC_ECC_SYND               CVMX_LMCX_ECC_SYND(0)
#define CVMX_LMC_FADR                   CVMX_LMCX_FADR(0)
#define CVMX_LMC_IFB_CNT_HI             CVMX_LMCX_IFB_CNT_HI(0)
#define CVMX_LMC_IFB_CNT_LO             CVMX_LMCX_IFB_CNT_LO(0)
#define CVMX_LMC_MEM_CFG0               CVMX_LMCX_MEM_CFG0(0)
#define CVMX_LMC_MEM_CFG1               CVMX_LMCX_MEM_CFG1(0)
#define CVMX_LMC_OPS_CNT_HI             CVMX_LMCX_OPS_CNT_HI(0)
#define CVMX_LMC_OPS_CNT_LO             CVMX_LMCX_OPS_CNT_LO(0)
#define CVMX_LMC_PLL_BWCTL              CVMX_LMCX_PLL_BWCTL(0)
#define CVMX_LMC_PLL_CTL                CVMX_LMCX_PLL_CTL(0)
#define CVMX_LMC_PLL_STATUS             CVMX_LMCX_PLL_STATUS(0)
#define CVMX_LMC_READ_LEVEL_CTL         CVMX_LMCX_READ_LEVEL_CTL(0)
#define CVMX_LMC_READ_LEVEL_DBG         CVMX_LMCX_READ_LEVEL_DBG(0)
#define CVMX_LMC_READ_LEVEL_RANKX       CVMX_LMCX_READ_LEVEL_RANKX(0)
#define CVMX_LMC_RODT_COMP_CTL          CVMX_LMCX_RODT_COMP_CTL(0)
#define CVMX_LMC_RODT_CTL               CVMX_LMCX_RODT_CTL(0)
#define CVMX_LMC_WODT_CTL               CVMX_LMCX_WODT_CTL0(0)
#define CVMX_LMC_WODT_CTL0              CVMX_LMCX_WODT_CTL0(0)
#define CVMX_LMC_WODT_CTL1              CVMX_LMCX_WODT_CTL1(0)

/* The CN3XXX and CN58XX chips used to not have a TWSI bus number
    passed to the address macros. These are here to supply backwards
    compatability with old code. Code should really use the new addresses
    with bus arguments for support on other chips */
#define CVMX_MIO_TWS_INT            CVMX_MIO_TWSX_INT(0)
#define CVMX_MIO_TWS_SW_TWSI        CVMX_MIO_TWSX_SW_TWSI(0)
#define CVMX_MIO_TWS_SW_TWSI_EXT    CVMX_MIO_TWSX_SW_TWSI_EXT(0)
#define CVMX_MIO_TWS_TWSI_SW        CVMX_MIO_TWSX_TWSI_SW(0)

/* The CN3XXX and CN58XX chips used to not have a SMI/MDIO bus number
    passed to the address macros. These are here to supply backwards
    compatability with old code. Code should really use the new addresses
    with bus arguments for support on other chips */
#define CVMX_SMI_CLK    CVMX_SMIX_CLK(0)
#define CVMX_SMI_CMD    CVMX_SMIX_CMD(0)
#define CVMX_SMI_EN     CVMX_SMIX_EN(0)
#define CVMX_SMI_RD_DAT CVMX_SMIX_RD_DAT(0)
#define CVMX_SMI_WR_DAT CVMX_SMIX_WR_DAT(0)

#endif /* __CVMX_CSR_H__ */

