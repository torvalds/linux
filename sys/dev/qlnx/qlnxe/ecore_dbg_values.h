/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */


#ifndef __DBG_VALUES_H__
#define __DBG_VALUES_H__

/* modes tree buffer */
static const u8 dbg_modes_tree_buf[] = {
	0x02, 0x00, 0x04, 0x00, 0x01, 0x09, 0x01, 0x08, 0x07, 0x02, 0x00, 0x01, 
	0x04, 0x05, 0x00, 0x01, 0x07, 0x09, 0x02, 0x00, 0x01, 0x04, 0x12, 0x00, 
	0x00, 0x06, 0x02, 0x00, 0x01, 0x04, 0x05, 0x00, 0x00, 0x06, 0x02, 0x00, 
	0x01, 0x05, 0x12, 0x00, 0x00, 0x06, 0x02, 0x00, 0x04, 0x00, 0x01, 0x09, 
	0x00, 0x06, 0x02, 0x00, 0x04, 0x02, 0x00, 0x0b, 0x0e, 0x00, 0x01, 0x00, 
	0x06, 0x01, 0x04, 0x05, 0x02, 0x00, 0x12, 0x00, 0x01, 0x07, 0x09, 0x02, 
	0x00, 0x04, 0x00, 0x01, 0x08, 0x07, 0x02, 0x00, 0x04, 0x00, 0x01, 0x07, 
	0x09, 0x02, 0x00, 0x04, 0x02, 0x00, 0x0b, 0x10, 0x02, 0x00, 0x04, 0x02, 
	0x00, 0x0b, 0x0f, 0x02, 0x04, 0x00, 0x01, 0x07, 0x09, 0x02, 0x00, 0x04, 
	0x02, 0x0b, 0x0e, 0x02, 0x00, 0x04, 0x00, 0x00, 0x06, 0x02, 0x04, 0x02, 
	0x00, 0x11, 0x0f, 0x02, 0x05, 0x00, 0x01, 0x07, 0x09, 0x02, 0x04, 0x02, 
	0x00, 0x11, 0x0e, 0x02, 0x00, 0x04, 0x02, 0x0b, 0x0f, 0x02, 0x00, 0x04, 
	0x02, 0x0b, 0x10, 0x02, 0x00, 0x06, 0x01, 0x04, 0x05, 0x01, 0x06, 0x01, 
	0x08, 0x0a, 0x02, 0x00, 0x04, 0x00, 0x09, 0x02, 0x04, 0x00, 0x00, 0x06, 
	0x02, 0x00, 0x06, 0x00, 0x04, 0x02, 0x05, 0x00, 0x00, 0x06, 0x02, 0x00, 
	0x06, 0x0f, 0x02, 0x00, 0x06, 0x05, 0x02, 0x00, 0x06, 0x12, 0x02, 0x00, 
	0x06, 0x04, 0x02, 0x04, 0x00, 0x11, 0x02, 0x00, 0x04, 0x0c, 0x02, 0x00, 
	0x04, 0x0f, 0x02, 0x00, 0x04, 0x10, 0x02, 0x00, 0x06, 0x10, 0x02, 0x00, 
	0x06, 0x0e, 0x02, 0x05, 0x00, 0x10, 0x02, 0x04, 0x11, 0x02, 0x04, 0x06, 
	0x02, 0x04, 0x0f, 0x02, 0x12, 0x10, 0x02, 0x05, 0x10, 0x01, 0x0b, 0x0d, 
	0x00, 0x0d, 0x00, 0x05, 0x03, 
};
/* Data size: 245 bytes */

/* Array of registers to be dumped */
static const u32 dump_reg[] = {
	0x00000ce7, 	/* split NONE */
	0x06000000, 	/* block grc */
	0x02014000, 	/* grc.override_window_mem_self_init_start .. grc.override_window_mem_self_init_done (2 regs) */
	0x0a014010, 	/* grc.rsv_attn_access_data_0 .. grc.trace_fifo_valid_data (10 regs) */
	0x1201401c, 	/* grc.trace_fifo_enable .. grc.dbg_force_frame (18 regs) */
	0x0201403a, 	/* grc.dbgsyn_status .. grc.dbgsyn_almost_full_thr (2 regs) */
	0x02014060, 	/* grc.INT_STS_0 .. grc.INT_MASK_0 (2 regs) */
	0x04014100, 	/* grc.timeout_val .. grc.number_valid_override_window (4 regs) */
	0x0c010000, 	/* block miscs */
	0x02002410, 	/* miscs.reset_config .. miscs.reset_config_por (2 regs) */
	0x0500241c, 	/* miscs.clk_100g_mode .. miscs.NVM_WR_EN (5 regs) */
	0x0100245b, 	/* miscs.memctrl_status (1 regs) */
	0x02002460, 	/* miscs.INT_STS_0 .. miscs.INT_MASK_0 (2 regs) */
	0x83002500, 	/* miscs.gpio0_driver .. miscs.gpio_event_en (131 regs) */
	0x120025af, 	/* miscs.LINK_HOLDOFF_STATUS .. miscs.func_hide_pin (18 regs) */
	0x050025c4, 	/* miscs.sel_vaux .. miscs.isolate_path (5 regs) */
	0x0c0025cb, 	/* miscs.hot_reset_unprepared .. miscs.unprepared_fw (12 regs) */
	0x070025d8, 	/* miscs.VAUX_PRESENT .. miscs.CHIP_TEST_REG (7 regs) */
	0x080025e0, 	/* miscs.PCIE_DIS_IO .. miscs.perst_deassert_cnt (8 regs) */
	0x010025eb, 	/* miscs.hot_reset_en (1 regs) */
	0x020025ed, 	/* miscs.eco_reserved .. miscs.mcp_rom_tm (2 regs) */
	0x09020000, 	/* block misc */
	0x01002010, 	/* misc.reset_config (1 regs) */
	0x02002060, 	/* misc.INT_STS .. misc.INT_MASK (2 regs) */
	0xff002100, 	/* misc.aeu_general_attn_0 .. misc.aeu_after_invert_9_mcp (255 regs) */
	0x0c0021ff, 	/* misc.aeu_sys_kill_occurred .. misc.aeu_general_mask (12 regs) */
	0x0100220c, 	/* misc.aeu_mask_attn_igu_msb (1 regs) */
	0x0200220e, 	/* misc.aeu_vpd_latch_status .. misc.attn_num_st (2 regs) */
	0x01002300, 	/* misc.port_mode (1 regs) */
	0x16002303, 	/* misc.opte_mode .. misc.sw_timer_reload_val_8 (22 regs) */
	0x0f00231a, 	/* misc.sw_timer_event .. misc.eco_reserved (15 regs) */
	0x1d040000, 	/* block pglue_b */
	0x010aa001, 	/* pglue_b.init_done_inb_int_mem (1 regs) */
	0x010aa003, 	/* pglue_b.init_done_ptt_gtt (1 regs) */
	0x010aa005, 	/* pglue_b.init_done_zone_a (1 regs) */
	0x020aa060, 	/* pglue_b.INT_STS .. pglue_b.INT_MASK (2 regs) */
	0x050aa100, 	/* pglue_b.dbg_select .. pglue_b.dbg_force_frame (5 regs) */
	0x170aa118, 	/* pglue_b.pgl_eco_reserved .. pglue_b.DISABLE_HIGHER_BW (23 regs) */
	0x040aa132, 	/* pglue_b.memctrl_status .. pglue_b.tc_per_vq (4 regs) */
	0x020aa148, 	/* pglue_b.pgl_control0 .. pglue_b.cssnoop_almost_full_thr (2 regs) */
	0x020aa158, 	/* pglue_b.pgl_txr_cdts .. pglue_b.pgl_txw_cdts (2 regs) */
	0x050aa800, 	/* pglue_b.cfg_space_a_address .. pglue_b.cfg_space_a_request (5 regs) */
	0x010aa806, 	/* pglue_b.cfg_space_b_request (1 regs) */
	0x090aa808, 	/* pglue_b.flr_request_vf_31_0 .. pglue_b.flr_request_pf_31_0 (9 regs) */
	0x020aa81a, 	/* pglue_b.DISABLE_FLR_SRIOV_DISABLED .. pglue_b.sr_iov_disabled_request (2 regs) */
	0x090aa81d, 	/* pglue_b.shadow_bme_vf_31_0 .. pglue_b.shadow_bme_pf_31_0 (9 regs) */
	0x0a0aa82f, 	/* pglue_b.shadow_ats_enable_vf_31_0 .. pglue_b.shadow_vf_enable_pf_31_0 (10 regs) */
	0x0c0aa83a, 	/* pglue_b.shadow_ido_bits .. pglue_b.was_error_pf_31_0 (12 regs) */
	0x0b0aa84f, 	/* pglue_b.rx_err_details .. pglue_b.tx_err_wr_details_icpl (11 regs) */
	0x400aa85e, 	/* pglue_b.internal_vfid_enable_31_0_value .. pglue_b.psdm_inb_int_b_vf_1 (64 regs) */
	0x290aa8c6, 	/* pglue_b.tsdm_zone_a_size_pf .. pglue_b.vf_grc_space_violation_details (41 regs) */
	0x090aa8f0, 	/* pglue_b.ido_enable_master_rw .. pglue_b.cpu_mbist_memctrl_1_cntrl_cmd (9 regs) */
	0x030aa8fb, 	/* pglue_b.disable_tcpl_translation_size_check .. pglue_b.cpu_mbist_memctrl_3_cntrl_cmd (3 regs) */
	0x010aa900, 	/* pglue_b.pgl_tgtwr_mlength (1 regs) */
	0x070aa905, 	/* pglue_b.pgl_exp_rom_addr .. pglue_b.pgl_tags_limit (7 regs) */
	0x0e0aa951, 	/* pglue_b.master_zlr_err_add_31_0 .. pglue_b.psdm_queue_zone_size (14 regs) */
	0x050aa960, 	/* pglue_b.sdm_channel_enable .. pglue_b.MASTER_ATTENTION_SETTING (5 regs) */
	0x170aab80, 	/* pglue_b.write_fifo_occupancy_level .. pglue_b.pcie_ltr_state (23 regs) */
	0x070aab9c, 	/* pglue_b.mctp_tc .. pglue_b.expansion_rom_attn (7 regs) */
	0x010aaba4, 	/* pglue_b.mps_attn (1 regs) */
	0x060aaba6, 	/* pglue_b.vpd_request_pf_31_0 .. pglue_b.sticky_master_error_en (6 regs) */
	0x0b060000, 	/* block cpmu */
	0x0200c088, 	/* cpmu.obff_mode_config .. cpmu.obff_mode_control (2 regs) */
	0x1300c08b, 	/* cpmu.obff_mem_timer_short_threshold .. cpmu.sw_force_l1 (19 regs) */
	0x0500c09f, 	/* cpmu.ltr_mode_config .. cpmu.sw_force_ltr (5 regs) */
	0x0e00c0a5, 	/* cpmu.clk_en_config .. cpmu.sw_force_main_clk_slowdown (14 regs) */
	0x0300c0b4, 	/* cpmu.storm_clk_slowdown_entry_en .. cpmu.sw_force_storm_clk_slowdown (3 regs) */
	0x0300c0b8, 	/* cpmu.nw_clk_slowdown_entry_en .. cpmu.sw_force_nw_clk_slowdown (3 regs) */
	0x0300c0bc, 	/* cpmu.pci_clk_slowdown_entry_en .. cpmu.sw_force_pci_clk_slowdown (3 regs) */
	0x0900c0c0, 	/* cpmu.pxp_vq_empty_status_e0_0 .. cpmu.cpmu_output_sig_status (9 regs) */
	0x1000c0cf, 	/* cpmu.obff_stall_mem_stat_ro .. cpmu.pcs_duration_stat_ro (16 regs) */
	0x1500c0e5, 	/* cpmu.obff_stall_mem_stat .. cpmu.INT_MASK_0 (21 regs) */
	0x0400c0fc, 	/* cpmu.sdm_sq_counter_e0_p0 .. cpmu.sdm_sq_counter_e1_p1 (4 regs) */
	0x010a0000, 	/* block pcie */
	0x04015080, 	/* pcie.eco_reserved .. pcie.pcie_debug_bits (4 regs) */
	0x180b0000, 	/* block mcp */
	0x0e380020, 	/* mcp.mcp_control .. mcp.mcp_doorbell (14 regs) */
	0x09380031, 	/* mcp.mcp_vfid .. mcp.gp_event_vec (9 regs) */
	0x03381400, 	/* mcp.cpu_mode .. mcp.cpu_event_mask (3 regs) */
	0x03381407, 	/* mcp.cpu_program_counter .. mcp.cpu_data_access (3 regs) */
	0x0438140b, 	/* mcp.cpu_interrupt_vector .. mcp.cpu_debug_vect_peek (4 regs) */
	0x01381412, 	/* mcp.cpu_last_branch_addr (1 regs) */
	0x0538152a, 	/* mcp.mdio_auto_poll .. mcp.mdio_auto_status (5 regs) */
	0x0b381640, 	/* mcp.ucint_warp_mode .. mcp.ucint_avs_address (11 regs) */
	0x02381680, 	/* mcp.imc_command .. mcp.imc_slave_control (2 regs) */
	0x07381685, 	/* mcp.imc_timing0 .. mcp.imc_datareg3 (7 regs) */
	0x01381840, 	/* mcp.m2p_m2p_status (1 regs) */
	0x09381842, 	/* mcp.m2p_m2p_vdm_length .. mcp.m2p_m2p_path_id (9 regs) */
	0x17381880, 	/* mcp.p2m_p2m_status .. mcp.p2m_p2m_other_hdr_fields (23 regs) */
	0x023818c0, 	/* mcp.cache_pim_nvram_base .. mcp.cache_paging_enable (2 regs) */
	0x1b3818c3, 	/* mcp.cache_cache_ctrl_status_0 .. mcp.cache_cache_error_status (27 regs) */
	0x0d381900, 	/* mcp.nvm_command .. mcp.nvm_reconfig (13 regs) */
	0x16381a00, 	/* mcp.erngn_exp_rom_ctrl .. mcp.erngn_img_loader2_cfg (22 regs) */
	0x14382000, 	/* mcp.smbus_config .. mcp.smbus_slave_data_read (20 regs) */
	0x01382020, 	/* mcp.smbus_arp_state (1 regs) */
	0x08382024, 	/* mcp.smbus_udid0_3 .. mcp.smbus_udid1_0 (8 regs) */
	0x013820ff, 	/* mcp.smbus_smb_reg_end (1 regs) */
	0x01382109, 	/* mcp.frm_bmb_fifo_status (1 regs) */
	0x0538210b, 	/* mcp.frm_bmb_fifo_rd_data .. mcp.frm_bmb_fifo_sop_dscr3 (5 regs) */
	0x013821ff, 	/* mcp.bmb_reg_end (1 regs) */
	0x060d0000, 	/* block pswhst */
	0x020a8000, 	/* pswhst.zone_perm_table_init .. pswhst.zone_perm_table_init_done (2 regs) */
	0x1c0a8010, 	/* pswhst.discard_internal_writes .. pswhst.is_in_drain_mode (28 regs) */
	0x040a802d, 	/* pswhst.timeout_data .. pswhst.source_usdm_credits (4 regs) */
	0x0d0a8032, 	/* pswhst.host_strict_priority .. pswhst.psdm_swap_mode (13 regs) */
	0x050a8040, 	/* pswhst.dbg_select .. pswhst.dbg_force_frame (5 regs) */
	0x020a8060, 	/* pswhst.INT_STS .. pswhst.INT_MASK (2 regs) */
	0x020e0000, 	/* block pswhst2 */
	0x0b0a7810, 	/* pswhst2.header_fifo_status .. pswhst2.dbg_force_frame (11 regs) */
	0x020a7860, 	/* pswhst2.INT_STS .. pswhst2.INT_MASK (2 regs) */
	0x030f0000, 	/* block pswrd */
	0x050a7010, 	/* pswrd.dbg_select .. pswrd.dbg_force_frame (5 regs) */
	0x030a7028, 	/* pswrd.eco_reserved .. pswrd.fifo_full_sticky (3 regs) */
	0x020a7060, 	/* pswrd.INT_STS .. pswrd.INT_MASK (2 regs) */
	0x09100000, 	/* block pswrd2 */
	0x020a7400, 	/* pswrd2.start_init .. pswrd2.init_done (2 regs) */
	0x040a7418, 	/* pswrd2.mask_error_to_clients .. pswrd2.cpl_err_details2 (4 regs) */
	0x050a741d, 	/* pswrd2.arb_delay .. pswrd2.eco_reserved (5 regs) */
	0x060a7430, 	/* pswrd2.pbf_swap_mode .. pswrd2.ptu_swap_mode (6 regs) */
	0x1e0a7438, 	/* pswrd2.almost_full_0 .. pswrd2.max_fill_level_pbf (30 regs) */
	0x020a7460, 	/* pswrd2.INT_STS .. pswrd2.INT_MASK (2 regs) */
	0x050a7500, 	/* pswrd2.dbg_select .. pswrd2.dbg_force_frame (5 regs) */
	0x030a7518, 	/* pswrd2.disable_inputs .. pswrd2.blk_num_cfg (3 regs) */
	0x100a751c, 	/* pswrd2.continue_serving_pbf .. pswrd2.prm_additional_requests (16 regs) */
	0x03110000, 	/* block pswwr */
	0x160a6810, 	/* pswwr.usdm_full_th .. pswwr.dbg_force_frame (22 regs) */
	0x010a6832, 	/* pswwr.eco_reserved (1 regs) */
	0x020a6860, 	/* pswwr.INT_STS .. pswwr.INT_MASK (2 regs) */
	0x03120000, 	/* block pswwr2 */
	0x030a6c10, 	/* pswwr2.cdu_full_th2 .. pswwr2.pglue_eop_err_details (3 regs) */
	0x060a6c14, 	/* pswwr2.prm_curr_fill_level .. pswwr2.eco_reserved (6 regs) */
	0x020a6c60, 	/* pswwr2.INT_STS .. pswwr2.INT_MASK (2 regs) */
	0x03130000, 	/* block pswrq */
	0x050a0008, 	/* pswrq.dbg_select .. pswrq.dbg_force_frame (5 regs) */
	0x010a0018, 	/* pswrq.eco_reserved (1 regs) */
	0x020a0060, 	/* pswrq.INT_STS .. pswrq.INT_MASK (2 regs) */
	0x28140000, 	/* block pswrq2 */
	0x03090000, 	/* pswrq2.rbc_done .. pswrq2.reset_stt (3 regs) */
	0x0609001e, 	/* pswrq2.endianity_00 .. pswrq2.m2p_endian_m (6 regs) */
	0x05090040, 	/* pswrq2.dbg_select .. pswrq2.dbg_force_frame (5 regs) */
	0x02090060, 	/* pswrq2.INT_STS .. pswrq2.INT_MASK (2 regs) */
	0x06090100, 	/* pswrq2.wr_mbs0 .. pswrq2.dram_align_rd (6 regs) */
	0x40090115, 	/* pswrq2.vq0_entry_cnt .. pswrq2.vq31_max_entry_cnt (64 regs) */
	0x0d090156, 	/* pswrq2.QM_PCI_ATTR .. pswrq2.done_fifo_th (13 regs) */
	0x03090164, 	/* pswrq2.BW_ADD1 .. pswrq2.BW_ADD3 (3 regs) */
	0x06090169, 	/* pswrq2.BW_ADD6 .. pswrq2.BW_ADD11 (6 regs) */
	0x01090171, 	/* pswrq2.BW_ADD14 (1 regs) */
	0x0109017f, 	/* pswrq2.BW_ADD28 (1 regs) */
	0x01090182, 	/* pswrq2.BW_ADD31 (1 regs) */
	0x03090184, 	/* pswrq2.BW_UB1 .. pswrq2.BW_UB3 (3 regs) */
	0x06090189, 	/* pswrq2.BW_UB6 .. pswrq2.BW_UB11 (6 regs) */
	0x01090191, 	/* pswrq2.BW_UB14 (1 regs) */
	0x0109019f, 	/* pswrq2.BW_UB28 (1 regs) */
	0x010901a2, 	/* pswrq2.BW_UB31 (1 regs) */
	0x030901a4, 	/* pswrq2.BW_L1 .. pswrq2.BW_L3 (3 regs) */
	0x060901a9, 	/* pswrq2.BW_L6 .. pswrq2.BW_L11 (6 regs) */
	0x010901b1, 	/* pswrq2.BW_L14 (1 regs) */
	0x010901bf, 	/* pswrq2.BW_L28 (1 regs) */
	0x050901c2, 	/* pswrq2.BW_L31 .. pswrq2.l2p_tm (5 regs) */
	0x010901c8, 	/* pswrq2.pdr_limit (1 regs) */
	0x020901cb, 	/* pswrq2.l2p_mode .. pswrq2.dram_align_sel (2 regs) */
	0x010901cf, 	/* pswrq2.vq_rd_disable (1 regs) */
	0x01090200, 	/* pswrq2.close_gate_vq_lsb_en (1 regs) */
	0x01090202, 	/* pswrq2.stall_mem_vq_lsb_en (1 regs) */
	0x01090204, 	/* pswrq2.stall_int_vq_lsb_en (1 regs) */
	0x01090209, 	/* pswrq2.assert_if_ilt_fail (1 regs) */
	0x04090211, 	/* pswrq2.sr_cnt_wr_cnt .. pswrq2.sr_cnt_usdmdp_cnt (4 regs) */
	0x0b090217, 	/* pswrq2.sr_cnt_wr_byte_lsb .. pswrq2.sr_cnt_window_value (11 regs) */
	0x0e090224, 	/* pswrq2.sr_cnt_start_mode .. pswrq2.last_wr_sr_log_4 (14 regs) */
	0x01090237, 	/* pswrq2.PTU_PCI_ATTR (1 regs) */
	0x02090239, 	/* pswrq2.M2P_PCI_ATTR .. pswrq2.MULD_PCI_ATTR (2 regs) */
	0x0109023c, 	/* pswrq2.XYLD_PCI_ATTR (1 regs) */
	0x0c090242, 	/* pswrq2.rmm_enable .. pswrq2.l2p_err_details2 (12 regs) */
	0xa209024f, 	/* pswrq2.sr_num_cfg .. pswrq2.max_srs_vq31 (162 regs) */
	0x070902f2, 	/* pswrq2.l2p_close_gate_sts .. pswrq2.garb_vq_2_strict_lsb (7 regs) */
	0x130902fa, 	/* pswrq2.garb_strict0_2_vq_0 .. pswrq2.wait_for_eop (19 regs) */
	0x05090310, 	/* pswrq2.pdr_cnt .. pswrq2.l2p_validate_vfid (5 regs) */
	0x01150000, 	/* block pglcs */
	0x02000740, 	/* pglcs.INT_STS .. pglcs.INT_MASK (2 regs) */
	0x05160000, 	/* block dmae */
	0x01003000, 	/* dmae.init (1 regs) */
	0x22003010, 	/* dmae.pci_ifen .. dmae.go_c31 (34 regs) */
	0x02003060, 	/* dmae.INT_STS .. dmae.INT_MASK (2 regs) */
	0x14003100, 	/* dmae.pxp_req_init_crd .. dmae.fsm_st (20 regs) */
	0x06003143, 	/* dmae.memctrl_status .. dmae.dbg_force_frame (6 regs) */
	0x07170000, 	/* block ptu */
	0x02158000, 	/* ptu.atc_init_array .. ptu.atc_init_done (2 regs) */
	0x0e158010, 	/* ptu.LOG_TRANSPEND_REUSE_MISS_TID .. ptu.inv_err_ctr (14 regs) */
	0x14158023, 	/* ptu.inv_halt_on_reuse_cnt_err .. ptu.ptu_b0_disable (20 regs) */
	0x05158040, 	/* ptu.dbg_select .. ptu.dbg_force_frame (5 regs) */
	0x02158060, 	/* ptu.INT_STS .. ptu.INT_MASK (2 regs) */
	0x65158100, 	/* ptu.atc_num_sets .. ptu.atc_during_inv (101 regs) */
	0x06158173, 	/* ptu.dbgsyn_almost_full_thr .. ptu.atc_ireq_fifo_tm (6 regs) */
	0x24180000, 	/* block tcm */
	0x01460000, 	/* tcm.init (1 regs) */
	0x05460010, 	/* tcm.dbg_select .. tcm.dbg_force_frame (5 regs) */
	0x02460060, 	/* tcm.INT_STS_0 .. tcm.INT_MASK_0 (2 regs) */
	0x02460064, 	/* tcm.INT_STS_1 .. tcm.INT_MASK_1 (2 regs) */
	0x02460068, 	/* tcm.INT_STS_2 .. tcm.INT_MASK_2 (2 regs) */
	0x01460100, 	/* tcm.ifen (1 regs) */
	0x08460109, 	/* tcm.qm_task_base_evnt_id_0 .. tcm.qm_task_base_evnt_id_7 (8 regs) */
	0x10460121, 	/* tcm.qm_agg_task_ctx_part_size_0 .. tcm.qm_sm_task_ctx_ldst_flg_7 (16 regs) */
	0x08460141, 	/* tcm.qm_task_use_st_flg_0 .. tcm.qm_task_use_st_flg_7 (8 regs) */
	0x09460151, 	/* tcm.tm_task_evnt_id_0 .. tcm.err_evnt_id (9 regs) */
	0x02460181, 	/* tcm.storm_weight .. tcm.msem_weight (2 regs) */
	0x02460184, 	/* tcm.dorq_weight .. tcm.pbf_weight (2 regs) */
	0x01460187, 	/* tcm.grc_weight (1 regs) */
	0x0a460189, 	/* tcm.qm_p_weight .. tcm.ia_arb_sp_timeout (10 regs) */
	0x09460199, 	/* tcm.sdm_err_handle_en .. tcm.ia_trans_part_fill_lvl (9 regs) */
	0x1b4601c1, 	/* tcm.xx_msg_up_bnd .. tcm.xx_tbyp_tbl_up_bnd (27 regs) */
	0x054601e4, 	/* tcm.xx_byp_lock_msg_thr .. tcm.unlock_miss (5 regs) */
	0x04460201, 	/* tcm.prcs_agg_con_curr_st .. tcm.prcs_sm_task_curr_st (4 regs) */
	0x1b46020d, 	/* tcm.n_sm_task_ctx_ld_0 .. tcm.trans_data_buf_crd_dir (27 regs) */
	0x0a460230, 	/* tcm.agg_task_ctx_size_0 .. tcm.sm_task_ctx_size (10 regs) */
	0x06460261, 	/* tcm.agg_task_rule0_q .. tcm.agg_task_rule5_q (6 regs) */
	0x0d460281, 	/* tcm.in_prcs_tbl_crd_agg .. tcm.xx_byp_task_state_evnt_id_flg (13 regs) */
	0x054602a1, 	/* tcm.ccfc_init_crd .. tcm.fic_init_crd (5 regs) */
	0x014602a9, 	/* tcm.dir_byp_msg_cnt (1 regs) */
	0x024602ab, 	/* tcm.dorq_length_mis .. tcm.pbf_length_mis (2 regs) */
	0x034602ae, 	/* tcm.grc_buf_empty .. tcm.storm_msg_cntr (3 regs) */
	0x014602b2, 	/* tcm.msem_msg_cntr (1 regs) */
	0x024602b4, 	/* tcm.dorq_msg_cntr .. tcm.pbf_msg_cntr (2 regs) */
	0x034602b7, 	/* tcm.qm_p_msg_cntr .. tcm.tm_msg_cntr (3 regs) */
	0x044602bb, 	/* tcm.is_qm_p_fill_lvl .. tcm.is_storm_fill_lvl (4 regs) */
	0x014602c0, 	/* tcm.is_msem_fill_lvl (1 regs) */
	0x024602c2, 	/* tcm.is_dorq_fill_lvl .. tcm.is_pbf_fill_lvl (2 regs) */
	0x074602d1, 	/* tcm.fic_msg_cntr .. tcm.tcfc_cntr (7 regs) */
	0x034602e1, 	/* tcm.eco_reserved .. tcm.is_foc_msem_nxt_inf_unit (3 regs) */
	0x024602e6, 	/* tcm.is_foc_pbf_nxt_inf_unit .. tcm.is_foc_dorq_nxt_inf_unit (2 regs) */
	0x05460530, 	/* tcm.ctx_rbc_accs .. tcm.sm_task_ctx (5 regs) */
	0x21190000, 	/* block mcm */
	0x01480000, 	/* mcm.init (1 regs) */
	0x05480010, 	/* mcm.dbg_select .. mcm.dbg_force_frame (5 regs) */
	0x02480060, 	/* mcm.INT_STS_0 .. mcm.INT_MASK_0 (2 regs) */
	0x02480064, 	/* mcm.INT_STS_1 .. mcm.INT_MASK_1 (2 regs) */
	0x02480068, 	/* mcm.INT_STS_2 .. mcm.INT_MASK_2 (2 regs) */
	0x01480100, 	/* mcm.ifen (1 regs) */
	0x08480109, 	/* mcm.qm_task_base_evnt_id_0 .. mcm.qm_task_base_evnt_id_7 (8 regs) */
	0x20480121, 	/* mcm.qm_agg_task_ctx_part_size_0 .. mcm.qm_tcfc_xxlock_cmd_7 (32 regs) */
	0x09480151, 	/* mcm.qm_task_use_st_flg_0 .. mcm.err_evnt_id (9 regs) */
	0x02480181, 	/* mcm.storm_weight .. mcm.usem_weight (2 regs) */
	0x02480184, 	/* mcm.pbf_weight .. mcm.grc_weight (2 regs) */
	0x0c480187, 	/* mcm.ysdm_weight .. mcm.ia_arb_sp_timeout (12 regs) */
	0x0948019b, 	/* mcm.sdm_err_handle_en .. mcm.ia_trans_part_fill_lvl (9 regs) */
	0x1b4801c1, 	/* mcm.xx_msg_up_bnd .. mcm.xx_tbyp_tbl_up_bnd (27 regs) */
	0x054801e4, 	/* mcm.xx_byp_lock_msg_thr .. mcm.unlock_miss (5 regs) */
	0x04480201, 	/* mcm.prcs_agg_con_curr_st .. mcm.prcs_sm_task_curr_st (4 regs) */
	0x1b48020d, 	/* mcm.n_sm_task_ctx_ld_0 .. mcm.trans_data_buf_crd_dir (27 regs) */
	0x0a480230, 	/* mcm.agg_task_ctx_size_0 .. mcm.sm_task_ctx_size (10 regs) */
	0x07480250, 	/* mcm.agg_task_rule0_q .. mcm.agg_task_rule6_q (7 regs) */
	0x0b480281, 	/* mcm.in_prcs_tbl_crd_agg .. mcm.xx_byp_task_state_evnt_id_flg (11 regs) */
	0x064802a1, 	/* mcm.ccfc_init_crd .. mcm.fic_init_crd (6 regs) */
	0x014802a9, 	/* mcm.dir_byp_msg_cnt (1 regs) */
	0x074802ab, 	/* mcm.ysdm_length_mis .. mcm.storm_msg_cntr (7 regs) */
	0x044802b3, 	/* mcm.ysdm_msg_cntr .. mcm.usem_msg_cntr (4 regs) */
	0x034802b8, 	/* mcm.pbf_msg_cntr .. mcm.qm_s_msg_cntr (3 regs) */
	0x034802bc, 	/* mcm.is_qm_p_fill_lvl .. mcm.is_storm_fill_lvl (3 regs) */
	0x044802c0, 	/* mcm.is_ysdm_fill_lvl .. mcm.is_usem_fill_lvl (4 regs) */
	0x014802c5, 	/* mcm.is_pbf_fill_lvl (1 regs) */
	0x074802d1, 	/* mcm.fic_msg_cntr .. mcm.tcfc_cntr (7 regs) */
	0x034802e1, 	/* mcm.eco_reserved .. mcm.is_foc_usem_nxt_inf_unit (3 regs) */
	0x014802e5, 	/* mcm.is_foc_pbf_nxt_inf_unit (1 regs) */
	0x034802e7, 	/* mcm.is_foc_usdm_nxt_inf_unit .. mcm.is_foc_tmld_nxt_inf_unit (3 regs) */
	0x05480600, 	/* mcm.ctx_rbc_accs .. mcm.sm_task_ctx (5 regs) */
	0x1d1a0000, 	/* block ucm */
	0x014a0000, 	/* ucm.init (1 regs) */
	0x064a0013, 	/* ucm.memctrl_status .. ucm.dbg_force_frame (6 regs) */
	0x024a0060, 	/* ucm.INT_STS_0 .. ucm.INT_MASK_0 (2 regs) */
	0x024a0064, 	/* ucm.INT_STS_1 .. ucm.INT_MASK_1 (2 regs) */
	0x024a0068, 	/* ucm.INT_STS_2 .. ucm.INT_MASK_2 (2 regs) */
	0x014a0100, 	/* ucm.ifen (1 regs) */
	0x084a0109, 	/* ucm.qm_task_base_evnt_id_0 .. ucm.qm_task_base_evnt_id_7 (8 regs) */
	0x104a0121, 	/* ucm.qm_agg_task_ctx_part_size_0 .. ucm.qm_sm_task_ctx_ldst_flg_7 (16 regs) */
	0x084a0141, 	/* ucm.qm_task_use_st_flg_0 .. ucm.qm_task_use_st_flg_7 (8 regs) */
	0x094a0151, 	/* ucm.tm_task_evnt_id_0 .. ucm.err_evnt_id (9 regs) */
	0x0a4a0181, 	/* ucm.storm_weight .. ucm.muld_weight (10 regs) */
	0x0a4a018c, 	/* ucm.qm_p_weight .. ucm.ia_arb_sp_timeout (10 regs) */
	0x094a01a1, 	/* ucm.sdm_err_handle_en .. ucm.ia_trans_part_fill_lvl (9 regs) */
	0x1b4a01c1, 	/* ucm.xx_msg_up_bnd .. ucm.xx_tbyp_tbl_up_bnd (27 regs) */
	0x054a01e4, 	/* ucm.xx_byp_lock_msg_thr .. ucm.unlock_miss (5 regs) */
	0x044a0201, 	/* ucm.prcs_agg_con_curr_st .. ucm.prcs_sm_task_curr_st (4 regs) */
	0x1b4a020d, 	/* ucm.n_sm_task_ctx_ld_0 .. ucm.trans_data_buf_crd_dir (27 regs) */
	0x0a4a0230, 	/* ucm.agg_task_ctx_size_0 .. ucm.sm_task_ctx_size (10 regs) */
	0x074a025a, 	/* ucm.agg_task_rule0_q .. ucm.agg_task_rule6_q (7 regs) */
	0x0d4a0281, 	/* ucm.in_prcs_tbl_crd_agg .. ucm.xx_byp_task_state_evnt_id_flg (13 regs) */
	0x054a02a1, 	/* ucm.ccfc_init_crd .. ucm.fic_init_crd (5 regs) */
	0x094a02a9, 	/* ucm.dir_byp_msg_cnt .. ucm.muld_length_mis (9 regs) */
	0x094a02b3, 	/* ucm.grc_buf_empty .. ucm.muld_msg_cntr (9 regs) */
	0x054a02bd, 	/* ucm.dorq_msg_cntr .. ucm.tm_msg_cntr (5 regs) */
	0x0a4a02c3, 	/* ucm.is_qm_p_fill_lvl .. ucm.is_muld_fill_lvl (10 regs) */
	0x024a02ce, 	/* ucm.is_dorq_fill_lvl .. ucm.is_pbf_fill_lvl (2 regs) */
	0x094a02d1, 	/* ucm.fic_msg_cntr .. ucm.tcfc_cntr (9 regs) */
	0x0a4a02e1, 	/* ucm.eco_reserved .. ucm.is_foc_muld_nxt_inf_unit (10 regs) */
	0x054a05c0, 	/* ucm.ctx_rbc_accs .. ucm.sm_task_ctx (5 regs) */
	0x1f1b0000, 	/* block xcm */
	0x01400000, 	/* xcm.init (1 regs) */
	0x01400002, 	/* xcm.qm_act_st_cnt_init_done (1 regs) */
	0x05400010, 	/* xcm.dbg_select .. xcm.dbg_force_frame (5 regs) */
	0x02400060, 	/* xcm.INT_STS_0 .. xcm.INT_MASK_0 (2 regs) */
	0x02400064, 	/* xcm.INT_STS_1 .. xcm.INT_MASK_1 (2 regs) */
	0x02400068, 	/* xcm.INT_STS_2 .. xcm.INT_MASK_2 (2 regs) */
	0x01400100, 	/* xcm.ifen (1 regs) */
	0x01400131, 	/* xcm.err_evnt_id (1 regs) */
	0x03400181, 	/* xcm.storm_weight .. xcm.usem_weight (3 regs) */
	0x03400185, 	/* xcm.dorq_weight .. xcm.grc_weight (3 regs) */
	0x0d400189, 	/* xcm.xsdm_weight .. xcm.ia_arb_sp_timeout (13 regs) */
	0x064001a0, 	/* xcm.sdm_err_handle_en .. xcm.ia_trans_part_fill_lvl (6 regs) */
	0x184001c1, 	/* xcm.xx_msg_up_bnd .. xcm.xx_cbyp_tbl_up_bnd (24 regs) */
	0x054001e1, 	/* xcm.xx_byp_lock_msg_thr .. xcm.unlock_miss (5 regs) */
	0x02400201, 	/* xcm.prcs_agg_con_curr_st .. xcm.prcs_sm_con_curr_st (2 regs) */
	0x0a40020b, 	/* xcm.agg_con_fic_buf_fill_lvl .. xcm.trans_data_buf_crd_dir (10 regs) */
	0x0240021d, 	/* xcm.cm_con_reg0_sz .. xcm.sm_con_ctx_size (2 regs) */
	0x09400281, 	/* xcm.in_prcs_tbl_crd_agg .. xcm.xx_byp_con_state_evnt_id_flg (9 regs) */
	0x054002a1, 	/* xcm.ccfc_init_crd .. xcm.fic_init_crd (5 regs) */
	0x014002a9, 	/* xcm.dir_byp_msg_cnt (1 regs) */
	0x084002ab, 	/* xcm.xsdm_length_mis .. xcm.storm_msg_cntr (8 regs) */
	0x054002b4, 	/* xcm.xsdm_msg_cntr .. xcm.usem_msg_cntr (5 regs) */
	0x054002ba, 	/* xcm.dorq_msg_cntr .. xcm.tm_msg_cntr (5 regs) */
	0x044002c0, 	/* xcm.is_qm_p_fill_lvl .. xcm.is_storm_fill_lvl (4 regs) */
	0x054002c5, 	/* xcm.is_xsdm_fill_lvl .. xcm.is_usem_fill_lvl (5 regs) */
	0x024002cb, 	/* xcm.is_dorq_fill_lvl .. xcm.is_pbf_fill_lvl (2 regs) */
	0x0b4002d1, 	/* xcm.qm_act_st_fifo_fill_lvl .. xcm.ccfc_cntr (11 regs) */
	0x044002e1, 	/* xcm.eco_reserved .. xcm.is_foc_xsem_nxt_inf_unit (4 regs) */
	0x024002e6, 	/* xcm.is_foc_pbf_nxt_inf_unit .. xcm.is_foc_dorq_nxt_inf_unit (2 regs) */
	0x034002e9, 	/* xcm.is_foc_usdm_nxt_inf_unit .. xcm.is_foc_ysdm_nxt_inf_unit (3 regs) */
	0x03400600, 	/* xcm.ctx_rbc_accs .. xcm.sm_con_ctx (3 regs) */
	0x1d1c0000, 	/* block ycm */
	0x01420000, 	/* ycm.init (1 regs) */
	0x05420010, 	/* ycm.dbg_select .. ycm.dbg_force_frame (5 regs) */
	0x02420060, 	/* ycm.INT_STS_0 .. ycm.INT_MASK_0 (2 regs) */
	0x02420064, 	/* ycm.INT_STS_1 .. ycm.INT_MASK_1 (2 regs) */
	0x02420068, 	/* ycm.INT_STS_2 .. ycm.INT_MASK_2 (2 regs) */
	0x01420100, 	/* ycm.ifen (1 regs) */
	0x08420109, 	/* ycm.qm_task_base_evnt_id_0 .. ycm.qm_task_base_evnt_id_7 (8 regs) */
	0x20420121, 	/* ycm.qm_agg_task_ctx_part_size_0 .. ycm.qm_tcfc_xxlock_cmd_7 (32 regs) */
	0x09420151, 	/* ycm.qm_task_use_st_flg_0 .. ycm.err_evnt_id (9 regs) */
	0x05420181, 	/* ycm.storm_weight .. ycm.grc_weight (5 regs) */
	0x0b420187, 	/* ycm.ysdm_weight .. ycm.ia_arb_sp_timeout (11 regs) */
	0x09420199, 	/* ycm.sdm_err_handle_en .. ycm.ia_trans_part_fill_lvl (9 regs) */
	0x1b4201c1, 	/* ycm.xx_msg_up_bnd .. ycm.xx_tbyp_tbl_up_bnd (27 regs) */
	0x054201e4, 	/* ycm.xx_byp_lock_msg_thr .. ycm.unlock_miss (5 regs) */
	0x04420201, 	/* ycm.prcs_agg_con_curr_st .. ycm.prcs_sm_task_curr_st (4 regs) */
	0x1b42020d, 	/* ycm.n_sm_task_ctx_ld_0 .. ycm.trans_data_buf_crd_dir (27 regs) */
	0x0a420230, 	/* ycm.agg_task_ctx_size_0 .. ycm.sm_task_ctx_size (10 regs) */
	0x0b420281, 	/* ycm.in_prcs_tbl_crd_agg .. ycm.xx_byp_task_state_evnt_id_flg (11 regs) */
	0x064202a1, 	/* ycm.ccfc_init_crd .. ycm.fic_init_crd (6 regs) */
	0x014202a9, 	/* ycm.dir_byp_msg_cnt (1 regs) */
	0x064202ab, 	/* ycm.ysdm_length_mis .. ycm.storm_msg_cntr (6 regs) */
	0x074202b2, 	/* ycm.ysdm_msg_cntr .. ycm.qm_s_msg_cntr (7 regs) */
	0x034202ba, 	/* ycm.is_qm_p_fill_lvl .. ycm.is_storm_fill_lvl (3 regs) */
	0x054202be, 	/* ycm.is_ysdm_fill_lvl .. ycm.is_pbf_fill_lvl (5 regs) */
	0x084202d1, 	/* ycm.fic_msg_cntr .. ycm.tcfc_cntr (8 regs) */
	0x034202e1, 	/* ycm.eco_reserved .. ycm.is_foc_usem_nxt_inf_unit (3 regs) */
	0x014202e5, 	/* ycm.is_foc_pbf_nxt_inf_unit (1 regs) */
	0x024202e7, 	/* ycm.is_foc_ysdm_nxt_inf_unit .. ycm.is_foc_xyld_nxt_inf_unit (2 regs) */
	0x05420600, 	/* ycm.ctx_rbc_accs .. ycm.sm_task_ctx (5 regs) */
	0x151d0000, 	/* block pcm */
	0x01440000, 	/* pcm.init (1 regs) */
	0x05440010, 	/* pcm.dbg_select .. pcm.dbg_force_frame (5 regs) */
	0x02440060, 	/* pcm.INT_STS_0 .. pcm.INT_MASK_0 (2 regs) */
	0x02440064, 	/* pcm.INT_STS_1 .. pcm.INT_MASK_1 (2 regs) */
	0x02440068, 	/* pcm.INT_STS_2 .. pcm.INT_MASK_2 (2 regs) */
	0x02440100, 	/* pcm.ifen .. pcm.err_evnt_id (2 regs) */
	0x01440181, 	/* pcm.storm_weight (1 regs) */
	0x01440183, 	/* pcm.grc_weight (1 regs) */
	0x07440185, 	/* pcm.ia_group_pr0 .. pcm.ia_arb_sp_timeout (7 regs) */
	0x0544018f, 	/* pcm.sdm_err_handle_en .. pcm.ia_trans_part_fill_lvl (5 regs) */
	0x184401c1, 	/* pcm.xx_msg_up_bnd .. pcm.unlock_miss (24 regs) */
	0x01440201, 	/* pcm.prcs_sm_con_curr_st (1 regs) */
	0x0644020a, 	/* pcm.sm_con_fic_buf_fill_lvl .. pcm.sm_con_ctx_size (6 regs) */
	0x07440281, 	/* pcm.in_prcs_tbl_crd_agg .. pcm.xx_byp_con_state_evnt_id_flg (7 regs) */
	0x024402a1, 	/* pcm.ccfc_init_crd .. pcm.fic_init_crd (2 regs) */
	0x014402a9, 	/* pcm.dir_byp_msg_cnt (1 regs) */
	0x034402ac, 	/* pcm.grc_buf_empty .. pcm.storm_msg_cntr (3 regs) */
	0x014402b2, 	/* pcm.is_storm_fill_lvl (1 regs) */
	0x024402d1, 	/* pcm.fic_msg_cntr .. pcm.ccfc_cntr (2 regs) */
	0x024402e1, 	/* pcm.eco_reserved .. pcm.is_foc_psem_nxt_inf_unit (2 regs) */
	0x02440510, 	/* pcm.ctx_rbc_accs .. pcm.sm_con_ctx (2 regs) */
	0x1c1e0000, 	/* block qm */
	0x020bc060, 	/* qm.INT_STS .. qm.INT_MASK (2 regs) */
	0x0d0bc100, 	/* qm.wrc_drop_cnt_0 .. qm.cm_push_int_en (13 regs) */
	0x380bc110, 	/* qm.MaxPqSizeTxSel_0 .. qm.MaxPqSizeTxSel_55 (56 regs) */
	0x040bc200, 	/* qm.OutLdReqSizeConnTx .. qm.OutLdReqCrdConnOther (4 regs) */
	0x0e0bc410, 	/* qm.QstatusTx_0 .. qm.QstatusTx_13 (14 regs) */
	0x020bc430, 	/* qm.QstatusOther_0 .. qm.QstatusOther_1 (2 regs) */
	0x280bc488, 	/* qm.CtxRegTcfc_0 .. qm.CtxRegTcfc_39 (40 regs) */
	0x280bc508, 	/* qm.ActCtrInitValTcfc_0 .. qm.ActCtrInitValTcfc_39 (40 regs) */
	0x040bc548, 	/* qm.PciReqQId .. qm.QmPageSize (4 regs) */
	0x050bc54d, 	/* qm.PciReqPadToCacheLine .. qm.OvfErrorOther (5 regs) */
	0x010bc580, 	/* qm.VoqCrdLineFull (1 regs) */
	0x010bc5c0, 	/* qm.TaskLineCrdCost (1 regs) */
	0x010bc600, 	/* qm.VoqCrdByteFull (1 regs) */
	0x220bc640, 	/* qm.TaskByteCrdCost_0 .. qm.WrrOtherPqGrp_7 (34 regs) */
	0x040bc67a, 	/* qm.WrrOtherGrpWeight_0 .. qm.WrrOtherGrpWeight_3 (4 regs) */
	0x170bc682, 	/* qm.WrrTxGrpWeight_0 .. qm.CmIntEn (23 regs) */
	0x010bc780, 	/* qm.VoqByteCrdEnable (1 regs) */
	0x040bc900, 	/* qm.MHQTxNumSel .. qm.QOtherLevelMHVal (4 regs) */
	0x390bcb00, 	/* qm.Soft_Reset .. qm.PqTx2Pf_55 (57 regs) */
	0x080bcb81, 	/* qm.PqOther2Pf_0 .. qm.PqOther2Pf_7 (8 regs) */
	0x090bcb99, 	/* qm.arb_tx_en .. qm.dbg_force_frame (9 regs) */
	0x020bcba6, 	/* qm.eco_reserved .. qm.TxPqMap_MaskAccess (2 regs) */
	0x0f0bcbaf, 	/* qm.Xsdm_Fifo_Full_Thr .. qm.RlGlblPeriodSel_7 (15 regs) */
	0x090bd300, 	/* qm.RlGlblEnable .. qm.RlPfPeriodTimer (9 regs) */
	0x060bd380, 	/* qm.RlPfEnable .. qm.Err_Mask_RlPfCrd (6 regs) */
	0x130bd700, 	/* qm.WfqPfEnable .. qm.Voq_Arb_Grp0_Weight_7 (19 regs) */
	0x080bd72b, 	/* qm.Voq_Arb_Grp1_Weight_0 .. qm.Voq_Arb_Grp1_Weight_7 (8 regs) */
	0x200bd74b, 	/* qm.Voq_Arb_Timeout .. qm.cam_bist_status (32 regs) */
	0x0a1f0000, 	/* block tm */
	0x100b0000, 	/* tm.memory_self_init_start .. tm.ac_command_fifo_init (16 regs) */
	0x040b0018, 	/* tm.pxp_interface_enable .. tm.client_in_interface_enable (4 regs) */
	0x050b001e, 	/* tm.pxp_request_credit .. tm.load_request_credit (5 regs) */
	0x020b0060, 	/* tm.INT_STS_0 .. tm.INT_MASK_0 (2 regs) */
	0x020b0064, 	/* tm.INT_STS_1 .. tm.INT_MASK_1 (2 regs) */
	0x0b0b0100, 	/* tm.pxp_read_data_fifo_a_f_thr .. tm.ac_command_fifo_a_f_thr (11 regs) */
	0x2d0b0112, 	/* tm.tick_timer_val .. tm.task_timer_threshold_2 (45 regs) */
	0x030b0143, 	/* tm.during_scan_conn .. tm.during_scan (3 regs) */
	0x1f0b0180, 	/* tm.completed_scans .. tm.pxp_read_data_error (31 regs) */
	0x2f0b01c0, 	/* tm.current_time .. tm.dbg_force_frame (47 regs) */
	0x18200000, 	/* block dorq */
	0x01040000, 	/* dorq.INIT (1 regs) */
	0x01040010, 	/* dorq.ifen (1 regs) */
	0x02040060, 	/* dorq.INT_STS .. dorq.INT_MASK (2 regs) */
	0x15040118, 	/* dorq.dems_target_1 .. dorq.dems_agg_cmd_7 (21 regs) */
	0x0204013d, 	/* dorq.pwm_agg_cmd .. dorq.cm_ac_upd (2 regs) */
	0x05040180, 	/* dorq.dpm_l2_succ_cflg_cmd .. dorq.dpm_l2_abrt_agg_cmd (5 regs) */
	0x030401be, 	/* dorq.xcm_agg_type .. dorq.tcm_agg_type (3 regs) */
	0x010401c2, 	/* dorq.xcm_sm_ctx_ld_st_flg_dpm (1 regs) */
	0x07040200, 	/* dorq.xcm_ccfc_regn .. dorq.dpm_xcm_db_abrt_th (7 regs) */
	0x06040208, 	/* dorq.dpm_ent_abrt_th .. dorq.dpm_timeout (6 regs) */
	0x0304020f, 	/* dorq.dq_pxp_full_en .. dorq.dq_full_cycles (3 regs) */
	0x04040229, 	/* dorq.grh_nxt_header .. dorq.crc32_bswap (4 regs) */
	0x01040233, 	/* dorq.rroce_dst_udp_port (1 regs) */
	0x01040240, 	/* dorq.l2_edpm_num_bd_thr (1 regs) */
	0x08040243, 	/* dorq.l2_edpm_tunnel_gre_eth_en .. dorq.l2_edpm_pkt_hdr_size (8 regs) */
	0x04040260, 	/* dorq.xcm_msg_init_crd .. dorq.pbf_cmd_init_crd (4 regs) */
	0x03040277, 	/* dorq.db_drop_reason_mask .. dorq.auto_freeze_st (3 regs) */
	0x0204027b, 	/* dorq.auto_drop_en .. dorq.auto_drop_st (2 regs) */
	0x0c04027e, 	/* dorq.pxp_trans_size .. dorq.db_drop_details (12 regs) */
	0x0704028b, 	/* dorq.db_drop_reason .. dorq.dpm_abort_details_reason (7 regs) */
	0x11040293, 	/* dorq.dpm_abort_reason .. dorq.cfc_bypass_cnt (17 regs) */
	0x048402a4, 	/* dorq.mini_cache_entry .. dorq.cfc_lcres_err_detail (4 regs, WB) */
	0x030402a8, 	/* dorq.cfc_ld_req_cnt .. dorq.eco_reserved (3 regs) */
	0x060402b3, 	/* dorq.memctrl_status .. dorq.dbg_force_frame (6 regs) */
	0x33210000, 	/* block brb */
	0x030d0001, 	/* brb.hw_init_en .. brb.start_en (3 regs) */
	0x020d0030, 	/* brb.INT_STS_0 .. brb.INT_MASK_0 (2 regs) */
	0x020d0036, 	/* brb.INT_STS_1 .. brb.INT_MASK_1 (2 regs) */
	0x020d003c, 	/* brb.INT_STS_2 .. brb.INT_MASK_2 (2 regs) */
	0x020d0042, 	/* brb.INT_STS_3 .. brb.INT_MASK_3 (2 regs) */
	0x020d0048, 	/* brb.INT_STS_4 .. brb.INT_MASK_4 (2 regs) */
	0x020d004e, 	/* brb.INT_STS_5 .. brb.INT_MASK_5 (2 regs) */
	0x020d0054, 	/* brb.INT_STS_6 .. brb.INT_MASK_6 (2 regs) */
	0x020d005a, 	/* brb.INT_STS_7 .. brb.INT_MASK_7 (2 regs) */
	0x020d0061, 	/* brb.INT_STS_8 .. brb.INT_MASK_8 (2 regs) */
	0x020d0067, 	/* brb.INT_STS_9 .. brb.INT_MASK_9 (2 regs) */
	0x020d006d, 	/* brb.INT_STS_10 .. brb.INT_MASK_10 (2 regs) */
	0x020d0073, 	/* brb.INT_STS_11 .. brb.INT_MASK_11 (2 regs) */
	0x010d0200, 	/* brb.big_ram_address (1 regs) */
	0x020d0210, 	/* brb.max_releases .. brb.stop_on_len_err (2 regs) */
	0x120d0240, 	/* brb.tc_guarantied_0 .. brb.tc_guarantied_17 (18 regs) */
	0x100d025e, 	/* brb.main_tc_guarantied_hyst_0 .. brb.main_tc_guarantied_hyst_15 (16 regs) */
	0x120d0276, 	/* brb.lb_tc_guarantied_hyst_0 .. brb.lb_tc_guarantied_hyst_17 (18 regs) */
	0x100d0294, 	/* brb.main_tc_pause_xoff_threshold_0 .. brb.main_tc_pause_xoff_threshold_15 (16 regs) */
	0x120d02ac, 	/* brb.lb_tc_pause_xoff_threshold_0 .. brb.lb_tc_pause_xoff_threshold_17 (18 regs) */
	0x100d02ca, 	/* brb.main_tc_pause_xon_threshold_0 .. brb.main_tc_pause_xon_threshold_15 (16 regs) */
	0x120d02e2, 	/* brb.lb_tc_pause_xon_threshold_0 .. brb.lb_tc_pause_xon_threshold_17 (18 regs) */
	0x100d0300, 	/* brb.main_tc_full_xoff_threshold_0 .. brb.main_tc_full_xoff_threshold_15 (16 regs) */
	0x120d0318, 	/* brb.lb_tc_full_xoff_threshold_0 .. brb.lb_tc_full_xoff_threshold_17 (18 regs) */
	0x100d0336, 	/* brb.main_tc_full_xon_threshold_0 .. brb.main_tc_full_xon_threshold_15 (16 regs) */
	0x120d034e, 	/* brb.lb_tc_full_xon_threshold_0 .. brb.lb_tc_full_xon_threshold_17 (18 regs) */
	0x080d036c, 	/* brb.lossless_threshold .. brb.rc_pkt_priority (8 regs) */
	0x110d0382, 	/* brb.rc_sop_priority .. brb.pm_tc_latency_sensitive_1 (17 regs) */
	0x080d03b1, 	/* brb.dbgsyn_almost_full_thr .. brb.dbg_force_frame (8 regs) */
	0x060d03ca, 	/* brb.inp_if_enable .. brb.wc_empty_3 (6 regs) */
	0x040d03dc, 	/* brb.wc_full_0 .. brb.wc_full_3 (4 regs) */
	0x070d03ec, 	/* brb.wc_bandwidth_if_full .. brb.rc_pkt_empty_4 (7 regs) */
	0x050d03fd, 	/* brb.rc_pkt_full_0 .. brb.rc_pkt_full_4 (5 regs) */
	0x050d040c, 	/* brb.rc_pkt_status_0 .. brb.rc_pkt_status_4 (5 regs) */
	0x0b0d041b, 	/* brb.rc_sop_empty .. brb.empty_if_1 (11 regs) */
	0x050d042a, 	/* brb.rc_sop_inp_sync_fifo_push_status .. brb.rc_inp_sync_fifo_push_status_3 (5 regs) */
	0x050d043a, 	/* brb.rc_out_sync_fifo_push_status_0 .. brb.rc_out_sync_fifo_push_status_4 (5 regs) */
	0x010d0449, 	/* brb.rc_eop_inp_sync_fifo_push_status_0 (1 regs) */
	0x010d0458, 	/* brb.rc_eop_out_sync_fifo_push_status_0 (1 regs) */
	0x040d0467, 	/* brb.pkt_avail_sync_fifo_push_status .. brb.rc_pkt_state (4 regs) */
	0x020d046e, 	/* brb.mac_free_shared_hr_0 .. brb.mac_free_shared_hr_1 (2 regs) */
	0x090d0474, 	/* brb.mac0_tc_occupancy_0 .. brb.mac0_tc_occupancy_8 (9 regs) */
	0x090d0484, 	/* brb.mac1_tc_occupancy_0 .. brb.mac1_tc_occupancy_8 (9 regs) */
	0x020d04b4, 	/* brb.available_mac_size_0 .. brb.available_mac_size_1 (2 regs) */
	0x020d04ba, 	/* brb.main_tc_pause_0 .. brb.main_tc_pause_1 (2 regs) */
	0x020d04c0, 	/* brb.lb_tc_pause_0 .. brb.lb_tc_pause_1 (2 regs) */
	0x020d04c6, 	/* brb.main_tc_full_0 .. brb.main_tc_full_1 (2 regs) */
	0x020d04cc, 	/* brb.lb_tc_full_0 .. brb.lb_tc_full_1 (2 regs) */
	0x080d04d2, 	/* brb.main0_tc_lossless_cnt_0 .. brb.main0_tc_lossless_cnt_7 (8 regs) */
	0x080d04e2, 	/* brb.main1_tc_lossless_cnt_0 .. brb.main1_tc_lossless_cnt_7 (8 regs) */
	0x020d0512, 	/* brb.main_tc_lossless_int_0 .. brb.main_tc_lossless_int_1 (2 regs) */
	0x08220000, 	/* block src */
	0x0108e010, 	/* src.CTRL (1 regs) */
	0x0108e076, 	/* src.INT_STS (1 regs) */
	0x0108e079, 	/* src.INT_MASK (1 regs) */
	0x0b08e100, 	/* src.KeySearch_0 .. src.KeySearch_vlan (11 regs) */
	0x0708e120, 	/* src.IF_Stat_PF_Config .. src.IF_Stat_No_Read_Counter (7 regs) */
	0x0108e180, 	/* src.PXP_CTRL (1 regs) */
	0x0508e1c0, 	/* src.dbg_select .. src.dbg_force_frame (5 regs) */
	0x0208e1d2, 	/* src.eco_reserved .. src.soft_rst (2 regs) */
	0x25230000, 	/* block prs */
	0x0107c000, 	/* prs.soft_rst (1 regs) */
	0x0307c002, 	/* prs.mac_vlan_cache_init_done .. prs.cam_scrub_miss_en (3 regs) */
	0x0207c010, 	/* prs.INT_STS_0 .. prs.INT_MASK_0 (2 regs) */
	0x0107c050, 	/* prs.task_inc_value (1 regs) */
	0x0107c059, 	/* prs.search_resp_initiator_type (1 regs) */
	0x0607c05e, 	/* prs.task_id_segment .. prs.roce_con_type (6 regs) */
	0x0107c065, 	/* prs.roce_opcode_req_res (1 regs) */
	0x0107c067, 	/* prs.cfc_load_mini_cache_en (1 regs) */
	0x0107c080, 	/* prs.eco_reserved (1 regs) */
	0x0107c104, 	/* prs.search_tcp_first_frag (1 regs) */
	0x0107c10a, 	/* prs.roce_spcl_qp_val (1 regs) */
	0x0907c114, 	/* prs.tenant_id_default_val_enable .. prs.tenant_id_default_val_ttag (9 regs) */
	0x0407c11e, 	/* prs.tenant_id_mask_eth_nge .. prs.tenant_id_default_val_ip_nge (4 regs) */
	0x0407c140, 	/* prs.ports_arb_scheme .. prs.max_packet_size (4 regs) */
	0x0b07c1c1, 	/* prs.llc_type_threshold .. prs.icmpv4_protocol (11 regs) */
	0x0107c1cd, 	/* prs.gre_protocol (1 regs) */
	0x0e07c1d1, 	/* prs.fcoe_type .. prs.tag_len_5 (14 regs) */
	0x0407c1ef, 	/* prs.dst_mac_global_0 .. prs.dst_mac_global_mask_1 (4 regs) */
	0x0107c21a, 	/* prs.nge_eth_type (1 regs) */
	0x0107c21c, 	/* prs.rroce_port (1 regs) */
	0x0507c240, 	/* prs.l2_irreg_cases .. prs.light_l2 (5 regs) */
	0x0307c24d, 	/* prs.l2_regular_pkt .. prs.def_l2_con_type (3 regs) */
	0x0607c254, 	/* prs.light_l2_ethertype_0 .. prs.light_l2_ethertype_5 (6 regs) */
	0x0307c25c, 	/* prs.dst_mac_select .. prs.vlan_tag_select (3 regs) */
	0x0407c270, 	/* prs.mac_vlan_flex_upper .. prs.mac_vlan_flex_bitmask_1 (4 regs) */
	0x0107c275, 	/* prs.sack_blk_override (1 regs) */
	0x0807c277, 	/* prs.rdma_syn_seed_0 .. prs.rdma_syn_seed_7 (8 regs) */
	0x0207c2c0, 	/* prs.num_of_cfc_flush_messages .. prs.num_of_transparent_flush_messages (2 regs) */
	0x0887c2cc, 	/* prs.fifo_empty_flags .. prs.fifo_full_flags (8 regs, WB) */
	0x0107c2d7, 	/* prs.stop_parsing_status (1 regs) */
	0x0287c2d8, 	/* prs.mini_cache_entry (2 regs, WB) */
	0x0407c2da, 	/* prs.mini_cache_failed_response .. prs.dbg_shift (4 regs) */
	0x0207c2e8, 	/* prs.dbg_force_valid .. prs.dbg_force_frame (2 regs) */
	0x0207c3c1, 	/* prs.ccfc_search_initial_credit .. prs.tcfc_search_initial_credit (2 regs) */
	0x0a07c3c4, 	/* prs.ccfc_search_current_credit .. prs.eop_req_ct (10 regs) */
	0x0f07c460, 	/* prs.gft_hash_key_0 .. prs.gft_tunnel_vlan_select (15 regs) */
	0x0407c471, 	/* prs.gft_connection_type .. prs.gft_cam_scrub_miss_en (4 regs) */
	0x0a240000, 	/* block tsdm */
	0x053ec001, 	/* tsdm.enable_in1 .. tsdm.disable_engine (5 regs) */
	0x023ec010, 	/* tsdm.INT_STS .. tsdm.INT_MASK (2 regs) */
	0x023ec100, 	/* tsdm.timer_tick .. tsdm.timers_tick_enable (2 regs) */
	0x093ec103, 	/* tsdm.grc_privilege_level .. tsdm.eco_reserved (9 regs) */
	0x053ec140, 	/* tsdm.init_credit_pxp .. tsdm.init_credit_cm (5 regs) */
	0x0c3ec180, 	/* tsdm.num_of_dma_cmd .. tsdm.num_of_dpm_req (12 regs) */
	0x033ec1c0, 	/* tsdm.brb_almost_full .. tsdm.dorq_almost_full (3 regs) */
	0x203ec300, 	/* tsdm.queue_full .. tsdm.prm_fifo_full (32 regs) */
	0x1a3ec340, 	/* tsdm.int_cmpl_pend_empty .. tsdm.prm_fifo_empty (26 regs) */
	0x053ec38a, 	/* tsdm.dbg_select .. tsdm.dbg_force_frame (5 regs) */
	0x0a250000, 	/* block msdm */
	0x053f0001, 	/* msdm.enable_in1 .. msdm.disable_engine (5 regs) */
	0x023f0010, 	/* msdm.INT_STS .. msdm.INT_MASK (2 regs) */
	0x023f0100, 	/* msdm.timer_tick .. msdm.timers_tick_enable (2 regs) */
	0x093f0103, 	/* msdm.grc_privilege_level .. msdm.eco_reserved (9 regs) */
	0x053f0140, 	/* msdm.init_credit_pxp .. msdm.init_credit_cm (5 regs) */
	0x0c3f0180, 	/* msdm.num_of_dma_cmd .. msdm.num_of_dpm_req (12 regs) */
	0x033f01c0, 	/* msdm.brb_almost_full .. msdm.dorq_almost_full (3 regs) */
	0x203f0300, 	/* msdm.queue_full .. msdm.prm_fifo_full (32 regs) */
	0x1a3f0340, 	/* msdm.int_cmpl_pend_empty .. msdm.prm_fifo_empty (26 regs) */
	0x053f038a, 	/* msdm.dbg_select .. msdm.dbg_force_frame (5 regs) */
	0x0a260000, 	/* block usdm */
	0x053f4001, 	/* usdm.enable_in1 .. usdm.disable_engine (5 regs) */
	0x023f4010, 	/* usdm.INT_STS .. usdm.INT_MASK (2 regs) */
	0x023f4100, 	/* usdm.timer_tick .. usdm.timers_tick_enable (2 regs) */
	0x093f4103, 	/* usdm.grc_privilege_level .. usdm.eco_reserved (9 regs) */
	0x053f4140, 	/* usdm.init_credit_pxp .. usdm.init_credit_cm (5 regs) */
	0x0c3f4180, 	/* usdm.num_of_dma_cmd .. usdm.num_of_dpm_req (12 regs) */
	0x033f41c0, 	/* usdm.brb_almost_full .. usdm.dorq_almost_full (3 regs) */
	0x203f4300, 	/* usdm.queue_full .. usdm.prm_fifo_full (32 regs) */
	0x1a3f4340, 	/* usdm.int_cmpl_pend_empty .. usdm.prm_fifo_empty (26 regs) */
	0x053f438a, 	/* usdm.dbg_select .. usdm.dbg_force_frame (5 regs) */
	0x0b270000, 	/* block xsdm */
	0x053e0001, 	/* xsdm.enable_in1 .. xsdm.disable_engine (5 regs) */
	0x023e0010, 	/* xsdm.INT_STS .. xsdm.INT_MASK (2 regs) */
	0x023e0100, 	/* xsdm.timer_tick .. xsdm.timers_tick_enable (2 regs) */
	0x093e0103, 	/* xsdm.grc_privilege_level .. xsdm.eco_reserved (9 regs) */
	0x053e0140, 	/* xsdm.init_credit_pxp .. xsdm.init_credit_cm (5 regs) */
	0x013e0148, 	/* xsdm.init_credit_cm_rmt (1 regs) */
	0x0c3e0180, 	/* xsdm.num_of_dma_cmd .. xsdm.num_of_dpm_req (12 regs) */
	0x033e01c0, 	/* xsdm.brb_almost_full .. xsdm.dorq_almost_full (3 regs) */
	0x203e0300, 	/* xsdm.queue_full .. xsdm.prm_fifo_full (32 regs) */
	0x1a3e0340, 	/* xsdm.int_cmpl_pend_empty .. xsdm.prm_fifo_empty (26 regs) */
	0x053e038a, 	/* xsdm.dbg_select .. xsdm.dbg_force_frame (5 regs) */
	0x0a280000, 	/* block ysdm */
	0x053e4001, 	/* ysdm.enable_in1 .. ysdm.disable_engine (5 regs) */
	0x023e4010, 	/* ysdm.INT_STS .. ysdm.INT_MASK (2 regs) */
	0x023e4100, 	/* ysdm.timer_tick .. ysdm.timers_tick_enable (2 regs) */
	0x093e4103, 	/* ysdm.grc_privilege_level .. ysdm.eco_reserved (9 regs) */
	0x053e4140, 	/* ysdm.init_credit_pxp .. ysdm.init_credit_cm (5 regs) */
	0x0c3e4180, 	/* ysdm.num_of_dma_cmd .. ysdm.num_of_dpm_req (12 regs) */
	0x033e41c0, 	/* ysdm.brb_almost_full .. ysdm.dorq_almost_full (3 regs) */
	0x203e4300, 	/* ysdm.queue_full .. ysdm.prm_fifo_full (32 regs) */
	0x1a3e4340, 	/* ysdm.int_cmpl_pend_empty .. ysdm.prm_fifo_empty (26 regs) */
	0x053e438a, 	/* ysdm.dbg_select .. ysdm.dbg_force_frame (5 regs) */
	0x0a290000, 	/* block psdm */
	0x053e8001, 	/* psdm.enable_in1 .. psdm.disable_engine (5 regs) */
	0x023e8010, 	/* psdm.INT_STS .. psdm.INT_MASK (2 regs) */
	0x023e8100, 	/* psdm.timer_tick .. psdm.timers_tick_enable (2 regs) */
	0x093e8103, 	/* psdm.grc_privilege_level .. psdm.eco_reserved (9 regs) */
	0x053e8140, 	/* psdm.init_credit_pxp .. psdm.init_credit_cm (5 regs) */
	0x0c3e8180, 	/* psdm.num_of_dma_cmd .. psdm.num_of_dpm_req (12 regs) */
	0x033e81c0, 	/* psdm.brb_almost_full .. psdm.dorq_almost_full (3 regs) */
	0x203e8300, 	/* psdm.queue_full .. psdm.prm_fifo_full (32 regs) */
	0x1a3e8340, 	/* psdm.int_cmpl_pend_empty .. psdm.prm_fifo_empty (26 regs) */
	0x053e838a, 	/* psdm.dbg_select .. psdm.dbg_force_frame (5 regs) */
	0x252a0000, 	/* block tsem */
	0x025c0010, 	/* tsem.INT_STS_0 .. tsem.INT_MASK_0 (2 regs) */
	0x025c0014, 	/* tsem.INT_STS_1 .. tsem.INT_MASK_1 (2 regs) */
	0x035c0110, 	/* tsem.pf_err_vector .. tsem.exception_int (3 regs) */
	0x025c0116, 	/* tsem.allow_lp_sleep_thrd .. tsem.eco_reserved (2 regs) */
	0x025c01a0, 	/* tsem.foc_credit (2 regs) */
	0x015c02c0, 	/* tsem.num_of_threads (1 regs) */
	0x025c0400, 	/* tsem.dbg_alm_full .. tsem.passive_alm_full (2 regs) */
	0x015c0403, 	/* tsem.sync_ram_wr_alm_full (1 regs) */
	0x015c0441, 	/* tsem.ext_pas_empty (1 regs) */
	0x015c0448, 	/* tsem.fic_empty (1 regs) */
	0x025c0454, 	/* tsem.slow_ext_store_empty .. tsem.slow_ext_load_empty (2 regs) */
	0x025c0457, 	/* tsem.slow_ram_wr_empty .. tsem.sync_dbg_empty (2 regs) */
	0x025c0480, 	/* tsem.ext_pas_full .. tsem.ext_store_if_full (2 regs) */
	0x015c0488, 	/* tsem.fic_full (1 regs) */
	0x015c0491, 	/* tsem.ram_if_full (1 regs) */
	0x065c0497, 	/* tsem.slow_ext_store_full .. tsem.sync_dbg_full (6 regs) */
	0x055c054a, 	/* tsem.dbg_select .. tsem.dbg_force_frame (5 regs) */
	0x025d0010, 	/* tsem.fast_memory.INT_STS .. tsem.fast_memory.INT_MASK (2 regs) */
	0x025d0120, 	/* tsem.fast_memory.gpre0 .. tsem.fast_memory.stall_mask (2 regs) */
	0x045d0128, 	/* tsem.fast_memory.storm_stack_size .. tsem.fast_memory.pram_prty_addr_high (4 regs) */
	0x025d012e, 	/* tsem.fast_memory.port_id_width .. tsem.fast_memory.port_id_offset (2 regs) */
	0x015d0131, 	/* tsem.fast_memory.state_machine (1 regs) */
	0x035d0133, 	/* tsem.fast_memory.iram_ecc_error_inj .. tsem.fast_memory.storm_pc (3 regs) */
	0x0e5d018a, 	/* tsem.fast_memory.rt_clk_enable .. tsem.fast_memory.cam_init_in_process (14 regs) */
	0x0d5d01d0, 	/* tsem.fast_memory.debug_active .. tsem.fast_memory.dbg_store_addr_value (13 regs) */
	0x045d0210, 	/* tsem.fast_memory.sync_dra_rd_alm_full .. tsem.fast_memory.dbg_alm_full (4 regs) */
	0x035d0250, 	/* tsem.fast_memory.full .. tsem.fast_memory.alm_full (3 regs) */
	0x015d0290, 	/* tsem.fast_memory.active_filter_enable (1 regs) */
	0x015d0292, 	/* tsem.fast_memory.stall_cycles_mask (1 regs) */
	0x015d02d3, 	/* tsem.fast_memory.vfc_status (1 regs) */
	0x045d0310, 	/* tsem.fast_memory.cam_bist_en .. tsem.fast_memory.cam_bist_status (4 regs) */
	0x0e5d2800, 	/* tsem.fast_memory.vfc_config.mask_lsb_0_low .. tsem.fast_memory.vfc_config.indications2 (14 regs) */
	0x055d280f, 	/* tsem.fast_memory.vfc_config.memories_rst .. tsem.fast_memory.vfc_config.interrupt_mask (5 regs) */
	0x055d2816, 	/* tsem.fast_memory.vfc_config.inp_fifo_tm .. tsem.fast_memory.vfc_config.vfc_cam_bist_status (5 regs) */
	0x065d281c, 	/* tsem.fast_memory.vfc_config.inp_fifo_alm_full .. tsem.fast_memory.vfc_config.cpu_mbist_memctrl_1_cntrl_cmd (6 regs) */
	0x125d2824, 	/* tsem.fast_memory.vfc_config.debug_data .. tsem.fast_memory.vfc_config.mask_lsb_7_high (18 regs) */
	0x0c5d283e, 	/* tsem.fast_memory.vfc_config.offset_alu_vector_0 .. tsem.fast_memory.vfc_config.cam_bist_skip_error_cnt (12 regs) */
	0x1f2b0000, 	/* block msem */
	0x02600010, 	/* msem.INT_STS_0 .. msem.INT_MASK_0 (2 regs) */
	0x02600014, 	/* msem.INT_STS_1 .. msem.INT_MASK_1 (2 regs) */
	0x03600110, 	/* msem.pf_err_vector .. msem.exception_int (3 regs) */
	0x02600116, 	/* msem.allow_lp_sleep_thrd .. msem.eco_reserved (2 regs) */
	0x066001a0, 	/* msem.foc_credit (6 regs) */
	0x016002c0, 	/* msem.num_of_threads (1 regs) */
	0x02600400, 	/* msem.dbg_alm_full .. msem.passive_alm_full (2 regs) */
	0x01600403, 	/* msem.sync_ram_wr_alm_full (1 regs) */
	0x01600441, 	/* msem.ext_pas_empty (1 regs) */
	0x01600448, 	/* msem.fic_empty (1 regs) */
	0x02600454, 	/* msem.slow_ext_store_empty .. msem.slow_ext_load_empty (2 regs) */
	0x02600457, 	/* msem.slow_ram_wr_empty .. msem.sync_dbg_empty (2 regs) */
	0x02600480, 	/* msem.ext_pas_full .. msem.ext_store_if_full (2 regs) */
	0x01600488, 	/* msem.fic_full (1 regs) */
	0x01600491, 	/* msem.ram_if_full (1 regs) */
	0x06600497, 	/* msem.slow_ext_store_full .. msem.sync_dbg_full (6 regs) */
	0x0560054a, 	/* msem.dbg_select .. msem.dbg_force_frame (5 regs) */
	0x02610010, 	/* msem.fast_memory.INT_STS .. msem.fast_memory.INT_MASK (2 regs) */
	0x02610120, 	/* msem.fast_memory.gpre0 .. msem.fast_memory.stall_mask (2 regs) */
	0x04610128, 	/* msem.fast_memory.storm_stack_size .. msem.fast_memory.pram_prty_addr_high (4 regs) */
	0x0261012e, 	/* msem.fast_memory.port_id_width .. msem.fast_memory.port_id_offset (2 regs) */
	0x01610131, 	/* msem.fast_memory.state_machine (1 regs) */
	0x03610133, 	/* msem.fast_memory.iram_ecc_error_inj .. msem.fast_memory.storm_pc (3 regs) */
	0x0e61018a, 	/* msem.fast_memory.rt_clk_enable .. msem.fast_memory.cam_init_in_process (14 regs) */
	0x0d6101d0, 	/* msem.fast_memory.debug_active .. msem.fast_memory.dbg_store_addr_value (13 regs) */
	0x04610210, 	/* msem.fast_memory.sync_dra_rd_alm_full .. msem.fast_memory.dbg_alm_full (4 regs) */
	0x03610250, 	/* msem.fast_memory.full .. msem.fast_memory.alm_full (3 regs) */
	0x01610290, 	/* msem.fast_memory.active_filter_enable (1 regs) */
	0x01610292, 	/* msem.fast_memory.stall_cycles_mask (1 regs) */
	0x016102d3, 	/* msem.fast_memory.vfc_status (1 regs) */
	0x04610310, 	/* msem.fast_memory.cam_bist_en .. msem.fast_memory.cam_bist_status (4 regs) */
	0x1f2c0000, 	/* block usem */
	0x02640010, 	/* usem.INT_STS_0 .. usem.INT_MASK_0 (2 regs) */
	0x02640014, 	/* usem.INT_STS_1 .. usem.INT_MASK_1 (2 regs) */
	0x03640110, 	/* usem.pf_err_vector .. usem.exception_int (3 regs) */
	0x02640116, 	/* usem.allow_lp_sleep_thrd .. usem.eco_reserved (2 regs) */
	0x056401a0, 	/* usem.foc_credit (5 regs) */
	0x016402c0, 	/* usem.num_of_threads (1 regs) */
	0x02640400, 	/* usem.dbg_alm_full .. usem.passive_alm_full (2 regs) */
	0x01640403, 	/* usem.sync_ram_wr_alm_full (1 regs) */
	0x01640441, 	/* usem.ext_pas_empty (1 regs) */
	0x01640448, 	/* usem.fic_empty (1 regs) */
	0x02640454, 	/* usem.slow_ext_store_empty .. usem.slow_ext_load_empty (2 regs) */
	0x02640457, 	/* usem.slow_ram_wr_empty .. usem.sync_dbg_empty (2 regs) */
	0x02640480, 	/* usem.ext_pas_full .. usem.ext_store_if_full (2 regs) */
	0x01640488, 	/* usem.fic_full (1 regs) */
	0x01640491, 	/* usem.ram_if_full (1 regs) */
	0x06640497, 	/* usem.slow_ext_store_full .. usem.sync_dbg_full (6 regs) */
	0x0564054a, 	/* usem.dbg_select .. usem.dbg_force_frame (5 regs) */
	0x02650010, 	/* usem.fast_memory.INT_STS .. usem.fast_memory.INT_MASK (2 regs) */
	0x02650120, 	/* usem.fast_memory.gpre0 .. usem.fast_memory.stall_mask (2 regs) */
	0x04650128, 	/* usem.fast_memory.storm_stack_size .. usem.fast_memory.pram_prty_addr_high (4 regs) */
	0x0265012e, 	/* usem.fast_memory.port_id_width .. usem.fast_memory.port_id_offset (2 regs) */
	0x01650131, 	/* usem.fast_memory.state_machine (1 regs) */
	0x03650133, 	/* usem.fast_memory.iram_ecc_error_inj .. usem.fast_memory.storm_pc (3 regs) */
	0x0e65018a, 	/* usem.fast_memory.rt_clk_enable .. usem.fast_memory.cam_init_in_process (14 regs) */
	0x0d6501d0, 	/* usem.fast_memory.debug_active .. usem.fast_memory.dbg_store_addr_value (13 regs) */
	0x04650210, 	/* usem.fast_memory.sync_dra_rd_alm_full .. usem.fast_memory.dbg_alm_full (4 regs) */
	0x03650250, 	/* usem.fast_memory.full .. usem.fast_memory.alm_full (3 regs) */
	0x01650290, 	/* usem.fast_memory.active_filter_enable (1 regs) */
	0x01650292, 	/* usem.fast_memory.stall_cycles_mask (1 regs) */
	0x016502d3, 	/* usem.fast_memory.vfc_status (1 regs) */
	0x04650310, 	/* usem.fast_memory.cam_bist_en .. usem.fast_memory.cam_bist_status (4 regs) */
	0x1d2d0000, 	/* block xsem */
	0x02500010, 	/* xsem.INT_STS_0 .. xsem.INT_MASK_0 (2 regs) */
	0x02500014, 	/* xsem.INT_STS_1 .. xsem.INT_MASK_1 (2 regs) */
	0x03500110, 	/* xsem.pf_err_vector .. xsem.exception_int (3 regs) */
	0x02500116, 	/* xsem.allow_lp_sleep_thrd .. xsem.eco_reserved (2 regs) */
	0x025001a0, 	/* xsem.foc_credit (2 regs) */
	0x015002c0, 	/* xsem.num_of_threads (1 regs) */
	0x02500400, 	/* xsem.dbg_alm_full .. xsem.passive_alm_full (2 regs) */
	0x01500403, 	/* xsem.sync_ram_wr_alm_full (1 regs) */
	0x01500441, 	/* xsem.ext_pas_empty (1 regs) */
	0x02500454, 	/* xsem.slow_ext_store_empty .. xsem.slow_ext_load_empty (2 regs) */
	0x02500457, 	/* xsem.slow_ram_wr_empty .. xsem.sync_dbg_empty (2 regs) */
	0x02500480, 	/* xsem.ext_pas_full .. xsem.ext_store_if_full (2 regs) */
	0x01500491, 	/* xsem.ram_if_full (1 regs) */
	0x06500497, 	/* xsem.slow_ext_store_full .. xsem.sync_dbg_full (6 regs) */
	0x0550054a, 	/* xsem.dbg_select .. xsem.dbg_force_frame (5 regs) */
	0x02510010, 	/* xsem.fast_memory.INT_STS .. xsem.fast_memory.INT_MASK (2 regs) */
	0x02510120, 	/* xsem.fast_memory.gpre0 .. xsem.fast_memory.stall_mask (2 regs) */
	0x04510128, 	/* xsem.fast_memory.storm_stack_size .. xsem.fast_memory.pram_prty_addr_high (4 regs) */
	0x0251012e, 	/* xsem.fast_memory.port_id_width .. xsem.fast_memory.port_id_offset (2 regs) */
	0x01510131, 	/* xsem.fast_memory.state_machine (1 regs) */
	0x03510133, 	/* xsem.fast_memory.iram_ecc_error_inj .. xsem.fast_memory.storm_pc (3 regs) */
	0x0e51018a, 	/* xsem.fast_memory.rt_clk_enable .. xsem.fast_memory.cam_init_in_process (14 regs) */
	0x0d5101d0, 	/* xsem.fast_memory.debug_active .. xsem.fast_memory.dbg_store_addr_value (13 regs) */
	0x04510210, 	/* xsem.fast_memory.sync_dra_rd_alm_full .. xsem.fast_memory.dbg_alm_full (4 regs) */
	0x03510250, 	/* xsem.fast_memory.full .. xsem.fast_memory.alm_full (3 regs) */
	0x01510290, 	/* xsem.fast_memory.active_filter_enable (1 regs) */
	0x01510292, 	/* xsem.fast_memory.stall_cycles_mask (1 regs) */
	0x015102d3, 	/* xsem.fast_memory.vfc_status (1 regs) */
	0x04510310, 	/* xsem.fast_memory.cam_bist_en .. xsem.fast_memory.cam_bist_status (4 regs) */
	0x1d2e0000, 	/* block ysem */
	0x02540010, 	/* ysem.INT_STS_0 .. ysem.INT_MASK_0 (2 regs) */
	0x02540014, 	/* ysem.INT_STS_1 .. ysem.INT_MASK_1 (2 regs) */
	0x03540110, 	/* ysem.pf_err_vector .. ysem.exception_int (3 regs) */
	0x02540116, 	/* ysem.allow_lp_sleep_thrd .. ysem.eco_reserved (2 regs) */
	0x065401a0, 	/* ysem.foc_credit (6 regs) */
	0x015402c0, 	/* ysem.num_of_threads (1 regs) */
	0x02540400, 	/* ysem.dbg_alm_full .. ysem.passive_alm_full (2 regs) */
	0x01540403, 	/* ysem.sync_ram_wr_alm_full (1 regs) */
	0x01540441, 	/* ysem.ext_pas_empty (1 regs) */
	0x02540454, 	/* ysem.slow_ext_store_empty .. ysem.slow_ext_load_empty (2 regs) */
	0x02540457, 	/* ysem.slow_ram_wr_empty .. ysem.sync_dbg_empty (2 regs) */
	0x02540480, 	/* ysem.ext_pas_full .. ysem.ext_store_if_full (2 regs) */
	0x01540491, 	/* ysem.ram_if_full (1 regs) */
	0x06540497, 	/* ysem.slow_ext_store_full .. ysem.sync_dbg_full (6 regs) */
	0x0554054a, 	/* ysem.dbg_select .. ysem.dbg_force_frame (5 regs) */
	0x02550010, 	/* ysem.fast_memory.INT_STS .. ysem.fast_memory.INT_MASK (2 regs) */
	0x02550120, 	/* ysem.fast_memory.gpre0 .. ysem.fast_memory.stall_mask (2 regs) */
	0x04550128, 	/* ysem.fast_memory.storm_stack_size .. ysem.fast_memory.pram_prty_addr_high (4 regs) */
	0x0255012e, 	/* ysem.fast_memory.port_id_width .. ysem.fast_memory.port_id_offset (2 regs) */
	0x01550131, 	/* ysem.fast_memory.state_machine (1 regs) */
	0x03550133, 	/* ysem.fast_memory.iram_ecc_error_inj .. ysem.fast_memory.storm_pc (3 regs) */
	0x0e55018a, 	/* ysem.fast_memory.rt_clk_enable .. ysem.fast_memory.cam_init_in_process (14 regs) */
	0x0d5501d0, 	/* ysem.fast_memory.debug_active .. ysem.fast_memory.dbg_store_addr_value (13 regs) */
	0x04550210, 	/* ysem.fast_memory.sync_dra_rd_alm_full .. ysem.fast_memory.dbg_alm_full (4 regs) */
	0x03550250, 	/* ysem.fast_memory.full .. ysem.fast_memory.alm_full (3 regs) */
	0x01550290, 	/* ysem.fast_memory.active_filter_enable (1 regs) */
	0x01550292, 	/* ysem.fast_memory.stall_cycles_mask (1 regs) */
	0x015502d3, 	/* ysem.fast_memory.vfc_status (1 regs) */
	0x04550310, 	/* ysem.fast_memory.cam_bist_en .. ysem.fast_memory.cam_bist_status (4 regs) */
	0x1f2f0000, 	/* block psem */
	0x02580010, 	/* psem.INT_STS_0 .. psem.INT_MASK_0 (2 regs) */
	0x02580014, 	/* psem.INT_STS_1 .. psem.INT_MASK_1 (2 regs) */
	0x03580110, 	/* psem.pf_err_vector .. psem.exception_int (3 regs) */
	0x02580116, 	/* psem.allow_lp_sleep_thrd .. psem.eco_reserved (2 regs) */
	0x025801a0, 	/* psem.foc_credit (2 regs) */
	0x015802c0, 	/* psem.num_of_threads (1 regs) */
	0x02580400, 	/* psem.dbg_alm_full .. psem.passive_alm_full (2 regs) */
	0x01580403, 	/* psem.sync_ram_wr_alm_full (1 regs) */
	0x01580441, 	/* psem.ext_pas_empty (1 regs) */
	0x01580448, 	/* psem.fic_empty (1 regs) */
	0x02580454, 	/* psem.slow_ext_store_empty .. psem.slow_ext_load_empty (2 regs) */
	0x02580457, 	/* psem.slow_ram_wr_empty .. psem.sync_dbg_empty (2 regs) */
	0x02580480, 	/* psem.ext_pas_full .. psem.ext_store_if_full (2 regs) */
	0x01580488, 	/* psem.fic_full (1 regs) */
	0x01580491, 	/* psem.ram_if_full (1 regs) */
	0x06580497, 	/* psem.slow_ext_store_full .. psem.sync_dbg_full (6 regs) */
	0x0558054a, 	/* psem.dbg_select .. psem.dbg_force_frame (5 regs) */
	0x02590010, 	/* psem.fast_memory.INT_STS .. psem.fast_memory.INT_MASK (2 regs) */
	0x02590120, 	/* psem.fast_memory.gpre0 .. psem.fast_memory.stall_mask (2 regs) */
	0x04590128, 	/* psem.fast_memory.storm_stack_size .. psem.fast_memory.pram_prty_addr_high (4 regs) */
	0x0259012e, 	/* psem.fast_memory.port_id_width .. psem.fast_memory.port_id_offset (2 regs) */
	0x01590131, 	/* psem.fast_memory.state_machine (1 regs) */
	0x03590133, 	/* psem.fast_memory.iram_ecc_error_inj .. psem.fast_memory.storm_pc (3 regs) */
	0x0e59018a, 	/* psem.fast_memory.rt_clk_enable .. psem.fast_memory.cam_init_in_process (14 regs) */
	0x0d5901d0, 	/* psem.fast_memory.debug_active .. psem.fast_memory.dbg_store_addr_value (13 regs) */
	0x04590210, 	/* psem.fast_memory.sync_dra_rd_alm_full .. psem.fast_memory.dbg_alm_full (4 regs) */
	0x03590250, 	/* psem.fast_memory.full .. psem.fast_memory.alm_full (3 regs) */
	0x01590290, 	/* psem.fast_memory.active_filter_enable (1 regs) */
	0x01590292, 	/* psem.fast_memory.stall_cycles_mask (1 regs) */
	0x015902d3, 	/* psem.fast_memory.vfc_status (1 regs) */
	0x04590310, 	/* psem.fast_memory.cam_bist_en .. psem.fast_memory.cam_bist_status (4 regs) */
	0x04300000, 	/* block rss */
	0x0308e201, 	/* rss.rss_init_en .. rss.if_enable (3 regs) */
	0x0208e260, 	/* rss.INT_STS .. rss.INT_MASK (2 regs) */
	0x0208e300, 	/* rss.key_rss_ext5 .. rss.tmld_credit (2 regs) */
	0x0608e312, 	/* rss.eco_reserved .. rss.dbg_force_frame (6 regs) */
	0x06310000, 	/* block tmld */
	0x01134000, 	/* tmld.scbd_strict_prio (1 regs) */
	0x2c134003, 	/* tmld.foci_foc_credits .. tmld.cm_hdr_127_96 (44 regs) */
	0x05134030, 	/* tmld.stat_fic_msg .. tmld.len_err_log_2 (5 regs) */
	0x01134036, 	/* tmld.len_err_log_v (1 regs) */
	0x02134060, 	/* tmld.INT_STS .. tmld.INT_MASK (2 regs) */
	0x05134580, 	/* tmld.dbg_select .. tmld.dbg_force_frame (5 regs) */
	0x05320000, 	/* block muld */
	0x38138000, 	/* muld.scbd_strict_prio .. muld.cm_hdr_127_96 (56 regs) */
	0x05138039, 	/* muld.stat_fic_msg .. muld.len_err_log_2 (5 regs) */
	0x0113803f, 	/* muld.len_err_log_v (1 regs) */
	0x02138060, 	/* muld.INT_STS .. muld.INT_MASK (2 regs) */
	0x05138580, 	/* muld.dbg_select .. muld.dbg_force_frame (5 regs) */
	0x07340000, 	/* block xyld */
	0x01130000, 	/* xyld.scbd_strict_prio (1 regs) */
	0x0a130003, 	/* xyld.foci_foc_credits .. xyld.tid_req_credits (10 regs) */
	0x2313000e, 	/* xyld.tid_remain_credits .. xyld.cm_hdr_127_96 (35 regs) */
	0x05130038, 	/* xyld.stat_fic_msg .. xyld.len_err_log_2 (5 regs) */
	0x0113003e, 	/* xyld.len_err_log_v (1 regs) */
	0x02130060, 	/* xyld.INT_STS .. xyld.INT_MASK (2 regs) */
	0x05130580, 	/* xyld.dbg_select .. xyld.dbg_force_frame (5 regs) */
	0x06370000, 	/* block prm */
	0x0608c000, 	/* prm.disable_prm .. prm.disable_outputs (6 regs) */
	0x0208c010, 	/* prm.INT_STS .. prm.INT_MASK (2 regs) */
	0x0508c108, 	/* prm.pad_data .. prm.init_credit_rdif_pth (5 regs) */
	0x0508c140, 	/* prm.rpb_db_full_thr .. prm.pxp_resp_full_thr (5 regs) */
	0x0208c180, 	/* prm.num_of_mstorm_cmd .. prm.num_of_ustorm_cmd (2 regs) */
	0x0508c1aa, 	/* prm.dbg_select .. prm.dbg_force_frame (5 regs) */
	0x05380000, 	/* block pbf_pb1 */
	0x02368010, 	/* pbf_pb1.INT_STS .. pbf_pb1.INT_MASK (2 regs) */
	0x0d368100, 	/* pbf_pb1.control .. pbf_pb1.crc_mask_3_3 (13 regs) */
	0x09368140, 	/* pbf_pb1.db_empty .. pbf_pb1.tq_th_empty (9 regs) */
	0x06368180, 	/* pbf_pb1.errored_crc .. pbf_pb1.eco_reserved (6 regs) */
	0x053681ca, 	/* pbf_pb1.dbg_select .. pbf_pb1.dbg_force_frame (5 regs) */
	0x05390000, 	/* block pbf_pb2 */
	0x02369010, 	/* pbf_pb2.INT_STS .. pbf_pb2.INT_MASK (2 regs) */
	0x0d369100, 	/* pbf_pb2.control .. pbf_pb2.crc_mask_3_3 (13 regs) */
	0x09369140, 	/* pbf_pb2.db_empty .. pbf_pb2.tq_th_empty (9 regs) */
	0x06369180, 	/* pbf_pb2.errored_crc .. pbf_pb2.eco_reserved (6 regs) */
	0x053691ca, 	/* pbf_pb2.dbg_select .. pbf_pb2.dbg_force_frame (5 regs) */
	0x053a0000, 	/* block rpb */
	0x0208f010, 	/* rpb.INT_STS .. rpb.INT_MASK (2 regs) */
	0x0d08f100, 	/* rpb.control .. rpb.crc_mask_3_3 (13 regs) */
	0x0908f140, 	/* rpb.db_empty .. rpb.tq_th_empty (9 regs) */
	0x0608f180, 	/* rpb.errored_crc .. rpb.eco_reserved (6 regs) */
	0x0508f1ca, 	/* rpb.dbg_select .. rpb.dbg_force_frame (5 regs) */
	0x173b0000, 	/* block btb */
	0x0336c001, 	/* btb.hw_init_en .. btb.start_en (3 regs) */
	0x0236c030, 	/* btb.INT_STS_0 .. btb.INT_MASK_0 (2 regs) */
	0x0236c036, 	/* btb.INT_STS_1 .. btb.INT_MASK_1 (2 regs) */
	0x0236c03c, 	/* btb.INT_STS_2 .. btb.INT_MASK_2 (2 regs) */
	0x0236c042, 	/* btb.INT_STS_3 .. btb.INT_MASK_3 (2 regs) */
	0x0236c048, 	/* btb.INT_STS_4 .. btb.INT_MASK_4 (2 regs) */
	0x0236c04e, 	/* btb.INT_STS_5 .. btb.INT_MASK_5 (2 regs) */
	0x0236c054, 	/* btb.INT_STS_6 .. btb.INT_MASK_6 (2 regs) */
	0x0236c061, 	/* btb.INT_STS_8 .. btb.INT_MASK_8 (2 regs) */
	0x0236c067, 	/* btb.INT_STS_9 .. btb.INT_MASK_9 (2 regs) */
	0x0236c06d, 	/* btb.INT_STS_10 .. btb.INT_MASK_10 (2 regs) */
	0x0236c073, 	/* btb.INT_STS_11 .. btb.INT_MASK_11 (2 regs) */
	0x0236c200, 	/* btb.big_ram_address .. btb.header_size (2 regs) */
	0x0536c210, 	/* btb.max_releases .. btb.rc_pkt_priority (5 regs) */
	0x1436c223, 	/* btb.rc_sop_priority .. btb.dbg_force_frame (20 regs) */
	0x0636c242, 	/* btb.inp_if_enable .. btb.wc_empty_0 (6 regs) */
	0x0136c257, 	/* btb.wc_full_0 (1 regs) */
	0x0636c267, 	/* btb.wc_bandwidth_if_full .. btb.rc_pkt_empty_3 (6 regs) */
	0x0436c278, 	/* btb.rc_pkt_full_0 .. btb.rc_pkt_full_3 (4 regs) */
	0x0436c287, 	/* btb.rc_pkt_status_0 .. btb.rc_pkt_status_3 (4 regs) */
	0x0936c296, 	/* btb.rc_sop_empty .. btb.wc_sync_fifo_push_status_0 (9 regs) */
	0x0236c2ad, 	/* btb.rls_sync_fifo_push_status .. btb.rc_pkt_state (2 regs) */
	0x0136c2b2, 	/* btb.clocks_ratio (1 regs) */
	0x293c0000, 	/* block pbf */
	0x01360000, 	/* pbf.init (1 regs) */
	0x01360010, 	/* pbf.if_enable_reg (1 regs) */
	0x05360018, 	/* pbf.dbg_select .. pbf.dbg_force_frame (5 regs) */
	0x0336002a, 	/* pbf.fc_dbg_select .. pbf.fc_dbg_shift (3 regs) */
	0x08b60030, 	/* pbf.fc_dbg_out_data (8 regs, WB) */
	0x04360038, 	/* pbf.fc_dbg_force_valid .. pbf.fc_dbg_out_frame (4 regs) */
	0x01360043, 	/* pbf.memctrl_status (1 regs) */
	0x02360060, 	/* pbf.INT_STS .. pbf.INT_MASK (2 regs) */
	0x06360100, 	/* pbf.pxp_req_if_init_crd .. pbf.tm_if_init_crd (6 regs) */
	0x05360107, 	/* pbf.tcm_if_init_crd .. pbf.ycm_if_init_crd (5 regs) */
	0x08360110, 	/* pbf.pb1_db_almost_full_thrsh .. pbf.mrku_almost_full_thrsh (8 regs) */
	0x04360120, 	/* pbf.tag_ethertype_0 .. pbf.tag_ethertype_3 (4 regs) */
	0x04360126, 	/* pbf.tag_len_0 .. pbf.tag_len_3 (4 regs) */
	0x0836013e, 	/* pbf.llc_type_threshold .. pbf.gre_protocol (8 regs) */
	0x01360148, 	/* pbf.nge_eth_type (1 regs) */
	0x01360161, 	/* pbf.regular_inband_tag_order (1 regs) */
	0x07360163, 	/* pbf.dst_mac_global_0 .. pbf.udp_dst_port_cfg_2 (7 regs) */
	0x02360175, 	/* pbf.l2_edpm_thrsh .. pbf.cpmu_thrsh (2 regs) */
	0x0e360180, 	/* pbf.ip_id_mask_0 .. pbf.tcm_snd_nxt_reg_offset (14 regs) */
	0x02360190, 	/* pbf.pci_vq_id .. pbf.drop_pkt_upon_err (2 regs) */
	0x07360196, 	/* pbf.per_voq_stat_mask .. pbf.num_pkts_sent_with_drop_to_btb (7 regs) */
	0x0c3601a8, 	/* pbf.ycmd_qs_num_lines_voq0 .. pbf.num_blocks_allocated_cons_voq0 (12 regs) */
	0x0c3601b8, 	/* pbf.ycmd_qs_num_lines_voq1 .. pbf.num_blocks_allocated_cons_voq1 (12 regs) */
	0x0c3601c8, 	/* pbf.ycmd_qs_num_lines_voq2 .. pbf.num_blocks_allocated_cons_voq2 (12 regs) */
	0x0c3601d8, 	/* pbf.ycmd_qs_num_lines_voq3 .. pbf.num_blocks_allocated_cons_voq3 (12 regs) */
	0x0c3601e8, 	/* pbf.ycmd_qs_num_lines_voq4 .. pbf.num_blocks_allocated_cons_voq4 (12 regs) */
	0x0c3601f8, 	/* pbf.ycmd_qs_num_lines_voq5 .. pbf.num_blocks_allocated_cons_voq5 (12 regs) */
	0x0c360208, 	/* pbf.ycmd_qs_num_lines_voq6 .. pbf.num_blocks_allocated_cons_voq6 (12 regs) */
	0x0c360218, 	/* pbf.ycmd_qs_num_lines_voq7 .. pbf.num_blocks_allocated_cons_voq7 (12 regs) */
	0x0c360228, 	/* pbf.ycmd_qs_num_lines_voq8 .. pbf.num_blocks_allocated_cons_voq8 (12 regs) */
	0x0c360238, 	/* pbf.ycmd_qs_num_lines_voq9 .. pbf.num_blocks_allocated_cons_voq9 (12 regs) */
	0x0c360248, 	/* pbf.ycmd_qs_num_lines_voq10 .. pbf.num_blocks_allocated_cons_voq10 (12 regs) */
	0x0c360258, 	/* pbf.ycmd_qs_num_lines_voq11 .. pbf.num_blocks_allocated_cons_voq11 (12 regs) */
	0x0c360268, 	/* pbf.ycmd_qs_num_lines_voq12 .. pbf.num_blocks_allocated_cons_voq12 (12 regs) */
	0x0c360278, 	/* pbf.ycmd_qs_num_lines_voq13 .. pbf.num_blocks_allocated_cons_voq13 (12 regs) */
	0x0c360288, 	/* pbf.ycmd_qs_num_lines_voq14 .. pbf.num_blocks_allocated_cons_voq14 (12 regs) */
	0x0c360298, 	/* pbf.ycmd_qs_num_lines_voq15 .. pbf.num_blocks_allocated_cons_voq15 (12 regs) */
	0x0c3602a8, 	/* pbf.ycmd_qs_num_lines_voq16 .. pbf.num_blocks_allocated_cons_voq16 (12 regs) */
	0x0c3602b8, 	/* pbf.ycmd_qs_num_lines_voq17 .. pbf.num_blocks_allocated_cons_voq17 (12 regs) */
	0x0c3602c8, 	/* pbf.ycmd_qs_num_lines_voq18 .. pbf.num_blocks_allocated_cons_voq18 (12 regs) */
	0x0c3602d8, 	/* pbf.ycmd_qs_num_lines_voq19 .. pbf.num_blocks_allocated_cons_voq19 (12 regs) */
	0x053d0000, 	/* block rdif */
	0x040c0010, 	/* rdif.stop_on_error .. rdif.min_eob2wf_l1_rd_del (4 regs) */
	0x010c0015, 	/* rdif.dirty_l1 (1 regs) */
	0x1c0c001c, 	/* rdif.debug_command_fifo_empty .. rdif.stat_num_err_interval_0 (28 regs) */
	0x020c0060, 	/* rdif.INT_STS .. rdif.INT_MASK (2 regs) */
	0x050c0140, 	/* rdif.dbg_select .. rdif.dbg_force_frame (5 regs) */
	0x043e0000, 	/* block tdif */
	0x060c4010, 	/* tdif.stop_on_error .. tdif.dirty_l1 (6 regs) */
	0x200c401c, 	/* tdif.debug_command_fifo_empty .. tdif.stat_num_err_interval_3 (32 regs) */
	0x020c4060, 	/* tdif.INT_STS .. tdif.INT_MASK (2 regs) */
	0x050c4140, 	/* tdif.dbg_select .. tdif.dbg_force_frame (5 regs) */
	0x0a3f0000, 	/* block cdu */
	0x01160010, 	/* cdu.control0 (1 regs) */
	0x01160070, 	/* cdu.INT_STS (1 regs) */
	0x01160073, 	/* cdu.INT_MASK (1 regs) */
	0x04160100, 	/* cdu.ccfc_ctx_valid0 .. cdu.tcfc_ctx_valid1 (4 regs) */
	0x02160140, 	/* cdu.ldbuf_af_thresh .. cdu.wbbuf_af_thresh (2 regs) */
	0x04160180, 	/* cdu.ccfc_pxp .. cdu.wb_vqid (4 regs) */
	0x061601c0, 	/* cdu.debug .. cdu.dbg_force_frame (6 regs) */
	0x011601d2, 	/* cdu.eco_reserved (1 regs) */
	0x06160200, 	/* cdu.ccfc_cvld_error_data .. cdu.tcfc_wb_l1_num_error_data (6 regs) */
	0x03160240, 	/* cdu.cid_addr_params .. cdu.segment1_params (3 regs) */
	0x10400000, 	/* block ccfc */
	0x050b8000, 	/* ccfc.init_reg .. ccfc.tidram_init_done (5 regs) */
	0x020b8060, 	/* ccfc.INT_STS_0 .. ccfc.INT_MASK_0 (2 regs) */
	0x0c0b8100, 	/* ccfc.lc_blocked .. ccfc.cdu_write_backs (12 regs) */
	0x050b8140, 	/* ccfc.dbg_select .. ccfc.dbg_force_frame (5 regs) */
	0x090b8152, 	/* ccfc.eco_reserved .. ccfc.arbiters_reg (9 regs) */
	0x060b8170, 	/* ccfc.debug0 .. ccfc.cdu_pcie_err_mask (6 regs) */
	0x020b8177, 	/* ccfc.sreq_full_sticky .. ccfc.prsresp_full_sticky (2 regs) */
	0x0b0b8180, 	/* ccfc.num_lcids_empty .. ccfc.max_inside (11 regs) */
	0x020b81c4, 	/* ccfc.LoadRetry_Types .. ccfc.MiniCache_Control (2 regs) */
	0x010b81c7, 	/* ccfc.control0 (1 regs) */
	0x040b81e0, 	/* ccfc.prsresp_credit .. ccfc.cduwb_credit (4 regs) */
	0x040b8200, 	/* ccfc.ll_policy_cfg .. ccfc.empty_size (4 regs) */
	0x1f0b8240, 	/* ccfc.lc_client_0_lcid_threshold .. ccfc.wave_sm_2_one_count (31 regs) */
	0x120b8280, 	/* ccfc.cache_string_type .. ccfc.include_vlan_in_hash (18 regs) */
	0x080b82c0, 	/* ccfc.cid_cam_bist_en .. ccfc.string_cam_bist_status (8 regs) */
	0x020bb400, 	/* ccfc.vpf1_lstate_sel .. ccfc.vpf2_lstate_sel (2 regs) */
	0x10410000, 	/* block tcfc */
	0x050b4000, 	/* tcfc.init_reg .. tcfc.tidram_init_done (5 regs) */
	0x020b4060, 	/* tcfc.INT_STS_0 .. tcfc.INT_MASK_0 (2 regs) */
	0x0c0b4100, 	/* tcfc.lc_blocked .. tcfc.cdu_write_backs (12 regs) */
	0x050b4140, 	/* tcfc.dbg_select .. tcfc.dbg_force_frame (5 regs) */
	0x090b4152, 	/* tcfc.eco_reserved .. tcfc.arbiters_reg (9 regs) */
	0x060b4170, 	/* tcfc.debug0 .. tcfc.cdu_pcie_err_mask (6 regs) */
	0x020b4177, 	/* tcfc.sreq_full_sticky .. tcfc.prsresp_full_sticky (2 regs) */
	0x0b0b4180, 	/* tcfc.num_lcids_empty .. tcfc.max_inside (11 regs) */
	0x020b41c4, 	/* tcfc.LoadRetry_Types .. tcfc.MiniCache_Control (2 regs) */
	0x010b41c7, 	/* tcfc.control0 (1 regs) */
	0x040b41e0, 	/* tcfc.prsresp_credit .. tcfc.cduwb_credit (4 regs) */
	0x040b4200, 	/* tcfc.ll_policy_cfg .. tcfc.empty_size (4 regs) */
	0x1f0b4240, 	/* tcfc.lc_client_0_lcid_threshold .. tcfc.wave_sm_2_one_count (31 regs) */
	0x120b4280, 	/* tcfc.cache_string_type .. tcfc.include_vlan_in_hash (18 regs) */
	0x080b42c0, 	/* tcfc.cid_cam_bist_en .. tcfc.string_cam_bist_status (8 regs) */
	0x020b7400, 	/* tcfc.vpf1_lstate_sel .. tcfc.vpf2_lstate_sel (2 regs) */
	0x0e420000, 	/* block igu */
	0x01060000, 	/* igu.reset_memories (1 regs) */
	0x01060010, 	/* igu.block_configuration (1 regs) */
	0x01060014, 	/* igu.pxp_requester_initial_credit (1 regs) */
	0x02060060, 	/* igu.INT_STS .. igu.INT_MASK (2 regs) */
	0x08060103, 	/* igu.pxp_request_counter .. igu.cons_upd_counter (8 regs) */
	0x01060202, 	/* igu.message_fields (1 regs) */
	0x01060213, 	/* igu.statistic_en (1 regs) */
	0x03060218, 	/* igu.cam_parity_scrubbing .. igu.eco_reserved (3 regs) */
	0x10060300, 	/* igu.vf_with_more_16sb_0 .. igu.vf_with_more_16sb_15 (16 regs) */
	0x03060480, 	/* igu.global_rate_limiter_vari0 .. igu.global_rate_tick_rate_counter (3 regs) */
	0x01060485, 	/* igu.clk25_counter_sensitivity (1 regs) */
	0x06060487, 	/* igu.group_rl_en_0 .. igu.group_rl_pending_1 (6 regs) */
	0x08060540, 	/* igu.attention_signal_p0_status .. igu.Interrupt_status (8 regs) */
	0x1706054c, 	/* igu.error_handling_data_valid .. igu.dbg_force_frame (23 regs) */
	0x0f430000, 	/* block cau */
	0x01070035, 	/* cau.INT_STS (1 regs) */
	0x01070038, 	/* cau.INT_MASK (1 regs) */
	0x05070100, 	/* cau.num_pi_per_sb .. cau.reset_memories (5 regs) */
	0x02070140, 	/* cau.in_arb_priority .. cau.in_arb_timeout (2 regs) */
	0x040701c0, 	/* cau.tick_size .. cau.stop_scan (4 regs) */
	0x01070220, 	/* cau.wdata_fifo_afull_thr (1 regs) */
	0x02070260, 	/* cau.igu_req_credit_status .. cau.igu_cmd_credit_status (2 regs) */
	0x060702a0, 	/* cau.stat_ctrl_sb_select .. cau.stat_ctrl_timer_cmd_type (6 regs) */
	0x0a0702e0, 	/* cau.stat_counter_sb_gen .. cau.stat_counter_fsm1_line (10 regs) */
	0x06070320, 	/* cau.debug_fifo_status .. cau.error_cleanup_cmd_reg (6 regs) */
	0x0107032b, 	/* cau.eco_reserved (1 regs) */
	0x05070360, 	/* cau.debug_record_mask_min_sb .. cau.debug_record_mask_cmd_type (5 regs) */
	0x03070380, 	/* cau.req_counter .. cau.wdone_counter (3 regs) */
	0x050703aa, 	/* cau.dbg_select .. cau.dbg_force_frame (5 regs) */
	0x030703c0, 	/* cau.main_fsm_status .. cau.igu_dma_fsm_status (3 regs) */
	0x0b4a0000, 	/* block dbg */
	0x01004001, 	/* dbg.client_enable (1 regs) */
	0x01004003, 	/* dbg.output_enable (1 regs) */
	0x14004005, 	/* dbg.calendar_slot0 .. dbg.full_mode (20 regs) */
	0x02004060, 	/* dbg.INT_STS .. dbg.INT_MASK (2 regs) */
	0x02004100, 	/* dbg.intr_buffer_rd_ptr .. dbg.intr_buffer_wr_ptr (2 regs) */
	0x04804102, 	/* dbg.ext_buffer_rd_ptr .. dbg.ext_buffer_wr_ptr (4 regs, WB) */
	0x13004106, 	/* dbg.wrap_on_int_buffer .. dbg.pci_logic_addr (19 regs) */
	0xff004150, 	/* dbg.pattern_recognition_disable .. dbg.trigger_state_set_cnstr_cyclic_15 (255 regs) */
	0x6100424f, 	/* dbg.trigger_state_set_cnstr_cyclic_16 .. dbg.dbg_driver_trigger (97 regs) */
	0x210042c4, 	/* dbg.hw_id_num .. dbg.filter_status_match_cnstr (33 regs) */
	0x020042e8, 	/* dbg.memctrl_status .. dbg.num_of_empty_lines_in_int_buffer (2 regs) */
	0x264b0000, 	/* block nig */
	0x02140010, 	/* nig.INT_STS_0 .. nig.INT_MASK_0 (2 regs) */
	0x02140014, 	/* nig.INT_STS_1 .. nig.INT_MASK_1 (2 regs) */
	0x02140018, 	/* nig.INT_STS_2 .. nig.INT_MASK_2 (2 regs) */
	0x0214001c, 	/* nig.INT_STS_3 .. nig.INT_MASK_3 (2 regs) */
	0x02140020, 	/* nig.INT_STS_4 .. nig.INT_MASK_4 (2 regs) */
	0x02140024, 	/* nig.INT_STS_5 .. nig.INT_MASK_5 (2 regs) */
	0x12140200, 	/* nig.close_gate_disable .. nig.tx_lb_drop_fwderr (18 regs) */
	0x04140301, 	/* nig.lb_sopq_empty .. nig.tx_sopq_full (4 regs) */
	0x04140380, 	/* nig.dorq_in_en .. nig.ppp_out_en (4 regs) */
	0x01140401, 	/* nig.initial_header_size (1 regs) */
	0x01140403, 	/* nig.llh_arp_type (1 regs) */
	0x0214041b, 	/* nig.roce_type .. nig.gre_eth_type (2 regs) */
	0x01140421, 	/* nig.gre_protocol (1 regs) */
	0x0114044e, 	/* nig.rx_llh_svol_mcp_fwd_allpf (1 regs) */
	0x01140454, 	/* nig.rx_llh_svol_brb_dntfwd_allpf (1 regs) */
	0x0114047c, 	/* nig.rx_llh_brb_gate_dntfwd_clsfailed (1 regs) */
	0x01140598, 	/* nig.lb_llh_brb_gate_dntfwd_clsfailed (1 regs) */
	0x041406e0, 	/* nig.llh_eng_cls_type .. nig.llh_eng_cls_crc8_init_val (4 regs) */
	0x029406e4, 	/* nig.llh_eng_cls_eng_id_tbl (2 regs, WB) */
	0x011406e6, 	/* nig.llh_eng_cls_roce_qp_sel (1 regs) */
	0x04140715, 	/* nig.ppp_address .. nig.ppp_trig (4 regs) */
	0x0114074a, 	/* nig.stat_rx_storm_packet_sent (1 regs) */
	0x0114082b, 	/* nig.bmb_fifo_alm_full_thr (1 regs) */
	0x05140830, 	/* nig.dorq_fifo_alm_full_thr .. nig.debug_port (5 regs) */
	0x04140836, 	/* nig.debug_pkt_wait_size .. nig.debug_fifo_full (4 regs) */
	0x10140871, 	/* nig.rx_fc_dbg_select_pllh .. nig.eco_reserved (16 regs) */
	0x02142216, 	/* nig.pm_timer_select .. nig.ts_for_semi_select (2 regs) */
	0x0214221c, 	/* nig.ts_output_enable_pda .. nig.ts_output_enable_hv (2 regs) */
	0x0214222a, 	/* nig.tsgen_free_cnt_value_lsb .. nig.tsgen_free_cnt_value_msb (2 regs) */
	0x0214222e, 	/* nig.tsgen_freecnt_lsb .. nig.tsgen_freecnt_msb (2 regs) */
	0x02142234, 	/* nig.tsgen_pps_high_time .. nig.tsgen_pps_low_time (2 regs) */
	0x03142238, 	/* nig.tsgen_tsio_oeb .. nig.edpm_fifo_full_thresh (3 regs) */
	0x011422c3, 	/* nig.rroce_port (1 regs) */
	0x011422cd, 	/* nig.nge_eth_type (1 regs) */
	0x021422d0, 	/* nig.bth_hdr_flow_ctrl_opcode_1 .. nig.bth_hdr_flow_ctrl_opcode_2 (2 regs) */
	0x011422d7, 	/* nig.dbgmux_ovflw_ind_en (1 regs) */
	0x011422df, 	/* nig.tx_parity_error_timer (1 regs) */
	0x021422e3, 	/* nig.tx_inhibit_bmb_arb_en .. nig.lb_inhibit_bmb_arb_en (2 regs) */
	0x02050005, 	/* mode bb, block cnig */
	0x0108608e, 	/* cnig.eco_reserved (1 regs) */
	0x020860ba, 	/* cnig.INT_STS .. cnig.INT_MASK (2 regs) */
	0x11180000, 	/* block tcm */
	0x08460101, 	/* tcm.qm_con_base_evnt_id_0 .. tcm.qm_con_base_evnt_id_7 (8 regs) */
	0x10460111, 	/* tcm.qm_agg_con_ctx_part_size_0 .. tcm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10460131, 	/* tcm.qm_xxlock_cmd_0 .. tcm.qm_con_use_st_flg_7 (16 regs) */
	0x08460149, 	/* tcm.tm_con_evnt_id_0 .. tcm.tm_con_evnt_id_7 (8 regs) */
	0x01460183, 	/* tcm.ysem_weight (1 regs) */
	0x01460188, 	/* tcm.tsdm_weight (1 regs) */
	0x084601dc, 	/* tcm.xx_byp_msg_up_bnd_0 .. tcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08460205, 	/* tcm.n_sm_con_ctx_ld_0 .. tcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08460228, 	/* tcm.agg_con_ctx_size_0 .. tcm.agg_con_ctx_size_7 (8 regs) */
	0x1c460245, 	/* tcm.agg_con_cf0_q .. tcm.agg_task_cf7_q (28 regs) */
	0x014602aa, 	/* tcm.tsdm_length_mis (1 regs) */
	0x014602b1, 	/* tcm.tsdm_msg_cntr (1 regs) */
	0x014602b3, 	/* tcm.ysem_msg_cntr (1 regs) */
	0x014602bf, 	/* tcm.is_tsdm_fill_lvl (1 regs) */
	0x014602c1, 	/* tcm.is_ysem_fill_lvl (1 regs) */
	0x014602e4, 	/* tcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014602e8, 	/* tcm.is_foc_tsdm_nxt_inf_unit (1 regs) */
	0x10190000, 	/* block mcm */
	0x08480101, 	/* mcm.qm_con_base_evnt_id_0 .. mcm.qm_con_base_evnt_id_7 (8 regs) */
	0x10480111, 	/* mcm.qm_agg_con_ctx_part_size_0 .. mcm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10480141, 	/* mcm.qm_xxlock_cmd_0 .. mcm.qm_con_use_st_flg_7 (16 regs) */
	0x01480183, 	/* mcm.ysem_weight (1 regs) */
	0x01480186, 	/* mcm.msdm_weight (1 regs) */
	0x084801dc, 	/* mcm.xx_byp_msg_up_bnd_0 .. mcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08480205, 	/* mcm.n_sm_con_ctx_ld_0 .. mcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08480228, 	/* mcm.agg_con_ctx_size_0 .. mcm.agg_con_ctx_size_7 (8 regs) */
	0x0b480245, 	/* mcm.agg_con_cf0_q .. mcm.agg_task_cf2_q (11 regs) */
	0x014802aa, 	/* mcm.msdm_length_mis (1 regs) */
	0x014802b2, 	/* mcm.msdm_msg_cntr (1 regs) */
	0x014802b7, 	/* mcm.ysem_msg_cntr (1 regs) */
	0x014802bf, 	/* mcm.is_msdm_fill_lvl (1 regs) */
	0x014802c4, 	/* mcm.is_ysem_fill_lvl (1 regs) */
	0x014802e4, 	/* mcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014802e6, 	/* mcm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x081a0000, 	/* block ucm */
	0x084a0101, 	/* ucm.qm_con_base_evnt_id_0 .. ucm.qm_con_base_evnt_id_7 (8 regs) */
	0x104a0111, 	/* ucm.qm_agg_con_ctx_part_size_0 .. ucm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x104a0131, 	/* ucm.qm_xxlock_cmd_0 .. ucm.qm_con_use_st_flg_7 (16 regs) */
	0x084a0149, 	/* ucm.tm_con_evnt_id_0 .. ucm.tm_con_evnt_id_7 (8 regs) */
	0x084a01dc, 	/* ucm.xx_byp_msg_up_bnd_0 .. ucm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x084a0205, 	/* ucm.n_sm_con_ctx_ld_0 .. ucm.n_sm_con_ctx_ld_7 (8 regs) */
	0x084a0228, 	/* ucm.agg_con_ctx_size_0 .. ucm.agg_con_ctx_size_7 (8 regs) */
	0x154a0245, 	/* ucm.agg_con_cf0_q .. ucm.agg_task_cf4_q (21 regs) */
	0x0e1b0000, 	/* block xcm */
	0x30400101, 	/* xcm.qm_con_base_evnt_id_0 .. xcm.tm_con_evnt_id_7 (48 regs) */
	0x01400184, 	/* xcm.ysem_weight (1 regs) */
	0x01400188, 	/* xcm.msdm_weight (1 regs) */
	0x084001d9, 	/* xcm.xx_byp_msg_up_bnd_0 .. xcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08400203, 	/* xcm.n_sm_con_ctx_ld_0 .. xcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08400215, 	/* xcm.agg_con_ctx_size_0 .. xcm.agg_con_ctx_size_7 (8 regs) */
	0x32400242, 	/* xcm.agg_con_cf0_q .. xcm.agg_con_rule25_q (50 regs) */
	0x014002aa, 	/* xcm.msdm_length_mis (1 regs) */
	0x014002b3, 	/* xcm.msdm_msg_cntr (1 regs) */
	0x014002b9, 	/* xcm.ysem_msg_cntr (1 regs) */
	0x014002c4, 	/* xcm.is_msdm_fill_lvl (1 regs) */
	0x014002ca, 	/* xcm.is_ysem_fill_lvl (1 regs) */
	0x014002e5, 	/* xcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014002e8, 	/* xcm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x0d1c0000, 	/* block ycm */
	0x08420101, 	/* ycm.qm_con_base_evnt_id_0 .. ycm.qm_con_base_evnt_id_7 (8 regs) */
	0x10420111, 	/* ycm.qm_agg_con_ctx_part_size_0 .. ycm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10420141, 	/* ycm.qm_xxlock_cmd_0 .. ycm.qm_con_use_st_flg_7 (16 regs) */
	0x01420186, 	/* ycm.msdm_weight (1 regs) */
	0x084201dc, 	/* ycm.xx_byp_msg_up_bnd_0 .. ycm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08420205, 	/* ycm.n_sm_con_ctx_ld_0 .. ycm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08420228, 	/* ycm.agg_con_ctx_size_0 .. ycm.agg_con_ctx_size_7 (8 regs) */
	0x11420245, 	/* ycm.agg_con_cf0_q .. ycm.agg_task_rule6_q (17 regs) */
	0x014202aa, 	/* ycm.msdm_length_mis (1 regs) */
	0x014202b1, 	/* ycm.msdm_msg_cntr (1 regs) */
	0x014202bd, 	/* ycm.is_msdm_fill_lvl (1 regs) */
	0x014202e4, 	/* ycm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014202e6, 	/* ycm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x061d0000, 	/* block pcm */
	0x01440184, 	/* pcm.psdm_weight (1 regs) */
	0x08440202, 	/* pcm.n_sm_con_ctx_ld_0 .. pcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x014402aa, 	/* pcm.psdm_length_mis (1 regs) */
	0x014402af, 	/* pcm.psdm_msg_cntr (1 regs) */
	0x014402b3, 	/* pcm.is_psdm_fill_lvl (1 regs) */
	0x014402e4, 	/* pcm.is_foc_psdm_nxt_inf_unit (1 regs) */
	0x021e0000, 	/* block qm */
	0x280bc448, 	/* qm.CtxRegCcfc_0 .. qm.CtxRegCcfc_39 (40 regs) */
	0x280bc4c8, 	/* qm.ActCtrInitValCcfc_0 .. qm.ActCtrInitValCcfc_39 (40 regs) */
	0x03200000, 	/* block dorq */
	0x1004012d, 	/* dorq.qm_en_byp_mask_0 .. dorq.dpi_val_sup_7 (16 regs) */
	0x38040185, 	/* dorq.xcm_agg_flg_mask_conn_0 .. dorq.dpm_xcm_event_id_7 (56 regs) */
	0x080401c3, 	/* dorq.qm_byp_agg_ctx_size_0 .. dorq.qm_byp_agg_ctx_size_7 (8 regs) */
	0x02210000, 	/* block brb */
	0x020d0220, 	/* brb.shared_hr_area (2 regs) */
	0x020d0230, 	/* brb.total_mac_size (2 regs) */
	0x04230000, 	/* block prs */
	0x1007c040, 	/* prs.packet_region_0 .. prs.pure_region_7 (16 regs) */
	0x0807c051, 	/* prs.con_inc_value_0 .. prs.con_inc_value_7 (8 regs) */
	0x0807c245, 	/* prs.cm_hdr_event_id_0 .. prs.cm_hdr_event_id_7 (8 regs) */
	0x1007c25f, 	/* prs.output_format_0_0 .. prs.output_format_7_1 (16 regs) */
	0x012a0000, 	/* block tsem */
	0x04dc0108, 	/* tsem.vf_err_vector (4 regs, WB) */
	0x012b0000, 	/* block msem */
	0x04e00108, 	/* msem.vf_err_vector (4 regs, WB) */
	0x012c0000, 	/* block usem */
	0x04e40108, 	/* usem.vf_err_vector (4 regs, WB) */
	0x012d0000, 	/* block xsem */
	0x04d00108, 	/* xsem.vf_err_vector (4 regs, WB) */
	0x012e0000, 	/* block ysem */
	0x04d40108, 	/* ysem.vf_err_vector (4 regs, WB) */
	0x012f0000, 	/* block psem */
	0x04d80108, 	/* psem.vf_err_vector (4 regs, WB) */
	0x01310000, 	/* block tmld */
	0x02134001, 	/* tmld.scbd_wrr_weight_q0 .. tmld.scbd_wrr_weight_q1 (2 regs) */
	0x01340000, 	/* block xyld */
	0x02130001, 	/* xyld.scbd_wrr_weight_q0 .. xyld.scbd_wrr_weight_q1 (2 regs) */
	0x013c0000, 	/* block pbf */
	0x013603a8, 	/* pbf.eco_reserved (1 regs) */
	0x1118001b, 	/* mode k2, block tcm */
	0x08460101, 	/* tcm.qm_con_base_evnt_id_0 .. tcm.qm_con_base_evnt_id_7 (8 regs) */
	0x10460111, 	/* tcm.qm_agg_con_ctx_part_size_0 .. tcm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10460131, 	/* tcm.qm_xxlock_cmd_0 .. tcm.qm_con_use_st_flg_7 (16 regs) */
	0x08460149, 	/* tcm.tm_con_evnt_id_0 .. tcm.tm_con_evnt_id_7 (8 regs) */
	0x01460183, 	/* tcm.ysem_weight (1 regs) */
	0x01460188, 	/* tcm.tsdm_weight (1 regs) */
	0x084601dc, 	/* tcm.xx_byp_msg_up_bnd_0 .. tcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08460205, 	/* tcm.n_sm_con_ctx_ld_0 .. tcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08460228, 	/* tcm.agg_con_ctx_size_0 .. tcm.agg_con_ctx_size_7 (8 regs) */
	0x1c460245, 	/* tcm.agg_con_cf0_q .. tcm.agg_task_cf7_q (28 regs) */
	0x014602aa, 	/* tcm.tsdm_length_mis (1 regs) */
	0x014602b1, 	/* tcm.tsdm_msg_cntr (1 regs) */
	0x014602b3, 	/* tcm.ysem_msg_cntr (1 regs) */
	0x014602bf, 	/* tcm.is_tsdm_fill_lvl (1 regs) */
	0x014602c1, 	/* tcm.is_ysem_fill_lvl (1 regs) */
	0x014602e4, 	/* tcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014602e8, 	/* tcm.is_foc_tsdm_nxt_inf_unit (1 regs) */
	0x10190000, 	/* block mcm */
	0x08480101, 	/* mcm.qm_con_base_evnt_id_0 .. mcm.qm_con_base_evnt_id_7 (8 regs) */
	0x10480111, 	/* mcm.qm_agg_con_ctx_part_size_0 .. mcm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10480141, 	/* mcm.qm_xxlock_cmd_0 .. mcm.qm_con_use_st_flg_7 (16 regs) */
	0x01480183, 	/* mcm.ysem_weight (1 regs) */
	0x01480186, 	/* mcm.msdm_weight (1 regs) */
	0x084801dc, 	/* mcm.xx_byp_msg_up_bnd_0 .. mcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08480205, 	/* mcm.n_sm_con_ctx_ld_0 .. mcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08480228, 	/* mcm.agg_con_ctx_size_0 .. mcm.agg_con_ctx_size_7 (8 regs) */
	0x0b480245, 	/* mcm.agg_con_cf0_q .. mcm.agg_task_cf2_q (11 regs) */
	0x014802aa, 	/* mcm.msdm_length_mis (1 regs) */
	0x014802b2, 	/* mcm.msdm_msg_cntr (1 regs) */
	0x014802b7, 	/* mcm.ysem_msg_cntr (1 regs) */
	0x014802bf, 	/* mcm.is_msdm_fill_lvl (1 regs) */
	0x014802c4, 	/* mcm.is_ysem_fill_lvl (1 regs) */
	0x014802e4, 	/* mcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014802e6, 	/* mcm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x081a0000, 	/* block ucm */
	0x084a0101, 	/* ucm.qm_con_base_evnt_id_0 .. ucm.qm_con_base_evnt_id_7 (8 regs) */
	0x104a0111, 	/* ucm.qm_agg_con_ctx_part_size_0 .. ucm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x104a0131, 	/* ucm.qm_xxlock_cmd_0 .. ucm.qm_con_use_st_flg_7 (16 regs) */
	0x084a0149, 	/* ucm.tm_con_evnt_id_0 .. ucm.tm_con_evnt_id_7 (8 regs) */
	0x084a01dc, 	/* ucm.xx_byp_msg_up_bnd_0 .. ucm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x084a0205, 	/* ucm.n_sm_con_ctx_ld_0 .. ucm.n_sm_con_ctx_ld_7 (8 regs) */
	0x084a0228, 	/* ucm.agg_con_ctx_size_0 .. ucm.agg_con_ctx_size_7 (8 regs) */
	0x154a0245, 	/* ucm.agg_con_cf0_q .. ucm.agg_task_cf4_q (21 regs) */
	0x0e1b0000, 	/* block xcm */
	0x30400101, 	/* xcm.qm_con_base_evnt_id_0 .. xcm.tm_con_evnt_id_7 (48 regs) */
	0x01400184, 	/* xcm.ysem_weight (1 regs) */
	0x01400188, 	/* xcm.msdm_weight (1 regs) */
	0x084001d9, 	/* xcm.xx_byp_msg_up_bnd_0 .. xcm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08400203, 	/* xcm.n_sm_con_ctx_ld_0 .. xcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08400215, 	/* xcm.agg_con_ctx_size_0 .. xcm.agg_con_ctx_size_7 (8 regs) */
	0x32400242, 	/* xcm.agg_con_cf0_q .. xcm.agg_con_rule25_q (50 regs) */
	0x014002aa, 	/* xcm.msdm_length_mis (1 regs) */
	0x014002b3, 	/* xcm.msdm_msg_cntr (1 regs) */
	0x014002b9, 	/* xcm.ysem_msg_cntr (1 regs) */
	0x014002c4, 	/* xcm.is_msdm_fill_lvl (1 regs) */
	0x014002ca, 	/* xcm.is_ysem_fill_lvl (1 regs) */
	0x014002e5, 	/* xcm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014002e8, 	/* xcm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x0d1c0000, 	/* block ycm */
	0x08420101, 	/* ycm.qm_con_base_evnt_id_0 .. ycm.qm_con_base_evnt_id_7 (8 regs) */
	0x10420111, 	/* ycm.qm_agg_con_ctx_part_size_0 .. ycm.qm_sm_con_ctx_ldst_flg_7 (16 regs) */
	0x10420141, 	/* ycm.qm_xxlock_cmd_0 .. ycm.qm_con_use_st_flg_7 (16 regs) */
	0x01420186, 	/* ycm.msdm_weight (1 regs) */
	0x084201dc, 	/* ycm.xx_byp_msg_up_bnd_0 .. ycm.xx_byp_msg_up_bnd_7 (8 regs) */
	0x08420205, 	/* ycm.n_sm_con_ctx_ld_0 .. ycm.n_sm_con_ctx_ld_7 (8 regs) */
	0x08420228, 	/* ycm.agg_con_ctx_size_0 .. ycm.agg_con_ctx_size_7 (8 regs) */
	0x11420245, 	/* ycm.agg_con_cf0_q .. ycm.agg_task_rule6_q (17 regs) */
	0x014202aa, 	/* ycm.msdm_length_mis (1 regs) */
	0x014202b1, 	/* ycm.msdm_msg_cntr (1 regs) */
	0x014202bd, 	/* ycm.is_msdm_fill_lvl (1 regs) */
	0x014202e4, 	/* ycm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x014202e6, 	/* ycm.is_foc_msdm_nxt_inf_unit (1 regs) */
	0x061d0000, 	/* block pcm */
	0x01440184, 	/* pcm.psdm_weight (1 regs) */
	0x08440202, 	/* pcm.n_sm_con_ctx_ld_0 .. pcm.n_sm_con_ctx_ld_7 (8 regs) */
	0x014402aa, 	/* pcm.psdm_length_mis (1 regs) */
	0x014402af, 	/* pcm.psdm_msg_cntr (1 regs) */
	0x014402b3, 	/* pcm.is_psdm_fill_lvl (1 regs) */
	0x014402e4, 	/* pcm.is_foc_psdm_nxt_inf_unit (1 regs) */
	0x021e0000, 	/* block qm */
	0x280bc448, 	/* qm.CtxRegCcfc_0 .. qm.CtxRegCcfc_39 (40 regs) */
	0x280bc4c8, 	/* qm.ActCtrInitValCcfc_0 .. qm.ActCtrInitValCcfc_39 (40 regs) */
	0x03200000, 	/* block dorq */
	0x1004012d, 	/* dorq.qm_en_byp_mask_0 .. dorq.dpi_val_sup_7 (16 regs) */
	0x38040185, 	/* dorq.xcm_agg_flg_mask_conn_0 .. dorq.dpm_xcm_event_id_7 (56 regs) */
	0x080401c3, 	/* dorq.qm_byp_agg_ctx_size_0 .. dorq.qm_byp_agg_ctx_size_7 (8 regs) */
	0x04230000, 	/* block prs */
	0x1007c040, 	/* prs.packet_region_0 .. prs.pure_region_7 (16 regs) */
	0x0807c051, 	/* prs.con_inc_value_0 .. prs.con_inc_value_7 (8 regs) */
	0x0807c245, 	/* prs.cm_hdr_event_id_0 .. prs.cm_hdr_event_id_7 (8 regs) */
	0x1007c25f, 	/* prs.output_format_0_0 .. prs.output_format_7_1 (16 regs) */
	0x01310000, 	/* block tmld */
	0x02134001, 	/* tmld.scbd_wrr_weight_q0 .. tmld.scbd_wrr_weight_q1 (2 regs) */
	0x01340000, 	/* block xyld */
	0x02130001, 	/* xyld.scbd_wrr_weight_q0 .. xyld.scbd_wrr_weight_q1 (2 regs) */
	0x013c0000, 	/* block pbf */
	0x013603a8, 	/* pbf.eco_reserved (1 regs) */
	0x01500000, 	/* block nws */
	0x031c0014, 	/* nws.external_signal_detect .. nws.eco_reserved (3 regs) */
	0x04010003, 	/* mode !bb, block miscs */
	0x0100245c, 	/* miscs.bsc_smbio_enable_glitch_filter (1 regs) */
	0x060024f0, 	/* miscs.pcie_link_up_state .. miscs.main_pll_status (6 regs) */
	0x05002594, 	/* miscs.bsc_sda_sel .. miscs.pcie_phy_rst_n_status (5 regs) */
	0x010025ae, 	/* miscs.core_rst_n_status (1 regs) */
	0x07040000, 	/* block pglue_b */
	0x030aa14a, 	/* pglue_b.txw_h_syncfifo_almostfull_th .. pglue_b.txr_h_syncfifo_almostfull_th (3 regs) */
	0x010aabac, 	/* pglue_b.cfg_no_l1_on_int (1 regs) */
	0x020aabaf, 	/* pglue_b.mctp_max_length .. pglue_b.mctp_reqid (2 regs) */
	0x090aabb2, 	/* pglue_b.pbus_num .. pglue_b.mrrs_attn (9 regs) */
	0x090aabbc, 	/* pglue_b.txw_b2b_disable .. pglue_b.pgl_pm_dstate_47_32 (9 regs) */
	0x0f0aabd7, 	/* pglue_b.check_tc_on_err .. pglue_b.mctp_venderid_chk_disable (15 regs) */
	0x0b0aabea, 	/* pglue_b.ext_tag_en_pf_31_0 .. pglue_b.rxd_syncfifo_pop_status (11 regs) */
	0x03050000, 	/* block cnig */
	0x08086080, 	/* cnig.nig_port0_conf .. cnig.INT_MASK (8 regs) */
	0x0108608a, 	/* cnig.nwm_lpi_defualt_value (1 regs) */
	0x06086094, 	/* cnig.eco_reserved .. cnig.dbg_force_frame (6 regs) */
	0x01060000, 	/* block cpmu */
	0x0200c100, 	/* cpmu.sdm_sq_counter_e0_p2 .. cpmu.sdm_sq_counter_e0_p3 (2 regs) */
	0x040a0000, 	/* block pcie */
	0x6a015084, 	/* pcie.soft_reset_control .. pcie.msix_synch_sticky (106 regs) */
	0x010151e9, 	/* pcie.INT_MASK (1 regs) */
	0x050151fa, 	/* pcie.dbg_select .. pcie.dbg_force_frame (5 regs) */
	0x02015200, 	/* pcie.reset_status_2 .. pcie.reset_status_3 (2 regs) */
	0x02150000, 	/* block pglcs */
	0x06000744, 	/* pglcs.rasdp_error_mode_en_off .. pglcs.dbg_force_frame (6 regs) */
	0x0e000e01, 	/* pglcs.discard_poisoned_mctp_tgtwr .. pglcs.tx_syncfifo_pop_status (14 regs) */
	0x01170000, 	/* block ptu */
	0x01158037, 	/* ptu.atc_otb_overrun_fix_chicken_bit (1 regs) */
	0x061e0000, 	/* block qm */
	0x080bc148, 	/* qm.MaxPqSizeTxSel_56 .. qm.MaxPqSizeTxSel_63 (8 regs) */
	0x020bc41e, 	/* qm.QstatusTx_14 .. qm.QstatusTx_15 (2 regs) */
	0x020bc432, 	/* qm.QstatusOther_2 .. qm.QstatusOther_3 (2 regs) */
	0x080bc662, 	/* qm.WrrOtherPqGrp_8 .. qm.WrrOtherPqGrp_15 (8 regs) */
	0x080bcb39, 	/* qm.PqTx2Pf_56 .. qm.PqTx2Pf_63 (8 regs) */
	0x080bcb89, 	/* qm.PqOther2Pf_8 .. qm.PqOther2Pf_15 (8 regs) */
	0x01200000, 	/* block dorq */
	0x0204024b, 	/* dorq.l2_edpm_tunnel_nge_ip_en .. dorq.l2_edpm_tunnel_nge_eth_en (2 regs) */
	0x18210000, 	/* block brb */
	0x040d0220, 	/* brb.shared_hr_area (4 regs) */
	0x040d0230, 	/* brb.total_mac_size (4 regs) */
	0x020d0252, 	/* brb.tc_guarantied_18 .. brb.tc_guarantied_19 (2 regs) */
	0x020d0288, 	/* brb.lb_tc_guarantied_hyst_18 .. brb.lb_tc_guarantied_hyst_19 (2 regs) */
	0x020d02be, 	/* brb.lb_tc_pause_xoff_threshold_18 .. brb.lb_tc_pause_xoff_threshold_19 (2 regs) */
	0x020d02f4, 	/* brb.lb_tc_pause_xon_threshold_18 .. brb.lb_tc_pause_xon_threshold_19 (2 regs) */
	0x020d032a, 	/* brb.lb_tc_full_xoff_threshold_18 .. brb.lb_tc_full_xoff_threshold_19 (2 regs) */
	0x020d0360, 	/* brb.lb_tc_full_xon_threshold_18 .. brb.lb_tc_full_xon_threshold_19 (2 regs) */
	0x020d0374, 	/* brb.wc_no_dead_cycles_en .. brb.wc_highest_pri_en (2 regs) */
	0x020d0393, 	/* brb.pm_tc_latency_sensitive_2 .. brb.pm_tc_latency_sensitive_3 (2 regs) */
	0x040d03d0, 	/* brb.wc_empty_4 .. brb.wc_empty_7 (4 regs) */
	0x040d03e0, 	/* brb.wc_full_4 .. brb.wc_full_7 (4 regs) */
	0x020d0426, 	/* brb.empty_if_2 .. brb.empty_if_3 (2 regs) */
	0x020d0470, 	/* brb.mac_free_shared_hr_2 .. brb.mac_free_shared_hr_3 (2 regs) */
	0x050d0494, 	/* brb.mac2_tc_occupancy_0 .. brb.mac2_tc_occupancy_4 (5 regs) */
	0x050d04a4, 	/* brb.mac3_tc_occupancy_0 .. brb.mac3_tc_occupancy_4 (5 regs) */
	0x020d04b6, 	/* brb.available_mac_size_2 .. brb.available_mac_size_3 (2 regs) */
	0x020d04bc, 	/* brb.main_tc_pause_2 .. brb.main_tc_pause_3 (2 regs) */
	0x020d04c2, 	/* brb.lb_tc_pause_2 .. brb.lb_tc_pause_3 (2 regs) */
	0x020d04c8, 	/* brb.main_tc_full_2 .. brb.main_tc_full_3 (2 regs) */
	0x020d04ce, 	/* brb.lb_tc_full_2 .. brb.lb_tc_full_3 (2 regs) */
	0x040d04f2, 	/* brb.main2_tc_lossless_cnt_0 .. brb.main2_tc_lossless_cnt_3 (4 regs) */
	0x040d0502, 	/* brb.main3_tc_lossless_cnt_0 .. brb.main3_tc_lossless_cnt_3 (4 regs) */
	0x020d0514, 	/* brb.main_tc_lossless_int_2 .. brb.main_tc_lossless_int_3 (2 regs) */
	0x02240000, 	/* block tsdm */
	0x023ec320, 	/* tsdm.rmt_xcm_fifo_full .. tsdm.rmt_ycm_fifo_full (2 regs) */
	0x023ec35a, 	/* tsdm.rmt_xcm_fifo_empty .. tsdm.rmt_ycm_fifo_empty (2 regs) */
	0x02250000, 	/* block msdm */
	0x023f0320, 	/* msdm.rmt_xcm_fifo_full .. msdm.rmt_ycm_fifo_full (2 regs) */
	0x023f035a, 	/* msdm.rmt_xcm_fifo_empty .. msdm.rmt_ycm_fifo_empty (2 regs) */
	0x02260000, 	/* block usdm */
	0x023f4320, 	/* usdm.rmt_xcm_fifo_full .. usdm.rmt_ycm_fifo_full (2 regs) */
	0x023f435a, 	/* usdm.rmt_xcm_fifo_empty .. usdm.rmt_ycm_fifo_empty (2 regs) */
	0x02270000, 	/* block xsdm */
	0x023e0320, 	/* xsdm.rmt_xcm_fifo_full .. xsdm.rmt_ycm_fifo_full (2 regs) */
	0x023e035a, 	/* xsdm.rmt_xcm_fifo_empty .. xsdm.rmt_ycm_fifo_empty (2 regs) */
	0x02280000, 	/* block ysdm */
	0x023e4320, 	/* ysdm.rmt_xcm_fifo_full .. ysdm.rmt_ycm_fifo_full (2 regs) */
	0x023e435a, 	/* ysdm.rmt_xcm_fifo_empty .. ysdm.rmt_ycm_fifo_empty (2 regs) */
	0x02290000, 	/* block psdm */
	0x023e8320, 	/* psdm.rmt_xcm_fifo_full .. psdm.rmt_ycm_fifo_full (2 regs) */
	0x023e835a, 	/* psdm.rmt_xcm_fifo_empty .. psdm.rmt_ycm_fifo_empty (2 regs) */
	0x012a0000, 	/* block tsem */
	0x08dc0108, 	/* tsem.vf_err_vector (8 regs, WB) */
	0x012b0000, 	/* block msem */
	0x08e00108, 	/* msem.vf_err_vector (8 regs, WB) */
	0x012c0000, 	/* block usem */
	0x08e40108, 	/* usem.vf_err_vector (8 regs, WB) */
	0x012d0000, 	/* block xsem */
	0x08d00108, 	/* xsem.vf_err_vector (8 regs, WB) */
	0x012e0000, 	/* block ysem */
	0x08d40108, 	/* ysem.vf_err_vector (8 regs, WB) */
	0x012f0000, 	/* block psem */
	0x08d80108, 	/* psem.vf_err_vector (8 regs, WB) */
	0x01370000, 	/* block prm */
	0x0108d800, 	/* prm.nop_without_completion_fix_disable (1 regs) */
	0x043b0000, 	/* block btb */
	0x0236c215, 	/* btb.wc_no_dead_cycles_en .. btb.wc_highest_pri_en (2 regs) */
	0x0436c26d, 	/* btb.rc_pkt_empty_4 .. btb.rc_pkt_empty_7 (4 regs) */
	0x0436c27c, 	/* btb.rc_pkt_full_4 .. btb.rc_pkt_full_7 (4 regs) */
	0x0436c28b, 	/* btb.rc_pkt_status_4 .. btb.rc_pkt_status_7 (4 regs) */
	0x013e0000, 	/* block tdif */
	0x040c403c, 	/* tdif.stat_num_err_interval_4 .. tdif.stat_num_err_interval_7 (4 regs) */
	0x01420000, 	/* block igu */
	0x05060310, 	/* igu.vf_with_more_16sb_16 .. igu.vf_with_more_16sb_20 (5 regs) */
	0x014a0000, 	/* block dbg */
	0x01004119, 	/* dbg.ifmux_select (1 regs) */
	0x094b0000, 	/* block nig */
	0x02140028, 	/* nig.INT_STS_6 .. nig.INT_MASK_6 (2 regs) */
	0x0214002c, 	/* nig.INT_STS_7 .. nig.INT_MASK_7 (2 regs) */
	0x02140030, 	/* nig.INT_STS_8 .. nig.INT_MASK_8 (2 regs) */
	0x02140034, 	/* nig.INT_STS_9 .. nig.INT_MASK_9 (2 regs) */
	0x07142400, 	/* nig.tx_tdm_0_enable .. nig.tsgen_pps_out_sel_mask_3 (7 regs) */
	0x01142409, 	/* nig.tsgen_tsio_in_val (1 regs) */
	0x01142413, 	/* nig.ts_for_pxp_select (1 regs) */
	0x02942414, 	/* nig.ptm_time_latch (2 regs, WB) */
	0x0114241a, 	/* nig.mpa_mul_pdu_crc_calc_en (1 regs) */
	0x034d0000, 	/* block bmbn */
	0x02184010, 	/* bmbn.INT_STS_0 .. bmbn.INT_MASK_0 (2 regs) */
	0x05184050, 	/* bmbn.dbg_select .. bmbn.dbg_force_frame (5 regs) */
	0x0318407e, 	/* bmbn.tag_len_0 .. bmbn.eco_reserved (3 regs) */
	0x644f0000, 	/* block nwm */
	0x01200001, 	/* nwm.INT_STS (1 regs) */
	0x3b200005, 	/* nwm.mac0_peer_delay .. nwm.dbg_force_frame (59 regs) */
	0x06200100, 	/* nwm.mac0.REVISION .. nwm.mac0.FRM_LENGTH (6 regs) */
	0x08200107, 	/* nwm.mac0.RX_FIFO_SECTIONS .. nwm.mac0.MDIO_DATA (8 regs) */
	0x0e200110, 	/* nwm.mac0.STATUS .. nwm.mac0.RX_PAUSE_STATUS (14 regs) */
	0x0220011f, 	/* nwm.mac0.TS_TIMESTAMP .. nwm.mac0.XIF_MODE (2 regs) */
	0x03200138, 	/* nwm.mac0.STATN_CONFIG .. nwm.mac0.STATN_CLEARVALUE_HI (3 regs) */
	0x36200140, 	/* nwm.mac0.etherStatsOctets .. nwm.mac0.aInRangeLengthError_h (54 regs) */
	0x04200180, 	/* nwm.mac0.TXetherStatsOctets .. nwm.mac0.TXOctetsOK_h (4 regs) */
	0x24200186, 	/* nwm.mac0.TXaPauseMacCtrlFrames .. nwm.mac0.TXetherStatsPkts1519toTX_MTU_h (36 regs) */
	0x022001b0, 	/* nwm.mac0.TXaMACControlFrames .. nwm.mac0.TXaMACControlFrames_h (2 regs) */
	0x262001e0, 	/* nwm.mac0.aCBFCPAUSEFramesReceived_0 .. nwm.mac1.FRM_LENGTH (38 regs) */
	0x08200207, 	/* nwm.mac1.RX_FIFO_SECTIONS .. nwm.mac1.MDIO_DATA (8 regs) */
	0x0e200210, 	/* nwm.mac1.STATUS .. nwm.mac1.RX_PAUSE_STATUS (14 regs) */
	0x0220021f, 	/* nwm.mac1.TS_TIMESTAMP .. nwm.mac1.XIF_MODE (2 regs) */
	0x03200238, 	/* nwm.mac1.STATN_CONFIG .. nwm.mac1.STATN_CLEARVALUE_HI (3 regs) */
	0x36200240, 	/* nwm.mac1.etherStatsOctets .. nwm.mac1.aInRangeLengthError_h (54 regs) */
	0x04200280, 	/* nwm.mac1.TXetherStatsOctets .. nwm.mac1.TXOctetsOK_h (4 regs) */
	0x24200286, 	/* nwm.mac1.TXaPauseMacCtrlFrames .. nwm.mac1.TXetherStatsPkts1519toTX_MTU_h (36 regs) */
	0x022002b0, 	/* nwm.mac1.TXaMACControlFrames .. nwm.mac1.TXaMACControlFrames_h (2 regs) */
	0x262002e0, 	/* nwm.mac1.aCBFCPAUSEFramesReceived_0 .. nwm.mac2.FRM_LENGTH (38 regs) */
	0x08200307, 	/* nwm.mac2.RX_FIFO_SECTIONS .. nwm.mac2.MDIO_DATA (8 regs) */
	0x0e200310, 	/* nwm.mac2.STATUS .. nwm.mac2.RX_PAUSE_STATUS (14 regs) */
	0x0220031f, 	/* nwm.mac2.TS_TIMESTAMP .. nwm.mac2.XIF_MODE (2 regs) */
	0x03200338, 	/* nwm.mac2.STATN_CONFIG .. nwm.mac2.STATN_CLEARVALUE_HI (3 regs) */
	0x36200340, 	/* nwm.mac2.etherStatsOctets .. nwm.mac2.aInRangeLengthError_h (54 regs) */
	0x04200380, 	/* nwm.mac2.TXetherStatsOctets .. nwm.mac2.TXOctetsOK_h (4 regs) */
	0x24200386, 	/* nwm.mac2.TXaPauseMacCtrlFrames .. nwm.mac2.TXetherStatsPkts1519toTX_MTU_h (36 regs) */
	0x022003b0, 	/* nwm.mac2.TXaMACControlFrames .. nwm.mac2.TXaMACControlFrames_h (2 regs) */
	0x262003e0, 	/* nwm.mac2.aCBFCPAUSEFramesReceived_0 .. nwm.mac3.FRM_LENGTH (38 regs) */
	0x08200407, 	/* nwm.mac3.RX_FIFO_SECTIONS .. nwm.mac3.MDIO_DATA (8 regs) */
	0x0e200410, 	/* nwm.mac3.STATUS .. nwm.mac3.RX_PAUSE_STATUS (14 regs) */
	0x0220041f, 	/* nwm.mac3.TS_TIMESTAMP .. nwm.mac3.XIF_MODE (2 regs) */
	0x03200438, 	/* nwm.mac3.STATN_CONFIG .. nwm.mac3.STATN_CLEARVALUE_HI (3 regs) */
	0x36200440, 	/* nwm.mac3.etherStatsOctets .. nwm.mac3.aInRangeLengthError_h (54 regs) */
	0x04200480, 	/* nwm.mac3.TXetherStatsOctets .. nwm.mac3.TXOctetsOK_h (4 regs) */
	0x24200486, 	/* nwm.mac3.TXaPauseMacCtrlFrames .. nwm.mac3.TXetherStatsPkts1519toTX_MTU_h (36 regs) */
	0x022004b0, 	/* nwm.mac3.TXaMACControlFrames .. nwm.mac3.TXaMACControlFrames_h (2 regs) */
	0x272004e0, 	/* nwm.mac3.aCBFCPAUSEFramesReceived_0 .. nwm.pcs_reg91_0.RS_FEC_LANEMAP (39 regs) */
	0x0820050a, 	/* nwm.pcs_reg91_0.RS_FEC_SYMBLERR0_LO .. nwm.pcs_reg91_0.RS_FEC_SYMBLERR3_HI (8 regs) */
	0x08200580, 	/* nwm.pcs_reg91_0.RS_FEC_VENDOR_CONTROL .. nwm.pcs_reg91_0.RS_FEC_VENDOR_TX_TESTTRIGGER (8 regs) */
	0x07200600, 	/* nwm.pcs_reg91_1.RS_FEC_CONTROL .. nwm.pcs_reg91_1.RS_FEC_LANEMAP (7 regs) */
	0x0820060a, 	/* nwm.pcs_reg91_1.RS_FEC_SYMBLERR0_LO .. nwm.pcs_reg91_1.RS_FEC_SYMBLERR3_HI (8 regs) */
	0x08200680, 	/* nwm.pcs_reg91_1.RS_FEC_VENDOR_CONTROL .. nwm.pcs_reg91_1.RS_FEC_VENDOR_TX_TESTTRIGGER (8 regs) */
	0x07200700, 	/* nwm.pcs_reg91_2.RS_FEC_CONTROL .. nwm.pcs_reg91_2.RS_FEC_LANEMAP (7 regs) */
	0x0820070a, 	/* nwm.pcs_reg91_2.RS_FEC_SYMBLERR0_LO .. nwm.pcs_reg91_2.RS_FEC_SYMBLERR3_HI (8 regs) */
	0x08200780, 	/* nwm.pcs_reg91_2.RS_FEC_VENDOR_CONTROL .. nwm.pcs_reg91_2.RS_FEC_VENDOR_TX_TESTTRIGGER (8 regs) */
	0x07200800, 	/* nwm.pcs_reg91_3.RS_FEC_CONTROL .. nwm.pcs_reg91_3.RS_FEC_LANEMAP (7 regs) */
	0x0820080a, 	/* nwm.pcs_reg91_3.RS_FEC_SYMBLERR0_LO .. nwm.pcs_reg91_3.RS_FEC_SYMBLERR3_HI (8 regs) */
	0x08200880, 	/* nwm.pcs_reg91_3.RS_FEC_VENDOR_CONTROL .. nwm.pcs_reg91_3.RS_FEC_VENDOR_TX_TESTTRIGGER (8 regs) */
	0x09200900, 	/* nwm.pcs_ls0.CONTROL .. nwm.pcs_ls0.LP_NP_RX (9 regs) */
	0x06200910, 	/* nwm.pcs_ls0.SCRATCH .. nwm.pcs_ls0.DECODE_ERRORS (6 regs) */
	0x09200920, 	/* nwm.pcs_ls1.CONTROL .. nwm.pcs_ls1.LP_NP_RX (9 regs) */
	0x06200930, 	/* nwm.pcs_ls1.SCRATCH .. nwm.pcs_ls1.DECODE_ERRORS (6 regs) */
	0x09200940, 	/* nwm.pcs_ls2.CONTROL .. nwm.pcs_ls2.LP_NP_RX (9 regs) */
	0x06200950, 	/* nwm.pcs_ls2.SCRATCH .. nwm.pcs_ls2.DECODE_ERRORS (6 regs) */
	0x09200960, 	/* nwm.pcs_ls3.CONTROL .. nwm.pcs_ls3.LP_NP_RX (9 regs) */
	0x06200970, 	/* nwm.pcs_ls3.SCRATCH .. nwm.pcs_ls3.DECODE_ERRORS (6 regs) */
	0x09210000, 	/* nwm.pcs_hs0.CONTROL1 .. nwm.pcs_hs0.STATUS2 (9 regs) */
	0x0221000e, 	/* nwm.pcs_hs0.PKG_ID0 .. nwm.pcs_hs0.PKG_ID1 (2 regs) */
	0x01210014, 	/* nwm.pcs_hs0.EEE_CTRL_CAPABILITY (1 regs) */
	0x01210016, 	/* nwm.pcs_hs0.WAKE_ERR_COUNTER (1 regs) */
	0x0e210020, 	/* nwm.pcs_hs0.BASER_STATUS1 .. nwm.pcs_hs0.ERR_BLK_HIGH_ORDER_CNT (14 regs) */
	0x01210032, 	/* nwm.pcs_hs0.MULTILANE_ALIGN_STAT1 (1 regs) */
	0x01210034, 	/* nwm.pcs_hs0.MULTILANE_ALIGN_STAT3 (1 regs) */
	0x042100c8, 	/* nwm.pcs_hs0.BIP_ERR_CNT_LANE0 .. nwm.pcs_hs0.BIP_ERR_CNT_LANE3 (4 regs) */
	0x04210190, 	/* nwm.pcs_hs0.LANE0_MAPPING .. nwm.pcs_hs0.LANE3_MAPPING (4 regs) */
	0x05218000, 	/* nwm.pcs_hs0.VENDOR_SCRATCH .. nwm.pcs_hs0.VENDOR_RXLAUI_CONFIG (5 regs) */
	0x09218008, 	/* nwm.pcs_hs0.VENDOR_VL0_0 .. nwm.pcs_hs0.VENDOR_PCS_MODE (9 regs) */
	0x09220000, 	/* nwm.pcs_hs1.CONTROL1 .. nwm.pcs_hs1.STATUS2 (9 regs) */
	0x0222000e, 	/* nwm.pcs_hs1.PKG_ID0 .. nwm.pcs_hs1.PKG_ID1 (2 regs) */
	0x01220014, 	/* nwm.pcs_hs1.EEE_CTRL_CAPABILITY (1 regs) */
	0x01220016, 	/* nwm.pcs_hs1.WAKE_ERR_COUNTER (1 regs) */
	0x0e220020, 	/* nwm.pcs_hs1.BASER_STATUS1 .. nwm.pcs_hs1.ERR_BLK_HIGH_ORDER_CNT (14 regs) */
	0x01220032, 	/* nwm.pcs_hs1.MULTILANE_ALIGN_STAT1 (1 regs) */
	0x01220034, 	/* nwm.pcs_hs1.MULTILANE_ALIGN_STAT3 (1 regs) */
	0x042200c8, 	/* nwm.pcs_hs1.BIP_ERR_CNT_LANE0 .. nwm.pcs_hs1.BIP_ERR_CNT_LANE3 (4 regs) */
	0x04220190, 	/* nwm.pcs_hs1.LANE0_MAPPING .. nwm.pcs_hs1.LANE3_MAPPING (4 regs) */
	0x05228000, 	/* nwm.pcs_hs1.VENDOR_SCRATCH .. nwm.pcs_hs1.VENDOR_RXLAUI_CONFIG (5 regs) */
	0x09228008, 	/* nwm.pcs_hs1.VENDOR_VL0_0 .. nwm.pcs_hs1.VENDOR_PCS_MODE (9 regs) */
	0x09230000, 	/* nwm.pcs_hs2.CONTROL1 .. nwm.pcs_hs2.STATUS2 (9 regs) */
	0x0223000e, 	/* nwm.pcs_hs2.PKG_ID0 .. nwm.pcs_hs2.PKG_ID1 (2 regs) */
	0x01230014, 	/* nwm.pcs_hs2.EEE_CTRL_CAPABILITY (1 regs) */
	0x01230016, 	/* nwm.pcs_hs2.WAKE_ERR_COUNTER (1 regs) */
	0x0e230020, 	/* nwm.pcs_hs2.BASER_STATUS1 .. nwm.pcs_hs2.ERR_BLK_HIGH_ORDER_CNT (14 regs) */
	0x01230032, 	/* nwm.pcs_hs2.MULTILANE_ALIGN_STAT1 (1 regs) */
	0x01230034, 	/* nwm.pcs_hs2.MULTILANE_ALIGN_STAT3 (1 regs) */
	0x042300c8, 	/* nwm.pcs_hs2.BIP_ERR_CNT_LANE0 .. nwm.pcs_hs2.BIP_ERR_CNT_LANE3 (4 regs) */
	0x04238000, 	/* nwm.pcs_hs2.VENDOR_SCRATCH .. nwm.pcs_hs2.VENDOR_TXLANE_THRESH (4 regs) */
	0x09238008, 	/* nwm.pcs_hs2.VENDOR_VL0_0 .. nwm.pcs_hs2.VENDOR_PCS_MODE (9 regs) */
	0x09240000, 	/* nwm.pcs_hs3.CONTROL1 .. nwm.pcs_hs3.STATUS2 (9 regs) */
	0x0224000e, 	/* nwm.pcs_hs3.PKG_ID0 .. nwm.pcs_hs3.PKG_ID1 (2 regs) */
	0x01240014, 	/* nwm.pcs_hs3.EEE_CTRL_CAPABILITY (1 regs) */
	0x01240016, 	/* nwm.pcs_hs3.WAKE_ERR_COUNTER (1 regs) */
	0x0e240020, 	/* nwm.pcs_hs3.BASER_STATUS1 .. nwm.pcs_hs3.ERR_BLK_HIGH_ORDER_CNT (14 regs) */
	0x01240032, 	/* nwm.pcs_hs3.MULTILANE_ALIGN_STAT1 (1 regs) */
	0x01240034, 	/* nwm.pcs_hs3.MULTILANE_ALIGN_STAT3 (1 regs) */
	0x042400c8, 	/* nwm.pcs_hs3.BIP_ERR_CNT_LANE0 .. nwm.pcs_hs3.BIP_ERR_CNT_LANE3 (4 regs) */
	0x04248000, 	/* nwm.pcs_hs3.VENDOR_SCRATCH .. nwm.pcs_hs3.VENDOR_TXLANE_THRESH (4 regs) */
	0x09248008, 	/* nwm.pcs_hs3.VENDOR_VL0_0 .. nwm.pcs_hs3.VENDOR_PCS_MODE (9 regs) */
	0x02500000, 	/* block nws */
	0x0b1c004a, 	/* nws.dbg_select .. nws.dbg_fw_trigger_enable (11 regs) */
	0x021c0060, 	/* nws.INT_STS_0 .. nws.INT_MASK_0 (2 regs) */
	0x02530000, 	/* block led */
	0x061ae006, 	/* led.mac_led_swap .. led.eco_reserved (6 regs) */
	0x021ae060, 	/* led.INT_STS_0 .. led.INT_MASK_0 (2 regs) */
	0x01550000, 	/* block pxpreqbus */
	0x01015880, 	/* pxpreqbus.eco_reserved (1 regs) */
	0x01040015, 	/* mode !(bb|k2), block pglue_b */
	0x020aa13a, 	/* pglue_b.dorq_access_via_bar0 .. pglue_b.vsc_en (2 regs) */
	0x010d0000, 	/* block pswhst */
	0x010a8031, 	/* pswhst.dest_dorq_credits (1 regs) */
	0x03100000, 	/* block pswrd2 */
	0x020a7436, 	/* pswrd2.rgsrc_swap_mode .. pswrd2.tgsrc_swap_mode (2 regs) */
	0x030a7456, 	/* pswrd2.almost_full_16 .. pswrd2.max_fill_level5 (3 regs) */
	0x020a752c, 	/* pswrd2.tgsrc_additional_requests .. pswrd2.rgsrc_additional_requests (2 regs) */
	0x01110000, 	/* block pswwr */
	0x030a6833, 	/* pswwr.prms_full_th .. pswwr.tgsrc_full_th (3 regs) */
	0x01120000, 	/* block pswwr2 */
	0x030a6c1a, 	/* pswwr2.prms_full_th2 .. pswwr2.prms_max_fill_level (3 regs) */
	0x11140000, 	/* block pswrq2 */
	0x01090163, 	/* pswrq2.BW_ADD0 (1 regs) */
	0x02090167, 	/* pswrq2.BW_ADD4 .. pswrq2.BW_ADD5 (2 regs) */
	0x0209016f, 	/* pswrq2.BW_ADD12 .. pswrq2.BW_ADD13 (2 regs) */
	0x0d090172, 	/* pswrq2.BW_ADD15 .. pswrq2.BW_ADD27 (13 regs) */
	0x02090180, 	/* pswrq2.BW_ADD29 .. pswrq2.BW_ADD30 (2 regs) */
	0x01090183, 	/* pswrq2.BW_UB0 (1 regs) */
	0x02090187, 	/* pswrq2.BW_UB4 .. pswrq2.BW_UB5 (2 regs) */
	0x0209018f, 	/* pswrq2.BW_UB12 .. pswrq2.BW_UB13 (2 regs) */
	0x0d090192, 	/* pswrq2.BW_UB15 .. pswrq2.BW_UB27 (13 regs) */
	0x020901a0, 	/* pswrq2.BW_UB29 .. pswrq2.BW_UB30 (2 regs) */
	0x010901a3, 	/* pswrq2.BW_L0 (1 regs) */
	0x020901a7, 	/* pswrq2.BW_L4 .. pswrq2.BW_L5 (2 regs) */
	0x020901af, 	/* pswrq2.BW_L12 .. pswrq2.BW_L13 (2 regs) */
	0x0d0901b2, 	/* pswrq2.BW_L15 .. pswrq2.BW_L27 (13 regs) */
	0x020901c0, 	/* pswrq2.BW_L29 .. pswrq2.BW_L30 (2 regs) */
	0x2a090315, 	/* pswrq2.mem_base_addr_vq0 .. pswrq2.PRMS_PCI_ATTR (42 regs) */
	0x1e090345, 	/* pswrq2.cnt_eop_15 .. pswrq2.tgsrc_endian_m (30 regs) */
	0x01160000, 	/* block dmae */
	0x01003032, 	/* dmae.go_sticky (1 regs) */
	0x01170000, 	/* block ptu */
	0x0515817b, 	/* ptu.LOG_INV_HALT_RSC_TYPE .. ptu.index2_rsc_type_mask (5 regs) */
	0x0c180000, 	/* block tcm */
	0x2c460022, 	/* tcm.affinity_type_0 .. tcm.agg_con_cf11_q (44 regs) */
	0x0b46015a, 	/* tcm.agg_con_rule0_q .. tcm.agg_con_rule10_q (11 regs) */
	0x014601a2, 	/* tcm.ext_rd_fill_lvl (1 regs) */
	0x134601e9, 	/* tcm.err_affinity_type .. tcm.xx_byp_msg_up_bnd_15 (19 regs) */
	0x08460267, 	/* tcm.cm_task_event_id_bwidth_0 .. tcm.cm_task_event_id_bwidth_7 (8 regs) */
	0x1046028e, 	/* tcm.cm_con_event_id_bwidth_0 .. tcm.cm_con_event_id_bwidth_15 (16 regs) */
	0x85460640, 	/* tcm.tm_con_evnt_id_0 .. tcm.is_foc_tsdm_nxt_inf_unit (133 regs) */
	0x054606e0, 	/* tcm.psdm_weight .. tcm.is_foc_psdm_nxt_inf_unit (5 regs) */
	0x05460700, 	/* tcm.msdm_weight .. tcm.is_foc_msdm_nxt_inf_unit (5 regs) */
	0x04460720, 	/* tcm.ysem_weight .. tcm.is_foc_ysem_nxt_inf_unit (4 regs) */
	0x05460780, 	/* tcm.ptld_weight .. tcm.is_foc_ptld_nxt_inf_unit (5 regs) */
	0x08460a00, 	/* tcm.agg_task_cf0_q .. tcm.agg_task_cf7_q (8 regs) */
	0x0c190000, 	/* block mcm */
	0x23480022, 	/* mcm.affinity_type_0 .. mcm.agg_con_cf2_q (35 regs) */
	0x0548015a, 	/* mcm.agg_con_rule0_q .. mcm.agg_con_rule4_q (5 regs) */
	0x014801a4, 	/* mcm.ext_rd_fill_lvl (1 regs) */
	0x134801e9, 	/* mcm.err_affinity_type .. mcm.xx_byp_msg_up_bnd_15 (19 regs) */
	0x09480257, 	/* mcm.agg_task_rule7_q .. mcm.cm_task_event_id_bwidth_7 (9 regs) */
	0x1048028c, 	/* mcm.cm_con_event_id_bwidth_0 .. mcm.cm_con_event_id_bwidth_15 (16 regs) */
	0x70480740, 	/* mcm.n_sm_con_ctx_ld_0 .. mcm.qm_sm_con_ctx_ldst_flg_15 (112 regs) */
	0x05480a00, 	/* mcm.tsdm_weight .. mcm.is_foc_tsdm_nxt_inf_unit (5 regs) */
	0x05480a20, 	/* mcm.psdm_weight .. mcm.is_foc_psdm_nxt_inf_unit (5 regs) */
	0x05480a40, 	/* mcm.msdm_weight .. mcm.is_foc_msdm_nxt_inf_unit (5 regs) */
	0x04480a80, 	/* mcm.ysem_weight .. mcm.is_foc_ysem_nxt_inf_unit (4 regs) */
	0x05480b80, 	/* mcm.agg_task_cf0_q .. mcm.agg_task_cf4_q (5 regs) */
	0x091a0000, 	/* block ucm */
	0x294a002a, 	/* ucm.affinity_type_0 .. ucm.agg_con_cf8_q (41 regs) */
	0x094a015a, 	/* ucm.agg_con_rule0_q .. ucm.agg_con_rule8_q (9 regs) */
	0x014a01aa, 	/* ucm.ext_rd_fill_lvl (1 regs) */
	0x134a01e9, 	/* ucm.err_affinity_type .. ucm.xx_byp_msg_up_bnd_15 (19 regs) */
	0x0a4a0261, 	/* ucm.agg_task_rule7_q .. ucm.cm_task_event_id_bwidth_7 (10 regs) */
	0x104a028e, 	/* ucm.cm_con_event_id_bwidth_0 .. ucm.cm_con_event_id_bwidth_15 (16 regs) */
	0x804a0700, 	/* ucm.tm_con_evnt_id_0 .. ucm.qm_sm_con_ctx_ldst_flg_15 (128 regs) */
	0x064a0a00, 	/* ucm.ring_base .. ucm.is_foc_ysem_nxt_inf_unit (6 regs) */
	0x064a0a80, 	/* ucm.agg_task_cf0_q .. ucm.agg_task_cf5_q (6 regs) */
	0x071b0000, 	/* block xcm */
	0x39400022, 	/* xcm.affinity_type_0 .. xcm.agg_con_cf24_q (57 regs) */
	0x1c400132, 	/* xcm.agg_con_rule0_q .. xcm.agg_con_rule27_q (28 regs) */
	0x014001a6, 	/* xcm.ext_rd_fill_lvl (1 regs) */
	0x134001e6, 	/* xcm.err_affinity_type .. xcm.xx_byp_msg_up_bnd_15 (19 regs) */
	0x1040028a, 	/* xcm.cm_con_event_id_bwidth_0 .. xcm.cm_con_event_id_bwidth_15 (16 regs) */
	0x95400700, 	/* xcm.tm_con_evnt_id_0 .. xcm.is_foc_msdm_nxt_inf_unit (149 regs) */
	0x044007c0, 	/* xcm.ysem_weight .. xcm.is_foc_ysem_nxt_inf_unit (4 regs) */
	0x091c0000, 	/* block ycm */
	0x23420022, 	/* ycm.affinity_type_0 .. ycm.agg_con_cf2_q (35 regs) */
	0x0542015a, 	/* ycm.agg_con_rule0_q .. ycm.agg_con_rule4_q (5 regs) */
	0x014201a2, 	/* ycm.ext_rd_fill_lvl (1 regs) */
	0x134201e9, 	/* ycm.err_affinity_type .. ycm.xx_byp_msg_up_bnd_15 (19 regs) */
	0x10420251, 	/* ycm.agg_task_rule0_q .. ycm.cm_task_event_id_bwidth_7 (16 regs) */
	0x1042028c, 	/* ycm.cm_con_event_id_bwidth_0 .. ycm.cm_con_event_id_bwidth_15 (16 regs) */
	0x75420740, 	/* ycm.n_sm_con_ctx_ld_0 .. ycm.is_foc_msdm_nxt_inf_unit (117 regs) */
	0x01420900, 	/* ycm.is_foc_ysem_nxt_inf_unit (1 regs) */
	0x04420b00, 	/* ycm.agg_task_cf0_q .. ycm.agg_task_cf4_q (4 regs) */
	0x041d0000, 	/* block pcm */
	0x01440194, 	/* pcm.ext_rd_fill_lvl (1 regs) */
	0x034401d9, 	/* pcm.err_affinity_type .. pcm.err_src_affinity (3 regs) */
	0x154405c4, 	/* pcm.n_sm_con_ctx_ld_0 .. pcm.is_foc_psdm_nxt_inf_unit (21 regs) */
	0x054405f0, 	/* pcm.ypld_weight .. pcm.is_foc_ypld_nxt_inf_unit (5 regs) */
	0x031e0000, 	/* block qm */
	0x080bd733, 	/* qm.Voq_Arb_Grp2_Weight_0 .. qm.Voq_Arb_Grp2_Weight_7 (8 regs) */
	0xa00bda00, 	/* qm.CtxRegCcfc_0 .. qm.ActCtrInitValCcfc_79 (160 regs) */
	0x040bff80, 	/* qm.AFullQmBypThrLineVoqMask_msb .. qm.VoqCrdByteFull_msb (4 regs) */
	0x08200000, 	/* block dorq */
	0x010402ab, 	/* dorq.iedpm_payload_endianity (1 regs) */
	0x04040a00, 	/* dorq.glb_max_icid_0 .. dorq.glb_range2conn_type_1 (4 regs) */
	0x02040a14, 	/* dorq.iedpm_exist_in_qm_en .. dorq.iedpm_agg_type (2 regs) */
	0xc3040a1a, 	/* dorq.edpm_agg_type_sel_0 .. dorq.rtc_en (195 regs) */
	0x02040adf, 	/* dorq.crc32c_bswap .. dorq.iwarp_opcode_en (2 regs) */
	0x0c040ae9, 	/* dorq.rdma_en_pbf_spc_roce .. dorq.iedpm_abort_details_reason (12 regs) */
	0x06040af6, 	/* dorq.iedpm_abort_reason .. dorq.iedpm_drop_details_db_icid (6 regs) */
	0x41040afd, 	/* dorq.iedpm_drop_reason .. dorq.dpm_iedpm_success_cnt (65 regs) */
	0x01210000, 	/* block brb */
	0x090d1000, 	/* brb.wc_ll_high_pri .. brb.limit_oversubscrition_pause_lb (9 regs) */
	0x0b230000, 	/* block prs */
	0x0307c2ec, 	/* prs.fc_dbg_select_a .. prs.fc_dbg_shift_a (3 regs) */
	0x0887c2f0, 	/* prs.fc_dbg_out_data_a (8 regs, WB) */
	0x0407c2f8, 	/* prs.fc_dbg_force_valid_a .. prs.fc_dbg_out_frame_a (4 regs) */
	0x0307c380, 	/* prs.fc_dbg_select_b .. prs.fc_dbg_shift_b (3 regs) */
	0x0887c388, 	/* prs.fc_dbg_out_data_b (8 regs, WB) */
	0x0407c390, 	/* prs.fc_dbg_force_valid_b .. prs.fc_dbg_out_frame_b (4 regs) */
	0x0107c3c0, 	/* prs.ptld_initial_credit (1 regs) */
	0x0107c3c3, 	/* prs.ptld_current_credit (1 regs) */
	0x0707c3ce, 	/* prs.rgfs_initial_credit .. prs.fce_use_single_fc_chicken_bit (7 regs) */
	0xb907c500, 	/* prs.compare_gre_version .. prs.xrc_opcodes (185 regs) */
	0x0407c5ba, 	/* prs.new_entry_exclusive_classify_failed .. prs.en_ipv6_ext_event_id (4 regs) */
	0x01240000, 	/* block tsdm */
	0x013ec148, 	/* tsdm.init_credit_cm_rmt (1 regs) */
	0x122a0000, 	/* block tsem */
	0x085c0005, 	/* tsem.passive_buffer_write_wrr_arbiter .. tsem.passive_buffer_dra_wr (8 regs) */
	0x025c0018, 	/* tsem.INT_STS_2 .. tsem.INT_MASK_2 (2 regs) */
	0x015c0114, 	/* tsem.gpi_data_a (1 regs) */
	0x035c0118, 	/* tsem.pb_wr_sdm_dma_mode .. tsem.gpi_data_b (3 regs) */
	0x015c02c1, 	/* tsem.thread_error_low (1 regs) */
	0x025c02c6, 	/* tsem.thread_number .. tsem.thread_error_high (2 regs) */
	0x015c0402, 	/* tsem.sync_dra_wr_credit (1 regs) */
	0x095c0404, 	/* tsem.sync_foc_fifo_wr_alm_full .. tsem.stall_on_breakpoint (9 regs) */
	0x055c045b, 	/* tsem.pb_queue_empty .. tsem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x035c049f, 	/* tsem.sync_ready_fifo_full .. tsem.sync_foc_fifo_full (3 regs) */
	0x105c050a, 	/* tsem.dbg_queue_peformance_mon_stat .. tsem.dbg_queue_max_sleep_value (16 regs) */
	0x015d0122, 	/* tsem.fast_memory.stall_common (1 regs) */
	0x015d0132, 	/* tsem.fast_memory.pram_last_addr_a (1 regs) */
	0x075d0136, 	/* tsem.fast_memory.data_breakpoint_address_start .. tsem.fast_memory.stall_storm_b (7 regs) */
	0x015d01dd, 	/* tsem.fast_memory.dbg_gpre_vect (1 regs) */
	0x015d0214, 	/* tsem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x015d0291, 	/* tsem.fast_memory.storm_active_cycles_a (1 regs) */
	0x085d0293, 	/* tsem.fast_memory.storm_stall_cycles_a .. tsem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x122b0000, 	/* block msem */
	0x08600005, 	/* msem.passive_buffer_write_wrr_arbiter .. msem.passive_buffer_dra_wr (8 regs) */
	0x02600018, 	/* msem.INT_STS_2 .. msem.INT_MASK_2 (2 regs) */
	0x01600114, 	/* msem.gpi_data_a (1 regs) */
	0x03600118, 	/* msem.pb_wr_sdm_dma_mode .. msem.gpi_data_b (3 regs) */
	0x016002c1, 	/* msem.thread_error_low (1 regs) */
	0x026002c6, 	/* msem.thread_number .. msem.thread_error_high (2 regs) */
	0x01600402, 	/* msem.sync_dra_wr_credit (1 regs) */
	0x09600404, 	/* msem.sync_foc_fifo_wr_alm_full .. msem.stall_on_breakpoint (9 regs) */
	0x0560045b, 	/* msem.pb_queue_empty .. msem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x0360049f, 	/* msem.sync_ready_fifo_full .. msem.sync_foc_fifo_full (3 regs) */
	0x1060050a, 	/* msem.dbg_queue_peformance_mon_stat .. msem.dbg_queue_max_sleep_value (16 regs) */
	0x01610122, 	/* msem.fast_memory.stall_common (1 regs) */
	0x01610132, 	/* msem.fast_memory.pram_last_addr_a (1 regs) */
	0x07610136, 	/* msem.fast_memory.data_breakpoint_address_start .. msem.fast_memory.stall_storm_b (7 regs) */
	0x016101dd, 	/* msem.fast_memory.dbg_gpre_vect (1 regs) */
	0x01610214, 	/* msem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x01610291, 	/* msem.fast_memory.storm_active_cycles_a (1 regs) */
	0x08610293, 	/* msem.fast_memory.storm_stall_cycles_a .. msem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x122c0000, 	/* block usem */
	0x08640005, 	/* usem.passive_buffer_write_wrr_arbiter .. usem.passive_buffer_dra_wr (8 regs) */
	0x02640018, 	/* usem.INT_STS_2 .. usem.INT_MASK_2 (2 regs) */
	0x01640114, 	/* usem.gpi_data_a (1 regs) */
	0x03640118, 	/* usem.pb_wr_sdm_dma_mode .. usem.gpi_data_b (3 regs) */
	0x016402c1, 	/* usem.thread_error_low (1 regs) */
	0x026402c6, 	/* usem.thread_number .. usem.thread_error_high (2 regs) */
	0x01640402, 	/* usem.sync_dra_wr_credit (1 regs) */
	0x09640404, 	/* usem.sync_foc_fifo_wr_alm_full .. usem.stall_on_breakpoint (9 regs) */
	0x0564045b, 	/* usem.pb_queue_empty .. usem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x0364049f, 	/* usem.sync_ready_fifo_full .. usem.sync_foc_fifo_full (3 regs) */
	0x1064050a, 	/* usem.dbg_queue_peformance_mon_stat .. usem.dbg_queue_max_sleep_value (16 regs) */
	0x01650122, 	/* usem.fast_memory.stall_common (1 regs) */
	0x01650132, 	/* usem.fast_memory.pram_last_addr_a (1 regs) */
	0x07650136, 	/* usem.fast_memory.data_breakpoint_address_start .. usem.fast_memory.stall_storm_b (7 regs) */
	0x016501dd, 	/* usem.fast_memory.dbg_gpre_vect (1 regs) */
	0x01650214, 	/* usem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x01650291, 	/* usem.fast_memory.storm_active_cycles_a (1 regs) */
	0x08650293, 	/* usem.fast_memory.storm_stall_cycles_a .. usem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x122d0000, 	/* block xsem */
	0x08500005, 	/* xsem.passive_buffer_write_wrr_arbiter .. xsem.passive_buffer_dra_wr (8 regs) */
	0x02500018, 	/* xsem.INT_STS_2 .. xsem.INT_MASK_2 (2 regs) */
	0x01500114, 	/* xsem.gpi_data_a (1 regs) */
	0x03500118, 	/* xsem.pb_wr_sdm_dma_mode .. xsem.gpi_data_b (3 regs) */
	0x015002c1, 	/* xsem.thread_error_low (1 regs) */
	0x025002c6, 	/* xsem.thread_number .. xsem.thread_error_high (2 regs) */
	0x01500402, 	/* xsem.sync_dra_wr_credit (1 regs) */
	0x09500404, 	/* xsem.sync_foc_fifo_wr_alm_full .. xsem.stall_on_breakpoint (9 regs) */
	0x0550045b, 	/* xsem.pb_queue_empty .. xsem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x0350049f, 	/* xsem.sync_ready_fifo_full .. xsem.sync_foc_fifo_full (3 regs) */
	0x1050050a, 	/* xsem.dbg_queue_peformance_mon_stat .. xsem.dbg_queue_max_sleep_value (16 regs) */
	0x01510122, 	/* xsem.fast_memory.stall_common (1 regs) */
	0x01510132, 	/* xsem.fast_memory.pram_last_addr_a (1 regs) */
	0x07510136, 	/* xsem.fast_memory.data_breakpoint_address_start .. xsem.fast_memory.stall_storm_b (7 regs) */
	0x015101dd, 	/* xsem.fast_memory.dbg_gpre_vect (1 regs) */
	0x01510214, 	/* xsem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x01510291, 	/* xsem.fast_memory.storm_active_cycles_a (1 regs) */
	0x08510293, 	/* xsem.fast_memory.storm_stall_cycles_a .. xsem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x122e0000, 	/* block ysem */
	0x08540005, 	/* ysem.passive_buffer_write_wrr_arbiter .. ysem.passive_buffer_dra_wr (8 regs) */
	0x02540018, 	/* ysem.INT_STS_2 .. ysem.INT_MASK_2 (2 regs) */
	0x01540114, 	/* ysem.gpi_data_a (1 regs) */
	0x03540118, 	/* ysem.pb_wr_sdm_dma_mode .. ysem.gpi_data_b (3 regs) */
	0x015402c1, 	/* ysem.thread_error_low (1 regs) */
	0x025402c6, 	/* ysem.thread_number .. ysem.thread_error_high (2 regs) */
	0x01540402, 	/* ysem.sync_dra_wr_credit (1 regs) */
	0x09540404, 	/* ysem.sync_foc_fifo_wr_alm_full .. ysem.stall_on_breakpoint (9 regs) */
	0x0554045b, 	/* ysem.pb_queue_empty .. ysem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x0354049f, 	/* ysem.sync_ready_fifo_full .. ysem.sync_foc_fifo_full (3 regs) */
	0x1054050a, 	/* ysem.dbg_queue_peformance_mon_stat .. ysem.dbg_queue_max_sleep_value (16 regs) */
	0x01550122, 	/* ysem.fast_memory.stall_common (1 regs) */
	0x01550132, 	/* ysem.fast_memory.pram_last_addr_a (1 regs) */
	0x07550136, 	/* ysem.fast_memory.data_breakpoint_address_start .. ysem.fast_memory.stall_storm_b (7 regs) */
	0x015501dd, 	/* ysem.fast_memory.dbg_gpre_vect (1 regs) */
	0x01550214, 	/* ysem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x01550291, 	/* ysem.fast_memory.storm_active_cycles_a (1 regs) */
	0x08550293, 	/* ysem.fast_memory.storm_stall_cycles_a .. ysem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x122f0000, 	/* block psem */
	0x08580005, 	/* psem.passive_buffer_write_wrr_arbiter .. psem.passive_buffer_dra_wr (8 regs) */
	0x02580018, 	/* psem.INT_STS_2 .. psem.INT_MASK_2 (2 regs) */
	0x01580114, 	/* psem.gpi_data_a (1 regs) */
	0x03580118, 	/* psem.pb_wr_sdm_dma_mode .. psem.gpi_data_b (3 regs) */
	0x015802c1, 	/* psem.thread_error_low (1 regs) */
	0x025802c6, 	/* psem.thread_number .. psem.thread_error_high (2 regs) */
	0x01580402, 	/* psem.sync_dra_wr_credit (1 regs) */
	0x09580404, 	/* psem.sync_foc_fifo_wr_alm_full .. psem.stall_on_breakpoint (9 regs) */
	0x0558045b, 	/* psem.pb_queue_empty .. psem.ext_store_pre_fetch_fifo_empty (5 regs) */
	0x0358049f, 	/* psem.sync_ready_fifo_full .. psem.sync_foc_fifo_full (3 regs) */
	0x1058050a, 	/* psem.dbg_queue_peformance_mon_stat .. psem.dbg_queue_max_sleep_value (16 regs) */
	0x01590122, 	/* psem.fast_memory.stall_common (1 regs) */
	0x01590132, 	/* psem.fast_memory.pram_last_addr_a (1 regs) */
	0x07590136, 	/* psem.fast_memory.data_breakpoint_address_start .. psem.fast_memory.stall_storm_b (7 regs) */
	0x015901dd, 	/* psem.fast_memory.dbg_gpre_vect (1 regs) */
	0x01590214, 	/* psem.fast_memory.sync_dra_wr_alm_full (1 regs) */
	0x01590291, 	/* psem.fast_memory.storm_active_cycles_a (1 regs) */
	0x08590293, 	/* psem.fast_memory.storm_stall_cycles_a .. psem.fast_memory.lock_max_cycle_stall (8 regs) */
	0x01300000, 	/* block rss */
	0x0408e326, 	/* rss.fifo_full_status1 .. rss.state_machines1 (4 regs) */
	0x01310000, 	/* block tmld */
	0x3a134240, 	/* tmld.l2ma_aggr_config1 .. tmld.scbd_wrr_weight_q3 (58 regs) */
	0x01320000, 	/* block muld */
	0x37138500, 	/* muld.l2ma_aggr_config1 .. muld.page_size (55 regs) */
	0x01340000, 	/* block xyld */
	0x3a130240, 	/* xyld.l2ma_aggr_config1 .. xyld.scbd_wrr_weight_q3 (58 regs) */
	0x06350000, 	/* block ptld */
	0x10168000, 	/* ptld.foci_foc_credits .. ptld.cm_hdr_127_96 (16 regs) */
	0x03168011, 	/* ptld.stat_fic_msg .. ptld.len_err_log_2 (3 regs) */
	0x01168015, 	/* ptld.len_err_log_v (1 regs) */
	0x02168060, 	/* ptld.INT_STS .. ptld.INT_MASK (2 regs) */
	0x36168200, 	/* ptld.l2ma_aggr_config1 .. ptld.ld_max_msg_size (54 regs) */
	0x05168580, 	/* ptld.dbg_select .. ptld.dbg_force_frame (5 regs) */
	0x06360000, 	/* block ypld */
	0x10170000, 	/* ypld.foci_foc_credits .. ypld.cm_hdr_127_96 (16 regs) */
	0x03170011, 	/* ypld.stat_fic_msg .. ypld.len_err_log_2 (3 regs) */
	0x01170015, 	/* ypld.len_err_log_v (1 regs) */
	0x02170060, 	/* ypld.INT_STS .. ypld.INT_MASK (2 regs) */
	0x36170200, 	/* ypld.l2ma_aggr_config1 .. ypld.ld_max_msg_size (54 regs) */
	0x05170580, 	/* ypld.dbg_select .. ypld.dbg_force_frame (5 regs) */
	0x01370000, 	/* block prm */
	0x1308c182, 	/* prm.num_of_wdone .. prm.num_of_xcm_done (19 regs) */
	0x013b0000, 	/* block btb */
	0x0236d000, 	/* btb.wc_ll_high_pri .. btb.br_fix_high_pri_collision (2 regs) */
	0x1b3c0000, 	/* block pbf */
	0x03360002, 	/* pbf.sal_cache_init_done .. pbf.sal_cam_scrub_miss_en (3 regs) */
	0x0336002d, 	/* pbf.fc_dbg_select_b .. pbf.fc_dbg_shift_b (3 regs) */
	0x0436003c, 	/* pbf.fc_dbg_force_valid_b .. pbf.fc_dbg_out_frame_b (4 regs) */
	0x08b60058, 	/* pbf.fc_dbg_out_data_b (8 regs, WB) */
	0x0236010c, 	/* pbf.tgfs_main_if_init_crd .. pbf.tgfs_side_if_init_crd (2 regs) */
	0x0b36014a, 	/* pbf.same_as_last_config .. pbf.mpls_types (11 regs) */
	0x06360156, 	/* pbf.ipv6_ext_hdr_types_0_3 .. pbf.ipv6_ext_hdr_type_valids (6 regs) */
	0x0336016a, 	/* pbf.event_id_mask_config .. pbf.misc_parsing_config (3 regs) */
	0x01360177, 	/* pbf.rdma_edpm_thrsh (1 regs) */
	0x0136019d, 	/* pbf.per_voq_stat_mask_loopback (1 regs) */
	0x0c3602e8, 	/* pbf.ycmd_qs_num_lines_voq20 .. pbf.num_blocks_allocated_cons_voq20 (12 regs) */
	0x0c3602f8, 	/* pbf.ycmd_qs_num_lines_voq21 .. pbf.num_blocks_allocated_cons_voq21 (12 regs) */
	0x0c360308, 	/* pbf.ycmd_qs_num_lines_voq22 .. pbf.num_blocks_allocated_cons_voq22 (12 regs) */
	0x0c360318, 	/* pbf.ycmd_qs_num_lines_voq23 .. pbf.num_blocks_allocated_cons_voq23 (12 regs) */
	0x0c360328, 	/* pbf.ycmd_qs_num_lines_voq24 .. pbf.num_blocks_allocated_cons_voq24 (12 regs) */
	0x0c360338, 	/* pbf.ycmd_qs_num_lines_voq25 .. pbf.num_blocks_allocated_cons_voq25 (12 regs) */
	0x0c360348, 	/* pbf.ycmd_qs_num_lines_voq26 .. pbf.num_blocks_allocated_cons_voq26 (12 regs) */
	0x0c360358, 	/* pbf.ycmd_qs_num_lines_voq27 .. pbf.num_blocks_allocated_cons_voq27 (12 regs) */
	0x0c360368, 	/* pbf.ycmd_qs_num_lines_voq28 .. pbf.num_blocks_allocated_cons_voq28 (12 regs) */
	0x0c360378, 	/* pbf.ycmd_qs_num_lines_voq29 .. pbf.num_blocks_allocated_cons_voq29 (12 regs) */
	0x0c360388, 	/* pbf.ycmd_qs_num_lines_voq30 .. pbf.num_blocks_allocated_cons_voq30 (12 regs) */
	0x0c360398, 	/* pbf.ycmd_qs_num_lines_voq31 .. pbf.num_blocks_allocated_cons_voq31 (12 regs) */
	0x0c3603a8, 	/* pbf.ycmd_qs_num_lines_voq32 .. pbf.num_blocks_allocated_cons_voq32 (12 regs) */
	0x0c3603b8, 	/* pbf.ycmd_qs_num_lines_voq33 .. pbf.num_blocks_allocated_cons_voq33 (12 regs) */
	0x0c3603c8, 	/* pbf.ycmd_qs_num_lines_voq34 .. pbf.num_blocks_allocated_cons_voq34 (12 regs) */
	0x0c3603d8, 	/* pbf.ycmd_qs_num_lines_voq35 .. pbf.num_blocks_allocated_cons_voq35 (12 regs) */
	0x013603e8, 	/* pbf.eco_reserved (1 regs) */
	0x013d0000, 	/* block rdif */
	0x010c0038, 	/* rdif.e4_backward_compatible_mode (1 regs) */
	0x013e0000, 	/* block tdif */
	0x090c4040, 	/* tdif.stat_num_err_interval_8 .. tdif.e4_backward_compatible_mode (9 regs) */
	0x01400000, 	/* block ccfc */
	0x010b8204, 	/* ccfc.eio_threshold (1 regs) */
	0x01410000, 	/* block tcfc */
	0x010b4204, 	/* tcfc.eio_threshold (1 regs) */
	0x01420000, 	/* block igu */
	0x09060315, 	/* igu.vf_with_more_16sb_21 .. igu.vf_with_more_16sb_29 (9 regs) */
	0x06450000, 	/* block rgsrc */
	0x050c8010, 	/* rgsrc.dbg_select .. rgsrc.dbg_force_frame (5 regs) */
	0x020c8060, 	/* rgsrc.INT_STS .. rgsrc.INT_MASK (2 regs) */
	0x010c8080, 	/* rgsrc.eco_reserved (1 regs) */
	0x040c8100, 	/* rgsrc.cache_en .. rgsrc.max_hops (4 regs) */
	0x040c8113, 	/* rgsrc.pxp_ctrl .. rgsrc.num_inhouse_cmd (4 regs) */
	0x090c8118, 	/* rgsrc.num_src_cmd .. rgsrc.num_src_cmd_hit_hop_3_or_more (9 regs) */
	0x06470000, 	/* block tgsrc */
	0x050c8810, 	/* tgsrc.dbg_select .. tgsrc.dbg_force_frame (5 regs) */
	0x020c8860, 	/* tgsrc.INT_STS .. tgsrc.INT_MASK (2 regs) */
	0x010c8880, 	/* tgsrc.eco_reserved (1 regs) */
	0x040c8900, 	/* tgsrc.cache_en .. tgsrc.max_hops (4 regs) */
	0x040c8913, 	/* tgsrc.pxp_ctrl .. tgsrc.num_inhouse_cmd (4 regs) */
	0x090c8918, 	/* tgsrc.num_src_cmd .. tgsrc.num_src_cmd_hit_hop_3_or_more (9 regs) */
	0x024a0000, 	/* block dbg */
	0x0100411a, 	/* dbg.full_buffer_thr_high (1 regs) */
	0x020042ea, 	/* dbg.filter_mode .. dbg.trigger_semi_core (2 regs) */
	0x034b0000, 	/* block nig */
	0x0214003c, 	/* nig.INT_STS_10 .. nig.INT_MASK_10 (2 regs) */
	0x01143600, 	/* nig.tx_bmb_fifo_alm_full_thr (1 regs) */
	0x29143603, 	/* nig.tx_ooo_rfifo_full .. nig.ipv6_ext_authentication_hdr_type_valid (41 regs) */
	0x01500000, 	/* block nws */
	0x271c0000, 	/* nws.hss0_control_common .. nws.eco_reserved (39 regs) */
	0x08010047, 	/* mode !(k2|e5), block miscs */
	0x03002458, 	/* miscs.memctrl_wr_rd_n .. miscs.memctrl_address (3 regs) */
	0x02002464, 	/* miscs.INT_STS_1 .. miscs.INT_MASK_1 (2 regs) */
	0x010025c1, 	/* miscs.nig_dbg_vector (1 regs) */
	0x030025e8, 	/* miscs.pcie_rst_prepared_assert_cnt .. miscs.pcie_rst_deassert_cnt (3 regs) */
	0x010025ec, 	/* miscs.pcie_rst_n (1 regs) */
	0x030025ef, 	/* miscs.avs_otp_sram_ctrl .. miscs.avs_otp_ctrl_vmgmt (3 regs) */
	0x060025f4, 	/* miscs.avs_pvtmon_daccode .. miscs.opte_almfull_thr (6 regs) */
	0x040025fb, 	/* miscs.avs_clock_observe .. miscs.avs_tp_out (4 regs) */
	0x01020000, 	/* block misc */
	0x02002301, 	/* misc.xmac_phy_port_mode .. misc.xmac_core_port_mode (2 regs) */
	0x01040000, 	/* block pglue_b */
	0x030aa12f, 	/* pglue_b.memctrl_wr_rd_n .. pglue_b.memctrl_address (3 regs) */
	0x04050000, 	/* block cnig */
	0x02086080, 	/* cnig.nw_port_mode .. cnig.nw_serdes_swap (2 regs) */
	0x25086095, 	/* cnig.mac_led_swap .. cnig.cnig_dbg_ifmux_phy_lasi_b (37 regs) */
	0x010860be, 	/* cnig.cnig_dbg_nigtx_fifo_afull_thresh_large (1 regs) */
	0x0a0860c8, 	/* cnig.pmeg_sign_ext .. cnig.pmfc_crc_tx_corrupt_on_error (10 regs) */
	0x0c150000, 	/* block pglcs */
	0x02000904, 	/* pglcs.pgl_cs.pm_data_a .. pglcs.pgl_cs.pm_data_b (2 regs) */
	0x0200090e, 	/* pglcs.pgl_cs.reg_id_val2 .. pglcs.pgl_cs.reg_id_val3 (2 regs) */
	0x01000932, 	/* pglcs.pgl_cs.reg_msix_pba_off_bir (1 regs) */
	0x01000934, 	/* pglcs.pgl_cs.reg_pcie_capability (1 regs) */
	0x01000937, 	/* pglcs.pgl_cs.reg_link_capability (1 regs) */
	0x0300093a, 	/* pglcs.pgl_cs.reg_pcie_link_capability_2 .. pglcs.pgl_cs.reg_pcie_link_capability_rc (3 regs) */
	0x04000940, 	/* pglcs.pgl_cs.reg_dev_ser_num_cap_id .. pglcs.pgl_cs.reg_adv_err_cap (4 regs) */
	0x01000955, 	/* pglcs.pgl_cs.reg_vsec_hdr (1 regs) */
	0x0100097c, 	/* pglcs.pgl_cs.reg_PTM_CAP (1 regs) */
	0x01000983, 	/* pglcs.pgl_cs.reg_VF_SUPP_PAGE_SIZE (1 regs) */
	0x02000985, 	/* pglcs.pgl_cs.reg_VF_MSIX_TBL_BIR_OFF .. pglcs.pgl_cs.reg_VF_msix_pba_off_bit (2 regs) */
	0x0100098a, 	/* pglcs.pgl_cs.reg_vf_nsp (1 regs) */
	0x01160000, 	/* block dmae */
	0x03003140, 	/* dmae.memctrl_wr_rd_n .. dmae.memctrl_address (3 regs) */
	0x011a0000, 	/* block ucm */
	0x034a0010, 	/* ucm.memctrl_wr_rd_n .. ucm.memctrl_address (3 regs) */
	0x01200000, 	/* block dorq */
	0x030402b0, 	/* dorq.memctrl_wr_rd_n .. dorq.memctrl_address (3 regs) */
	0x01210000, 	/* block brb */
	0x040d0700, 	/* brb.memctrl_wr_rd_n .. brb.memctrl_status (4 regs) */
	0x01230000, 	/* block prs */
	0x0707c3e0, 	/* prs.cam_bist_en .. prs.cam_bist_dbg_compare_en (7 regs) */
	0x012a0000, 	/* block tsem */
	0x045d0330, 	/* tsem.fast_memory.memctrl_wr_rd_n .. tsem.fast_memory.memctrl_status (4 regs) */
	0x012b0000, 	/* block msem */
	0x04610330, 	/* msem.fast_memory.memctrl_wr_rd_n .. msem.fast_memory.memctrl_status (4 regs) */
	0x012c0000, 	/* block usem */
	0x04650330, 	/* usem.fast_memory.memctrl_wr_rd_n .. usem.fast_memory.memctrl_status (4 regs) */
	0x012d0000, 	/* block xsem */
	0x04510330, 	/* xsem.fast_memory.memctrl_wr_rd_n .. xsem.fast_memory.memctrl_status (4 regs) */
	0x012e0000, 	/* block ysem */
	0x04550330, 	/* ysem.fast_memory.memctrl_wr_rd_n .. ysem.fast_memory.memctrl_status (4 regs) */
	0x012f0000, 	/* block psem */
	0x04590330, 	/* psem.fast_memory.memctrl_wr_rd_n .. psem.fast_memory.memctrl_status (4 regs) */
	0x01300000, 	/* block rss */
	0x0408e322, 	/* rss.memctrl_wr_rd_n .. rss.memctrl_status (4 regs) */
	0x013c0000, 	/* block pbf */
	0x03360040, 	/* pbf.memctrl_wr_rd_n .. pbf.memctrl_address (3 regs) */
	0x013f0000, 	/* block cdu */
	0x041601d3, 	/* cdu.memctrl_wr_rd_n .. cdu.memctrl_status (4 regs) */
	0x01420000, 	/* block igu */
	0x07060018, 	/* igu.cam_bist_en .. igu.cam_bist_dbg_compare_en (7 regs) */
	0x024a0000, 	/* block dbg */
	0x0a0042b0, 	/* dbg.cpu_mbist_memctrl_0_cntrl_cmd .. dbg.cpu_mbist_memctrl_9_cntrl_cmd (10 regs) */
	0x030042e5, 	/* dbg.memctrl_wr_rd_n .. dbg.memctrl_address (3 regs) */
	0x024b0000, 	/* block nig */
	0x04142218, 	/* nig.user_one_step_type .. nig.ts_shift (4 regs) */
	0x0114221e, 	/* nig.user_one_step_32 (1 regs) */
	0x03010083, 	/* mode !e5, block miscs */
	0x020025c2, 	/* miscs.four_port_shared_mdio_en .. miscs.sel_dbg_ifmux_test (2 regs) */
	0x020025c9, 	/* miscs.mdio_override .. miscs.mdio_subscription (2 regs) */
	0x010025df, 	/* miscs.LINK_IN_L23 (1 regs) */
	0x01100000, 	/* block pswrd2 */
	0x010a751b, 	/* pswrd2.atc_global_enable (1 regs) */
	0x22140000, 	/* block pswrq2 */
	0x0f090106, 	/* pswrq2.usdm_entry_th .. pswrq2.UFIFO (15 regs) */
	0x01090155, 	/* pswrq2.ufifo_num_of_entry (1 regs) */
	0x01090163, 	/* pswrq2.bw_rd_add0 (1 regs) */
	0x02090167, 	/* pswrq2.bw_rd_add4 .. pswrq2.bw_rd_add5 (2 regs) */
	0x0209016f, 	/* pswrq2.bw_rd_add12 .. pswrq2.bw_rd_add13 (2 regs) */
	0x0d090172, 	/* pswrq2.bw_rd_add15 .. pswrq2.bw_rd_add27 (13 regs) */
	0x02090180, 	/* pswrq2.bw_wr_add29 .. pswrq2.bw_wr_add30 (2 regs) */
	0x01090183, 	/* pswrq2.bw_rd_ubound0 (1 regs) */
	0x02090187, 	/* pswrq2.bw_rd_ubound4 .. pswrq2.bw_rd_ubound5 (2 regs) */
	0x0209018f, 	/* pswrq2.bw_rd_ubound12 .. pswrq2.bw_rd_ubound13 (2 regs) */
	0x0d090192, 	/* pswrq2.bw_rd_ubound15 .. pswrq2.bw_rd_ubound27 (13 regs) */
	0x020901a0, 	/* pswrq2.bw_wr_ubound29 .. pswrq2.bw_wr_ubound30 (2 regs) */
	0x010901a3, 	/* pswrq2.bw_rd_l0 (1 regs) */
	0x020901a7, 	/* pswrq2.bw_rd_l4 .. pswrq2.bw_rd_l5 (2 regs) */
	0x020901af, 	/* pswrq2.bw_rd_l12 .. pswrq2.bw_rd_l13 (2 regs) */
	0x0d0901b2, 	/* pswrq2.bw_rd_l15 .. pswrq2.bw_rd_l27 (13 regs) */
	0x020901c0, 	/* pswrq2.bw_wr_l29 .. pswrq2.bw_wr_l30 (2 regs) */
	0x010901c7, 	/* pswrq2.slow_th (1 regs) */
	0x020901c9, 	/* pswrq2.dbg_head_mux_sel .. pswrq2.dbg_tail_mux_sel (2 regs) */
	0x020901cd, 	/* pswrq2.cxr_ram0_tm .. pswrq2.cxr_ram1_tm (2 regs) */
	0x2c0901d0, 	/* pswrq2.qc_reg1 .. pswrq2.atc_vq_enable (44 regs) */
	0x030901fd, 	/* pswrq2.atc_internal_ats_enable_all .. pswrq2.atc_global_enable (3 regs) */
	0x01090201, 	/* pswrq2.close_gate_vq_msb_en (1 regs) */
	0x01090203, 	/* pswrq2.stall_mem_vq_msb_en (1 regs) */
	0x04090205, 	/* pswrq2.stall_int_vq_msb_en .. pswrq2.atc_treq_fifo_tm (4 regs) */
	0x0709020a, 	/* pswrq2.hoq_ram_rd_req .. pswrq2.hoq_ram_data_rd_3 (7 regs) */
	0x02090215, 	/* pswrq2.sr_cnt_treq_cnt .. pswrq2.sr_cnt_icpl_cnt (2 regs) */
	0x05090232, 	/* pswrq2.msdm_entry_th .. pswrq2.ptu_entry_th (5 regs) */
	0x01090238, 	/* pswrq2.m2p_entry_th (1 regs) */
	0x0109023b, 	/* pswrq2.xyld_entry_th (1 regs) */
	0x0509023d, 	/* pswrq2.atc_msdm_flags .. pswrq2.atc_xyld_flags (5 regs) */
	0x010902f1, 	/* pswrq2.reqif_del_delay (1 regs) */
	0x010902f9, 	/* pswrq2.garb_vq_2_strict_msb (1 regs) */
	0x0309030d, 	/* pswrq2.add2q_2_delhoq0_delay .. pswrq2.delhoq0_2_delhoq0_delay_1 (3 regs) */
	0x01170000, 	/* block ptu */
	0x0e158165, 	/* ptu.atc_fli_done_vf_31_0 .. ptu.atc_fli_done_clr_pf_15_0 (14 regs) */
	0x06180000, 	/* block tcm */
	0x01460186, 	/* tcm.prs_weight (1 regs) */
	0x06460193, 	/* tcm.storm_frwrd_mode .. tcm.pbf_frwrd_mode (6 regs) */
	0x014602ad, 	/* tcm.prs_length_mis (1 regs) */
	0x014602b6, 	/* tcm.prs_msg_cntr (1 regs) */
	0x014602c4, 	/* tcm.is_prs_fill_lvl (1 regs) */
	0x014602e5, 	/* tcm.is_foc_prs_nxt_inf_unit (1 regs) */
	0x01190000, 	/* block mcm */
	0x08480193, 	/* mcm.storm_frwrd_mode .. mcm.pbf_frwrd_mode (8 regs) */
	0x061a0000, 	/* block ucm */
	0x014a018b, 	/* ucm.yuld_weight (1 regs) */
	0x0b4a0196, 	/* ucm.storm_frwrd_mode .. ucm.pbf_frwrd_mode (11 regs) */
	0x014a02b2, 	/* ucm.yuld_length_mis (1 regs) */
	0x014a02bc, 	/* ucm.yuld_msg_cntr (1 regs) */
	0x014a02cd, 	/* ucm.is_yuld_fill_lvl (1 regs) */
	0x014a02eb, 	/* ucm.is_foc_yuld_nxt_inf_unit (1 regs) */
	0x011b0000, 	/* block xcm */
	0x0a400196, 	/* xcm.storm_frwrd_mode .. xcm.pbf_frwrd_mode (10 regs) */
	0x011c0000, 	/* block ycm */
	0x07420192, 	/* ycm.storm_frwrd_mode .. ycm.pbf_frwrd_mode (7 regs) */
	0x061d0000, 	/* block pcm */
	0x01440182, 	/* pcm.pbf_weight (1 regs) */
	0x0344018c, 	/* pcm.storm_frwrd_mode .. pcm.pbf_frwrd_mode (3 regs) */
	0x014402ab, 	/* pcm.pbf_length_mis (1 regs) */
	0x014402b0, 	/* pcm.pbf_msg_cntr (1 regs) */
	0x014402b4, 	/* pcm.is_pbf_fill_lvl (1 regs) */
	0x014402e3, 	/* pcm.is_foc_pbf_nxt_inf_unit (1 regs) */
	0x02200000, 	/* block dorq */
	0x0104020e, 	/* dorq.edpm_exist_in_qm_en (1 regs) */
	0x02040241, 	/* dorq.l2_edpm_ext_hdr_size .. dorq.l2_edpm_ext_hdr_offs (2 regs) */
	0x02210000, 	/* block brb */
	0x010d044a, 	/* brb.rc_eop_inp_sync_fifo_push_status_1 (1 regs) */
	0x010d0459, 	/* brb.rc_eop_out_sync_fifo_push_status_1 (1 regs) */
	0x06230000, 	/* block prs */
	0x0107c2d4, 	/* prs.prs_pkt_ct (1 regs) */
	0x0307c2ec, 	/* prs.fc_dbg_select .. prs.fc_dbg_shift (3 regs) */
	0x0887c2f0, 	/* prs.fc_dbg_out_data (8 regs, WB) */
	0x0407c2f8, 	/* prs.fc_dbg_force_valid .. prs.fc_dbg_out_frame (4 regs) */
	0x0107c3c0, 	/* prs.tcm_initial_credit (1 regs) */
	0x0107c3c3, 	/* prs.tcm_current_credit (1 regs) */
	0x1f2a0000, 	/* block tsem */
	0x045c0001, 	/* tsem.enable_in .. tsem.pas_disable (4 regs) */
	0x015c0100, 	/* tsem.arb_cycle_size (1 regs) */
	0x035c0113, 	/* tsem.ext_store_free_entries .. tsem.gpre_samp_period (3 regs) */
	0x015c0180, 	/* tsem.fic_min_msg (1 regs) */
	0x025c0188, 	/* tsem.fic_empty_ct_mode .. tsem.fic_empty_ct_cnt (2 regs) */
	0x015c01b0, 	/* tsem.full_foc_dra_strt_en (1 regs) */
	0x10dc01c0, 	/* tsem.fin_command (16 regs, WB) */
	0x015c0240, 	/* tsem.invld_pas_wr_en (1 regs) */
	0x035c0260, 	/* tsem.arbiter_request .. tsem.arbiter_slot (3 regs) */
	0x025c02c1, 	/* tsem.thread_error .. tsem.thread_rdy (2 regs) */
	0x015c02c5, 	/* tsem.threads_list (1 regs) */
	0x015c0380, 	/* tsem.order_pop_en (1 regs) */
	0x015c0382, 	/* tsem.order_wake_en (1 regs) */
	0x015c0384, 	/* tsem.pf_num_order_base (1 regs) */
	0x015c0402, 	/* tsem.sync_dra_wr_alm_full (1 regs) */
	0x015c0440, 	/* tsem.dra_empty (1 regs) */
	0x045c0450, 	/* tsem.slow_dbg_empty .. tsem.slow_dra_wr_empty (4 regs) */
	0x015c0456, 	/* tsem.slow_ram_rd_empty (1 regs) */
	0x025c0459, 	/* tsem.thread_fifo_empty .. tsem.ord_id_fifo_empty (2 regs) */
	0x015c0490, 	/* tsem.pas_if_full (1 regs) */
	0x055c0492, 	/* tsem.slow_dbg_alm_full .. tsem.slow_dra_wr_full (5 regs) */
	0x025c049d, 	/* tsem.thread_fifo_full .. tsem.ord_id_fifo_full (2 regs) */
	0x035c04c0, 	/* tsem.thread_inter_cnt .. tsem.thread_orun_num (3 regs) */
	0x0a5c0500, 	/* tsem.slow_dbg_active .. tsem.dbg_msg_src (10 regs) */
	0x015d0001, 	/* tsem.fast_memory.ram_ext_disable (1 regs) */
	0x035d0122, 	/* tsem.fast_memory.stall_0 .. tsem.fast_memory.stall_2 (3 regs) */
	0x015d0132, 	/* tsem.fast_memory.pram_last_addr (1 regs) */
	0x015d0291, 	/* tsem.fast_memory.storm_active_cycles (1 regs) */
	0x035d0293, 	/* tsem.fast_memory.storm_stall_cycles .. tsem.fast_memory.idle_inactive_cycles (3 regs) */
	0x025d2814, 	/* tsem.fast_memory.vfc_config.rss_ram_tm_0 .. tsem.fast_memory.vfc_config.rss_ram_tm_1 (2 regs) */
	0x015d281b, 	/* tsem.fast_memory.vfc_config.key_rss_ext5 (1 regs) */
	0x1d2b0000, 	/* block msem */
	0x04600001, 	/* msem.enable_in .. msem.pas_disable (4 regs) */
	0x01600100, 	/* msem.arb_cycle_size (1 regs) */
	0x03600113, 	/* msem.ext_store_free_entries .. msem.gpre_samp_period (3 regs) */
	0x01600180, 	/* msem.fic_min_msg (1 regs) */
	0x02600188, 	/* msem.fic_empty_ct_mode .. msem.fic_empty_ct_cnt (2 regs) */
	0x016001b0, 	/* msem.full_foc_dra_strt_en (1 regs) */
	0x10e001c0, 	/* msem.fin_command (16 regs, WB) */
	0x01600240, 	/* msem.invld_pas_wr_en (1 regs) */
	0x03600260, 	/* msem.arbiter_request .. msem.arbiter_slot (3 regs) */
	0x026002c1, 	/* msem.thread_error .. msem.thread_rdy (2 regs) */
	0x016002c5, 	/* msem.threads_list (1 regs) */
	0x01600380, 	/* msem.order_pop_en (1 regs) */
	0x01600382, 	/* msem.order_wake_en (1 regs) */
	0x01600384, 	/* msem.pf_num_order_base (1 regs) */
	0x01600402, 	/* msem.sync_dra_wr_alm_full (1 regs) */
	0x01600440, 	/* msem.dra_empty (1 regs) */
	0x04600450, 	/* msem.slow_dbg_empty .. msem.slow_dra_wr_empty (4 regs) */
	0x01600456, 	/* msem.slow_ram_rd_empty (1 regs) */
	0x02600459, 	/* msem.thread_fifo_empty .. msem.ord_id_fifo_empty (2 regs) */
	0x01600490, 	/* msem.pas_if_full (1 regs) */
	0x05600492, 	/* msem.slow_dbg_alm_full .. msem.slow_dra_wr_full (5 regs) */
	0x0260049d, 	/* msem.thread_fifo_full .. msem.ord_id_fifo_full (2 regs) */
	0x036004c0, 	/* msem.thread_inter_cnt .. msem.thread_orun_num (3 regs) */
	0x0a600500, 	/* msem.slow_dbg_active .. msem.dbg_msg_src (10 regs) */
	0x01610001, 	/* msem.fast_memory.ram_ext_disable (1 regs) */
	0x03610122, 	/* msem.fast_memory.stall_0 .. msem.fast_memory.stall_2 (3 regs) */
	0x01610132, 	/* msem.fast_memory.pram_last_addr (1 regs) */
	0x01610291, 	/* msem.fast_memory.storm_active_cycles (1 regs) */
	0x03610293, 	/* msem.fast_memory.storm_stall_cycles .. msem.fast_memory.idle_inactive_cycles (3 regs) */
	0x1d2c0000, 	/* block usem */
	0x04640001, 	/* usem.enable_in .. usem.pas_disable (4 regs) */
	0x01640100, 	/* usem.arb_cycle_size (1 regs) */
	0x03640113, 	/* usem.ext_store_free_entries .. usem.gpre_samp_period (3 regs) */
	0x01640180, 	/* usem.fic_min_msg (1 regs) */
	0x02640188, 	/* usem.fic_empty_ct_mode .. usem.fic_empty_ct_cnt (2 regs) */
	0x016401b0, 	/* usem.full_foc_dra_strt_en (1 regs) */
	0x10e401c0, 	/* usem.fin_command (16 regs, WB) */
	0x01640240, 	/* usem.invld_pas_wr_en (1 regs) */
	0x03640260, 	/* usem.arbiter_request .. usem.arbiter_slot (3 regs) */
	0x026402c1, 	/* usem.thread_error .. usem.thread_rdy (2 regs) */
	0x016402c5, 	/* usem.threads_list (1 regs) */
	0x01640380, 	/* usem.order_pop_en (1 regs) */
	0x01640382, 	/* usem.order_wake_en (1 regs) */
	0x01640384, 	/* usem.pf_num_order_base (1 regs) */
	0x01640402, 	/* usem.sync_dra_wr_alm_full (1 regs) */
	0x01640440, 	/* usem.dra_empty (1 regs) */
	0x04640450, 	/* usem.slow_dbg_empty .. usem.slow_dra_wr_empty (4 regs) */
	0x01640456, 	/* usem.slow_ram_rd_empty (1 regs) */
	0x02640459, 	/* usem.thread_fifo_empty .. usem.ord_id_fifo_empty (2 regs) */
	0x01640490, 	/* usem.pas_if_full (1 regs) */
	0x05640492, 	/* usem.slow_dbg_alm_full .. usem.slow_dra_wr_full (5 regs) */
	0x0264049d, 	/* usem.thread_fifo_full .. usem.ord_id_fifo_full (2 regs) */
	0x036404c0, 	/* usem.thread_inter_cnt .. usem.thread_orun_num (3 regs) */
	0x0a640500, 	/* usem.slow_dbg_active .. usem.dbg_msg_src (10 regs) */
	0x01650001, 	/* usem.fast_memory.ram_ext_disable (1 regs) */
	0x03650122, 	/* usem.fast_memory.stall_0 .. usem.fast_memory.stall_2 (3 regs) */
	0x01650132, 	/* usem.fast_memory.pram_last_addr (1 regs) */
	0x01650291, 	/* usem.fast_memory.storm_active_cycles (1 regs) */
	0x03650293, 	/* usem.fast_memory.storm_stall_cycles .. usem.fast_memory.idle_inactive_cycles (3 regs) */
	0x1c2d0000, 	/* block xsem */
	0x04500001, 	/* xsem.enable_in .. xsem.pas_disable (4 regs) */
	0x01500100, 	/* xsem.arb_cycle_size (1 regs) */
	0x03500113, 	/* xsem.ext_store_free_entries .. xsem.gpre_samp_period (3 regs) */
	0x02500188, 	/* xsem.fic_empty_ct_mode .. xsem.fic_empty_ct_cnt (2 regs) */
	0x015001b0, 	/* xsem.full_foc_dra_strt_en (1 regs) */
	0x10d001c0, 	/* xsem.fin_command (16 regs, WB) */
	0x01500240, 	/* xsem.invld_pas_wr_en (1 regs) */
	0x03500260, 	/* xsem.arbiter_request .. xsem.arbiter_slot (3 regs) */
	0x025002c1, 	/* xsem.thread_error .. xsem.thread_rdy (2 regs) */
	0x015002c5, 	/* xsem.threads_list (1 regs) */
	0x01500380, 	/* xsem.order_pop_en (1 regs) */
	0x01500382, 	/* xsem.order_wake_en (1 regs) */
	0x01500384, 	/* xsem.pf_num_order_base (1 regs) */
	0x01500402, 	/* xsem.sync_dra_wr_alm_full (1 regs) */
	0x01500440, 	/* xsem.dra_empty (1 regs) */
	0x04500450, 	/* xsem.slow_dbg_empty .. xsem.slow_dra_wr_empty (4 regs) */
	0x01500456, 	/* xsem.slow_ram_rd_empty (1 regs) */
	0x02500459, 	/* xsem.thread_fifo_empty .. xsem.ord_id_fifo_empty (2 regs) */
	0x01500490, 	/* xsem.pas_if_full (1 regs) */
	0x05500492, 	/* xsem.slow_dbg_alm_full .. xsem.slow_dra_wr_full (5 regs) */
	0x0250049d, 	/* xsem.thread_fifo_full .. xsem.ord_id_fifo_full (2 regs) */
	0x035004c0, 	/* xsem.thread_inter_cnt .. xsem.thread_orun_num (3 regs) */
	0x0a500500, 	/* xsem.slow_dbg_active .. xsem.dbg_msg_src (10 regs) */
	0x01510001, 	/* xsem.fast_memory.ram_ext_disable (1 regs) */
	0x03510122, 	/* xsem.fast_memory.stall_0 .. xsem.fast_memory.stall_2 (3 regs) */
	0x01510132, 	/* xsem.fast_memory.pram_last_addr (1 regs) */
	0x01510291, 	/* xsem.fast_memory.storm_active_cycles (1 regs) */
	0x03510293, 	/* xsem.fast_memory.storm_stall_cycles .. xsem.fast_memory.idle_inactive_cycles (3 regs) */
	0x1c2e0000, 	/* block ysem */
	0x04540001, 	/* ysem.enable_in .. ysem.pas_disable (4 regs) */
	0x01540100, 	/* ysem.arb_cycle_size (1 regs) */
	0x03540113, 	/* ysem.ext_store_free_entries .. ysem.gpre_samp_period (3 regs) */
	0x02540188, 	/* ysem.fic_empty_ct_mode .. ysem.fic_empty_ct_cnt (2 regs) */
	0x015401b0, 	/* ysem.full_foc_dra_strt_en (1 regs) */
	0x10d401c0, 	/* ysem.fin_command (16 regs, WB) */
	0x01540240, 	/* ysem.invld_pas_wr_en (1 regs) */
	0x03540260, 	/* ysem.arbiter_request .. ysem.arbiter_slot (3 regs) */
	0x025402c1, 	/* ysem.thread_error .. ysem.thread_rdy (2 regs) */
	0x015402c5, 	/* ysem.threads_list (1 regs) */
	0x01540380, 	/* ysem.order_pop_en (1 regs) */
	0x01540382, 	/* ysem.order_wake_en (1 regs) */
	0x01540384, 	/* ysem.pf_num_order_base (1 regs) */
	0x01540402, 	/* ysem.sync_dra_wr_alm_full (1 regs) */
	0x01540440, 	/* ysem.dra_empty (1 regs) */
	0x04540450, 	/* ysem.slow_dbg_empty .. ysem.slow_dra_wr_empty (4 regs) */
	0x01540456, 	/* ysem.slow_ram_rd_empty (1 regs) */
	0x02540459, 	/* ysem.thread_fifo_empty .. ysem.ord_id_fifo_empty (2 regs) */
	0x01540490, 	/* ysem.pas_if_full (1 regs) */
	0x05540492, 	/* ysem.slow_dbg_alm_full .. ysem.slow_dra_wr_full (5 regs) */
	0x0254049d, 	/* ysem.thread_fifo_full .. ysem.ord_id_fifo_full (2 regs) */
	0x035404c0, 	/* ysem.thread_inter_cnt .. ysem.thread_orun_num (3 regs) */
	0x0a540500, 	/* ysem.slow_dbg_active .. ysem.dbg_msg_src (10 regs) */
	0x01550001, 	/* ysem.fast_memory.ram_ext_disable (1 regs) */
	0x03550122, 	/* ysem.fast_memory.stall_0 .. ysem.fast_memory.stall_2 (3 regs) */
	0x01550132, 	/* ysem.fast_memory.pram_last_addr (1 regs) */
	0x01550291, 	/* ysem.fast_memory.storm_active_cycles (1 regs) */
	0x03550293, 	/* ysem.fast_memory.storm_stall_cycles .. ysem.fast_memory.idle_inactive_cycles (3 regs) */
	0x1d2f0000, 	/* block psem */
	0x04580001, 	/* psem.enable_in .. psem.pas_disable (4 regs) */
	0x01580100, 	/* psem.arb_cycle_size (1 regs) */
	0x03580113, 	/* psem.ext_store_free_entries .. psem.gpre_samp_period (3 regs) */
	0x01580180, 	/* psem.fic_min_msg (1 regs) */
	0x02580188, 	/* psem.fic_empty_ct_mode .. psem.fic_empty_ct_cnt (2 regs) */
	0x015801b0, 	/* psem.full_foc_dra_strt_en (1 regs) */
	0x10d801c0, 	/* psem.fin_command (16 regs, WB) */
	0x01580240, 	/* psem.invld_pas_wr_en (1 regs) */
	0x03580260, 	/* psem.arbiter_request .. psem.arbiter_slot (3 regs) */
	0x025802c1, 	/* psem.thread_error .. psem.thread_rdy (2 regs) */
	0x015802c5, 	/* psem.threads_list (1 regs) */
	0x01580380, 	/* psem.order_pop_en (1 regs) */
	0x01580382, 	/* psem.order_wake_en (1 regs) */
	0x01580384, 	/* psem.pf_num_order_base (1 regs) */
	0x01580402, 	/* psem.sync_dra_wr_alm_full (1 regs) */
	0x01580440, 	/* psem.dra_empty (1 regs) */
	0x04580450, 	/* psem.slow_dbg_empty .. psem.slow_dra_wr_empty (4 regs) */
	0x01580456, 	/* psem.slow_ram_rd_empty (1 regs) */
	0x02580459, 	/* psem.thread_fifo_empty .. psem.ord_id_fifo_empty (2 regs) */
	0x01580490, 	/* psem.pas_if_full (1 regs) */
	0x05580492, 	/* psem.slow_dbg_alm_full .. psem.slow_dra_wr_full (5 regs) */
	0x0258049d, 	/* psem.thread_fifo_full .. psem.ord_id_fifo_full (2 regs) */
	0x035804c0, 	/* psem.thread_inter_cnt .. psem.thread_orun_num (3 regs) */
	0x0a580500, 	/* psem.slow_dbg_active .. psem.dbg_msg_src (10 regs) */
	0x01590001, 	/* psem.fast_memory.ram_ext_disable (1 regs) */
	0x03590122, 	/* psem.fast_memory.stall_0 .. psem.fast_memory.stall_2 (3 regs) */
	0x01590132, 	/* psem.fast_memory.pram_last_addr (1 regs) */
	0x01590291, 	/* psem.fast_memory.storm_active_cycles (1 regs) */
	0x03590293, 	/* psem.fast_memory.storm_stall_cycles .. psem.fast_memory.idle_inactive_cycles (3 regs) */
	0x01300000, 	/* block rss */
	0x0508e30d, 	/* rss.rbc_status .. rss.state_machines (5 regs) */
	0x05330000, 	/* block yuld */
	0x2a132000, 	/* yuld.scbd_strict_prio .. yuld.cm_hdr_127_96 (42 regs) */
	0x0513202b, 	/* yuld.stat_fic_msg .. yuld.len_err_log_2 (5 regs) */
	0x01132031, 	/* yuld.len_err_log_v (1 regs) */
	0x02132060, 	/* yuld.INT_STS .. yuld.INT_MASK (2 regs) */
	0x05132580, 	/* yuld.dbg_select .. yuld.dbg_force_frame (5 regs) */
	0x03340000, 	/* block xyld */
	0x0113000d, 	/* xyld.ld_seg_msg_q (1 regs) */
	0x04130032, 	/* xyld.seg_msg_log .. xyld.seg_msg_log_len_arr_95_64 (4 regs) */
	0x01130037, 	/* xyld.seg_msg_log_v (1 regs) */
	0x033c0000, 	/* block pbf */
	0x01360106, 	/* pbf.pcm_if_init_crd (1 regs) */
	0x02360124, 	/* pbf.tag_ethertype_4 .. pbf.tag_ethertype_5 (2 regs) */
	0x0236012a, 	/* pbf.tag_len_4 .. pbf.tag_len_5 (2 regs) */
	0x05430000, 	/* block cau */
	0x05070180, 	/* cau.cqe_size .. cau.agg_release_timer (5 regs) */
	0x01070221, 	/* cau.cqe_fifo_afull_thr (1 regs) */
	0x070702ea, 	/* cau.stat_counter_cqe_msg_sent .. cau.stat_counter_cqe_partial_cache (7 regs) */
	0x04070327, 	/* cau.agg_units_0to15_state .. cau.agg_units_48to63_state (4 regs) */
	0x020703c3, 	/* cau.igu_cqe_cmd_fsm_status .. cau.igu_cqe_agg_fsm_status (2 regs) */
	0x024a0000, 	/* block dbg */
	0x01004002, 	/* dbg.other_client_enable (1 regs) */
	0x01004004, 	/* dbg.other_engine_mode (1 regs) */
	0x014b0000, 	/* block nig */
	0xa0140212, 	/* nig.tx_lb_vport_drop_0 .. nig.tx_lb_vport_drop_159 (160 regs) */
	0x0e150027, 	/* mode !(bb|e5), block pglcs */
	0x07000852, 	/* pglcs.pgl_cs.VC_BASE .. pglcs.pgl_cs.RESOURCE_STATUS_REG_VC0 (7 regs) */
	0x07000866, 	/* pglcs.pgl_cs.SPCIE_CAP_HEADER_REG .. pglcs.pgl_cs.SPCIE_CAP_OFF_18H_REG (7 regs) */
	0x020008a1, 	/* pglcs.pgl_cs.LTR_CAP_HDR_REG .. pglcs.pgl_cs.LTR_LATENCY_REG (2 regs) */
	0x030008f1, 	/* pglcs.pgl_cs.PTM_EXT_CAP_HDR_OFF .. pglcs.pgl_cs.PTM_CONTROL_OFF (3 regs) */
	0x090009c0, 	/* pglcs.pgl_cs.ACK_LATENCY_TIMER_OFF .. pglcs.pgl_cs.FILTER_MASK_2_OFF (9 regs) */
	0x0b0009ca, 	/* pglcs.pgl_cs.PL_DEBUG0_OFF .. pglcs.pgl_cs.VC0_CPL_RX_Q_CTRL_OFF (11 regs) */
	0x03000a03, 	/* pglcs.pgl_cs.GEN2_CTRL_OFF .. pglcs.pgl_cs.PHY_CONTROL_OFF (3 regs) */
	0x01000a24, 	/* pglcs.pgl_cs.GEN3_RELATED_OFF (1 regs) */
	0x01000a28, 	/* pglcs.pgl_cs.PF_HIDE_CONTROL (1 regs) */
	0x02000a2a, 	/* pglcs.pgl_cs.GEN3_EQ_CONTROL_OFF .. pglcs.pgl_cs.GEN3_EQ_FB_MODE_DIR_CHANGE_OFF (2 regs) */
	0x06000a2d, 	/* pglcs.pgl_cs.ORDER_RULE_CTRL_OFF .. pglcs.pgl_cs.TRGT_CPL_LUT_DELETE_ENTRY_OFF (6 regs) */
	0x01000a3f, 	/* pglcs.pgl_cs.PL_LAST_OFF (1 regs) */
	0x01000acc, 	/* pglcs.pgl_cs.PL_LTR_LATENCY_OFF (1 regs) */
	0x01000ad0, 	/* pglcs.pgl_cs.AUX_CLK_FREQ_OFF (1 regs) */
	0x02210000, 	/* block brb */
	0x020d044b, 	/* brb.rc_eop_inp_sync_fifo_push_status_2 .. brb.rc_eop_inp_sync_fifo_push_status_3 (2 regs) */
	0x020d045a, 	/* brb.rc_eop_out_sync_fifo_push_status_2 .. brb.rc_eop_out_sync_fifo_push_status_3 (2 regs) */
	0x0e2a0000, 	/* block tsem */
	0x015d0087, 	/* tsem.fast_memory.reserved_21C (1 regs) */
	0x015d008a, 	/* tsem.fast_memory.reserved_228 (1 regs) */
	0x015d008d, 	/* tsem.fast_memory.reserved_234 (1 regs) */
	0x015d008f, 	/* tsem.fast_memory.reserved_23C (1 regs) */
	0x015d0091, 	/* tsem.fast_memory.reserved_244 (1 regs) */
	0x015d0094, 	/* tsem.fast_memory.reserved_250 (1 regs) */
	0x015d0096, 	/* tsem.fast_memory.reserved_258 (1 regs) */
	0x015d0098, 	/* tsem.fast_memory.reserved_260 (1 regs) */
	0x015d009a, 	/* tsem.fast_memory.reserved_268 (1 regs) */
	0x015d009c, 	/* tsem.fast_memory.reserved_270 (1 regs) */
	0x015d009e, 	/* tsem.fast_memory.reserved_278 (1 regs) */
	0x015d00a0, 	/* tsem.fast_memory.reserved_280 (1 regs) */
	0x0f5d00a2, 	/* tsem.fast_memory.reserved_288 .. tsem.fast_memory.reserved_2C0 (15 regs) */
	0x025d00b3, 	/* tsem.fast_memory.reserved_2CC .. tsem.fast_memory.reserved_2D0 (2 regs) */
	0x0e2b0000, 	/* block msem */
	0x01610087, 	/* msem.fast_memory.reserved_21C (1 regs) */
	0x0161008a, 	/* msem.fast_memory.reserved_228 (1 regs) */
	0x0161008d, 	/* msem.fast_memory.reserved_234 (1 regs) */
	0x0161008f, 	/* msem.fast_memory.reserved_23C (1 regs) */
	0x01610091, 	/* msem.fast_memory.reserved_244 (1 regs) */
	0x01610094, 	/* msem.fast_memory.reserved_250 (1 regs) */
	0x01610096, 	/* msem.fast_memory.reserved_258 (1 regs) */
	0x01610098, 	/* msem.fast_memory.reserved_260 (1 regs) */
	0x0161009a, 	/* msem.fast_memory.reserved_268 (1 regs) */
	0x0161009c, 	/* msem.fast_memory.reserved_270 (1 regs) */
	0x0161009e, 	/* msem.fast_memory.reserved_278 (1 regs) */
	0x016100a0, 	/* msem.fast_memory.reserved_280 (1 regs) */
	0x0f6100a2, 	/* msem.fast_memory.reserved_288 .. msem.fast_memory.reserved_2C0 (15 regs) */
	0x026100b3, 	/* msem.fast_memory.reserved_2CC .. msem.fast_memory.reserved_2D0 (2 regs) */
	0x0e2c0000, 	/* block usem */
	0x01650087, 	/* usem.fast_memory.reserved_21C (1 regs) */
	0x0165008a, 	/* usem.fast_memory.reserved_228 (1 regs) */
	0x0165008d, 	/* usem.fast_memory.reserved_234 (1 regs) */
	0x0165008f, 	/* usem.fast_memory.reserved_23C (1 regs) */
	0x01650091, 	/* usem.fast_memory.reserved_244 (1 regs) */
	0x01650094, 	/* usem.fast_memory.reserved_250 (1 regs) */
	0x01650096, 	/* usem.fast_memory.reserved_258 (1 regs) */
	0x01650098, 	/* usem.fast_memory.reserved_260 (1 regs) */
	0x0165009a, 	/* usem.fast_memory.reserved_268 (1 regs) */
	0x0165009c, 	/* usem.fast_memory.reserved_270 (1 regs) */
	0x0165009e, 	/* usem.fast_memory.reserved_278 (1 regs) */
	0x016500a0, 	/* usem.fast_memory.reserved_280 (1 regs) */
	0x0f6500a2, 	/* usem.fast_memory.reserved_288 .. usem.fast_memory.reserved_2C0 (15 regs) */
	0x026500b3, 	/* usem.fast_memory.reserved_2CC .. usem.fast_memory.reserved_2D0 (2 regs) */
	0x0e2d0000, 	/* block xsem */
	0x01510087, 	/* xsem.fast_memory.reserved_21C (1 regs) */
	0x0151008a, 	/* xsem.fast_memory.reserved_228 (1 regs) */
	0x0151008d, 	/* xsem.fast_memory.reserved_234 (1 regs) */
	0x0151008f, 	/* xsem.fast_memory.reserved_23C (1 regs) */
	0x01510091, 	/* xsem.fast_memory.reserved_244 (1 regs) */
	0x01510094, 	/* xsem.fast_memory.reserved_250 (1 regs) */
	0x01510096, 	/* xsem.fast_memory.reserved_258 (1 regs) */
	0x01510098, 	/* xsem.fast_memory.reserved_260 (1 regs) */
	0x0151009a, 	/* xsem.fast_memory.reserved_268 (1 regs) */
	0x0151009c, 	/* xsem.fast_memory.reserved_270 (1 regs) */
	0x0151009e, 	/* xsem.fast_memory.reserved_278 (1 regs) */
	0x015100a0, 	/* xsem.fast_memory.reserved_280 (1 regs) */
	0x0f5100a2, 	/* xsem.fast_memory.reserved_288 .. xsem.fast_memory.reserved_2C0 (15 regs) */
	0x025100b3, 	/* xsem.fast_memory.reserved_2CC .. xsem.fast_memory.reserved_2D0 (2 regs) */
	0x0e2e0000, 	/* block ysem */
	0x01550087, 	/* ysem.fast_memory.reserved_21C (1 regs) */
	0x0155008a, 	/* ysem.fast_memory.reserved_228 (1 regs) */
	0x0155008d, 	/* ysem.fast_memory.reserved_234 (1 regs) */
	0x0155008f, 	/* ysem.fast_memory.reserved_23C (1 regs) */
	0x01550091, 	/* ysem.fast_memory.reserved_244 (1 regs) */
	0x01550094, 	/* ysem.fast_memory.reserved_250 (1 regs) */
	0x01550096, 	/* ysem.fast_memory.reserved_258 (1 regs) */
	0x01550098, 	/* ysem.fast_memory.reserved_260 (1 regs) */
	0x0155009a, 	/* ysem.fast_memory.reserved_268 (1 regs) */
	0x0155009c, 	/* ysem.fast_memory.reserved_270 (1 regs) */
	0x0155009e, 	/* ysem.fast_memory.reserved_278 (1 regs) */
	0x015500a0, 	/* ysem.fast_memory.reserved_280 (1 regs) */
	0x0f5500a2, 	/* ysem.fast_memory.reserved_288 .. ysem.fast_memory.reserved_2C0 (15 regs) */
	0x025500b3, 	/* ysem.fast_memory.reserved_2CC .. ysem.fast_memory.reserved_2D0 (2 regs) */
	0x0e2f0000, 	/* block psem */
	0x01590087, 	/* psem.fast_memory.reserved_21C (1 regs) */
	0x0159008a, 	/* psem.fast_memory.reserved_228 (1 regs) */
	0x0159008d, 	/* psem.fast_memory.reserved_234 (1 regs) */
	0x0159008f, 	/* psem.fast_memory.reserved_23C (1 regs) */
	0x01590091, 	/* psem.fast_memory.reserved_244 (1 regs) */
	0x01590094, 	/* psem.fast_memory.reserved_250 (1 regs) */
	0x01590096, 	/* psem.fast_memory.reserved_258 (1 regs) */
	0x01590098, 	/* psem.fast_memory.reserved_260 (1 regs) */
	0x0159009a, 	/* psem.fast_memory.reserved_268 (1 regs) */
	0x0159009c, 	/* psem.fast_memory.reserved_270 (1 regs) */
	0x0159009e, 	/* psem.fast_memory.reserved_278 (1 regs) */
	0x015900a0, 	/* psem.fast_memory.reserved_280 (1 regs) */
	0x0f5900a2, 	/* psem.fast_memory.reserved_288 .. psem.fast_memory.reserved_2C0 (15 regs) */
	0x025900b3, 	/* psem.fast_memory.reserved_2CC .. psem.fast_memory.reserved_2D0 (2 regs) */
	0x014b0000, 	/* block nig */
	0x301402b2, 	/* nig.tx_lb_vport_drop_160 .. nig.tx_lb_vport_drop_207 (48 regs) */
	0x04500000, 	/* block nws */
	0x141c0000, 	/* nws.common_control .. nws.ln3_an_link_outputs (20 regs) */
	0x021c0064, 	/* nws.INT_STS_1 .. nws.INT_MASK_1 (2 regs) */
	0x021c0068, 	/* nws.INT_STS_2 .. nws.INT_MASK_2 (2 regs) */
	0x021c006c, 	/* nws.INT_STS_3 .. nws.INT_MASK_3 (2 regs) */
	0x02540000, 	/* block avs_wrap */
	0x041ad000, 	/* avs_wrap.avs_control .. avs_wrap.INT_MASK (4 regs) */
	0x091ad00a, 	/* avs_wrap.eco_reserved .. avs_wrap.efuse_data_word_23 (9 regs) */
	0x01030073, 	/* mode !((!asic)|(bb|k2)), block dbu */
	0x01002800, 	/* dbu.cmd (1 regs) */
	0x0203002f, 	/* mode !(!asic), block dbu */
	0x02002802, 	/* dbu.config .. dbu.timing (2 regs) */
	0x02002805, 	/* dbu.txdata .. dbu.vfid_cfg (2 regs) */
	0x030c0000, 	/* block mcp2 */
	0x01014880, 	/* mcp2.eco_reserved (1 regs) */
	0x03014900, 	/* mcp2.dbg_select .. mcp2.dbg_shift (3 regs) */
	0x02014910, 	/* mcp2.dbg_force_valid .. mcp2.dbg_force_frame (2 regs) */
	0x06480000, 	/* block umac */
	0x02014401, 	/* umac.ipg_hd_bkp_cntl .. umac.command_config (2 regs) */
	0x01014405, 	/* umac.frm_length (1 regs) */
	0x01014411, 	/* umac.mac_mode (1 regs) */
	0x01014417, 	/* umac.tx_ipg_length (1 regs) */
	0x010144c0, 	/* umac.mac_pfc_type (1 regs) */
	0x010144cc, 	/* umac.pause_control (1 regs) */
	0x0207001d, 	/* mode !(emul_reduced|fpga), block ncsi */
	0xa2010080, 	/* ncsi.config .. ncsi.dbg_force_frame (162 regs) */
	0x03010132, 	/* ncsi.eco_reserved .. ncsi.INT_MASK_0 (3 regs) */
	0x26090000, 	/* block bmb */
	0x03150001, 	/* bmb.hw_init_en .. bmb.start_en (3 regs) */
	0x02150030, 	/* bmb.INT_STS_0 .. bmb.INT_MASK_0 (2 regs) */
	0x02150036, 	/* bmb.INT_STS_1 .. bmb.INT_MASK_1 (2 regs) */
	0x0215003c, 	/* bmb.INT_STS_2 .. bmb.INT_MASK_2 (2 regs) */
	0x02150042, 	/* bmb.INT_STS_3 .. bmb.INT_MASK_3 (2 regs) */
	0x02150048, 	/* bmb.INT_STS_4 .. bmb.INT_MASK_4 (2 regs) */
	0x0215004e, 	/* bmb.INT_STS_5 .. bmb.INT_MASK_5 (2 regs) */
	0x02150054, 	/* bmb.INT_STS_6 .. bmb.INT_MASK_6 (2 regs) */
	0x0215005a, 	/* bmb.INT_STS_7 .. bmb.INT_MASK_7 (2 regs) */
	0x02150061, 	/* bmb.INT_STS_8 .. bmb.INT_MASK_8 (2 regs) */
	0x02150067, 	/* bmb.INT_STS_9 .. bmb.INT_MASK_9 (2 regs) */
	0x0215006d, 	/* bmb.INT_STS_10 .. bmb.INT_MASK_10 (2 regs) */
	0x02150073, 	/* bmb.INT_STS_11 .. bmb.INT_MASK_11 (2 regs) */
	0x02150200, 	/* bmb.big_ram_address .. bmb.header_size (2 regs) */
	0x0a150210, 	/* bmb.max_releases .. bmb.tc_guarantied_5 (10 regs) */
	0x0615021e, 	/* bmb.tc_guarantied_hyst_0 .. bmb.tc_guarantied_hyst_5 (6 regs) */
	0x06150228, 	/* bmb.tc_pause_xoff_threshold_0 .. bmb.tc_pause_xoff_threshold_5 (6 regs) */
	0x06150232, 	/* bmb.tc_pause_xon_threshold_0 .. bmb.tc_pause_xon_threshold_5 (6 regs) */
	0x0615023c, 	/* bmb.tc_full_xoff_threshold_0 .. bmb.tc_full_xoff_threshold_5 (6 regs) */
	0x06150246, 	/* bmb.tc_full_xon_threshold_0 .. bmb.tc_full_xon_threshold_5 (6 regs) */
	0x02150250, 	/* bmb.no_dead_cycles_en .. bmb.rc_pkt_priority (2 regs) */
	0x0d150260, 	/* bmb.rc_sop_priority .. bmb.pm_tc_latency_sensitive_0 (13 regs) */
	0x0615028c, 	/* bmb.pm_cos_threshold_0 .. bmb.pm_cos_threshold_5 (6 regs) */
	0x0815029c, 	/* bmb.dbgsyn_almost_full_thr .. bmb.dbg_force_frame (8 regs) */
	0x0c1502b2, 	/* bmb.inp_if_enable .. bmb.wc_empty_9 (12 regs) */
	0x0a1502c4, 	/* bmb.wc_full_0 .. bmb.wc_full_9 (10 regs) */
	0x051502d4, 	/* bmb.wc_bandwidth_if_full .. bmb.rc_pkt_empty_2 (5 regs) */
	0x031502e5, 	/* bmb.rc_pkt_full_0 .. bmb.rc_pkt_full_2 (3 regs) */
	0x031502f4, 	/* bmb.rc_pkt_status_0 .. bmb.rc_pkt_status_2 (3 regs) */
	0x36150303, 	/* bmb.rc_sop_empty .. bmb.rc_dscr_pend_fifo_cnt_7 (54 regs) */
	0x03150341, 	/* bmb.rc_gnt_pend_fifo_empty .. bmb.rc_gnt_pend_fifo_cnt (3 regs) */
	0x02150353, 	/* bmb.wc_sync_fifo_push_status_8 .. bmb.wc_sync_fifo_push_status_9 (2 regs) */
	0x02150362, 	/* bmb.pkt_avail_sync_fifo_push_status .. bmb.rc_pkt_state (2 regs) */
	0x01150367, 	/* bmb.mac_free_shared_hr_0 (1 regs) */
	0x0615036d, 	/* bmb.tc_occupancy_0 .. bmb.tc_occupancy_5 (6 regs) */
	0x0115037d, 	/* bmb.available_mac_size_0 (1 regs) */
	0x01150383, 	/* bmb.tc_pause_0 (1 regs) */
	0x01150389, 	/* bmb.tc_full_0 (1 regs) */
	0x062f0000, 	/* block psem */
	0x0e592800, 	/* psem.fast_memory.vfc_config.mask_lsb_0_low .. psem.fast_memory.vfc_config.indications2 (14 regs) */
	0x0559280f, 	/* psem.fast_memory.vfc_config.memories_rst .. psem.fast_memory.vfc_config.interrupt_mask (5 regs) */
	0x05592816, 	/* psem.fast_memory.vfc_config.inp_fifo_tm .. psem.fast_memory.vfc_config.vfc_cam_bist_status (5 regs) */
	0x0659281c, 	/* psem.fast_memory.vfc_config.inp_fifo_alm_full .. psem.fast_memory.vfc_config.cpu_mbist_memctrl_1_cntrl_cmd (6 regs) */
	0x12592824, 	/* psem.fast_memory.vfc_config.debug_data .. psem.fast_memory.vfc_config.mask_lsb_7_high (18 regs) */
	0x0c59283e, 	/* psem.fast_memory.vfc_config.offset_alu_vector_0 .. psem.fast_memory.vfc_config.cam_bist_skip_error_cnt (12 regs) */
	0x0109009d, 	/* mode (!bb)&(!(emul_reduced|fpga)), block bmb */
	0x02150252, 	/* bmb.wc_no_dead_cycles_en .. bmb.wc_highest_pri_en (2 regs) */
	0x06090081, 	/* mode (!e5)&(!(emul_reduced|fpga)), block bmb */
	0x071502d9, 	/* bmb.rc_pkt_empty_3 .. bmb.rc_pkt_empty_9 (7 regs) */
	0x071502e8, 	/* bmb.rc_pkt_full_3 .. bmb.rc_pkt_full_9 (7 regs) */
	0x071502f7, 	/* bmb.rc_pkt_status_3 .. bmb.rc_pkt_status_9 (7 regs) */
	0x08150339, 	/* bmb.rc_sop_inp_sync_fifo_pop_empty_8 .. bmb.rc_sop_out_sync_fifo_push_status_9 (8 regs) */
	0x02150344, 	/* bmb.rc_out_sync_fifo_push_status_8 .. bmb.rc_out_sync_fifo_push_status_9 (2 regs) */
	0x01150364, 	/* bmb.rc_pkt_state_1 (1 regs) */
	0x022f0000, 	/* block psem */
	0x02592814, 	/* psem.fast_memory.vfc_config.rss_ram_tm_0 .. psem.fast_memory.vfc_config.rss_ram_tm_1 (2 regs) */
	0x0159281b, 	/* psem.fast_memory.vfc_config.key_rss_ext5 (1 regs) */
	0x03090013, 	/* mode (!(bb|k2))&(!(emul_reduced|fpga)), block bmb */
	0x08150339, 	/* bmb.rc_sop_inp_sync_fifo_pop_empty_1 .. bmb.rc_sop_out_sync_fifo_push_status_2 (8 regs) */
	0x02150344, 	/* bmb.rc_out_sync_fifo_push_status_1 .. bmb.rc_out_sync_fifo_push_status_2 (2 regs) */
	0x03151000, 	/* bmb.wc_ll_high_pri .. bmb.ncsi_if_sel (3 regs) */
	0x010a00df, 	/* mode (!bb)&(!(!asic)), block pcie */
	0x010151e8, 	/* pcie.INT_STS (1 regs) */
	0x04480000, 	/* block umac */
	0x07014423, 	/* umac.tx_ipg_length1 .. umac.dbg_force_frame (7 regs) */
	0x02014460, 	/* umac.INT_STS .. umac.INT_MASK (2 regs) */
	0x070144c1, 	/* umac.pause_opcode .. umac.mac_pause_sa_1 (7 regs) */
	0x030144cd, 	/* umac.rsv_err_mask .. umac.probe_data (3 regs) */
	0x044e0147, 	/* mode bb&(!(!asic)), block ipc */
	0x03008123, 	/* ipc.mdio_comm .. ipc.mdio_mode (3 regs) */
	0x0200812a, 	/* ipc.freq_main .. ipc.freq_storm (2 regs) */
	0x0d00812d, 	/* ipc.free_running_cntr_0 .. ipc.hw_straps (13 regs) */
	0x03008142, 	/* ipc.jtag_compliance .. ipc.INT_MASK_0 (3 regs) */
	0x064e015b, 	/* mode k2&(!(!asic)), block ipc */
	0x01008084, 	/* ipc.pll_main_bypass (1 regs) */
	0x01008089, 	/* ipc.pll_main_reset (1 regs) */
	0x0200809a, 	/* ipc.pll_storm_reset .. ipc.pll_storm_bypass (2 regs) */
	0x050080a1, 	/* ipc.mdio_mode .. ipc.sgmii_rstb_mdioregs (5 regs) */
	0x120080a7, 	/* ipc.freq_main .. ipc.INT_MASK_0 (18 regs) */
	0x230080bb, 	/* ipc.jtag_compliance .. ipc.eco_reserved (35 regs) */
	0x034e0035, 	/* mode (!(bb|k2))&(!(!asic)), block ipc */
	0x3e008080, 	/* ipc.pll_main_bypass .. ipc.sgmii_rstb_mdioregs (62 regs) */
	0x120080bf, 	/* ipc.freq_main .. ipc.INT_MASK_0 (18 regs) */
	0x230080d3, 	/* ipc.jtag_compliance .. ipc.eco_reserved (35 regs) */
	0x08480045, 	/* mode (!(k2|e5))&(!(!asic)), block umac */
	0x02014403, 	/* umac.mac_0 .. umac.mac_1 (2 regs) */
	0x01014406, 	/* umac.pause_quant (1 regs) */
	0x01014410, 	/* umac.sfd_offset (1 regs) */
	0x04014412, 	/* umac.tag_0 .. umac.tx_preamble (4 regs) */
	0x05014418, 	/* umac.pfc_xoff_timer .. umac.umac_eee_ref_count (5 regs) */
	0x0501441e, 	/* umac.umac_rx_pkt_drop_status .. umac.umac_rev_id (5 regs) */
	0x070144c1, 	/* umac.mac_pfc_opcode .. umac.tx_ts_data (7 regs) */
	0x050144cd, 	/* umac.flush_control .. umac.mac_pfc_refresh_ctrl (5 regs) */
	0x054e0000, 	/* block ipc */
	0xa3008080, 	/* ipc.mdio_voltage_sel .. ipc.swreg_sync_clk_en (163 regs) */
	0x03008126, 	/* ipc.nw_serdes_mdio_comm .. ipc.nw_serdes_mdio_mode (3 regs) */
	0x0100812c, 	/* ipc.freq_nw (1 regs) */
	0x0800813a, 	/* ipc.otp_config_0 .. ipc.otp_config_7 (8 regs) */
	0x0500814b, 	/* ipc.lcpll_refclk_sel .. ipc.cpu_otp_rd_syndrome (5 regs) */
	0x044c013d, 	/* mode (!bb)&(!fpga), block wol */
	0x02180010, 	/* wol.INT_STS_0 .. wol.INT_MASK_0 (2 regs) */
	0x05180050, 	/* wol.dbg_select .. wol.dbg_force_frame (5 regs) */
	0x06182067, 	/* wol.tag_len_0 .. wol.tag_len_5 (6 regs) */
	0x0118206e, 	/* wol.eco_reserved (1 regs) */
	0x03510000, 	/* block ms */
	0x061a8000, 	/* ms.common_control .. ms.eco_reserved (6 regs) */
	0x011a8060, 	/* ms.INT_STS (1 regs) */
	0x0b1a808a, 	/* ms.dbg_select .. ms.dbg_fw_trigger_enable (11 regs) */
	0x02520000, 	/* block phy_pcie */
	0x0c18a000, 	/* phy_pcie.eco_reserved .. phy_pcie.dbg_status (12 regs) */
	0x0518a7fa, 	/* phy_pcie.dbg_select .. phy_pcie.dbg_force_frame (5 regs) */
	0x044e0025, 	/* mode (!(bb|e5))&(!(!asic)), block ipc */
	0x04008080, 	/* ipc.pll_main_divr .. ipc.pll_main_range (4 regs) */
	0x04008085, 	/* ipc.pll_main_lock .. ipc.pll_main_divack (4 regs) */
	0x1000808a, 	/* ipc.pll_main_lock_detect_filter_status_was_cleared .. ipc.pll_storm_range (16 regs) */
	0x0500809c, 	/* ipc.pll_storm_lock .. ipc.pll_storm_lock_detect_filter_status_was_cleared (5 regs) */
	0xff500000, 	/* block nws */
	0x031c8000, 	/* nws.nws_cmu.phy0_top_ReservedRegister0 .. nws.nws_cmu.phy0_top_ReservedRegister2 (3 regs) */
	0x031c8004, 	/* nws.nws_cmu.phy0_top_ReservedRegister3 .. nws.nws_cmu.phy0_top_ReservedRegister5 (3 regs) */
	0x011c8009, 	/* nws.nws_cmu.phy0_top_ReservedRegister6 (1 regs) */
	0x041c8030, 	/* nws.nws_cmu.phy0_top_ReservedRegister7 .. nws.nws_cmu.phy0_top_ReservedRegister10 (4 regs) */
	0x021c8038, 	/* nws.nws_cmu.phy0_top_afe_atest_ctrl0 .. nws.nws_cmu.phy0_top_afe_atest_ctrl1 (2 regs) */
	0x011c8040, 	/* nws.nws_cmu.phy0_top_ReservedRegister11 (1 regs) */
	0x011c8050, 	/* nws.nws_cmu.phy0_top_ReservedRegister12 (1 regs) */
	0x041c8054, 	/* nws.nws_cmu.phy0_top_ReservedRegister13 .. nws.nws_cmu.phy0_top_ReservedRegister16 (4 regs) */
	0x0b1c80f0, 	/* nws.nws_cmu.phy0_top_ReservedRegister17 .. nws.nws_cmu.phy0_top_clock_cm_lc0_clk_cmudiv_ctrl1 (11 regs) */
	0x0a1c8100, 	/* nws.nws_cmu.phy0_top_ReservedRegister26 .. nws.nws_cmu.phy0_top_clock_cm_r0_clk_pll3div_ctrl1 (10 regs) */
	0x061c8110, 	/* nws.nws_cmu.phy0_top_clock_ln0_clk_tx .. nws.nws_cmu.phy0_top_ReservedRegister35 (6 regs) */
	0x061c8118, 	/* nws.nws_cmu.phy0_top_clock_ln1_clk_tx .. nws.nws_cmu.phy0_top_ReservedRegister39 (6 regs) */
	0x061c8120, 	/* nws.nws_cmu.phy0_top_clock_ln2_clk_tx .. nws.nws_cmu.phy0_top_ReservedRegister43 (6 regs) */
	0x061c8128, 	/* nws.nws_cmu.phy0_top_clock_ln3_clk_tx .. nws.nws_cmu.phy0_top_ReservedRegister47 (6 regs) */
	0x021c8130, 	/* nws.nws_cmu.phy0_top_ReservedRegister48 .. nws.nws_cmu.phy0_top_ReservedRegister49 (2 regs) */
	0x011c8170, 	/* nws.nws_cmu.phy0_top_ReservedRegister50 (1 regs) */
	0x031c8180, 	/* nws.nws_cmu.phy0_top_err_ctrl0 .. nws.nws_cmu.phy0_top_err_ctrl2 (3 regs) */
	0x061c8187, 	/* nws.nws_cmu.phy0_top_regbus_err_info_ctrl .. nws.nws_cmu.phy0_top_regbus_err_info_status4 (6 regs) */
	0x051c81a0, 	/* nws.nws_cmu.phy0_top_tbus_addr_7_0 .. nws.nws_cmu.phy0_top_ReservedRegister53 (5 regs) */
	0x021c81b0, 	/* nws.nws_cmu.phy0_top_tbus_data_7_0 .. nws.nws_cmu.phy0_top_tbus_data_11_8 (2 regs) */
	0x021c81c0, 	/* nws.nws_cmu.phy0_top_sim_ctrl .. nws.nws_cmu.phy0_top_fw_ctrl (2 regs) */
	0x011c8200, 	/* nws.nws_cmu.phy0_mb_cmd (1 regs) */
	0x081c8203, 	/* nws.nws_cmu.phy0_mb_cmd_data0 .. nws.nws_cmu.phy0_mb_cmd_data7 (8 regs) */
	0x011c8210, 	/* nws.nws_cmu.phy0_mb_rsp (1 regs) */
	0x101c8213, 	/* nws.nws_cmu.phy0_mb_rsp_data0 .. nws.nws_cmu.phy0_mb_rsp_data15 (16 regs) */
	0x221c8300, 	/* nws.nws_cmu.phy0_ovr_cmu_lc_ReservedRegister54 .. nws.nws_cmu.phy0_ovr_cmu_lc_ReservedRegister87 (34 regs) */
	0x0a1c8380, 	/* nws.nws_cmu.phy0_ovr_cmu_r_ReservedRegister88 .. nws.nws_cmu.phy0_ovr_cmu_r_ReservedRegister97 (10 regs) */
	0x931c8400, 	/* nws.nws_cmu.phy0_ovr_ln0_ReservedRegister98 .. nws.nws_cmu.phy0_ovr_ln0_ReservedRegister244 (147 regs) */
	0x931c8500, 	/* nws.nws_cmu.phy0_ovr_ln1_ReservedRegister245 .. nws.nws_cmu.phy0_ovr_ln1_ReservedRegister391 (147 regs) */
	0x931c8600, 	/* nws.nws_cmu.phy0_ovr_ln2_ReservedRegister392 .. nws.nws_cmu.phy0_ovr_ln2_ReservedRegister538 (147 regs) */
	0x931c8700, 	/* nws.nws_cmu.phy0_ovr_ln3_ReservedRegister539 .. nws.nws_cmu.phy0_ovr_ln3_ReservedRegister685 (147 regs) */
	0x021c8800, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister686 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister687 (2 regs) */
	0x011c8803, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister688 (1 regs) */
	0x061c8805, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister689 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister694 (6 regs) */
	0x011c880c, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister695 (1 regs) */
	0x021c881a, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister696 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister697 (2 regs) */
	0x081c881f, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister698 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister704 (8 regs) */
	0x011c8828, 	/* nws.nws_cmu.cmu_lc0_top_afe_tstclk_ctrl0 (1 regs) */
	0x051c8830, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister705 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister709 (5 regs) */
	0x031c8850, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister710 .. nws.nws_cmu.cmu_lc0_top_phy_if_status (3 regs) */
	0x021c8858, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister712 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister713 (2 regs) */
	0x031c8880, 	/* nws.nws_cmu.cmu_lc0_top_err_ctrl1 .. nws.nws_cmu.cmu_lc0_top_err_ctrl3 (3 regs) */
	0x031c888a, 	/* nws.nws_cmu.cmu_lc0_top_ReservedRegister714 .. nws.nws_cmu.cmu_lc0_top_ReservedRegister716 (3 regs) */
	0x041c8900, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister717 .. nws.nws_cmu.cmu_lc0_pll_afe_reg_ctrl1 (4 regs) */
	0x021c8905, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister720 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister721 (2 regs) */
	0x031c8908, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister722 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister724 (3 regs) */
	0x021c8910, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister725 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister726 (2 regs) */
	0x041c8913, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister727 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister730 (4 regs) */
	0x051c8918, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister731 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister735 (5 regs) */
	0x1b1c8920, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister736 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister762 (27 regs) */
	0x011c893c, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister763 (1 regs) */
	0x031c8944, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister764 .. nws.nws_cmu.cmu_lc0_pll_lockdet_status (3 regs) */
	0x091c8949, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister766 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister774 (9 regs) */
	0x021c8954, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister775 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister776 (2 regs) */
	0x021c8958, 	/* nws.nws_cmu.cmu_lc0_pll_ReservedRegister777 .. nws.nws_cmu.cmu_lc0_pll_ReservedRegister778 (2 regs) */
	0x041c8a00, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister779 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister782 (4 regs) */
	0x041c8a10, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister783 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister786 (4 regs) */
	0x071c8a20, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister787 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister793 (7 regs) */
	0x031c8a30, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister794 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister796 (3 regs) */
	0x091c8a40, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister797 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister805 (9 regs) */
	0x021c8a50, 	/* nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister806 .. nws.nws_cmu.cmu_lc0_gcfsm2_ReservedRegister807 (2 regs) */
	0x061c8b00, 	/* nws.nws_cmu.cmu_lc0_feature_ReservedRegister808 .. nws.nws_cmu.cmu_lc0_feature_ReservedRegister813 (6 regs) */
	0x011c8b08, 	/* nws.nws_cmu.cmu_lc0_feature_ReservedRegister814 (1 regs) */
	0x081c8b10, 	/* nws.nws_cmu.cmu_lc0_feature_ReservedRegister815 .. nws.nws_cmu.cmu_lc0_feature_ReservedRegister822 (8 regs) */
	0x021c8c00, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister823 .. nws.nws_cmu.cmu_r0_top_ReservedRegister824 (2 regs) */
	0x011c8c03, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister825 (1 regs) */
	0x041c8c10, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister826 .. nws.nws_cmu.cmu_r0_top_ReservedRegister829 (4 regs) */
	0x041c8c20, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister830 .. nws.nws_cmu.cmu_r0_top_ReservedRegister833 (4 regs) */
	0x011c8c30, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister834 (1 regs) */
	0x031c8c50, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister835 .. nws.nws_cmu.cmu_r0_top_phy_if_status (3 regs) */
	0x021c8c58, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister837 .. nws.nws_cmu.cmu_r0_top_ReservedRegister838 (2 regs) */
	0x031c8c80, 	/* nws.nws_cmu.cmu_r0_top_err_ctrl1 .. nws.nws_cmu.cmu_r0_top_err_ctrl3 (3 regs) */
	0x031c8c8a, 	/* nws.nws_cmu.cmu_r0_top_ReservedRegister839 .. nws.nws_cmu.cmu_r0_top_ReservedRegister841 (3 regs) */
	0x051c8d00, 	/* nws.nws_cmu.cmu_r0_rpll_afe_pd_ctrl0 .. nws.nws_cmu.cmu_r0_rpll_afe_vco_ctrl0 (5 regs) */
	0x011c8d06, 	/* nws.nws_cmu.cmu_r0_rpll_afe_clkdiv_ctrl0 (1 regs) */
	0x061c8d08, 	/* nws.nws_cmu.cmu_r0_rpll_ReservedRegister843 .. nws.nws_cmu.cmu_r0_rpll_afe_int_ctrl0 (6 regs) */
	0x041c8d0f, 	/* nws.nws_cmu.cmu_r0_rpll_afe_int_ctrl1 .. nws.nws_cmu.cmu_r0_rpll_afe_fracn_ctrl0 (4 regs) */
	0x021c8d16, 	/* nws.nws_cmu.cmu_r0_rpll_afe_misc_ctrl0 .. nws.nws_cmu.cmu_r0_rpll_afe_misc_ctrl1 (2 regs) */
	0x131c8d20, 	/* nws.nws_cmu.cmu_r0_rpll_ReservedRegister844 .. nws.nws_cmu.cmu_r0_rpll_ReservedRegister862 (19 regs) */
	0x071c8d34, 	/* nws.nws_cmu.cmu_r0_rpll_ReservedRegister863 .. nws.nws_cmu.cmu_r0_rpll_ReservedRegister869 (7 regs) */
	0x031c8d44, 	/* nws.nws_cmu.cmu_r0_rpll_ReservedRegister870 .. nws.nws_cmu.cmu_r0_rpll_lockdet_status (3 regs) */
	0x091c8d49, 	/* nws.nws_cmu.cmu_r0_rpll_ssc_gen_ctrl0 .. nws.nws_cmu.cmu_r0_rpll_fracn_ctrl2 (9 regs) */
	0x021c8d54, 	/* nws.nws_cmu.cmu_r0_rpll_fracn_ctrl3 .. nws.nws_cmu.cmu_r0_rpll_fracn_ctrl4 (2 regs) */
	0x021c8d58, 	/* nws.nws_cmu.cmu_r0_rpll_ReservedRegister872 .. nws.nws_cmu.cmu_r0_rpll_ReservedRegister873 (2 regs) */
	0x041c8e00, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister874 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister877 (4 regs) */
	0x041c8e10, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister878 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister881 (4 regs) */
	0x071c8e20, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister882 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister888 (7 regs) */
	0x031c8e30, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister889 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister891 (3 regs) */
	0x091c8e40, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister892 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister900 (9 regs) */
	0x021c8e50, 	/* nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister901 .. nws.nws_cmu.cmu_r0_gcfsm2_ReservedRegister902 (2 regs) */
	0x011c8f00, 	/* nws.nws_cmu.cmu_r0_feature_ReservedRegister903 (1 regs) */
	0x021c8f04, 	/* nws.nws_cmu.cmu_r0_feature_ReservedRegister904 .. nws.nws_cmu.cmu_r0_feature_ReservedRegister905 (2 regs) */
	0x011c8f08, 	/* nws.nws_cmu.cmu_r0_feature_ReservedRegister906 (1 regs) */
	0x081c8f10, 	/* nws.nws_cmu.cmu_r0_feature_ReservedRegister907 .. nws.nws_cmu.cmu_r0_feature_ReservedRegister914 (8 regs) */
	0x071c9800, 	/* nws.nws_cmu.ln0_top_afe_loopback_ctrl .. nws.nws_cmu.ln0_top_ReservedRegister920 (7 regs) */
	0x011c9810, 	/* nws.nws_cmu.ln0_top_ReservedRegister921 (1 regs) */
	0x031c9812, 	/* nws.nws_cmu.ln0_top_ReservedRegister922 .. nws.nws_cmu.ln0_top_ReservedRegister924 (3 regs) */
	0x011c9816, 	/* nws.nws_cmu.ln0_top_ReservedRegister925 (1 regs) */
	0x011c9819, 	/* nws.nws_cmu.ln0_top_ReservedRegister926 (1 regs) */
	0x021c981b, 	/* nws.nws_cmu.ln0_top_ReservedRegister927 .. nws.nws_cmu.ln0_top_ReservedRegister928 (2 regs) */
	0x011c981e, 	/* nws.nws_cmu.ln0_top_ReservedRegister929 (1 regs) */
	0x011c9822, 	/* nws.nws_cmu.ln0_top_dpl_txdp_ctrl1 (1 regs) */
	0x041c9824, 	/* nws.nws_cmu.ln0_top_dpl_rxdp_ctrl1 .. nws.nws_cmu.ln0_top_phy_if_status (4 regs) */
	0x021c9830, 	/* nws.nws_cmu.ln0_top_ReservedRegister932 .. nws.nws_cmu.ln0_top_ReservedRegister933 (2 regs) */
	0x091c9838, 	/* nws.nws_cmu.ln0_top_ln_stat_ctrl0 .. nws.nws_cmu.ln0_top_ReservedRegister940 (9 regs) */
	0x021c9842, 	/* nws.nws_cmu.ln0_top_ReservedRegister941 .. nws.nws_cmu.ln0_top_ReservedRegister942 (2 regs) */
	0x051c9848, 	/* nws.nws_cmu.ln0_top_ReservedRegister943 .. nws.nws_cmu.ln0_top_ReservedRegister947 (5 regs) */
	0x031c9850, 	/* nws.nws_cmu.ln0_top_err_ctrl1 .. nws.nws_cmu.ln0_top_err_ctrl3 (3 regs) */
	0x021c9890, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister948 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister949 (2 regs) */
	0x021c98a1, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister950 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister951 (2 regs) */
	0x061c98a6, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister952 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister957 (6 regs) */
	0x011c98ad, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister958 (1 regs) */
	0x031c98b0, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister959 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister961 (3 regs) */
	0x011c98b4, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister962 (1 regs) */
	0x021c98b6, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister963 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister964 (2 regs) */
	0x091c98b9, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister965 .. nws.nws_cmu.ln0_cdr_rxclk_dlpf_status4 (9 regs) */
	0x031c98c4, 	/* nws.nws_cmu.ln0_cdr_rxclk_dlpf_status5 .. nws.nws_cmu.ln0_cdr_rxclk_integral_status1 (3 regs) */
	0x081c98c8, 	/* nws.nws_cmu.ln0_cdr_rxclk_integral_status2 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister977 (8 regs) */
	0x041c98e0, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister978 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister981 (4 regs) */
	0x091c98e8, 	/* nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister982 .. nws.nws_cmu.ln0_cdr_rxclk_ReservedRegister990 (9 regs) */
	0x021c9900, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister991 .. nws.nws_cmu.ln0_cdr_refclk_ReservedRegister992 (2 regs) */
	0x011c9904, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister993 (1 regs) */
	0x011c9906, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister994 (1 regs) */
	0x031c990a, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister995 .. nws.nws_cmu.ln0_cdr_refclk_ReservedRegister997 (3 regs) */
	0x021c9910, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister998 .. nws.nws_cmu.ln0_cdr_refclk_ReservedRegister999 (2 regs) */
	0x041c9918, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister1000 .. nws.nws_cmu.ln0_cdr_refclk_ReservedRegister1003 (4 regs) */
	0x061c9920, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister1004 .. nws.nws_cmu.ln0_cdr_refclk_ReservedRegister1009 (6 regs) */
	0x011c9930, 	/* nws.nws_cmu.ln0_cdr_refclk_ReservedRegister1010 (1 regs) */
	0x0f1c9980, 	/* nws.nws_cmu.ln0_aneg_ReservedRegister1011 .. nws.nws_cmu.ln0_aneg_ReservedRegister1022 (15 regs) */
	0x011c9990, 	/* nws.nws_cmu.ln0_aneg_status0 (1 regs) */
	0x021c9994, 	/* nws.nws_cmu.ln0_aneg_status_dbg0 .. nws.nws_cmu.ln0_aneg_status_dbg1 (2 regs) */
	0x251c9998, 	/* nws.nws_cmu.ln0_aneg_base_page0 .. nws.nws_cmu.ln0_aneg_resolution_eee (37 regs) */
	0x021c99be, 	/* nws.nws_cmu.ln0_aneg_link_status0 .. nws.nws_cmu.ln0_aneg_link_status1 (2 regs) */
	0x031c99c1, 	/* nws.nws_cmu.ln0_eee_ReservedRegister1039 .. nws.nws_cmu.ln0_eee_ReservedRegister1041 (3 regs) */
	0x041c99c5, 	/* nws.nws_cmu.ln0_eee_ReservedRegister1042 .. nws.nws_cmu.ln0_eee_ReservedRegister1045 (4 regs) */
	0x011c9a00, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1046 (1 regs) */
	0x021c9a02, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1047 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1048 (2 regs) */
	0x011c9a05, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1049 (1 regs) */
	0x011c9a07, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1050 (1 regs) */
	0x041c9a09, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1051 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1054 (4 regs) */
	0x041c9a0e, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1055 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1058 (4 regs) */
	0x0d1c9a20, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1059 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1071 (13 regs) */
	0x011c9a2e, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1072 (1 regs) */
	0x0b1c9a30, 	/* nws.nws_cmu.ln0_leq_refclk_agclos_ctrl0 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1082 (11 regs) */
	0x021c9a3d, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1083 .. nws.nws_cmu.ln0_leq_refclk_ple_att_ctrl1 (2 regs) */
	0x071c9a40, 	/* nws.nws_cmu.ln0_leq_refclk_eq_hfg_sql_ctrl0 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1089 (7 regs) */
	0x191c9a50, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1090 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1114 (25 regs) */
	0x071c9a70, 	/* nws.nws_cmu.ln0_leq_refclk_gn_apg_ctrl0 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1120 (7 regs) */
	0x091c9a80, 	/* nws.nws_cmu.ln0_leq_refclk_eq_lfg_ctrl0 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1126 (9 regs) */
	0x071c9a90, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1127 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1133 (7 regs) */
	0x061c9a98, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1134 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1138 (6 regs) */
	0x041c9aa0, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1139 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1142 (4 regs) */
	0x041c9aa6, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1143 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1146 (4 regs) */
	0x021c9aab, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1147 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1148 (2 regs) */
	0x021c9aae, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1149 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1150 (2 regs) */
	0x021c9ab8, 	/* nws.nws_cmu.ln0_leq_refclk_ReservedRegister1151 .. nws.nws_cmu.ln0_leq_refclk_ReservedRegister1152 (2 regs) */
	0x061c9b00, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1153 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1158 (6 regs) */
	0x021c9b08, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1159 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1160 (2 regs) */
	0x021c9b0c, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1161 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1162 (2 regs) */
	0x021c9b10, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1163 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1164 (2 regs) */
	0x021c9b13, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1165 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1166 (2 regs) */
	0x021c9b16, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1167 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1168 (2 regs) */
	0x021c9b20, 	/* nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1169 .. nws.nws_cmu.ln0_leq_rxclk_ReservedRegister1170 (2 regs) */
	0x071c9b80, 	/* nws.nws_cmu.ln0_drv_refclk_afe_pd_ctrl0 .. nws.nws_cmu.ln0_drv_refclk_ReservedRegister1175 (7 regs) */
	0x061c9b88, 	/* nws.nws_cmu.ln0_drv_refclk_ReservedRegister1176 .. nws.nws_cmu.ln0_drv_refclk_ReservedRegister1181 (6 regs) */
	0x0a1c9b90, 	/* nws.nws_cmu.ln0_drv_refclk_txeq_ctrl0 .. nws.nws_cmu.ln0_drv_refclk_ReservedRegister1186 (10 regs) */
	0x011c9b9b, 	/* nws.nws_cmu.ln0_drv_refclk_ReservedRegister1187 (1 regs) */
	0x051c9c00, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1188 .. nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1192 (5 regs) */
	0x011c9c06, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1193 (1 regs) */
	0x011c9c0a, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1194 (1 regs) */
	0x011c9c0c, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1195 (1 regs) */
	0x011c9c0e, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1196 (1 regs) */
	0x011c9c10, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1197 (1 regs) */
	0x011c9c12, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1198 (1 regs) */
	0x011c9c14, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1199 (1 regs) */
	0x011c9c16, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1200 (1 regs) */
	0x021c9c18, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1201 .. nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1202 (2 regs) */
	0x011c9c1b, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1203 (1 regs) */
	0x091c9c20, 	/* nws.nws_cmu.ln0_dfe_refclk_fsm_ctrl0 .. nws.nws_cmu.ln0_dfe_refclk_fsm_status0 (9 regs) */
	0x191c9c2a, 	/* nws.nws_cmu.ln0_dfe_refclk_tap_ctrl0 .. nws.nws_cmu.ln0_dfe_refclk_tap_val_status7 (25 regs) */
	0x161c9c50, 	/* nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1211 .. nws.nws_cmu.ln0_dfe_refclk_ReservedRegister1232 (22 regs) */
	0x031c9c80, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1233 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1235 (3 regs) */
	0x081c9c86, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1236 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1243 (8 regs) */
	0x031c9c90, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1244 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1246 (3 regs) */
	0x091c9c96, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1247 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1255 (9 regs) */
	0x161c9ca4, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1256 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1277 (22 regs) */
	0x121c9cc0, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1278 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1295 (18 regs) */
	0x031c9cd6, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1296 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1298 (3 regs) */
	0x0c1c9ce0, 	/* nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1299 .. nws.nws_cmu.ln0_dfe_rxclk_ReservedRegister1310 (12 regs) */
	0x061c9d00, 	/* nws.nws_cmu.ln0_los_refclk_afe_cal_ctrl .. nws.nws_cmu.ln0_los_refclk_run_length_status0 (6 regs) */
	0x071c9d10, 	/* nws.nws_cmu.ln0_los_refclk_filter_ctrl0 .. nws.nws_cmu.ln0_los_refclk_filter_ctrl6 (7 regs) */
	0x051c9d20, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1313 .. nws.nws_cmu.ln0_los_refclk_ReservedRegister1317 (5 regs) */
	0x041c9d30, 	/* nws.nws_cmu.ln0_los_refclk_override_ctrl0 .. nws.nws_cmu.ln0_los_refclk_ReservedRegister1320 (4 regs) */
	0x041c9d40, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1321 .. nws.nws_cmu.ln0_los_refclk_ReservedRegister1324 (4 regs) */
	0x011c9d46, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1325 (1 regs) */
	0x011c9d51, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1326 (1 regs) */
	0x011c9d59, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1327 (1 regs) */
	0x011c9d60, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1328 (1 regs) */
	0x021c9d70, 	/* nws.nws_cmu.ln0_los_refclk_ReservedRegister1329 .. nws.nws_cmu.ln0_los_refclk_status0 (2 regs) */
	0x041c9d80, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1330 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1333 (4 regs) */
	0x041c9d90, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1334 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1337 (4 regs) */
	0x071c9da0, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1338 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1344 (7 regs) */
	0x031c9db0, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1345 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1347 (3 regs) */
	0x091c9dc0, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1348 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1356 (9 regs) */
	0x021c9dd0, 	/* nws.nws_cmu.ln0_gcfsm2_ReservedRegister1357 .. nws.nws_cmu.ln0_gcfsm2_ReservedRegister1358 (2 regs) */
	0x051c9e00, 	/* nws.nws_cmu.ln0_bist_tx_ctrl .. nws.nws_cmu.ln0_bist_tx_ReservedRegister1362 (5 regs) */
	0x081c9e06, 	/* nws.nws_cmu.ln0_bist_tx_ber_ctrl0 .. nws.nws_cmu.ln0_bist_tx_ber_ctrl7 (8 regs) */
	0x011c9e20, 	/* nws.nws_cmu.ln0_bist_tx_udp_shift_amount (1 regs) */
	0x191c9e24, 	/* nws.nws_cmu.ln0_bist_tx_udp_7_0 .. nws.nws_cmu.ln0_bist_tx_udp_199_192 (25 regs) */
	0x011c9e80, 	/* nws.nws_cmu.ln0_bist_rx_ctrl (1 regs) */
	0x011c9e84, 	/* nws.nws_cmu.ln0_bist_rx_status (1 regs) */
	0x031c9e88, 	/* nws.nws_cmu.ln0_bist_rx_ber_status0 .. nws.nws_cmu.ln0_bist_rx_ber_status2 (3 regs) */
	0x031c9e8c, 	/* nws.nws_cmu.ln0_bist_rx_ber_status4 .. nws.nws_cmu.ln0_bist_rx_ber_status6 (3 regs) */
	0x041c9e94, 	/* nws.nws_cmu.ln0_bist_rx_lock_ctrl0 .. nws.nws_cmu.ln0_bist_rx_lock_ctrl3 (4 regs) */
	0x051c9ea0, 	/* nws.nws_cmu.ln0_bist_rx_loss_lock_ctrl0 .. nws.nws_cmu.ln0_bist_rx_loss_lock_ctrl4 (5 regs) */
	0x011c9eb0, 	/* nws.nws_cmu.ln0_bist_rx_udp_shift_amount (1 regs) */
	0x191c9eb4, 	/* nws.nws_cmu.ln0_bist_rx_udp_7_0 .. nws.nws_cmu.ln0_bist_rx_udp_199_192 (25 regs) */
	0x021c9f00, 	/* nws.nws_cmu.ln0_feature_rxterm_cfg0 .. nws.nws_cmu.ln0_feature_rxclkdiv_cfg0 (2 regs) */
	0x061c9f04, 	/* nws.nws_cmu.ln0_feature_ReservedRegister1363 .. nws.nws_cmu.ln0_feature_ReservedRegister1368 (6 regs) */
	0x071c9f10, 	/* nws.nws_cmu.ln0_feature_ReservedRegister1369 .. nws.nws_cmu.ln0_feature_ReservedRegister1375 (7 regs) */
	0x0a1c9f20, 	/* nws.nws_cmu.ln0_feature_ReservedRegister1376 .. nws.nws_cmu.ln0_feature_ReservedRegister1378 (10 regs) */
	0x071c9f30, 	/* nws.nws_cmu.ln0_feature_dfe_cfg .. nws.nws_cmu.ln0_feature_dfe_adapt_tap5_cfg (7 regs) */
	0x101c9f38, 	/* nws.nws_cmu.ln0_feature_adapt_cont_cfg0 .. nws.nws_cmu.ln0_feature_ReservedRegister1390 (16 regs) */
	0x011c9f50, 	/* nws.nws_cmu.ln0_feature_test_cfg0 (1 regs) */
	0x081c9f58, 	/* nws.nws_cmu.ln0_feature_ReservedRegister1391 .. nws.nws_cmu.ln0_feature_ReservedRegister1398 (8 regs) */
	0x061c9f80, 	/* nws.nws_cmu.ln0_lt_tx_fsm_ctrl0 .. nws.nws_cmu.ln0_lt_tx_fsm_ctrl5 (6 regs) */
	0x011c9f90, 	/* nws.nws_cmu.ln0_lt_tx_fsm_status (1 regs) */
	0x031c9f93, 	/* nws.nws_cmu.ln0_lt_tx_prbs_ctrl0 .. nws.nws_cmu.ln0_lt_tx_prbs_ctrl2 (3 regs) */
	0x011c9fa0, 	/* nws.nws_cmu.ln0_lt_tx_coefficient_update_ctrl (1 regs) */
	0x011c9fa2, 	/* nws.nws_cmu.ln0_lt_tx_status_report_ctrl (1 regs) */
	0x021c9fb0, 	/* nws.nws_cmu.ln0_lt_tx_fsm_state_status0 .. nws.nws_cmu.ln0_lt_tx_fsm_state_status1 (2 regs) */
	0x011c9fc0, 	/* nws.nws_cmu.ln0_lt_rx_ctrl0 (1 regs) */
	0x021c9fc2, 	/* nws.nws_cmu.ln0_lt_rx_prbs_ctrl0 .. nws.nws_cmu.ln0_lt_rx_prbs_ctrl1 (2 regs) */
	0x031c9fc5, 	/* nws.nws_cmu.ln0_lt_rx_prbs_status0 .. nws.nws_cmu.ln0_lt_rx_prbs_status2 (3 regs) */
	0x011c9fd0, 	/* nws.nws_cmu.ln0_lt_rx_frame_ctrl (1 regs) */
	0x051c9fd3, 	/* nws.nws_cmu.ln0_lt_rx_frame_status0 .. nws.nws_cmu.ln0_lt_rx_frame_status4 (5 regs) */
	0x011c9fe0, 	/* nws.nws_cmu.ln0_lt_rx_coefficient_update_status (1 regs) */
	0x011c9fe2, 	/* nws.nws_cmu.ln0_lt_rx_report_status (1 regs) */
	0x071ca000, 	/* nws.nws_cmu.ln1_top_afe_loopback_ctrl .. nws.nws_cmu.ln1_top_ReservedRegister1404 (7 regs) */
	0x011ca010, 	/* nws.nws_cmu.ln1_top_ReservedRegister1405 (1 regs) */
	0x031ca012, 	/* nws.nws_cmu.ln1_top_ReservedRegister1406 .. nws.nws_cmu.ln1_top_ReservedRegister1408 (3 regs) */
	0x011ca016, 	/* nws.nws_cmu.ln1_top_ReservedRegister1409 (1 regs) */
	0x011ca019, 	/* nws.nws_cmu.ln1_top_ReservedRegister1410 (1 regs) */
	0x021ca01b, 	/* nws.nws_cmu.ln1_top_ReservedRegister1411 .. nws.nws_cmu.ln1_top_ReservedRegister1412 (2 regs) */
	0x011ca01e, 	/* nws.nws_cmu.ln1_top_ReservedRegister1413 (1 regs) */
	0x011ca022, 	/* nws.nws_cmu.ln1_top_dpl_txdp_ctrl1 (1 regs) */
	0x041ca024, 	/* nws.nws_cmu.ln1_top_dpl_rxdp_ctrl1 .. nws.nws_cmu.ln1_top_phy_if_status (4 regs) */
	0x021ca030, 	/* nws.nws_cmu.ln1_top_ReservedRegister1416 .. nws.nws_cmu.ln1_top_ReservedRegister1417 (2 regs) */
	0x091ca038, 	/* nws.nws_cmu.ln1_top_ln_stat_ctrl0 .. nws.nws_cmu.ln1_top_ReservedRegister1424 (9 regs) */
	0x021ca042, 	/* nws.nws_cmu.ln1_top_ReservedRegister1425 .. nws.nws_cmu.ln1_top_ReservedRegister1426 (2 regs) */
	0x051ca048, 	/* nws.nws_cmu.ln1_top_ReservedRegister1427 .. nws.nws_cmu.ln1_top_ReservedRegister1431 (5 regs) */
	0x031ca050, 	/* nws.nws_cmu.ln1_top_err_ctrl1 .. nws.nws_cmu.ln1_top_err_ctrl3 (3 regs) */
	0x021ca090, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1432 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1433 (2 regs) */
	0x021ca0a1, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1434 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1435 (2 regs) */
	0x061ca0a6, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1436 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1441 (6 regs) */
	0xff500000, 	/* block nws */
	0x011ca0ad, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1442 (1 regs) */
	0x031ca0b0, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1443 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1445 (3 regs) */
	0x011ca0b4, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1446 (1 regs) */
	0x021ca0b6, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1447 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1448 (2 regs) */
	0x091ca0b9, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1449 .. nws.nws_cmu.ln1_cdr_rxclk_dlpf_status4 (9 regs) */
	0x031ca0c4, 	/* nws.nws_cmu.ln1_cdr_rxclk_dlpf_status5 .. nws.nws_cmu.ln1_cdr_rxclk_integral_status1 (3 regs) */
	0x081ca0c8, 	/* nws.nws_cmu.ln1_cdr_rxclk_integral_status2 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1461 (8 regs) */
	0x041ca0e0, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1462 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1465 (4 regs) */
	0x091ca0e8, 	/* nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1466 .. nws.nws_cmu.ln1_cdr_rxclk_ReservedRegister1474 (9 regs) */
	0x021ca100, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1475 .. nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1476 (2 regs) */
	0x011ca104, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1477 (1 regs) */
	0x011ca106, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1478 (1 regs) */
	0x031ca10a, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1479 .. nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1481 (3 regs) */
	0x021ca110, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1482 .. nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1483 (2 regs) */
	0x041ca118, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1484 .. nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1487 (4 regs) */
	0x061ca120, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1488 .. nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1493 (6 regs) */
	0x011ca130, 	/* nws.nws_cmu.ln1_cdr_refclk_ReservedRegister1494 (1 regs) */
	0x0f1ca180, 	/* nws.nws_cmu.ln1_aneg_ReservedRegister1495 .. nws.nws_cmu.ln1_aneg_ReservedRegister1506 (15 regs) */
	0x011ca190, 	/* nws.nws_cmu.ln1_aneg_status0 (1 regs) */
	0x021ca194, 	/* nws.nws_cmu.ln1_aneg_status_dbg0 .. nws.nws_cmu.ln1_aneg_status_dbg1 (2 regs) */
	0x251ca198, 	/* nws.nws_cmu.ln1_aneg_base_page0 .. nws.nws_cmu.ln1_aneg_resolution_eee (37 regs) */
	0x021ca1be, 	/* nws.nws_cmu.ln1_aneg_link_status0 .. nws.nws_cmu.ln1_aneg_link_status1 (2 regs) */
	0x031ca1c1, 	/* nws.nws_cmu.ln1_eee_ReservedRegister1523 .. nws.nws_cmu.ln1_eee_ReservedRegister1525 (3 regs) */
	0x041ca1c5, 	/* nws.nws_cmu.ln1_eee_ReservedRegister1526 .. nws.nws_cmu.ln1_eee_ReservedRegister1529 (4 regs) */
	0x011ca200, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1530 (1 regs) */
	0x021ca202, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1531 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1532 (2 regs) */
	0x011ca205, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1533 (1 regs) */
	0x011ca207, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1534 (1 regs) */
	0x041ca209, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1535 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1538 (4 regs) */
	0x041ca20e, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1539 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1542 (4 regs) */
	0x0d1ca220, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1543 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1555 (13 regs) */
	0x011ca22e, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1556 (1 regs) */
	0x0b1ca230, 	/* nws.nws_cmu.ln1_leq_refclk_agclos_ctrl0 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1566 (11 regs) */
	0x021ca23d, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1567 .. nws.nws_cmu.ln1_leq_refclk_ple_att_ctrl1 (2 regs) */
	0x071ca240, 	/* nws.nws_cmu.ln1_leq_refclk_eq_hfg_sql_ctrl0 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1573 (7 regs) */
	0x191ca250, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1574 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1598 (25 regs) */
	0x071ca270, 	/* nws.nws_cmu.ln1_leq_refclk_gn_apg_ctrl0 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1604 (7 regs) */
	0x091ca280, 	/* nws.nws_cmu.ln1_leq_refclk_eq_lfg_ctrl0 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1610 (9 regs) */
	0x071ca290, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1611 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1617 (7 regs) */
	0x061ca298, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1618 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1622 (6 regs) */
	0x041ca2a0, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1623 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1626 (4 regs) */
	0x041ca2a6, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1627 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1630 (4 regs) */
	0x021ca2ab, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1631 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1632 (2 regs) */
	0x021ca2ae, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1633 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1634 (2 regs) */
	0x021ca2b8, 	/* nws.nws_cmu.ln1_leq_refclk_ReservedRegister1635 .. nws.nws_cmu.ln1_leq_refclk_ReservedRegister1636 (2 regs) */
	0x061ca300, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1637 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1642 (6 regs) */
	0x021ca308, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1643 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1644 (2 regs) */
	0x021ca30c, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1645 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1646 (2 regs) */
	0x021ca310, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1647 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1648 (2 regs) */
	0x021ca313, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1649 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1650 (2 regs) */
	0x021ca316, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1651 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1652 (2 regs) */
	0x021ca320, 	/* nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1653 .. nws.nws_cmu.ln1_leq_rxclk_ReservedRegister1654 (2 regs) */
	0x071ca380, 	/* nws.nws_cmu.ln1_drv_refclk_afe_pd_ctrl0 .. nws.nws_cmu.ln1_drv_refclk_ReservedRegister1659 (7 regs) */
	0x061ca388, 	/* nws.nws_cmu.ln1_drv_refclk_ReservedRegister1660 .. nws.nws_cmu.ln1_drv_refclk_ReservedRegister1665 (6 regs) */
	0x0a1ca390, 	/* nws.nws_cmu.ln1_drv_refclk_txeq_ctrl0 .. nws.nws_cmu.ln1_drv_refclk_ReservedRegister1670 (10 regs) */
	0x011ca39b, 	/* nws.nws_cmu.ln1_drv_refclk_ReservedRegister1671 (1 regs) */
	0x051ca400, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1672 .. nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1676 (5 regs) */
	0x011ca406, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1677 (1 regs) */
	0x011ca40a, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1678 (1 regs) */
	0x011ca40c, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1679 (1 regs) */
	0x011ca40e, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1680 (1 regs) */
	0x011ca410, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1681 (1 regs) */
	0x011ca412, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1682 (1 regs) */
	0x011ca414, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1683 (1 regs) */
	0x011ca416, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1684 (1 regs) */
	0x021ca418, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1685 .. nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1686 (2 regs) */
	0x011ca41b, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1687 (1 regs) */
	0x091ca420, 	/* nws.nws_cmu.ln1_dfe_refclk_fsm_ctrl0 .. nws.nws_cmu.ln1_dfe_refclk_fsm_status0 (9 regs) */
	0x191ca42a, 	/* nws.nws_cmu.ln1_dfe_refclk_tap_ctrl0 .. nws.nws_cmu.ln1_dfe_refclk_tap_val_status7 (25 regs) */
	0x161ca450, 	/* nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1695 .. nws.nws_cmu.ln1_dfe_refclk_ReservedRegister1716 (22 regs) */
	0x031ca480, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1717 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1719 (3 regs) */
	0x081ca486, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1720 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1727 (8 regs) */
	0x031ca490, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1728 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1730 (3 regs) */
	0x091ca496, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1731 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1739 (9 regs) */
	0x161ca4a4, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1740 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1761 (22 regs) */
	0x121ca4c0, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1762 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1779 (18 regs) */
	0x031ca4d6, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1780 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1782 (3 regs) */
	0x0c1ca4e0, 	/* nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1783 .. nws.nws_cmu.ln1_dfe_rxclk_ReservedRegister1794 (12 regs) */
	0x061ca500, 	/* nws.nws_cmu.ln1_los_refclk_afe_cal_ctrl .. nws.nws_cmu.ln1_los_refclk_run_length_status0 (6 regs) */
	0x071ca510, 	/* nws.nws_cmu.ln1_los_refclk_filter_ctrl0 .. nws.nws_cmu.ln1_los_refclk_filter_ctrl6 (7 regs) */
	0x051ca520, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1797 .. nws.nws_cmu.ln1_los_refclk_ReservedRegister1801 (5 regs) */
	0x041ca530, 	/* nws.nws_cmu.ln1_los_refclk_override_ctrl0 .. nws.nws_cmu.ln1_los_refclk_ReservedRegister1804 (4 regs) */
	0x041ca540, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1805 .. nws.nws_cmu.ln1_los_refclk_ReservedRegister1808 (4 regs) */
	0x011ca546, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1809 (1 regs) */
	0x011ca551, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1810 (1 regs) */
	0x011ca559, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1811 (1 regs) */
	0x011ca560, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1812 (1 regs) */
	0x021ca570, 	/* nws.nws_cmu.ln1_los_refclk_ReservedRegister1813 .. nws.nws_cmu.ln1_los_refclk_status0 (2 regs) */
	0x041ca580, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1814 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1817 (4 regs) */
	0x041ca590, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1818 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1821 (4 regs) */
	0x071ca5a0, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1822 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1828 (7 regs) */
	0x031ca5b0, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1829 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1831 (3 regs) */
	0x091ca5c0, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1832 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1840 (9 regs) */
	0x021ca5d0, 	/* nws.nws_cmu.ln1_gcfsm2_ReservedRegister1841 .. nws.nws_cmu.ln1_gcfsm2_ReservedRegister1842 (2 regs) */
	0x051ca600, 	/* nws.nws_cmu.ln1_bist_tx_ctrl .. nws.nws_cmu.ln1_bist_tx_ReservedRegister1846 (5 regs) */
	0x081ca606, 	/* nws.nws_cmu.ln1_bist_tx_ber_ctrl0 .. nws.nws_cmu.ln1_bist_tx_ber_ctrl7 (8 regs) */
	0x011ca620, 	/* nws.nws_cmu.ln1_bist_tx_udp_shift_amount (1 regs) */
	0x191ca624, 	/* nws.nws_cmu.ln1_bist_tx_udp_7_0 .. nws.nws_cmu.ln1_bist_tx_udp_199_192 (25 regs) */
	0x011ca680, 	/* nws.nws_cmu.ln1_bist_rx_ctrl (1 regs) */
	0x011ca684, 	/* nws.nws_cmu.ln1_bist_rx_status (1 regs) */
	0x031ca688, 	/* nws.nws_cmu.ln1_bist_rx_ber_status0 .. nws.nws_cmu.ln1_bist_rx_ber_status2 (3 regs) */
	0x031ca68c, 	/* nws.nws_cmu.ln1_bist_rx_ber_status4 .. nws.nws_cmu.ln1_bist_rx_ber_status6 (3 regs) */
	0x041ca694, 	/* nws.nws_cmu.ln1_bist_rx_lock_ctrl0 .. nws.nws_cmu.ln1_bist_rx_lock_ctrl3 (4 regs) */
	0x051ca6a0, 	/* nws.nws_cmu.ln1_bist_rx_loss_lock_ctrl0 .. nws.nws_cmu.ln1_bist_rx_loss_lock_ctrl4 (5 regs) */
	0x011ca6b0, 	/* nws.nws_cmu.ln1_bist_rx_udp_shift_amount (1 regs) */
	0x191ca6b4, 	/* nws.nws_cmu.ln1_bist_rx_udp_7_0 .. nws.nws_cmu.ln1_bist_rx_udp_199_192 (25 regs) */
	0x021ca700, 	/* nws.nws_cmu.ln1_feature_rxterm_cfg0 .. nws.nws_cmu.ln1_feature_rxclkdiv_cfg0 (2 regs) */
	0x061ca704, 	/* nws.nws_cmu.ln1_feature_ReservedRegister1847 .. nws.nws_cmu.ln1_feature_ReservedRegister1852 (6 regs) */
	0x071ca710, 	/* nws.nws_cmu.ln1_feature_ReservedRegister1853 .. nws.nws_cmu.ln1_feature_ReservedRegister1859 (7 regs) */
	0x0a1ca720, 	/* nws.nws_cmu.ln1_feature_ReservedRegister1860 .. nws.nws_cmu.ln1_feature_ReservedRegister1862 (10 regs) */
	0x071ca730, 	/* nws.nws_cmu.ln1_feature_dfe_cfg .. nws.nws_cmu.ln1_feature_dfe_adapt_tap5_cfg (7 regs) */
	0x101ca738, 	/* nws.nws_cmu.ln1_feature_adapt_cont_cfg0 .. nws.nws_cmu.ln1_feature_ReservedRegister1874 (16 regs) */
	0x011ca750, 	/* nws.nws_cmu.ln1_feature_test_cfg0 (1 regs) */
	0x081ca758, 	/* nws.nws_cmu.ln1_feature_ReservedRegister1875 .. nws.nws_cmu.ln1_feature_ReservedRegister1882 (8 regs) */
	0x061ca780, 	/* nws.nws_cmu.ln1_lt_tx_fsm_ctrl0 .. nws.nws_cmu.ln1_lt_tx_fsm_ctrl5 (6 regs) */
	0x011ca790, 	/* nws.nws_cmu.ln1_lt_tx_fsm_status (1 regs) */
	0x031ca793, 	/* nws.nws_cmu.ln1_lt_tx_prbs_ctrl0 .. nws.nws_cmu.ln1_lt_tx_prbs_ctrl2 (3 regs) */
	0x011ca7a0, 	/* nws.nws_cmu.ln1_lt_tx_coefficient_update_ctrl (1 regs) */
	0x011ca7a2, 	/* nws.nws_cmu.ln1_lt_tx_status_report_ctrl (1 regs) */
	0x021ca7b0, 	/* nws.nws_cmu.ln1_lt_tx_fsm_state_status0 .. nws.nws_cmu.ln1_lt_tx_fsm_state_status1 (2 regs) */
	0x011ca7c0, 	/* nws.nws_cmu.ln1_lt_rx_ctrl0 (1 regs) */
	0x021ca7c2, 	/* nws.nws_cmu.ln1_lt_rx_prbs_ctrl0 .. nws.nws_cmu.ln1_lt_rx_prbs_ctrl1 (2 regs) */
	0x031ca7c5, 	/* nws.nws_cmu.ln1_lt_rx_prbs_status0 .. nws.nws_cmu.ln1_lt_rx_prbs_status2 (3 regs) */
	0x011ca7d0, 	/* nws.nws_cmu.ln1_lt_rx_frame_ctrl (1 regs) */
	0x051ca7d3, 	/* nws.nws_cmu.ln1_lt_rx_frame_status0 .. nws.nws_cmu.ln1_lt_rx_frame_status4 (5 regs) */
	0x011ca7e0, 	/* nws.nws_cmu.ln1_lt_rx_coefficient_update_status (1 regs) */
	0x011ca7e2, 	/* nws.nws_cmu.ln1_lt_rx_report_status (1 regs) */
	0x071ca800, 	/* nws.nws_cmu.ln2_top_afe_loopback_ctrl .. nws.nws_cmu.ln2_top_ReservedRegister1888 (7 regs) */
	0x011ca810, 	/* nws.nws_cmu.ln2_top_ReservedRegister1889 (1 regs) */
	0x031ca812, 	/* nws.nws_cmu.ln2_top_ReservedRegister1890 .. nws.nws_cmu.ln2_top_ReservedRegister1892 (3 regs) */
	0x011ca816, 	/* nws.nws_cmu.ln2_top_ReservedRegister1893 (1 regs) */
	0x011ca819, 	/* nws.nws_cmu.ln2_top_ReservedRegister1894 (1 regs) */
	0x021ca81b, 	/* nws.nws_cmu.ln2_top_ReservedRegister1895 .. nws.nws_cmu.ln2_top_ReservedRegister1896 (2 regs) */
	0x011ca81e, 	/* nws.nws_cmu.ln2_top_ReservedRegister1897 (1 regs) */
	0x011ca822, 	/* nws.nws_cmu.ln2_top_dpl_txdp_ctrl1 (1 regs) */
	0x041ca824, 	/* nws.nws_cmu.ln2_top_dpl_rxdp_ctrl1 .. nws.nws_cmu.ln2_top_phy_if_status (4 regs) */
	0x021ca830, 	/* nws.nws_cmu.ln2_top_ReservedRegister1900 .. nws.nws_cmu.ln2_top_ReservedRegister1901 (2 regs) */
	0x091ca838, 	/* nws.nws_cmu.ln2_top_ln_stat_ctrl0 .. nws.nws_cmu.ln2_top_ReservedRegister1908 (9 regs) */
	0x021ca842, 	/* nws.nws_cmu.ln2_top_ReservedRegister1909 .. nws.nws_cmu.ln2_top_ReservedRegister1910 (2 regs) */
	0x051ca848, 	/* nws.nws_cmu.ln2_top_ReservedRegister1911 .. nws.nws_cmu.ln2_top_ReservedRegister1915 (5 regs) */
	0x031ca850, 	/* nws.nws_cmu.ln2_top_err_ctrl1 .. nws.nws_cmu.ln2_top_err_ctrl3 (3 regs) */
	0x021ca890, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1916 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1917 (2 regs) */
	0x021ca8a1, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1918 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1919 (2 regs) */
	0x061ca8a6, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1920 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1925 (6 regs) */
	0x011ca8ad, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1926 (1 regs) */
	0x031ca8b0, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1927 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1929 (3 regs) */
	0x011ca8b4, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1930 (1 regs) */
	0x021ca8b6, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1931 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1932 (2 regs) */
	0x091ca8b9, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1933 .. nws.nws_cmu.ln2_cdr_rxclk_dlpf_status4 (9 regs) */
	0x031ca8c4, 	/* nws.nws_cmu.ln2_cdr_rxclk_dlpf_status5 .. nws.nws_cmu.ln2_cdr_rxclk_integral_status1 (3 regs) */
	0x081ca8c8, 	/* nws.nws_cmu.ln2_cdr_rxclk_integral_status2 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1945 (8 regs) */
	0x041ca8e0, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1946 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1949 (4 regs) */
	0x091ca8e8, 	/* nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1950 .. nws.nws_cmu.ln2_cdr_rxclk_ReservedRegister1958 (9 regs) */
	0x021ca900, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1959 .. nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1960 (2 regs) */
	0x011ca904, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1961 (1 regs) */
	0x011ca906, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1962 (1 regs) */
	0x031ca90a, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1963 .. nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1965 (3 regs) */
	0x021ca910, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1966 .. nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1967 (2 regs) */
	0x041ca918, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1968 .. nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1971 (4 regs) */
	0x061ca920, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1972 .. nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1977 (6 regs) */
	0x011ca930, 	/* nws.nws_cmu.ln2_cdr_refclk_ReservedRegister1978 (1 regs) */
	0x0f1ca980, 	/* nws.nws_cmu.ln2_aneg_ReservedRegister1979 .. nws.nws_cmu.ln2_aneg_ReservedRegister1990 (15 regs) */
	0x011ca990, 	/* nws.nws_cmu.ln2_aneg_status0 (1 regs) */
	0x021ca994, 	/* nws.nws_cmu.ln2_aneg_status_dbg0 .. nws.nws_cmu.ln2_aneg_status_dbg1 (2 regs) */
	0x251ca998, 	/* nws.nws_cmu.ln2_aneg_base_page0 .. nws.nws_cmu.ln2_aneg_resolution_eee (37 regs) */
	0x021ca9be, 	/* nws.nws_cmu.ln2_aneg_link_status0 .. nws.nws_cmu.ln2_aneg_link_status1 (2 regs) */
	0x031ca9c1, 	/* nws.nws_cmu.ln2_eee_ReservedRegister2007 .. nws.nws_cmu.ln2_eee_ReservedRegister2009 (3 regs) */
	0x041ca9c5, 	/* nws.nws_cmu.ln2_eee_ReservedRegister2010 .. nws.nws_cmu.ln2_eee_ReservedRegister2013 (4 regs) */
	0x011caa00, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2014 (1 regs) */
	0x021caa02, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2015 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2016 (2 regs) */
	0x011caa05, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2017 (1 regs) */
	0x011caa07, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2018 (1 regs) */
	0x041caa09, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2019 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2022 (4 regs) */
	0x041caa0e, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2023 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2026 (4 regs) */
	0x0d1caa20, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2027 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2039 (13 regs) */
	0x011caa2e, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2040 (1 regs) */
	0x0b1caa30, 	/* nws.nws_cmu.ln2_leq_refclk_agclos_ctrl0 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2050 (11 regs) */
	0x021caa3d, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2051 .. nws.nws_cmu.ln2_leq_refclk_ple_att_ctrl1 (2 regs) */
	0x071caa40, 	/* nws.nws_cmu.ln2_leq_refclk_eq_hfg_sql_ctrl0 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2057 (7 regs) */
	0x191caa50, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2058 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2082 (25 regs) */
	0x071caa70, 	/* nws.nws_cmu.ln2_leq_refclk_gn_apg_ctrl0 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2088 (7 regs) */
	0x091caa80, 	/* nws.nws_cmu.ln2_leq_refclk_eq_lfg_ctrl0 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2094 (9 regs) */
	0x071caa90, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2095 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2101 (7 regs) */
	0x061caa98, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2102 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2106 (6 regs) */
	0x041caaa0, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2107 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2110 (4 regs) */
	0x041caaa6, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2111 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2114 (4 regs) */
	0x021caaab, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2115 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2116 (2 regs) */
	0x021caaae, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2117 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2118 (2 regs) */
	0x021caab8, 	/* nws.nws_cmu.ln2_leq_refclk_ReservedRegister2119 .. nws.nws_cmu.ln2_leq_refclk_ReservedRegister2120 (2 regs) */
	0x061cab00, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2121 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2126 (6 regs) */
	0x021cab08, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2127 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2128 (2 regs) */
	0x021cab0c, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2129 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2130 (2 regs) */
	0x021cab10, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2131 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2132 (2 regs) */
	0x021cab13, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2133 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2134 (2 regs) */
	0x021cab16, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2135 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2136 (2 regs) */
	0x021cab20, 	/* nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2137 .. nws.nws_cmu.ln2_leq_rxclk_ReservedRegister2138 (2 regs) */
	0x071cab80, 	/* nws.nws_cmu.ln2_drv_refclk_afe_pd_ctrl0 .. nws.nws_cmu.ln2_drv_refclk_ReservedRegister2143 (7 regs) */
	0x061cab88, 	/* nws.nws_cmu.ln2_drv_refclk_ReservedRegister2144 .. nws.nws_cmu.ln2_drv_refclk_ReservedRegister2149 (6 regs) */
	0x0a1cab90, 	/* nws.nws_cmu.ln2_drv_refclk_txeq_ctrl0 .. nws.nws_cmu.ln2_drv_refclk_ReservedRegister2154 (10 regs) */
	0x011cab9b, 	/* nws.nws_cmu.ln2_drv_refclk_ReservedRegister2155 (1 regs) */
	0x051cac00, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2156 .. nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2160 (5 regs) */
	0x011cac06, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2161 (1 regs) */
	0x011cac0a, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2162 (1 regs) */
	0x011cac0c, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2163 (1 regs) */
	0x011cac0e, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2164 (1 regs) */
	0x011cac10, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2165 (1 regs) */
	0x011cac12, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2166 (1 regs) */
	0x011cac14, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2167 (1 regs) */
	0x011cac16, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2168 (1 regs) */
	0x021cac18, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2169 .. nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2170 (2 regs) */
	0x011cac1b, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2171 (1 regs) */
	0x091cac20, 	/* nws.nws_cmu.ln2_dfe_refclk_fsm_ctrl0 .. nws.nws_cmu.ln2_dfe_refclk_fsm_status0 (9 regs) */
	0x191cac2a, 	/* nws.nws_cmu.ln2_dfe_refclk_tap_ctrl0 .. nws.nws_cmu.ln2_dfe_refclk_tap_val_status7 (25 regs) */
	0x161cac50, 	/* nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2179 .. nws.nws_cmu.ln2_dfe_refclk_ReservedRegister2200 (22 regs) */
	0x031cac80, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2201 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2203 (3 regs) */
	0x081cac86, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2204 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2211 (8 regs) */
	0x031cac90, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2212 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2214 (3 regs) */
	0x091cac96, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2215 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2223 (9 regs) */
	0x161caca4, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2224 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2245 (22 regs) */
	0x121cacc0, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2246 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2263 (18 regs) */
	0x031cacd6, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2264 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2266 (3 regs) */
	0x0c1cace0, 	/* nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2267 .. nws.nws_cmu.ln2_dfe_rxclk_ReservedRegister2278 (12 regs) */
	0x061cad00, 	/* nws.nws_cmu.ln2_los_refclk_afe_cal_ctrl .. nws.nws_cmu.ln2_los_refclk_run_length_status0 (6 regs) */
	0x071cad10, 	/* nws.nws_cmu.ln2_los_refclk_filter_ctrl0 .. nws.nws_cmu.ln2_los_refclk_filter_ctrl6 (7 regs) */
	0x051cad20, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2281 .. nws.nws_cmu.ln2_los_refclk_ReservedRegister2285 (5 regs) */
	0x041cad30, 	/* nws.nws_cmu.ln2_los_refclk_override_ctrl0 .. nws.nws_cmu.ln2_los_refclk_ReservedRegister2288 (4 regs) */
	0x041cad40, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2289 .. nws.nws_cmu.ln2_los_refclk_ReservedRegister2292 (4 regs) */
	0x011cad46, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2293 (1 regs) */
	0x011cad51, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2294 (1 regs) */
	0x011cad59, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2295 (1 regs) */
	0x011cad60, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2296 (1 regs) */
	0x021cad70, 	/* nws.nws_cmu.ln2_los_refclk_ReservedRegister2297 .. nws.nws_cmu.ln2_los_refclk_status0 (2 regs) */
	0x041cad80, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2298 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2301 (4 regs) */
	0x041cad90, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2302 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2305 (4 regs) */
	0x071cada0, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2306 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2312 (7 regs) */
	0x031cadb0, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2313 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2315 (3 regs) */
	0x091cadc0, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2316 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2324 (9 regs) */
	0x021cadd0, 	/* nws.nws_cmu.ln2_gcfsm2_ReservedRegister2325 .. nws.nws_cmu.ln2_gcfsm2_ReservedRegister2326 (2 regs) */
	0x051cae00, 	/* nws.nws_cmu.ln2_bist_tx_ctrl .. nws.nws_cmu.ln2_bist_tx_ReservedRegister2330 (5 regs) */
	0x081cae06, 	/* nws.nws_cmu.ln2_bist_tx_ber_ctrl0 .. nws.nws_cmu.ln2_bist_tx_ber_ctrl7 (8 regs) */
	0x011cae20, 	/* nws.nws_cmu.ln2_bist_tx_udp_shift_amount (1 regs) */
	0x191cae24, 	/* nws.nws_cmu.ln2_bist_tx_udp_7_0 .. nws.nws_cmu.ln2_bist_tx_udp_199_192 (25 regs) */
	0x011cae80, 	/* nws.nws_cmu.ln2_bist_rx_ctrl (1 regs) */
	0x011cae84, 	/* nws.nws_cmu.ln2_bist_rx_status (1 regs) */
	0x031cae88, 	/* nws.nws_cmu.ln2_bist_rx_ber_status0 .. nws.nws_cmu.ln2_bist_rx_ber_status2 (3 regs) */
	0x031cae8c, 	/* nws.nws_cmu.ln2_bist_rx_ber_status4 .. nws.nws_cmu.ln2_bist_rx_ber_status6 (3 regs) */
	0x041cae94, 	/* nws.nws_cmu.ln2_bist_rx_lock_ctrl0 .. nws.nws_cmu.ln2_bist_rx_lock_ctrl3 (4 regs) */
	0x051caea0, 	/* nws.nws_cmu.ln2_bist_rx_loss_lock_ctrl0 .. nws.nws_cmu.ln2_bist_rx_loss_lock_ctrl4 (5 regs) */
	0x011caeb0, 	/* nws.nws_cmu.ln2_bist_rx_udp_shift_amount (1 regs) */
	0x191caeb4, 	/* nws.nws_cmu.ln2_bist_rx_udp_7_0 .. nws.nws_cmu.ln2_bist_rx_udp_199_192 (25 regs) */
	0x021caf00, 	/* nws.nws_cmu.ln2_feature_rxterm_cfg0 .. nws.nws_cmu.ln2_feature_rxclkdiv_cfg0 (2 regs) */
	0x061caf04, 	/* nws.nws_cmu.ln2_feature_ReservedRegister2331 .. nws.nws_cmu.ln2_feature_ReservedRegister2336 (6 regs) */
	0x071caf10, 	/* nws.nws_cmu.ln2_feature_ReservedRegister2337 .. nws.nws_cmu.ln2_feature_ReservedRegister2343 (7 regs) */
	0x0a1caf20, 	/* nws.nws_cmu.ln2_feature_ReservedRegister2344 .. nws.nws_cmu.ln2_feature_ReservedRegister2346 (10 regs) */
	0x071caf30, 	/* nws.nws_cmu.ln2_feature_dfe_cfg .. nws.nws_cmu.ln2_feature_dfe_adapt_tap5_cfg (7 regs) */
	0xa0500000, 	/* block nws */
	0x101caf38, 	/* nws.nws_cmu.ln2_feature_adapt_cont_cfg0 .. nws.nws_cmu.ln2_feature_ReservedRegister2358 (16 regs) */
	0x011caf50, 	/* nws.nws_cmu.ln2_feature_test_cfg0 (1 regs) */
	0x081caf58, 	/* nws.nws_cmu.ln2_feature_ReservedRegister2359 .. nws.nws_cmu.ln2_feature_ReservedRegister2366 (8 regs) */
	0x061caf80, 	/* nws.nws_cmu.ln2_lt_tx_fsm_ctrl0 .. nws.nws_cmu.ln2_lt_tx_fsm_ctrl5 (6 regs) */
	0x011caf90, 	/* nws.nws_cmu.ln2_lt_tx_fsm_status (1 regs) */
	0x031caf93, 	/* nws.nws_cmu.ln2_lt_tx_prbs_ctrl0 .. nws.nws_cmu.ln2_lt_tx_prbs_ctrl2 (3 regs) */
	0x011cafa0, 	/* nws.nws_cmu.ln2_lt_tx_coefficient_update_ctrl (1 regs) */
	0x011cafa2, 	/* nws.nws_cmu.ln2_lt_tx_status_report_ctrl (1 regs) */
	0x021cafb0, 	/* nws.nws_cmu.ln2_lt_tx_fsm_state_status0 .. nws.nws_cmu.ln2_lt_tx_fsm_state_status1 (2 regs) */
	0x011cafc0, 	/* nws.nws_cmu.ln2_lt_rx_ctrl0 (1 regs) */
	0x021cafc2, 	/* nws.nws_cmu.ln2_lt_rx_prbs_ctrl0 .. nws.nws_cmu.ln2_lt_rx_prbs_ctrl1 (2 regs) */
	0x031cafc5, 	/* nws.nws_cmu.ln2_lt_rx_prbs_status0 .. nws.nws_cmu.ln2_lt_rx_prbs_status2 (3 regs) */
	0x011cafd0, 	/* nws.nws_cmu.ln2_lt_rx_frame_ctrl (1 regs) */
	0x051cafd3, 	/* nws.nws_cmu.ln2_lt_rx_frame_status0 .. nws.nws_cmu.ln2_lt_rx_frame_status4 (5 regs) */
	0x011cafe0, 	/* nws.nws_cmu.ln2_lt_rx_coefficient_update_status (1 regs) */
	0x011cafe2, 	/* nws.nws_cmu.ln2_lt_rx_report_status (1 regs) */
	0x071cb000, 	/* nws.nws_cmu.ln3_top_afe_loopback_ctrl .. nws.nws_cmu.ln3_top_ReservedRegister2372 (7 regs) */
	0x011cb010, 	/* nws.nws_cmu.ln3_top_ReservedRegister2373 (1 regs) */
	0x031cb012, 	/* nws.nws_cmu.ln3_top_ReservedRegister2374 .. nws.nws_cmu.ln3_top_ReservedRegister2376 (3 regs) */
	0x011cb016, 	/* nws.nws_cmu.ln3_top_ReservedRegister2377 (1 regs) */
	0x011cb019, 	/* nws.nws_cmu.ln3_top_ReservedRegister2378 (1 regs) */
	0x021cb01b, 	/* nws.nws_cmu.ln3_top_ReservedRegister2379 .. nws.nws_cmu.ln3_top_ReservedRegister2380 (2 regs) */
	0x011cb01e, 	/* nws.nws_cmu.ln3_top_ReservedRegister2381 (1 regs) */
	0x011cb022, 	/* nws.nws_cmu.ln3_top_dpl_txdp_ctrl1 (1 regs) */
	0x041cb024, 	/* nws.nws_cmu.ln3_top_dpl_rxdp_ctrl1 .. nws.nws_cmu.ln3_top_phy_if_status (4 regs) */
	0x021cb030, 	/* nws.nws_cmu.ln3_top_ReservedRegister2384 .. nws.nws_cmu.ln3_top_ReservedRegister2385 (2 regs) */
	0x091cb038, 	/* nws.nws_cmu.ln3_top_ln_stat_ctrl0 .. nws.nws_cmu.ln3_top_ReservedRegister2392 (9 regs) */
	0x021cb042, 	/* nws.nws_cmu.ln3_top_ReservedRegister2393 .. nws.nws_cmu.ln3_top_ReservedRegister2394 (2 regs) */
	0x051cb048, 	/* nws.nws_cmu.ln3_top_ReservedRegister2395 .. nws.nws_cmu.ln3_top_ReservedRegister2399 (5 regs) */
	0x031cb050, 	/* nws.nws_cmu.ln3_top_err_ctrl1 .. nws.nws_cmu.ln3_top_err_ctrl3 (3 regs) */
	0x021cb090, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2400 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2401 (2 regs) */
	0x021cb0a1, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2402 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2403 (2 regs) */
	0x061cb0a6, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2404 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2409 (6 regs) */
	0x011cb0ad, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2410 (1 regs) */
	0x031cb0b0, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2411 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2413 (3 regs) */
	0x011cb0b4, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2414 (1 regs) */
	0x021cb0b6, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2415 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2416 (2 regs) */
	0x091cb0b9, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2417 .. nws.nws_cmu.ln3_cdr_rxclk_dlpf_status4 (9 regs) */
	0x031cb0c4, 	/* nws.nws_cmu.ln3_cdr_rxclk_dlpf_status5 .. nws.nws_cmu.ln3_cdr_rxclk_integral_status1 (3 regs) */
	0x081cb0c8, 	/* nws.nws_cmu.ln3_cdr_rxclk_integral_status2 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2429 (8 regs) */
	0x041cb0e0, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2430 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2433 (4 regs) */
	0x091cb0e8, 	/* nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2434 .. nws.nws_cmu.ln3_cdr_rxclk_ReservedRegister2442 (9 regs) */
	0x021cb100, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2443 .. nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2444 (2 regs) */
	0x011cb104, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2445 (1 regs) */
	0x011cb106, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2446 (1 regs) */
	0x031cb10a, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2447 .. nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2449 (3 regs) */
	0x021cb110, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2450 .. nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2451 (2 regs) */
	0x041cb118, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2452 .. nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2455 (4 regs) */
	0x061cb120, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2456 .. nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2461 (6 regs) */
	0x011cb130, 	/* nws.nws_cmu.ln3_cdr_refclk_ReservedRegister2462 (1 regs) */
	0x0f1cb180, 	/* nws.nws_cmu.ln3_aneg_ReservedRegister2463 .. nws.nws_cmu.ln3_aneg_ReservedRegister2474 (15 regs) */
	0x011cb190, 	/* nws.nws_cmu.ln3_aneg_status0 (1 regs) */
	0x021cb194, 	/* nws.nws_cmu.ln3_aneg_status_dbg0 .. nws.nws_cmu.ln3_aneg_status_dbg1 (2 regs) */
	0x251cb198, 	/* nws.nws_cmu.ln3_aneg_base_page0 .. nws.nws_cmu.ln3_aneg_resolution_eee (37 regs) */
	0x021cb1be, 	/* nws.nws_cmu.ln3_aneg_link_status0 .. nws.nws_cmu.ln3_aneg_link_status1 (2 regs) */
	0x031cb1c1, 	/* nws.nws_cmu.ln3_eee_ReservedRegister2491 .. nws.nws_cmu.ln3_eee_ReservedRegister2493 (3 regs) */
	0x041cb1c5, 	/* nws.nws_cmu.ln3_eee_ReservedRegister2494 .. nws.nws_cmu.ln3_eee_ReservedRegister2497 (4 regs) */
	0x011cb200, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2498 (1 regs) */
	0x021cb202, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2499 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2500 (2 regs) */
	0x011cb205, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2501 (1 regs) */
	0x011cb207, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2502 (1 regs) */
	0x041cb209, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2503 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2506 (4 regs) */
	0x041cb20e, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2507 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2510 (4 regs) */
	0x0d1cb220, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2511 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2523 (13 regs) */
	0x011cb22e, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2524 (1 regs) */
	0x0b1cb230, 	/* nws.nws_cmu.ln3_leq_refclk_agclos_ctrl0 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2534 (11 regs) */
	0x021cb23d, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2535 .. nws.nws_cmu.ln3_leq_refclk_ple_att_ctrl1 (2 regs) */
	0x071cb240, 	/* nws.nws_cmu.ln3_leq_refclk_eq_hfg_sql_ctrl0 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2541 (7 regs) */
	0x191cb250, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2542 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2566 (25 regs) */
	0x071cb270, 	/* nws.nws_cmu.ln3_leq_refclk_gn_apg_ctrl0 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2572 (7 regs) */
	0x091cb280, 	/* nws.nws_cmu.ln3_leq_refclk_eq_lfg_ctrl0 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2578 (9 regs) */
	0x071cb290, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2579 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2585 (7 regs) */
	0x061cb298, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2586 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2590 (6 regs) */
	0x041cb2a0, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2591 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2594 (4 regs) */
	0x041cb2a6, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2595 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2598 (4 regs) */
	0x021cb2ab, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2599 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2600 (2 regs) */
	0x021cb2ae, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2601 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2602 (2 regs) */
	0x021cb2b8, 	/* nws.nws_cmu.ln3_leq_refclk_ReservedRegister2603 .. nws.nws_cmu.ln3_leq_refclk_ReservedRegister2604 (2 regs) */
	0x061cb300, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2605 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2610 (6 regs) */
	0x021cb308, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2611 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2612 (2 regs) */
	0x021cb30c, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2613 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2614 (2 regs) */
	0x021cb310, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2615 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2616 (2 regs) */
	0x021cb313, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2617 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2618 (2 regs) */
	0x021cb316, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2619 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2620 (2 regs) */
	0x021cb320, 	/* nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2621 .. nws.nws_cmu.ln3_leq_rxclk_ReservedRegister2622 (2 regs) */
	0x071cb380, 	/* nws.nws_cmu.ln3_drv_refclk_afe_pd_ctrl0 .. nws.nws_cmu.ln3_drv_refclk_ReservedRegister2627 (7 regs) */
	0x061cb388, 	/* nws.nws_cmu.ln3_drv_refclk_ReservedRegister2628 .. nws.nws_cmu.ln3_drv_refclk_ReservedRegister2633 (6 regs) */
	0x0a1cb390, 	/* nws.nws_cmu.ln3_drv_refclk_txeq_ctrl0 .. nws.nws_cmu.ln3_drv_refclk_ReservedRegister2638 (10 regs) */
	0x011cb39b, 	/* nws.nws_cmu.ln3_drv_refclk_ReservedRegister2639 (1 regs) */
	0x051cb400, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2640 .. nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2644 (5 regs) */
	0x011cb406, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2645 (1 regs) */
	0x011cb40a, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2646 (1 regs) */
	0x011cb40c, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2647 (1 regs) */
	0x011cb40e, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2648 (1 regs) */
	0x011cb410, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2649 (1 regs) */
	0x011cb412, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2650 (1 regs) */
	0x011cb414, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2651 (1 regs) */
	0x011cb416, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2652 (1 regs) */
	0x021cb418, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2653 .. nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2654 (2 regs) */
	0x011cb41b, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2655 (1 regs) */
	0x091cb420, 	/* nws.nws_cmu.ln3_dfe_refclk_fsm_ctrl0 .. nws.nws_cmu.ln3_dfe_refclk_fsm_status0 (9 regs) */
	0x191cb42a, 	/* nws.nws_cmu.ln3_dfe_refclk_tap_ctrl0 .. nws.nws_cmu.ln3_dfe_refclk_tap_val_status7 (25 regs) */
	0x161cb450, 	/* nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2663 .. nws.nws_cmu.ln3_dfe_refclk_ReservedRegister2684 (22 regs) */
	0x031cb480, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2685 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2687 (3 regs) */
	0x081cb486, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2688 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2695 (8 regs) */
	0x031cb490, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2696 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2698 (3 regs) */
	0x091cb496, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2699 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2707 (9 regs) */
	0x161cb4a4, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2708 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2729 (22 regs) */
	0x121cb4c0, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2730 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2747 (18 regs) */
	0x031cb4d6, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2748 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2750 (3 regs) */
	0x0c1cb4e0, 	/* nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2751 .. nws.nws_cmu.ln3_dfe_rxclk_ReservedRegister2762 (12 regs) */
	0x061cb500, 	/* nws.nws_cmu.ln3_los_refclk_afe_cal_ctrl .. nws.nws_cmu.ln3_los_refclk_run_length_status0 (6 regs) */
	0x071cb510, 	/* nws.nws_cmu.ln3_los_refclk_filter_ctrl0 .. nws.nws_cmu.ln3_los_refclk_filter_ctrl6 (7 regs) */
	0x051cb520, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2765 .. nws.nws_cmu.ln3_los_refclk_ReservedRegister2769 (5 regs) */
	0x041cb530, 	/* nws.nws_cmu.ln3_los_refclk_override_ctrl0 .. nws.nws_cmu.ln3_los_refclk_ReservedRegister2772 (4 regs) */
	0x041cb540, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2773 .. nws.nws_cmu.ln3_los_refclk_ReservedRegister2776 (4 regs) */
	0x011cb546, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2777 (1 regs) */
	0x011cb551, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2778 (1 regs) */
	0x011cb559, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2779 (1 regs) */
	0x011cb560, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2780 (1 regs) */
	0x021cb570, 	/* nws.nws_cmu.ln3_los_refclk_ReservedRegister2781 .. nws.nws_cmu.ln3_los_refclk_status0 (2 regs) */
	0x041cb580, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2782 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2785 (4 regs) */
	0x041cb590, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2786 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2789 (4 regs) */
	0x071cb5a0, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2790 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2796 (7 regs) */
	0x031cb5b0, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2797 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2799 (3 regs) */
	0x091cb5c0, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2800 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2808 (9 regs) */
	0x021cb5d0, 	/* nws.nws_cmu.ln3_gcfsm2_ReservedRegister2809 .. nws.nws_cmu.ln3_gcfsm2_ReservedRegister2810 (2 regs) */
	0x051cb600, 	/* nws.nws_cmu.ln3_bist_tx_ctrl .. nws.nws_cmu.ln3_bist_tx_ReservedRegister2814 (5 regs) */
	0x081cb606, 	/* nws.nws_cmu.ln3_bist_tx_ber_ctrl0 .. nws.nws_cmu.ln3_bist_tx_ber_ctrl7 (8 regs) */
	0x011cb620, 	/* nws.nws_cmu.ln3_bist_tx_udp_shift_amount (1 regs) */
	0x191cb624, 	/* nws.nws_cmu.ln3_bist_tx_udp_7_0 .. nws.nws_cmu.ln3_bist_tx_udp_199_192 (25 regs) */
	0x011cb680, 	/* nws.nws_cmu.ln3_bist_rx_ctrl (1 regs) */
	0x011cb684, 	/* nws.nws_cmu.ln3_bist_rx_status (1 regs) */
	0x031cb688, 	/* nws.nws_cmu.ln3_bist_rx_ber_status0 .. nws.nws_cmu.ln3_bist_rx_ber_status2 (3 regs) */
	0x031cb68c, 	/* nws.nws_cmu.ln3_bist_rx_ber_status4 .. nws.nws_cmu.ln3_bist_rx_ber_status6 (3 regs) */
	0x041cb694, 	/* nws.nws_cmu.ln3_bist_rx_lock_ctrl0 .. nws.nws_cmu.ln3_bist_rx_lock_ctrl3 (4 regs) */
	0x051cb6a0, 	/* nws.nws_cmu.ln3_bist_rx_loss_lock_ctrl0 .. nws.nws_cmu.ln3_bist_rx_loss_lock_ctrl4 (5 regs) */
	0x011cb6b0, 	/* nws.nws_cmu.ln3_bist_rx_udp_shift_amount (1 regs) */
	0x191cb6b4, 	/* nws.nws_cmu.ln3_bist_rx_udp_7_0 .. nws.nws_cmu.ln3_bist_rx_udp_199_192 (25 regs) */
	0x021cb700, 	/* nws.nws_cmu.ln3_feature_rxterm_cfg0 .. nws.nws_cmu.ln3_feature_rxclkdiv_cfg0 (2 regs) */
	0x061cb704, 	/* nws.nws_cmu.ln3_feature_ReservedRegister2815 .. nws.nws_cmu.ln3_feature_ReservedRegister2820 (6 regs) */
	0x071cb710, 	/* nws.nws_cmu.ln3_feature_ReservedRegister2821 .. nws.nws_cmu.ln3_feature_ReservedRegister2827 (7 regs) */
	0x0a1cb720, 	/* nws.nws_cmu.ln3_feature_ReservedRegister2828 .. nws.nws_cmu.ln3_feature_ReservedRegister2830 (10 regs) */
	0x071cb730, 	/* nws.nws_cmu.ln3_feature_dfe_cfg .. nws.nws_cmu.ln3_feature_dfe_adapt_tap5_cfg (7 regs) */
	0x101cb738, 	/* nws.nws_cmu.ln3_feature_adapt_cont_cfg0 .. nws.nws_cmu.ln3_feature_ReservedRegister2842 (16 regs) */
	0x011cb750, 	/* nws.nws_cmu.ln3_feature_test_cfg0 (1 regs) */
	0x081cb758, 	/* nws.nws_cmu.ln3_feature_ReservedRegister2843 .. nws.nws_cmu.ln3_feature_ReservedRegister2850 (8 regs) */
	0x061cb780, 	/* nws.nws_cmu.ln3_lt_tx_fsm_ctrl0 .. nws.nws_cmu.ln3_lt_tx_fsm_ctrl5 (6 regs) */
	0x011cb790, 	/* nws.nws_cmu.ln3_lt_tx_fsm_status (1 regs) */
	0x031cb793, 	/* nws.nws_cmu.ln3_lt_tx_prbs_ctrl0 .. nws.nws_cmu.ln3_lt_tx_prbs_ctrl2 (3 regs) */
	0x011cb7a0, 	/* nws.nws_cmu.ln3_lt_tx_coefficient_update_ctrl (1 regs) */
	0x011cb7a2, 	/* nws.nws_cmu.ln3_lt_tx_status_report_ctrl (1 regs) */
	0x021cb7b0, 	/* nws.nws_cmu.ln3_lt_tx_fsm_state_status0 .. nws.nws_cmu.ln3_lt_tx_fsm_state_status1 (2 regs) */
	0x011cb7c0, 	/* nws.nws_cmu.ln3_lt_rx_ctrl0 (1 regs) */
	0x021cb7c2, 	/* nws.nws_cmu.ln3_lt_rx_prbs_ctrl0 .. nws.nws_cmu.ln3_lt_rx_prbs_ctrl1 (2 regs) */
	0x031cb7c5, 	/* nws.nws_cmu.ln3_lt_rx_prbs_status0 .. nws.nws_cmu.ln3_lt_rx_prbs_status2 (3 regs) */
	0x011cb7d0, 	/* nws.nws_cmu.ln3_lt_rx_frame_ctrl (1 regs) */
	0x051cb7d3, 	/* nws.nws_cmu.ln3_lt_rx_frame_status0 .. nws.nws_cmu.ln3_lt_rx_frame_status4 (5 regs) */
	0x011cb7e0, 	/* nws.nws_cmu.ln3_lt_rx_coefficient_update_status (1 regs) */
	0x011cb7e2, 	/* nws.nws_cmu.ln3_lt_rx_report_status (1 regs) */
	0x014f008f, 	/* mode (!bb)&(!(emul_full|emul_reduced)), block nwm */
	0x01200002, 	/* nwm.INT_MASK (1 regs) */
	0x01510001, 	/* mode (!bb)&(!(fpga|(emul_full|emul_reduced))), block ms */
	0x011a8061, 	/* ms.INT_MASK (1 regs) */
	0x2c510055, 	/* mode (!bb)&(!(fpga|(!asic))), block ms */
	0x241a9000, 	/* ms.ms_cmu.ahb_cmu_csr_0_x0 .. ms.ms_cmu.ahb_cmu_csr_0_x35 (36 regs) */
	0x4f1a9038, 	/* ms.ms_cmu.ahb_cmu_csr_0_x56 .. ms.ms_cmu.ahb_cmu_csr_0_x134 (79 regs) */
	0x011a9090, 	/* ms.ms_cmu.ahb_cmu_csr_0_x144 (1 regs) */
	0x041a9092, 	/* ms.ms_cmu.ahb_cmu_csr_0_x146 .. ms.ms_cmu.ahb_cmu_csr_0_x149 (4 regs) */
	0x021a9099, 	/* ms.ms_cmu.ahb_cmu_csr_0_x153 .. ms.ms_cmu.ahb_cmu_csr_0_x154 (2 regs) */
	0x2a1a90a1, 	/* ms.ms_cmu.ahb_cmu_csr_0_x161 .. ms.ms_cmu.ahb_cmu_csr_0_x202 (42 regs) */
	0x011a90d2, 	/* ms.ms_cmu.ahb_cmu_csr_0_x210 (1 regs) */
	0x061a9400, 	/* ms.ms_cmu.ahb_lane_csr_1_x0 .. ms.ms_cmu.ahb_lane_csr_1_x5 (6 regs) */
	0x401a9407, 	/* ms.ms_cmu.ahb_lane_csr_1_x7 .. ms.ms_cmu.ahb_lane_csr_1_x70 (64 regs) */
	0x221a9448, 	/* ms.ms_cmu.ahb_lane_csr_1_x72 .. ms.ms_cmu.ahb_lane_csr_1_x105 (34 regs) */
	0x0e1a9473, 	/* ms.ms_cmu.ahb_lane_csr_1_x115 .. ms.ms_cmu.ahb_lane_csr_1_x128 (14 regs) */
	0x1b1a9482, 	/* ms.ms_cmu.ahb_lane_csr_1_x130 .. ms.ms_cmu.ahb_lane_csr_1_x156 (27 regs) */
	0x021a949e, 	/* ms.ms_cmu.ahb_lane_csr_1_x158 .. ms.ms_cmu.ahb_lane_csr_1_x159 (2 regs) */
	0x011a94a1, 	/* ms.ms_cmu.ahb_lane_csr_1_x161 (1 regs) */
	0x011a94a7, 	/* ms.ms_cmu.ahb_lane_csr_1_x167 (1 regs) */
	0x551a94c9, 	/* ms.ms_cmu.ahb_lane_csr_1_x201 .. ms.ms_cmu.ahb_lane_csr_1_x285 (85 regs) */
	0x081a952d, 	/* ms.ms_cmu.ahb_lane_csr_1_x301 .. ms.ms_cmu.ahb_lane_csr_1_x308 (8 regs) */
	0x011a9536, 	/* ms.ms_cmu.ahb_lane_csr_1_x310 (1 regs) */
	0x121a9539, 	/* ms.ms_cmu.ahb_lane_csr_1_ReservedReg53 .. ms.ms_cmu.ahb_lane_csr_1_x330 (18 regs) */
	0x021a9a00, 	/* ms.ms_cmu.ahb_comlane_csr_5_x0 .. ms.ms_cmu.ahb_comlane_csr_5_x1 (2 regs) */
	0x051a9a04, 	/* ms.ms_cmu.ahb_comlane_csr_5_x4 .. ms.ms_cmu.ahb_comlane_csr_5_x8 (5 regs) */
	0x011a9a1f, 	/* ms.ms_cmu.ahb_comlane_csr_5_x31 (1 regs) */
	0x031a9a2a, 	/* ms.ms_cmu.ahb_comlane_csr_5_ReservedReg17 .. ms.ms_cmu.ahb_comlane_csr_5_x44 (3 regs) */
	0x851a9a31, 	/* ms.ms_cmu.ahb_comlane_csr_5_x49 .. ms.ms_cmu.ahb_comlane_csr_5_x181 (133 regs) */
	0x341a9ad2, 	/* ms.ms_cmu.ahb_comlane_csr_5_x210 .. ms.ms_cmu.ahb_comlane_csr_5_x261 (52 regs) */
	0x0d1a9b07, 	/* ms.ms_cmu.ahb_comlane_csr_5_x263 .. ms.ms_cmu.ahb_comlane_csr_5_x275 (13 regs) */
	0x011a9b19, 	/* ms.ms_cmu.ahb_comlane_csr_5_x281 (1 regs) */
	0x021a9b28, 	/* ms.ms_cmu.ahb_comlane_csr_5_x296 .. ms.ms_cmu.ahb_comlane_csr_5_x297 (2 regs) */
	0x011a9b2d, 	/* ms.ms_cmu.ahb_comlane_csr_5_x301 (1 regs) */
	0x061a9b2f, 	/* ms.ms_cmu.ahb_comlane_csr_5_ReservedReg23 .. ms.ms_cmu.ahb_comlane_csr_5_x308 (6 regs) */
	0x061a9b36, 	/* ms.ms_cmu.ahb_comlane_csr_5_x310 .. ms.ms_cmu.ahb_comlane_csr_5_x315 (6 regs) */
	0x061a9b3d, 	/* ms.ms_cmu.ahb_comlane_csr_5_ReservedReg24 .. ms.ms_cmu.ahb_comlane_csr_5_x322 (6 regs) */
	0x471a9b44, 	/* ms.ms_cmu.ahb_comlane_csr_5_x324 .. ms.ms_cmu.ahb_comlane_csr_5_x394 (71 regs) */
	0x031a9b91, 	/* ms.ms_cmu.ahb_comlane_csr_5_x401 .. ms.ms_cmu.ahb_comlane_csr_5_x403 (3 regs) */
	0x041a9b96, 	/* ms.ms_cmu.ahb_comlane_csr_5_x406 .. ms.ms_cmu.ahb_comlane_csr_5_x409 (4 regs) */
	0x011a9b9c, 	/* ms.ms_cmu.ahb_comlane_csr_5_ReservedReg40 (1 regs) */
	0x361a9bbe, 	/* ms.ms_cmu.ahb_comlane_csr_5_x446 .. ms.ms_cmu.ahb_comlane_csr_5_x499 (54 regs) */
	0x241a9c00, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x0 .. ms.ms_cmu.ahb_cmu1_csr_6_x35 (36 regs) */
	0x4f1a9c38, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x56 .. ms.ms_cmu.ahb_cmu1_csr_6_x134 (79 regs) */
	0x011a9c90, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x144 (1 regs) */
	0x041a9c92, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x146 .. ms.ms_cmu.ahb_cmu1_csr_6_x149 (4 regs) */
	0x021a9c99, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x153 .. ms.ms_cmu.ahb_cmu1_csr_6_x154 (2 regs) */
	0x2a1a9ca1, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x161 .. ms.ms_cmu.ahb_cmu1_csr_6_x202 (42 regs) */
	0x011a9cd2, 	/* ms.ms_cmu.ahb_cmu1_csr_6_x210 (1 regs) */
	0x90520000, 	/* block phy_pcie */
	0x23188000, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x0 .. phy_pcie.phy0.ahb_cmu_csr_0_x34 (35 regs) */
	0x2f188038, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x56 .. phy_pcie.phy0.ahb_cmu_csr_0_x102 (47 regs) */
	0x2818806c, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x108 .. phy_pcie.phy0.ahb_cmu_csr_0_x147 (40 regs) */
	0x06188095, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x149 .. phy_pcie.phy0.ahb_cmu_csr_0_x154 (6 regs) */
	0x1c1880a1, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x161 .. phy_pcie.phy0.ahb_cmu_csr_0_x188 (28 regs) */
	0x141880bf, 	/* phy_pcie.phy0.ahb_cmu_csr_0_x191 .. phy_pcie.phy0.ahb_cmu_csr_0_x210 (20 regs) */
	0x06188200, 	/* phy_pcie.phy0.ahb_lane_csr_1_x0 .. phy_pcie.phy0.ahb_lane_csr_1_x5 (6 regs) */
	0x38188207, 	/* phy_pcie.phy0.ahb_lane_csr_1_x7 .. phy_pcie.phy0.ahb_lane_csr_1_x62 (56 regs) */
	0x21188241, 	/* phy_pcie.phy0.ahb_lane_csr_1_x65 .. phy_pcie.phy0.ahb_lane_csr_1_x97 (33 regs) */
	0x11188263, 	/* phy_pcie.phy0.ahb_lane_csr_1_x99 .. phy_pcie.phy0.ahb_lane_csr_1_x115 (17 regs) */
	0x0a188277, 	/* phy_pcie.phy0.ahb_lane_csr_1_x119 .. phy_pcie.phy0.ahb_lane_csr_1_x128 (10 regs) */
	0x29188282, 	/* phy_pcie.phy0.ahb_lane_csr_1_x130 .. phy_pcie.phy0.ahb_lane_csr_1_x170 (41 regs) */
	0x081882c9, 	/* phy_pcie.phy0.ahb_lane_csr_1_x201 .. phy_pcie.phy0.ahb_lane_csr_1_x208 (8 regs) */
	0x051882d5, 	/* phy_pcie.phy0.ahb_lane_csr_1_x213 .. phy_pcie.phy0.ahb_lane_csr_1_x217 (5 regs) */
	0x421882dc, 	/* phy_pcie.phy0.ahb_lane_csr_1_x220 .. phy_pcie.phy0.ahb_lane_csr_1_x285 (66 regs) */
	0x1b18832d, 	/* phy_pcie.phy0.ahb_lane_csr_1_x301 .. phy_pcie.phy0.ahb_lane_csr_1_x327 (27 regs) */
	0x0118834a, 	/* phy_pcie.phy0.ahb_lane_csr_1_x330 (1 regs) */
	0x06188400, 	/* phy_pcie.phy0.ahb_lane_csr_2_x0 .. phy_pcie.phy0.ahb_lane_csr_2_x5 (6 regs) */
	0x38188407, 	/* phy_pcie.phy0.ahb_lane_csr_2_x7 .. phy_pcie.phy0.ahb_lane_csr_2_x62 (56 regs) */
	0x21188441, 	/* phy_pcie.phy0.ahb_lane_csr_2_x65 .. phy_pcie.phy0.ahb_lane_csr_2_x97 (33 regs) */
	0x11188463, 	/* phy_pcie.phy0.ahb_lane_csr_2_x99 .. phy_pcie.phy0.ahb_lane_csr_2_x115 (17 regs) */
	0x0a188477, 	/* phy_pcie.phy0.ahb_lane_csr_2_x119 .. phy_pcie.phy0.ahb_lane_csr_2_x128 (10 regs) */
	0x29188482, 	/* phy_pcie.phy0.ahb_lane_csr_2_x130 .. phy_pcie.phy0.ahb_lane_csr_2_x170 (41 regs) */
	0x081884c9, 	/* phy_pcie.phy0.ahb_lane_csr_2_x201 .. phy_pcie.phy0.ahb_lane_csr_2_x208 (8 regs) */
	0x051884d5, 	/* phy_pcie.phy0.ahb_lane_csr_2_x213 .. phy_pcie.phy0.ahb_lane_csr_2_x217 (5 regs) */
	0x421884dc, 	/* phy_pcie.phy0.ahb_lane_csr_2_x220 .. phy_pcie.phy0.ahb_lane_csr_2_x285 (66 regs) */
	0x1b18852d, 	/* phy_pcie.phy0.ahb_lane_csr_2_x301 .. phy_pcie.phy0.ahb_lane_csr_2_x327 (27 regs) */
	0x0118854a, 	/* phy_pcie.phy0.ahb_lane_csr_2_x330 (1 regs) */
	0x06188600, 	/* phy_pcie.phy0.ahb_lane_csr_3_x0 .. phy_pcie.phy0.ahb_lane_csr_3_x5 (6 regs) */
	0x38188607, 	/* phy_pcie.phy0.ahb_lane_csr_3_x7 .. phy_pcie.phy0.ahb_lane_csr_3_x62 (56 regs) */
	0x21188641, 	/* phy_pcie.phy0.ahb_lane_csr_3_x65 .. phy_pcie.phy0.ahb_lane_csr_3_x97 (33 regs) */
	0x11188663, 	/* phy_pcie.phy0.ahb_lane_csr_3_x99 .. phy_pcie.phy0.ahb_lane_csr_3_x115 (17 regs) */
	0x0a188677, 	/* phy_pcie.phy0.ahb_lane_csr_3_x119 .. phy_pcie.phy0.ahb_lane_csr_3_x128 (10 regs) */
	0x29188682, 	/* phy_pcie.phy0.ahb_lane_csr_3_x130 .. phy_pcie.phy0.ahb_lane_csr_3_x170 (41 regs) */
	0x081886c9, 	/* phy_pcie.phy0.ahb_lane_csr_3_x201 .. phy_pcie.phy0.ahb_lane_csr_3_x208 (8 regs) */
	0x051886d5, 	/* phy_pcie.phy0.ahb_lane_csr_3_x213 .. phy_pcie.phy0.ahb_lane_csr_3_x217 (5 regs) */
	0x421886dc, 	/* phy_pcie.phy0.ahb_lane_csr_3_x220 .. phy_pcie.phy0.ahb_lane_csr_3_x285 (66 regs) */
	0x1b18872d, 	/* phy_pcie.phy0.ahb_lane_csr_3_x301 .. phy_pcie.phy0.ahb_lane_csr_3_x327 (27 regs) */
	0x0118874a, 	/* phy_pcie.phy0.ahb_lane_csr_3_x330 (1 regs) */
	0x06188800, 	/* phy_pcie.phy0.ahb_lane_csr_4_x0 .. phy_pcie.phy0.ahb_lane_csr_4_x5 (6 regs) */
	0x38188807, 	/* phy_pcie.phy0.ahb_lane_csr_4_x7 .. phy_pcie.phy0.ahb_lane_csr_4_x62 (56 regs) */
	0x21188841, 	/* phy_pcie.phy0.ahb_lane_csr_4_x65 .. phy_pcie.phy0.ahb_lane_csr_4_x97 (33 regs) */
	0x11188863, 	/* phy_pcie.phy0.ahb_lane_csr_4_x99 .. phy_pcie.phy0.ahb_lane_csr_4_x115 (17 regs) */
	0x0a188877, 	/* phy_pcie.phy0.ahb_lane_csr_4_x119 .. phy_pcie.phy0.ahb_lane_csr_4_x128 (10 regs) */
	0x29188882, 	/* phy_pcie.phy0.ahb_lane_csr_4_x130 .. phy_pcie.phy0.ahb_lane_csr_4_x170 (41 regs) */
	0x081888c9, 	/* phy_pcie.phy0.ahb_lane_csr_4_x201 .. phy_pcie.phy0.ahb_lane_csr_4_x208 (8 regs) */
	0x051888d5, 	/* phy_pcie.phy0.ahb_lane_csr_4_x213 .. phy_pcie.phy0.ahb_lane_csr_4_x217 (5 regs) */
	0x421888dc, 	/* phy_pcie.phy0.ahb_lane_csr_4_x220 .. phy_pcie.phy0.ahb_lane_csr_4_x285 (66 regs) */
	0x1b18892d, 	/* phy_pcie.phy0.ahb_lane_csr_4_x301 .. phy_pcie.phy0.ahb_lane_csr_4_x327 (27 regs) */
	0x0118894a, 	/* phy_pcie.phy0.ahb_lane_csr_4_x330 (1 regs) */
	0x05188a00, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x0 .. phy_pcie.phy0.ahb_comlane_csr_5_x4 (5 regs) */
	0x15188a06, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x6 .. phy_pcie.phy0.ahb_comlane_csr_5_x26 (21 regs) */
	0x07188a1d, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x29 .. phy_pcie.phy0.ahb_comlane_csr_5_x35 (7 regs) */
	0x04188a26, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x38 .. phy_pcie.phy0.ahb_comlane_csr_5_x41 (4 regs) */
	0x78188a2b, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x43 .. phy_pcie.phy0.ahb_comlane_csr_5_x162 (120 regs) */
	0x5e188aa8, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x168 .. phy_pcie.phy0.ahb_comlane_csr_5_x261 (94 regs) */
	0x23188b07, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x263 .. phy_pcie.phy0.ahb_comlane_csr_5_x297 (35 regs) */
	0x01188b2d, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x301 (1 regs) */
	0x05188b30, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x304 .. phy_pcie.phy0.ahb_comlane_csr_5_x308 (5 regs) */
	0x06188b36, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x310 .. phy_pcie.phy0.ahb_comlane_csr_5_x315 (6 regs) */
	0x05188b3e, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x318 .. phy_pcie.phy0.ahb_comlane_csr_5_x322 (5 regs) */
	0x0a188b44, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x324 .. phy_pcie.phy0.ahb_comlane_csr_5_x333 (10 regs) */
	0x02188b52, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x338 .. phy_pcie.phy0.ahb_comlane_csr_5_x339 (2 regs) */
	0x02188b55, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x341 .. phy_pcie.phy0.ahb_comlane_csr_5_x342 (2 regs) */
	0x01188b58, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x344 (1 regs) */
	0x0a188b5a, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x346 .. phy_pcie.phy0.ahb_comlane_csr_5_x355 (10 regs) */
	0x01188b66, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x358 (1 regs) */
	0x0c188b6a, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x362 .. phy_pcie.phy0.ahb_comlane_csr_5_x373 (12 regs) */
	0x11188b78, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x376 .. phy_pcie.phy0.ahb_comlane_csr_5_x392 (17 regs) */
	0x01188b8a, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x394 (1 regs) */
	0x0b188b91, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x401 .. phy_pcie.phy0.ahb_comlane_csr_5_x411 (11 regs) */
	0x57188b9d, 	/* phy_pcie.phy0.ahb_comlane_csr_5_x413 .. phy_pcie.phy0.ahb_comlane_csr_5_x499 (87 regs) */
	0x23189000, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x0 .. phy_pcie.phy1.ahb_cmu_csr_0_x34 (35 regs) */
	0x2f189038, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x56 .. phy_pcie.phy1.ahb_cmu_csr_0_x102 (47 regs) */
	0x2818906c, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x108 .. phy_pcie.phy1.ahb_cmu_csr_0_x147 (40 regs) */
	0x06189095, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x149 .. phy_pcie.phy1.ahb_cmu_csr_0_x154 (6 regs) */
	0x1c1890a1, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x161 .. phy_pcie.phy1.ahb_cmu_csr_0_x188 (28 regs) */
	0x141890bf, 	/* phy_pcie.phy1.ahb_cmu_csr_0_x191 .. phy_pcie.phy1.ahb_cmu_csr_0_x210 (20 regs) */
	0x06189200, 	/* phy_pcie.phy1.ahb_lane_csr_1_x0 .. phy_pcie.phy1.ahb_lane_csr_1_x5 (6 regs) */
	0x38189207, 	/* phy_pcie.phy1.ahb_lane_csr_1_x7 .. phy_pcie.phy1.ahb_lane_csr_1_x62 (56 regs) */
	0x21189241, 	/* phy_pcie.phy1.ahb_lane_csr_1_x65 .. phy_pcie.phy1.ahb_lane_csr_1_x97 (33 regs) */
	0x11189263, 	/* phy_pcie.phy1.ahb_lane_csr_1_x99 .. phy_pcie.phy1.ahb_lane_csr_1_x115 (17 regs) */
	0x0a189277, 	/* phy_pcie.phy1.ahb_lane_csr_1_x119 .. phy_pcie.phy1.ahb_lane_csr_1_x128 (10 regs) */
	0x29189282, 	/* phy_pcie.phy1.ahb_lane_csr_1_x130 .. phy_pcie.phy1.ahb_lane_csr_1_x170 (41 regs) */
	0x081892c9, 	/* phy_pcie.phy1.ahb_lane_csr_1_x201 .. phy_pcie.phy1.ahb_lane_csr_1_x208 (8 regs) */
	0x051892d5, 	/* phy_pcie.phy1.ahb_lane_csr_1_x213 .. phy_pcie.phy1.ahb_lane_csr_1_x217 (5 regs) */
	0x421892dc, 	/* phy_pcie.phy1.ahb_lane_csr_1_x220 .. phy_pcie.phy1.ahb_lane_csr_1_x285 (66 regs) */
	0x1b18932d, 	/* phy_pcie.phy1.ahb_lane_csr_1_x301 .. phy_pcie.phy1.ahb_lane_csr_1_x327 (27 regs) */
	0x0118934a, 	/* phy_pcie.phy1.ahb_lane_csr_1_x330 (1 regs) */
	0x06189400, 	/* phy_pcie.phy1.ahb_lane_csr_2_x0 .. phy_pcie.phy1.ahb_lane_csr_2_x5 (6 regs) */
	0x38189407, 	/* phy_pcie.phy1.ahb_lane_csr_2_x7 .. phy_pcie.phy1.ahb_lane_csr_2_x62 (56 regs) */
	0x21189441, 	/* phy_pcie.phy1.ahb_lane_csr_2_x65 .. phy_pcie.phy1.ahb_lane_csr_2_x97 (33 regs) */
	0x11189463, 	/* phy_pcie.phy1.ahb_lane_csr_2_x99 .. phy_pcie.phy1.ahb_lane_csr_2_x115 (17 regs) */
	0x0a189477, 	/* phy_pcie.phy1.ahb_lane_csr_2_x119 .. phy_pcie.phy1.ahb_lane_csr_2_x128 (10 regs) */
	0x29189482, 	/* phy_pcie.phy1.ahb_lane_csr_2_x130 .. phy_pcie.phy1.ahb_lane_csr_2_x170 (41 regs) */
	0x081894c9, 	/* phy_pcie.phy1.ahb_lane_csr_2_x201 .. phy_pcie.phy1.ahb_lane_csr_2_x208 (8 regs) */
	0x051894d5, 	/* phy_pcie.phy1.ahb_lane_csr_2_x213 .. phy_pcie.phy1.ahb_lane_csr_2_x217 (5 regs) */
	0x421894dc, 	/* phy_pcie.phy1.ahb_lane_csr_2_x220 .. phy_pcie.phy1.ahb_lane_csr_2_x285 (66 regs) */
	0x1b18952d, 	/* phy_pcie.phy1.ahb_lane_csr_2_x301 .. phy_pcie.phy1.ahb_lane_csr_2_x327 (27 regs) */
	0x0118954a, 	/* phy_pcie.phy1.ahb_lane_csr_2_x330 (1 regs) */
	0x06189600, 	/* phy_pcie.phy1.ahb_lane_csr_3_x0 .. phy_pcie.phy1.ahb_lane_csr_3_x5 (6 regs) */
	0x38189607, 	/* phy_pcie.phy1.ahb_lane_csr_3_x7 .. phy_pcie.phy1.ahb_lane_csr_3_x62 (56 regs) */
	0x21189641, 	/* phy_pcie.phy1.ahb_lane_csr_3_x65 .. phy_pcie.phy1.ahb_lane_csr_3_x97 (33 regs) */
	0x11189663, 	/* phy_pcie.phy1.ahb_lane_csr_3_x99 .. phy_pcie.phy1.ahb_lane_csr_3_x115 (17 regs) */
	0x0a189677, 	/* phy_pcie.phy1.ahb_lane_csr_3_x119 .. phy_pcie.phy1.ahb_lane_csr_3_x128 (10 regs) */
	0x29189682, 	/* phy_pcie.phy1.ahb_lane_csr_3_x130 .. phy_pcie.phy1.ahb_lane_csr_3_x170 (41 regs) */
	0x081896c9, 	/* phy_pcie.phy1.ahb_lane_csr_3_x201 .. phy_pcie.phy1.ahb_lane_csr_3_x208 (8 regs) */
	0x051896d5, 	/* phy_pcie.phy1.ahb_lane_csr_3_x213 .. phy_pcie.phy1.ahb_lane_csr_3_x217 (5 regs) */
	0x421896dc, 	/* phy_pcie.phy1.ahb_lane_csr_3_x220 .. phy_pcie.phy1.ahb_lane_csr_3_x285 (66 regs) */
	0x1b18972d, 	/* phy_pcie.phy1.ahb_lane_csr_3_x301 .. phy_pcie.phy1.ahb_lane_csr_3_x327 (27 regs) */
	0x0118974a, 	/* phy_pcie.phy1.ahb_lane_csr_3_x330 (1 regs) */
	0x06189800, 	/* phy_pcie.phy1.ahb_lane_csr_4_x0 .. phy_pcie.phy1.ahb_lane_csr_4_x5 (6 regs) */
	0x38189807, 	/* phy_pcie.phy1.ahb_lane_csr_4_x7 .. phy_pcie.phy1.ahb_lane_csr_4_x62 (56 regs) */
	0x21189841, 	/* phy_pcie.phy1.ahb_lane_csr_4_x65 .. phy_pcie.phy1.ahb_lane_csr_4_x97 (33 regs) */
	0x11189863, 	/* phy_pcie.phy1.ahb_lane_csr_4_x99 .. phy_pcie.phy1.ahb_lane_csr_4_x115 (17 regs) */
	0x0a189877, 	/* phy_pcie.phy1.ahb_lane_csr_4_x119 .. phy_pcie.phy1.ahb_lane_csr_4_x128 (10 regs) */
	0x29189882, 	/* phy_pcie.phy1.ahb_lane_csr_4_x130 .. phy_pcie.phy1.ahb_lane_csr_4_x170 (41 regs) */
	0x081898c9, 	/* phy_pcie.phy1.ahb_lane_csr_4_x201 .. phy_pcie.phy1.ahb_lane_csr_4_x208 (8 regs) */
	0x051898d5, 	/* phy_pcie.phy1.ahb_lane_csr_4_x213 .. phy_pcie.phy1.ahb_lane_csr_4_x217 (5 regs) */
	0x421898dc, 	/* phy_pcie.phy1.ahb_lane_csr_4_x220 .. phy_pcie.phy1.ahb_lane_csr_4_x285 (66 regs) */
	0x1b18992d, 	/* phy_pcie.phy1.ahb_lane_csr_4_x301 .. phy_pcie.phy1.ahb_lane_csr_4_x327 (27 regs) */
	0x0118994a, 	/* phy_pcie.phy1.ahb_lane_csr_4_x330 (1 regs) */
	0x05189a00, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x0 .. phy_pcie.phy1.ahb_comlane_csr_5_x4 (5 regs) */
	0x15189a06, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x6 .. phy_pcie.phy1.ahb_comlane_csr_5_x26 (21 regs) */
	0x07189a1d, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x29 .. phy_pcie.phy1.ahb_comlane_csr_5_x35 (7 regs) */
	0x04189a26, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x38 .. phy_pcie.phy1.ahb_comlane_csr_5_x41 (4 regs) */
	0x78189a2b, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x43 .. phy_pcie.phy1.ahb_comlane_csr_5_x162 (120 regs) */
	0x5e189aa8, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x168 .. phy_pcie.phy1.ahb_comlane_csr_5_x261 (94 regs) */
	0x23189b07, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x263 .. phy_pcie.phy1.ahb_comlane_csr_5_x297 (35 regs) */
	0x01189b2d, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x301 (1 regs) */
	0x05189b30, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x304 .. phy_pcie.phy1.ahb_comlane_csr_5_x308 (5 regs) */
	0x06189b36, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x310 .. phy_pcie.phy1.ahb_comlane_csr_5_x315 (6 regs) */
	0x05189b3e, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x318 .. phy_pcie.phy1.ahb_comlane_csr_5_x322 (5 regs) */
	0x0a189b44, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x324 .. phy_pcie.phy1.ahb_comlane_csr_5_x333 (10 regs) */
	0x02189b52, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x338 .. phy_pcie.phy1.ahb_comlane_csr_5_x339 (2 regs) */
	0x02189b55, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x341 .. phy_pcie.phy1.ahb_comlane_csr_5_x342 (2 regs) */
	0x01189b58, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x344 (1 regs) */
	0x0a189b5a, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x346 .. phy_pcie.phy1.ahb_comlane_csr_5_x355 (10 regs) */
	0x01189b66, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x358 (1 regs) */
	0x0c189b6a, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x362 .. phy_pcie.phy1.ahb_comlane_csr_5_x373 (12 regs) */
	0x11189b78, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x376 .. phy_pcie.phy1.ahb_comlane_csr_5_x392 (17 regs) */
	0x01189b8a, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x394 (1 regs) */
	0x0b189b91, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x401 .. phy_pcie.phy1.ahb_comlane_csr_5_x411 (11 regs) */
	0x57189b9d, 	/* phy_pcie.phy1.ahb_comlane_csr_5_x413 .. phy_pcie.phy1.ahb_comlane_csr_5_x499 (87 regs) */
	0x01000080, 	/* split PORT */
	0x03060000, 	/* block cpmu */
	0x0700c080, 	/* cpmu.lpi_mode_config .. cpmu.sw_force_lpi (7 regs) */
	0x0600c0c9, 	/* cpmu.lpi_tx_req_stat_ro .. cpmu.lpi_duration_stat_ro (6 regs) */
	0x0600c0df, 	/* cpmu.lpi_tx_req_stat .. cpmu.lpi_duration_stat (6 regs) */
	0x02200000, 	/* block dorq */
	0x0104013f, 	/* dorq.wake_misc_en (1 regs) */
	0x08040221, 	/* dorq.tag1_ethertype .. dorq.tag4_size (8 regs) */
	0x01210000, 	/* block brb */
	0x010d0201, 	/* brb.header_size (1 regs) */
	0x0e230000, 	/* block prs */
	0x0107c11d, 	/* prs.t_tag_tagnum (1 regs) */
	0x5607c144, 	/* prs.ets_packet_additional_network_size .. prs.wfq_port_arb_current_credit (86 regs) */
	0x0107c1c0, 	/* prs.prop_hdr_size (1 regs) */
	0x0107c1cc, 	/* prs.encapsulation_type_en (1 regs) */
	0x0107c1ce, 	/* prs.vxlan_port (1 regs) */
	0x1007c1df, 	/* prs.first_hdr_hdrs_after_basic .. prs.inner_hdr_must_have_hdrs (16 regs) */
	0x2007c1fa, 	/* prs.src_mac_0_0 .. prs.src_mac_15_1 (32 regs) */
	0x0107c21b, 	/* prs.nge_port (1 regs) */
	0x0207c21d, 	/* prs.rroce_enable .. prs.nge_comp_ver (2 regs) */
	0x0107c250, 	/* prs.no_match_pfid (1 regs) */
	0x0307c284, 	/* prs.classify_failed_pkt_len_stat_add_crc .. prs.classify_failed_pkt_len_stat_tags_not_counted_first (3 regs) */
	0x1307c288, 	/* prs.nig_classify_failed .. prs.ignore_udp_zero_checksum (19 regs) */
	0x0907c2c2, 	/* prs.num_of_packets_0 .. prs.num_of_packets_8 (9 regs) */
	0x0207c2d5, 	/* prs.queue_pkt_avail_status .. prs.storm_bkprs_status (2 regs) */
	0x073c0000, 	/* block pbf */
	0x0636012c, 	/* pbf.first_hdr_hdrs_after_basic .. pbf.first_hdr_hdrs_after_tag_3 (6 regs) */
	0x07360134, 	/* pbf.first_hdr_must_have_hdrs .. pbf.inner_hdr_hdrs_after_tag_3 (7 regs) */
	0x0136013d, 	/* pbf.inner_hdr_must_have_hdrs (1 regs) */
	0x02360146, 	/* pbf.vxlan_port .. pbf.nge_port (2 regs) */
	0x01360149, 	/* pbf.nge_comp_ver (1 regs) */
	0x01360162, 	/* pbf.t_tag_tagnum (1 regs) */
	0x05360170, 	/* pbf.btb_shared_area_size .. pbf.num_strict_priority_slots (5 regs) */
	0x2e4b0000, 	/* block nig */
	0x05140384, 	/* nig.mac_in_en .. nig.flowctrl_out_en (5 regs) */
	0x01140402, 	/* nig.rx_pkt_has_fcs (1 regs) */
	0x03140404, 	/* nig.llc_jumbo_type .. nig.first_hdr_hdrs_after_basic (3 regs) */
	0x07140408, 	/* nig.first_hdr_hdrs_after_tag_0 .. nig.inner_hdr_hdrs_after_basic (7 regs) */
	0x08140410, 	/* nig.inner_hdr_hdrs_after_tag_0 .. nig.vxlan_ctrl (8 regs) */
	0x2c140422, 	/* nig.llh_dest_mac_0_0 .. nig.rx_llh_svol_mcp_mask (44 regs) */
	0x04140450, 	/* nig.rx_llh_ncsi_brb_dntfwd_mask .. nig.rx_llh_svol_brb_dntfwd_mask (4 regs) */
	0x26140456, 	/* nig.l2filt_ethertype0 .. nig.rx_llh_brb_gate_dntfwd (38 regs) */
	0x0514047e, 	/* nig.storm_ethertype0 .. nig.rx_llh_storm_mask (5 regs) */
	0x061404c2, 	/* nig.rx_llh_dfifo_empty .. nig.rx_llh_rfifo_full (6 regs) */
	0x01140500, 	/* nig.storm_status (1 regs) */
	0x33140540, 	/* nig.lb_min_cyc_threshold .. nig.lb_arb_num_strict_arb_slots (51 regs) */
	0x02940574, 	/* nig.lb_arb_priority_client (2 regs, WB) */
	0x22140576, 	/* nig.lb_arb_burst_mode .. nig.lb_llh_brb_gate_dntfwd (34 regs) */
	0x0f1405d9, 	/* nig.lb_btb_fifo_alm_full_thr .. nig.lb_llh_rfifo_alm_full (15 regs) */
	0x01140600, 	/* nig.lb_llh_rfifo_full (1 regs) */
	0x29140640, 	/* nig.rx_ptp_en .. nig.outer_tag_value_mask (41 regs) */
	0x2d1406e8, 	/* nig.flowctrl_mode .. nig.rx_flowctrl_status_clear (45 regs) */
	0x08140719, 	/* nig.stat_rx_brb_packet_priority_0 .. nig.stat_rx_brb_packet_priority_7 (8 regs) */
	0x22140728, 	/* nig.stat_rx_brb_octet_priority_0 .. nig.stat_rx_brb_discard_priority_7 (34 regs) */
	0x0214074b, 	/* nig.stat_rx_storm_packet_discard .. nig.stat_rx_storm_packet_truncate (2 regs) */
	0x46140750, 	/* nig.stat_lb_brb_packet_priority_0 .. nig.stat_tx_octet_tc_7 (70 regs) */
	0x20940796, 	/* nig.tx_xoff_cyc_tc_0 .. nig.lb_xoff_cyc_tc_7 (32 regs, WB) */
	0x1a1407b6, 	/* nig.stat_rx_bmb_octet .. nig.tx_arb_num_strict_arb_slots (26 regs) */
	0x029407d0, 	/* nig.tx_arb_priority_client (2 regs, WB) */
	0x3d1407d2, 	/* nig.tx_arb_burst_mode .. nig.tx_gnt_fifo_full (61 regs) */
	0x02140825, 	/* nig.mng_tc .. nig.tx_mng_tc_en (2 regs) */
	0x03140828, 	/* nig.tx_mng_timestamp_pkt .. nig.bmb_pkt_len (3 regs) */
	0x0414082c, 	/* nig.tx_bmb_fifo_empty .. nig.lb_bmb_fifo_full (4 regs) */
	0x01140835, 	/* nig.debug_pkt_len (1 regs) */
	0x05140850, 	/* nig.dbg_select .. nig.dbg_force_frame (5 regs) */
	0x0f140862, 	/* nig.rx_fc_dbg_select .. nig.lb_fc_dbg_force_frame (15 regs) */
	0x01140881, 	/* nig.eco_reserved_perport (1 regs) */
	0x0214220a, 	/* nig.tx_ptp_one_stp_en .. nig.rx_ptp_one_stp_en (2 regs) */
	0x01142214, 	/* nig.add_freecnt_offset (1 regs) */
	0x0214222c, 	/* nig.tsgen_offset_value_lsb .. nig.tsgen_offset_value_msb (2 regs) */
	0x04142230, 	/* nig.tsgen_sync_time_lsb .. nig.tsgen_sw_pps_en (4 regs) */
	0x02142236, 	/* nig.tsgen_rst_drift_cntr .. nig.tsgen_drift_cntr_conf (2 regs) */
	0x0214223c, 	/* nig.roce_duplicate_to_host .. nig.default_engine_id_sel (2 regs) */
	0x43142280, 	/* nig.dscp_to_tc_map .. nig.rroce_zero_udp_ignore (67 regs) */
	0x011422c8, 	/* nig.add_eth_crc (1 regs) */
	0x031422ca, 	/* nig.nge_ip_enable .. nig.nge_comp_ver (3 regs) */
	0x021422ce, 	/* nig.nge_port .. nig.llh_lb_tc_remap (2 regs) */
	0x051422d2, 	/* nig.rx_llh_ncsi_mcp_mask_2 .. nig.tx_llh_ncsi_ntwk_mask_2 (5 regs) */
	0x071422d8, 	/* nig.timer_counter .. nig.tx_parity_error_close_egress (7 regs) */
	0x031422e0, 	/* nig.tx_arb_client_0_map .. nig.lb_arb_client_0_map (3 regs) */
	0x044b0003, 	/* mode !bb, block nig */
	0x02142407, 	/* nig.tsgen_tsio_in_sel_mask .. nig.tsgen_tsio_in_sel_pol (2 regs) */
	0x0494240a, 	/* nig.tsgen_tsio_in_latched_value .. nig.tsgen_tsio_out_next_toggle_time (4 regs, WB) */
	0x0514240e, 	/* nig.tsgen_pps_start_time_0 .. nig.ptp_update_sw_osts_pkt_time (5 regs) */
	0x04142416, 	/* nig.llh_dest_mac_6_0 .. nig.llh_dest_mac_7_1 (4 regs) */
	0x024d0000, 	/* block bmbn */
	0x06184078, 	/* bmbn.mng_outer_tag0_0 .. bmbn.mng_inner_vlan_tag1 (6 regs) */
	0x01184081, 	/* bmbn.eco_reserved_perport (1 regs) */
	0x01530000, 	/* block led */
	0x061ae000, 	/* led.control .. led.mac_led_speed (6 regs) */
	0x024b0015, 	/* mode !(bb|k2), block nig */
	0x02143420, 	/* nig.mng_to_mcp_ncsi_filter .. nig.mng_to_mcp_ncsi_filter_2 (2 regs) */
	0x02143601, 	/* nig.tx_order_fifo_full .. nig.lb_order_fifo_full (2 regs) */
	0x01050047, 	/* mode !(k2|e5), block cnig */
	0x0608608f, 	/* cnig.led_control .. cnig.mac_led_speed (6 regs) */
	0x144b0000, 	/* block nig */
	0x02140391, 	/* nig.rx_macfifo_empty .. nig.rx_macfifo_full (2 regs) */
	0x01140400, 	/* nig.hdr_skip_size (1 regs) */
	0x01140407, 	/* nig.first_hdr_hdrs_after_llc (1 regs) */
	0x0114040f, 	/* nig.inner_hdr_hdrs_after_llc (1 regs) */
	0x03140418, 	/* nig.ipv4_type .. nig.fcoe_type (3 regs) */
	0x0414041d, 	/* nig.tcp_protocol .. nig.icmpv6_protocol (4 regs) */
	0x1614080f, 	/* nig.mng_outer_tag0_0 .. nig.mng_prop_hdr1_7 (22 regs) */
	0x01140827, 	/* nig.tx_host_mng_enable (1 regs) */
	0x02142002, 	/* nig.mf_global_en .. nig.upon_mgmt (2 regs) */
	0x04142070, 	/* nig.wake_buffer_clear .. nig.wake_details (4 regs) */
	0x02142208, 	/* nig.tx_up_ts_en .. nig.rx_up_ts_en (2 regs) */
	0x0814220c, 	/* nig.tx_up_ts_addr_0 .. nig.rx_enable_up_rules (8 regs) */
	0x01142215, 	/* nig.up_ts_insert_en (1 regs) */
	0x0114221f, 	/* nig.llh_up_buf_seqid (1 regs) */
	0x04942220, 	/* nig.llh_up_buf_timestamp .. nig.llh_up_buf_src_addr (4 regs, WB) */
	0x01142224, 	/* nig.tx_llh_up_buf_seqid (1 regs) */
	0x04942226, 	/* nig.tx_llh_up_buf_timestamp .. nig.llh_up_buf_dst_addr (4 regs, WB) */
	0x0114223b, 	/* nig.mld_msg_type (1 regs) */
	0x041422c4, 	/* nig.acpi_tag_remove .. nig.rm_eth_crc (4 regs) */
	0x011422c9, 	/* nig.corrupt_eth_crc (1 regs) */
	0x033c0083, 	/* mode !e5, block pbf */
	0x02360132, 	/* pbf.first_hdr_hdrs_after_tag_4 .. pbf.first_hdr_hdrs_after_tag_5 (2 regs) */
	0x0236013b, 	/* pbf.inner_hdr_hdrs_after_tag_4 .. pbf.inner_hdr_hdrs_after_tag_5 (2 regs) */
	0x01360160, 	/* pbf.prop_hdr_size (1 regs) */
	0x034b0000, 	/* block nig */
	0x01140390, 	/* nig.tx_macfifo_alm_full_thr (1 regs) */
	0x02140393, 	/* nig.tx_macfifo_alm_full .. nig.tx_macfifo_empty (2 regs) */
	0x011403c0, 	/* nig.tx_macfifo_full (1 regs) */
	0x044c013d, 	/* mode (!bb)&(!fpga), block wol */
	0x02182000, 	/* wol.acpi_tag_rm .. wol.upon_mgmt (2 regs) */
	0x06182060, 	/* wol.wake_buffer_clear .. wol.acpi_pat_sel (6 regs) */
	0x0118206d, 	/* wol.wake_mem_rd_offset (1 regs) */
	0x0418206f, 	/* wol.eco_reserved_perport .. wol.hdr_fifo_error (4 regs) */
	0x0200010b, 	/* split PF */
	0x01010000, 	/* block miscs */
	0x010025d7, 	/* miscs.unprepared_dr (1 regs) */
	0x05040000, 	/* block pglue_b */
	0x040aa136, 	/* pglue_b.pseudo_vf_master_enable .. pglue_b.vf_base (4 regs) */
	0x030aa85b, 	/* pglue_b.internal_pfid_enable_master .. pglue_b.internal_pfid_enable_target_read (3 regs) */
	0x010aa950, 	/* pglue_b.pf_trusted (1 regs) */
	0x010aa965, 	/* pglue_b.mask_block_discard_attn_pf (1 regs) */
	0x040aab97, 	/* pglue_b.config_reg_78 .. pglue_b.vf_bar1_size (4 regs) */
	0x02140000, 	/* block pswrq2 */
	0x1b090003, 	/* pswrq2.cdut_p_size .. pswrq2.dbg_last_ilt (27 regs) */
	0x0b090024, 	/* pswrq2.tm_number_of_pf_blocks .. pswrq2.vf_last_ilt (11 regs) */
	0x01170000, 	/* block ptu */
	0x0515801e, 	/* ptu.inv_tid .. ptu.inv_halt_on_err (5 regs) */
	0x01180000, 	/* block tcm */
	0x04460241, 	/* tcm.con_phy_q0 .. tcm.task_phy_q1 (4 regs) */
	0x01190000, 	/* block mcm */
	0x04480241, 	/* mcm.con_phy_q0 .. mcm.task_phy_q1 (4 regs) */
	0x011a0000, 	/* block ucm */
	0x044a0241, 	/* ucm.con_phy_q0 .. ucm.task_phy_q1 (4 regs) */
	0x011b0000, 	/* block xcm */
	0x01400241, 	/* xcm.con_phy_q3 (1 regs) */
	0x011c0000, 	/* block ycm */
	0x04420241, 	/* ycm.con_phy_q0 .. ycm.task_phy_q1 (4 regs) */
	0x041e0000, 	/* block qm */
	0x030bc10d, 	/* qm.MaxPqSize_0 .. qm.MaxPqSize_2 (3 regs) */
	0x010bc54c, 	/* qm.PciReqTph (1 regs) */
	0x020bcba8, 	/* qm.pci_rd_err .. qm.pf_en (2 regs) */
	0x020bcbab, 	/* qm.usg_cnt_pf_tx .. qm.usg_cnt_pf_other (2 regs) */
	0x031f0000, 	/* block tm */
	0x010b010f, 	/* tm.pf_enable_conn (1 regs) */
	0x010b0111, 	/* tm.pf_enable_task (1 regs) */
	0x020b013f, 	/* tm.pf_scan_active_conn .. tm.pf_scan_active_task (2 regs) */
	0x07200000, 	/* block dorq */
	0x02040100, 	/* dorq.pf_min_addr_reg1 .. dorq.vf_min_addr_reg1 (2 regs) */
	0x06040112, 	/* dorq.pf_icid_bit_shift_norm .. dorq.vf_min_val_dpi (6 regs) */
	0x03040140, 	/* dorq.pf_net_port_id .. dorq.pf_db_enable (3 regs) */
	0x02040144, 	/* dorq.pf_dpm_enable .. dorq.vf_dpm_enable (2 regs) */
	0x0404022d, 	/* dorq.tag1_ovrd_mode .. dorq.tag4_ovrd_mode (4 regs) */
	0x01040270, 	/* dorq.pf_usage_cnt (1 regs) */
	0x03040272, 	/* dorq.pf_usage_cnt_lim .. dorq.pf_ovfl_sticky (3 regs) */
	0x05220000, 	/* block src */
	0x0208e127, 	/* src.NumIpv4Conn .. src.NumIpv6Conn (2 regs) */
	0x0288e140, 	/* src.FirstFree (2 regs, WB) */
	0x0288e148, 	/* src.LastFree (2 regs, WB) */
	0x0108e150, 	/* src.CountFree (1 regs) */
	0x0108e181, 	/* src.number_hash_bits (1 regs) */
	0x11230000, 	/* block prs */
	0x0407c05a, 	/* prs.task_id_max_initiator_pf .. prs.task_id_max_target_vf (4 regs) */
	0x0107c064, 	/* prs.roce_separate_rx_tx_cid_flg (1 regs) */
	0x0107c066, 	/* prs.load_l2_filter (1 regs) */
	0x0207c068, 	/* prs.target_initiator_select .. prs.fcoe_search_with_exchange_context (2 regs) */
	0x0407c100, 	/* prs.search_tcp .. prs.search_roce (4 regs) */
	0x0507c105, 	/* prs.tcp_search_key_mask .. prs.roce_build_cid_wo_search (5 regs) */
	0x0907c10b, 	/* prs.roce_dest_qp_max_vf .. prs.search_tenant_id (9 regs) */
	0x0207c1cf, 	/* prs.roce_icid_base_pf .. prs.roce_icid_base_vf (2 regs) */
	0x0507c1f5, 	/* prs.first_hdr_dst_ip_0 .. prs.first_hdr_dst_ip_4 (5 regs) */
	0x0307c251, 	/* prs.override_pfid_if_no_match .. prs.no_match_lcid (3 regs) */
	0x0207c25a, 	/* prs.light_l2_ethertype_en .. prs.use_light_l2 (2 regs) */
	0x0107c26f, 	/* prs.mac_vlan_cache_use_tenant_id (1 regs) */
	0x0107c274, 	/* prs.sort_sack (1 regs) */
	0x0107c276, 	/* prs.rdma_syn_mask (1 regs) */
	0x0507c27f, 	/* prs.rdma_syn_cookie_en .. prs.pkt_len_stat_tags_not_counted_first (5 regs) */
	0x0107c287, 	/* prs.msg_info (1 regs) */
	0x0207c46f, 	/* prs.search_gft .. prs.search_non_ip_as_gft (2 regs) */
	0x013f0000, 	/* block cdu */
	0x0a160243, 	/* cdu.pf_seg0_type_offset .. cdu.vf_fl_seg_type_offset (10 regs) */
	0x06400000, 	/* block ccfc */
	0x010b8176, 	/* ccfc.robustwb_pf (1 regs) */
	0x010b81c0, 	/* ccfc.weak_enable_pf (1 regs) */
	0x010b81c2, 	/* ccfc.strong_enable_pf (1 regs) */
	0x010b81c6, 	/* ccfc.pf_minicache_enable (1 regs) */
	0x010bb403, 	/* ccfc.pf_lstate_cnt1 (1 regs) */
	0x010bb405, 	/* ccfc.pf_lstate_cnt2 (1 regs) */
	0x06410000, 	/* block tcfc */
	0x010b4176, 	/* tcfc.robustwb_pf (1 regs) */
	0x010b41c0, 	/* tcfc.weak_enable_pf (1 regs) */
	0x010b41c2, 	/* tcfc.strong_enable_pf (1 regs) */
	0x010b41c6, 	/* tcfc.pf_minicache_enable (1 regs) */
	0x010b7403, 	/* tcfc.pf_lstate_cnt1 (1 regs) */
	0x010b7405, 	/* tcfc.pf_lstate_cnt2 (1 regs) */
	0x08420000, 	/* block igu */
	0x02060100, 	/* igu.statistic_num_pf_msg_sent (2 regs) */
	0x0106010b, 	/* igu.statistic_num_of_inta_asserted (1 regs) */
	0x01060200, 	/* igu.pf_configuration (1 regs) */
	0x0a060208, 	/* igu.attn_msg_addr_l .. igu.command_reg_32msb_data (10 regs) */
	0x05060328, 	/* igu.int_before_mask_sts_pf (5 regs) */
	0x05060338, 	/* igu.int_mask_sts_pf (5 regs) */
	0x05060348, 	/* igu.pba_sts_pf (5 regs) */
	0x01060486, 	/* igu.attn_tph (1 regs) */
	0x01430000, 	/* block cau */
	0x01070106, 	/* cau.cleanup_command_done (1 regs) */
	0x094b0000, 	/* block nig */
	0x01140300, 	/* nig.tx_lb_pf_drop_perpf (1 regs) */
	0x0114044f, 	/* nig.rx_llh_svol_mcp_fwd_perpf (1 regs) */
	0x01140455, 	/* nig.rx_llh_svol_brb_dntfwd_perpf (1 regs) */
	0x0114047d, 	/* nig.rx_llh_brb_gate_dntfwd_perpf (1 regs) */
	0x01140599, 	/* nig.lb_llh_brb_gate_dntfwd_perpf (1 regs) */
	0x011406e7, 	/* nig.llh_eng_cls_eng_id_perpf (1 regs) */
	0x07142051, 	/* nig.tcp_syn_enable .. nig.tcp_syn_ipv4_dst_port (7 regs) */
	0x08942058, 	/* nig.tcp_syn_ipv6_src_addr .. nig.tcp_syn_ipv6_dst_addr (8 regs, WB) */
	0x02142060, 	/* nig.tcp_syn_ipv4_src_addr .. nig.tcp_syn_ipv4_dst_addr (2 regs) */
	0x02040003, 	/* mode !bb, block pglue_b */
	0x020aabad, 	/* pglue_b.vf_bar0_size .. pglue_b.pf_rom_size (2 regs) */
	0x040aabe6, 	/* pglue_b.pgl_addr_e8_f0 .. pglue_b.pgl_addr_f4_f0 (4 regs) */
	0x01150000, 	/* block pglcs */
	0x01000e00, 	/* pglcs.first_vf (1 regs) */
	0x01140015, 	/* mode !(bb|k2), block pswrq2 */
	0x0609033f, 	/* pswrq2.tgsrc_p_size .. pswrq2.rgsrc_last_ilt (6 regs) */
	0x20150000, 	/* block pglcs */
	0x0e000800, 	/* pglcs.pgl_cs.PCIEEP_ID .. pglcs.pgl_cs.PCIEEP_CAP_PTR (14 regs) */
	0x0300080f, 	/* pglcs.pgl_cs.PCIEEP_INT .. pglcs.pgl_cs.PCIEEP_PM_CTL (3 regs) */
	0x0500081c, 	/* pglcs.pgl_cs.PCIEEP_E_CAP_LIST .. pglcs.pgl_cs.PCIEEP_LINK_CTL (5 regs) */
	0x04000825, 	/* pglcs.pgl_cs.PCIEEP_DEV_CAP2 .. pglcs.pgl_cs.PCIEEP_LINK_CTL2 (4 regs) */
	0x0300082c, 	/* pglcs.pgl_cs.PCIEEP_MSIX_CAP_CNTRL .. pglcs.pgl_cs.PCIEEP_MSIX_PBA (3 regs) */
	0x02000834, 	/* pglcs.pgl_cs.PCIEEP_VPD_BASE .. pglcs.pgl_cs.PCIEEP_VPD_DATA (2 regs) */
	0x0b000840, 	/* pglcs.pgl_cs.PCIEEP_EXT_CAP .. pglcs.pgl_cs.PCIEEP_HDR_LOG4 (11 regs) */
	0x0700084e, 	/* pglcs.pgl_cs.PCIEEP_TLP_PLOG1 .. pglcs.pgl_cs.PCIEEP_SN_DW2 (7 regs) */
	0x06000856, 	/* pglcs.pgl_cs.PCIEEP_PB_BASE .. pglcs.pgl_cs.PCIEEP_ARI_CAP_CTL (6 regs) */
	0x0b00085e, 	/* pglcs.pgl_cs.PCIEEP_SCAP_HDR .. pglcs.pgl_cs.PCIEEP_EQ_CTL1415 (11 regs) */
	0x0700086a, 	/* pglcs.pgl_cs.PCIEEP_PL16G_EXT_CAP_HDR .. pglcs.pgl_cs.PCIEEP_PL16G_SRET_DPAR_STAT (7 regs) */
	0x2a000872, 	/* pglcs.pgl_cs.PCIEEP_PL16G_EQ_CTL0123 .. pglcs.pgl_cs.PCIEEP_TPH_ST_TABLE (42 regs) */
	0x110008bb, 	/* pglcs.pgl_cs.PCIEEP_ACS_CAP_HDR .. pglcs.pgl_cs.PCIEEP_RAS_TBA_DATA (17 regs) */
	0x180008d2, 	/* pglcs.pgl_cs.PCIEEP_RAS_EINJ_EN .. pglcs.pgl_cs.PCIEEP_RAS_EINJ_CTL6PE (24 regs) */
	0x020008ee, 	/* pglcs.pgl_cs.PCIEEP_RAS_SD_CTL1 .. pglcs.pgl_cs.PCIEEP_RAS_SD_CTL2 (2 regs) */
	0x060008f2, 	/* pglcs.pgl_cs.PCIEEP_RAS_SD_L1LANE .. pglcs.pgl_cs.PCIEEP_RAS_SD_STATUSL3 (6 regs) */
	0x030008fa, 	/* pglcs.pgl_cs.PCIEEP_RAS_SD_EQ_CTL1 .. pglcs.pgl_cs.PCIEEP_RAS_SD_EQ_CTL3 (3 regs) */
	0x030008fe, 	/* pglcs.pgl_cs.PCIEEP_RAS_SD_EQ_STAT1 .. pglcs.pgl_cs.PCIEEP_RAS_SD_EQ_STAT3 (3 regs) */
	0x2f000906, 	/* pglcs.pgl_cs.PCIEEP_RASDP_CAP_HDR .. pglcs.pgl_cs.PCIEEP_VSECST_CTL (47 regs) */
	0x090009c0, 	/* pglcs.pgl_cs.PCIEEP_ACK_TIMER .. pglcs.pgl_cs.PCIEEP_FILT_MSK2 (9 regs) */
	0x0b0009ca, 	/* pglcs.pgl_cs.PCIEEP_DBG0 .. pglcs.pgl_cs.PCIEEP_C_RCV_CREDIT (11 regs) */
	0x03000a03, 	/* pglcs.pgl_cs.PCIEEP_GEN2_PORT .. pglcs.pgl_cs.PCIEEP_PHY_CTL (3 regs) */
	0x02000a23, 	/* pglcs.pgl_cs.PCIEEP_CLK_GATING_CTL .. pglcs.pgl_cs.PCIEEP_PHY_GEN3_CTL (2 regs) */
	0x01000a28, 	/* pglcs.pgl_cs.PCIEEP_HIDE_PF (1 regs) */
	0x02000a2a, 	/* pglcs.pgl_cs.PCIEEP_GEN3_EQ_CTL .. pglcs.pgl_cs.PCIEEP_GEN3_FB_MODE_DIR_CHG (2 regs) */
	0x06000a2d, 	/* pglcs.pgl_cs.PCIEEP_ORD_RULE_CTRL .. pglcs.pgl_cs.PCIEEP_CPL_LUT_DEL_ENT (6 regs) */
	0x02000a3e, 	/* pglcs.pgl_cs.PCIEEP_VER_NUM .. pglcs.pgl_cs.PCIEEP_VER_TYPE (2 regs) */
	0x01000acc, 	/* pglcs.pgl_cs.PCIEEP_PL_LTR_LAT (1 regs) */
	0x02000ad0, 	/* pglcs.pgl_cs.PCIEEP_AUX_CLK_FREQ .. pglcs.pgl_cs.PCIEEP_L1_SUBSTATES (2 regs) */
	0x02000ae0, 	/* pglcs.pgl_cs.PCIEEP_GEN4_LANE_MARGINING_1 .. pglcs.pgl_cs.PCIEEP_GEN4_LANE_MARGINING_2 (2 regs) */
	0x01000ae4, 	/* pglcs.pgl_cs.PCIEEP_PIPE_REL (1 regs) */
	0x01000b00, 	/* pglcs.pgl_cs.PCIEEP_RX_SER_Q_CTRL (1 regs) */
	0x01170000, 	/* block ptu */
	0x02158179, 	/* ptu.inv_rsc_type .. ptu.inv_rsc_type_mask (2 regs) */
	0x03200000, 	/* block dorq */
	0x10040a04, 	/* dorq.prv_pf_max_icid_2 .. dorq.prv_vf_range2conn_type_5 (16 regs) */
	0x02040add, 	/* dorq.ddp_version .. dorq.rdmap_version (2 regs) */
	0x08040ae1, 	/* dorq.pf_ext_pcp_roce .. dorq.pf_int_vid_iwarp (8 regs) */
	0x01230000, 	/* block prs */
	0x0107c5b9, 	/* prs.new_entry_exclusive (1 regs) */
	0x023c0000, 	/* block pbf */
	0x01360155, 	/* pbf.mpls_compare_label (1 regs) */
	0x0a36019e, 	/* pbf.tunnel_gre_eth_en .. pbf.tunnel_mpls_eth_en (10 regs) */
	0x03450000, 	/* block rgsrc */
	0x010c8104, 	/* rgsrc.hash_bin_bit_w (1 regs) */
	0x020c8111, 	/* rgsrc.table_t1_entry_size .. rgsrc.table_t2_entry_size (2 regs) */
	0x010c8117, 	/* rgsrc.was_error (1 regs) */
	0x03470000, 	/* block tgsrc */
	0x010c8904, 	/* tgsrc.hash_bin_bit_w (1 regs) */
	0x020c8911, 	/* tgsrc.table_t1_entry_size .. tgsrc.table_t2_entry_size (2 regs) */
	0x010c8917, 	/* tgsrc.was_error (1 regs) */
	0x01040047, 	/* mode !(k2|e5), block pglue_b */
	0x040aa901, 	/* pglue_b.pgl_addr_88_f0 .. pglue_b.pgl_addr_94_f0 (4 regs) */
	0x3e150000, 	/* block pglcs */
	0x0e000800, 	/* pglcs.pgl_cs.device_vendor_id .. pglcs.pgl_cs.cap_pointer (14 regs) */
	0x0100080f, 	/* pglcs.pgl_cs.lat_min_grant_int_pin_int_line (1 regs) */
	0x08000812, 	/* pglcs.pgl_cs.pm_cap .. pglcs.pgl_cs.msi_data (8 regs) */
	0x12000828, 	/* pglcs.pgl_cs.msix_cap .. pglcs.pgl_cs.slot_status_control_2 (18 regs) */
	0x0e000840, 	/* pglcs.pgl_cs.adv_err_cap .. pglcs.pgl_cs.root_err_id (14 regs) */
	0x0300084f, 	/* pglcs.pgl_cs.device_ser_num_cap .. pglcs.pgl_cs.upper_ser_num (3 regs) */
	0x0b000854, 	/* pglcs.pgl_cs.pwr_bdgt_cap .. pglcs.pgl_cs.vc_rsrc_status (11 regs) */
	0x07000860, 	/* pglcs.pgl_cs.vendor_cap .. pglcs.pgl_cs.vendor_specific_reg5 (7 regs) */
	0x1300086c, 	/* pglcs.pgl_cs.LTR_cap .. pglcs.pgl_cs.VF_BAR5 (19 regs) */
	0x03000880, 	/* pglcs.pgl_cs.PTM_extended_cap .. pglcs.pgl_cs.ptm_ctrl_reg (3 regs) */
	0x02000884, 	/* pglcs.pgl_cs.ATS_cap .. pglcs.pgl_cs.ATS_control (2 regs) */
	0x03000888, 	/* pglcs.pgl_cs.RBAR_ext_cap .. pglcs.pgl_cs.RBAR_CTRL (3 regs) */
	0x0300088c, 	/* pglcs.pgl_cs.TPH_extended_cap .. pglcs.pgl_cs.tph_req_control (3 regs) */
	0x04000890, 	/* pglcs.pgl_cs.PML1sub_capID .. pglcs.pgl_cs.PML1_sub_control2 (4 regs) */
	0x0b0008c0, 	/* pglcs.pgl_cs.Secondary_PCIE_Extended_Cap .. pglcs.pgl_cs.Lane14_15_equalization_ctrl (11 regs) */
	0x02000902, 	/* pglcs.pgl_cs.config_2 .. pglcs.pgl_cs.config_3 (2 regs) */
	0x01000906, 	/* pglcs.pgl_cs.pci_extended_bar_siz (1 regs) */
	0x0400090a, 	/* pglcs.pgl_cs.reg_vpd_intf .. pglcs.pgl_cs.reg_id_val1 (4 regs) */
	0x02000910, 	/* pglcs.pgl_cs.reg_id_val4 .. pglcs.pgl_cs.reg_id_val5 (2 regs) */
	0x04000913, 	/* pglcs.pgl_cs.reg_id_val6 .. pglcs.pgl_cs.reg_msi_addr_l (4 regs) */
	0x03000919, 	/* pglcs.pgl_cs.reg_msi_mask .. pglcs.pgl_cs.reg_pm_data_c (3 regs) */
	0x02000930, 	/* pglcs.pgl_cs.reg_msix_control .. pglcs.pgl_cs.reg_msix_tbl_off_bir (2 regs) */
	0x02000935, 	/* pglcs.pgl_cs.reg_device_capability .. pglcs.pgl_cs.reg_device_control (2 regs) */
	0x02000938, 	/* pglcs.pgl_cs.reg_bar2_config .. pglcs.pgl_cs.reg_pcie_device_capability_2 (2 regs) */
	0x0300093d, 	/* pglcs.pgl_cs.reg_bar3_config .. pglcs.pgl_cs.reg_root_control (3 regs) */
	0x0a000944, 	/* pglcs.pgl_cs.reg_pwr_bdgt_data_0 .. pglcs.pgl_cs.reg_pwr_bdgt_data_8 (10 regs) */
	0x02000950, 	/* pglcs.pgl_cs.reg_l1sub_cap .. pglcs.pgl_cs.reg_l1sub_ext_cap (2 regs) */
	0x01000954, 	/* pglcs.pgl_cs.reg_pwr_bdgt_capability (1 regs) */
	0x04000956, 	/* pglcs.pgl_cs.reg_rc_user_mem_lo1 .. pglcs.pgl_cs.reg_rc_user_mem_hi2 (4 regs) */
	0x0100097b, 	/* pglcs.pgl_cs.reg_PCIER_MC_WINDOW_SIZE_REQ (1 regs) */
	0x0600097d, 	/* pglcs.pgl_cs.reg_TPH_CAP .. pglcs.pgl_cs.reg_VF_BAR_REG (6 regs) */
	0x01000984, 	/* pglcs.pgl_cs.reg_VF_CAP_EN (1 regs) */
	0x03000987, 	/* pglcs.pgl_cs.reg_vf_msix_control .. pglcs.pgl_cs.reg_pf_initvf (3 regs) */
	0x0200098c, 	/* pglcs.pgl_cs.reg_ats_inld_queue_depth .. pglcs.pgl_cs.reg_VFTPH_CAP (2 regs) */
	0x0e000a00, 	/* pglcs.pgl_cs.tl_control_0 .. pglcs.pgl_cs.user_control_8 (14 regs) */
	0x0c000a0f, 	/* pglcs.pgl_cs.tl_control_6 .. pglcs.pgl_cs.tl_rst_ctrl (12 regs) */
	0x0a000a1c, 	/* pglcs.pgl_cs.tl_obff_ctrl .. pglcs.pgl_cs.tl_func14to15_stat (10 regs) */
	0x29000a40, 	/* pglcs.pgl_cs.tl_status_0 .. pglcs.pgl_cs.tl_rst_debug (41 regs) */
	0x01000a81, 	/* pglcs.pgl_cs.tl_iov_vfctl_0 (1 regs) */
	0x02000a84, 	/* pglcs.pgl_cs.tl_fcimm_np_limit .. pglcs.pgl_cs.tl_fcimm_p_limit (2 regs) */
	0x0a000a87, 	/* pglcs.pgl_cs.reg_capena_fn0_mask .. pglcs.pgl_cs.ptm_mstr_prop_dly (10 regs) */
	0x0a000a94, 	/* pglcs.pgl_cs.PCIER_TL_STAT_TX_CTL .. pglcs.pgl_cs.PCIER_TL_STAT_RX_CTR_HI (10 regs) */
	0x05000b00, 	/* pglcs.pgl_cs.PCIER_DBG_FIFO_CTLSTAT .. pglcs.pgl_cs.PCIER_TLPL_DBG_FIFO_CTL (5 regs) */
	0x1a000b06, 	/* pglcs.pgl_cs.PCIER_DBG_FIFO_RD_9 .. pglcs.pgl_cs.PCIER_TLDA1_RDFIFO_0 (26 regs) */
	0x0f000c00, 	/* pglcs.pgl_cs.pdl_control_0 .. pglcs.pgl_cs.pdl_control_14 (15 regs) */
	0x08000c10, 	/* pglcs.pgl_cs.DLATTN_VEC .. pglcs.pgl_cs.dl_spare0 (8 regs) */
	0x09000c40, 	/* pglcs.pgl_cs.mdio_addr .. pglcs.pgl_cs.ate_tlp_ctl (9 regs) */
	0x03000c4c, 	/* pglcs.pgl_cs.serdes_pmi_addr .. pglcs.pgl_cs.serdes_pmi_rdata (3 regs) */
	0x14000d00, 	/* pglcs.pgl_cs.dl_dbg_0 .. pglcs.pgl_cs.dl_dbg_19 (20 regs) */
	0x0a000e00, 	/* pglcs.pgl_cs.reg_phy_ctl_0 .. pglcs.pgl_cs.phy_err_attn_mask (10 regs) */
	0x08000e0c, 	/* pglcs.pgl_cs.reg_phy_ctl_8 .. pglcs.pgl_cs.reg_phy_ctl_15 (8 regs) */
	0x04000e15, 	/* pglcs.pgl_cs.reg_phy_ctl_16 .. pglcs.pgl_cs.pl_gen3_ena_frmerr (4 regs) */
	0x05000e40, 	/* pglcs.pgl_cs.pl_lpbk_master_ctl0 .. pglcs.pgl_cs.pl_lpbk_master_tx_setting (5 regs) */
	0x01000e4c, 	/* pglcs.pgl_cs.pl_sw_ltssm_ctl (1 regs) */
	0x10000e50, 	/* pglcs.pgl_cs.pcie_statis_ctl .. pglcs.pgl_cs.pcie_rxtlperr_statis (16 regs) */
	0x06000e68, 	/* pglcs.pgl_cs.ltssm_statis_ctl .. pglcs.pgl_cs.ltssm_statis_cnt (6 regs) */
	0x09000f00, 	/* pglcs.pgl_cs.Received_MCP_Errors_1512 .. pglcs.pgl_cs.rx_fts_limit (9 regs) */
	0x06000f34, 	/* pglcs.pgl_cs.fts_hist .. pglcs.pgl_cs.recovery_hist_1 (6 regs) */
	0x28000f3b, 	/* pglcs.pgl_cs.phy_ltssm_hist_0 .. pglcs.pgl_cs.phy_dbg_sed_extcfg_74 (40 regs) */
	0x01000f64, 	/* pglcs.pgl_cs.phy_dbg_preset_lut (1 regs) */
	0x01000f80, 	/* pglcs.pgl_cs.phy_dbg_muxed_sigs (1 regs) */
	0x05000f84, 	/* pglcs.pgl_cs.phy_dbg_clkreq_0 .. pglcs.pgl_cs.misc_dbg_status (5 regs) */
	0x01420000, 	/* block igu */
	0x03060203, 	/* igu.pci_pf_msi_en .. igu.pci_pf_msix_func_mask (3 regs) */
	0x01140083, 	/* mode !e5, block pswrq2 */
	0x010901fc, 	/* pswrq2.atc_internal_ats_enable (1 regs) */
	0x02200000, 	/* block dorq */
	0x10040102, 	/* dorq.pf_max_icid_0 .. dorq.vf_max_icid_7 (16 regs) */
	0x02040231, 	/* dorq.pf_pcp .. dorq.pf_ext_vid (2 regs) */
	0x1b150027, 	/* mode !(bb|e5), block pglcs */
	0x0e000800, 	/* pglcs.pgl_cs.DEVICE_ID_VENDOR_ID_REG .. pglcs.pgl_cs.PCI_CAP_PTR_REG (14 regs) */
	0x0300080f, 	/* pglcs.pgl_cs.MAX_LATENCY_MIN_GRANT_INTERRUPT_PIN_INTERRUPT_LINE_REG .. pglcs.pgl_cs.CON_STATUS_REG (3 regs) */
	0x06000814, 	/* pglcs.pgl_cs.PCI_MSI_CAP_ID_NEXT_CTRL_REG .. pglcs.pgl_cs.MSI_CAP_OFF_14H_REG (6 regs) */
	0x0500081c, 	/* pglcs.pgl_cs.PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG .. pglcs.pgl_cs.LINK_CONTROL_LINK_STATUS_REG (5 regs) */
	0x04000825, 	/* pglcs.pgl_cs.DEVICE_CAPABILITIES2_REG .. pglcs.pgl_cs.LINK_CONTROL2_LINK_STATUS2_REG (4 regs) */
	0x0300082c, 	/* pglcs.pgl_cs.PCI_MSIX_CAP_ID_NEXT_CTRL_REG .. pglcs.pgl_cs.MSIX_PBA_OFFSET_REG (3 regs) */
	0x02000834, 	/* pglcs.pgl_cs.VPD_BASE .. pglcs.pgl_cs.DATA_REG (2 regs) */
	0x0b000840, 	/* pglcs.pgl_cs.AER_EXT_CAP_HDR_OFF .. pglcs.pgl_cs.HDR_LOG_3_OFF (11 regs) */
	0x0400084e, 	/* pglcs.pgl_cs.TLP_PREFIX_LOG_1_OFF .. pglcs.pgl_cs.TLP_PREFIX_LOG_4_OFF (4 regs) */
	0x0300085a, 	/* pglcs.pgl_cs.SN_BASE .. pglcs.pgl_cs.SER_NUM_REG_DW_2 (3 regs) */
	0x0600085e, 	/* pglcs.pgl_cs.PB_BASE .. pglcs.pgl_cs.CAP_REG (6 regs) */
	0x1400086e, 	/* pglcs.pgl_cs.SRIOV_BASE_REG .. pglcs.pgl_cs.TPH_ST_TABLE_REG_0 (20 regs) */
	0x060008a3, 	/* pglcs.pgl_cs.RAS_DES_CAP_HEADER_REG .. pglcs.pgl_cs.TIME_BASED_ANALYSIS_DATA_REG (6 regs) */
	0x180008af, 	/* pglcs.pgl_cs.EINJ_ENABLE_REG .. pglcs.pgl_cs.EINJ6_TLP_REG (24 regs) */
	0x020008cb, 	/* pglcs.pgl_cs.SD_CONTROL1_REG .. pglcs.pgl_cs.SD_CONTROL2_REG (2 regs) */
	0x060008cf, 	/* pglcs.pgl_cs.SD_STATUS_L1LANE_REG .. pglcs.pgl_cs.SD_STATUS_L3_REG (6 regs) */
	0x030008d7, 	/* pglcs.pgl_cs.SD_EQ_CONTROL1_REG .. pglcs.pgl_cs.SD_EQ_CONTROL3_REG (3 regs) */
	0x030008db, 	/* pglcs.pgl_cs.SD_EQ_STATUS1_REG .. pglcs.pgl_cs.SD_EQ_STATUS3_REG (3 regs) */
	0x0e0008e3, 	/* pglcs.pgl_cs.RASDP_EXT_CAP_HDR_OFF .. pglcs.pgl_cs.RASDP_RAM_ADDR_UNCORR_ERROR_OFF (14 regs) */
	0x180008f4, 	/* pglcs.pgl_cs.PTM_REQ_CAP_HDR_OFF .. pglcs.pgl_cs.RESBAR_CTRL_REG_0_REG (24 regs) */
	0x01000d05, 	/* pglcs.pgl_cs_shadow.BAR1_MASK_REG (1 regs) */
	0x01000d0c, 	/* pglcs.pgl_cs_shadow.EXP_ROM_BAR_MASK_REG (1 regs) */
	0x01000d71, 	/* pglcs.pgl_cs_shadow.SHADOW_SRIOV_INITIAL_VFS (1 regs) */
	0x01000d73, 	/* pglcs.pgl_cs_shadow.SHADOW_SRIOV_VF_OFFSET_POSITION (1 regs) */
	0x01000d78, 	/* pglcs.pgl_cs_shadow.SRIOV_BAR1_MASK_REG (1 regs) */
	0x01000d7a, 	/* pglcs.pgl_cs_shadow.SRIOV_BAR3_MASK_REG (1 regs) */
	0x01000d7c, 	/* pglcs.pgl_cs_shadow.SRIOV_BAR5_MASK_REG (1 regs) */
	0x010401e5, 	/* mode !k2, block pglue_b */
	0x010aa839, 	/* pglue_b.shadow_ats_stu (1 regs) */
	0x044c013d, 	/* mode (!bb)&(!fpga), block wol */
	0x12182040, 	/* wol.acpi_enable .. wol.mpkt_enable (18 regs) */
	0x02982052, 	/* wol.mpkt_mac_addr (2 regs, WB) */
	0x01182054, 	/* wol.force_wol (1 regs) */
	0x01182066, 	/* wol.tcp_syn_enable (1 regs) */
	0x0300000f, 	/* split PORT_PF */
	0x01230000, 	/* block prs */
	0x0207c1f3, 	/* prs.first_hdr_dst_mac_0 .. prs.first_hdr_dst_mac_1 (2 regs) */
	0x034b0000, 	/* block nig */
	0x01140669, 	/* nig.llh_func_tagmac_cls_type (1 regs) */
	0x0d14066c, 	/* nig.llh_func_tag_en .. nig.llh_func_no_tag (13 regs) */
	0x0114223e, 	/* nig.dscp_to_tc_map_enable (1 regs) */
	0x054b0047, 	/* mode !(k2|e5), block nig */
	0x02142000, 	/* nig.acpi_tag_rm .. nig.acpi_prop_hdr_rm (2 regs) */
	0x11142040, 	/* nig.acpi_enable .. nig.acpi_pat_7_len (17 regs) */
	0x01142062, 	/* nig.mpkt_enable (1 regs) */
	0x02942064, 	/* nig.mpkt_mac_addr (2 regs, WB) */
	0x01142066, 	/* nig.force_wol (1 regs) */
	0x024b0083, 	/* mode !e5, block nig */
	0x20940680, 	/* nig.llh_func_filter_value (32 regs, WB) */
	0x401406a0, 	/* nig.llh_func_filter_en .. nig.llh_func_filter_hdr_sel (64 regs) */
	0x04000033, 	/* split VF */
	0x03040000, 	/* block pglue_b */
	0x010aa85a, 	/* pglue_b.internal_vfid_enable (1 regs) */
	0x010aa95f, 	/* pglue_b.fid_channel_enable (1 regs) */
	0x010aa966, 	/* pglue_b.mask_block_discard_attn_vf (1 regs) */
	0x021e0000, 	/* block qm */
	0x010bcbaa, 	/* qm.vf_en (1 regs) */
	0x020bcbad, 	/* qm.usg_cnt_vf_tx .. qm.usg_cnt_vf_other (2 regs) */
	0x031f0000, 	/* block tm */
	0x010b010e, 	/* tm.vf_enable_conn (1 regs) */
	0x010b0110, 	/* tm.vf_enable_task (1 regs) */
	0x020b0141, 	/* tm.vf_scan_active_conn .. tm.vf_scan_active_task (2 regs) */
	0x03200000, 	/* block dorq */
	0x01040143, 	/* dorq.vf_db_enable (1 regs) */
	0x01040271, 	/* dorq.vf_usage_cnt (1 regs) */
	0x01040275, 	/* dorq.vf_ovfl_sticky (1 regs) */
	0x04400000, 	/* block ccfc */
	0x010b81c1, 	/* ccfc.weak_enable_vf (1 regs) */
	0x010b81c3, 	/* ccfc.strong_enable_vf (1 regs) */
	0x010bb402, 	/* ccfc.vf_lstate_cnt1 (1 regs) */
	0x010bb404, 	/* ccfc.vf_lstate_cnt2 (1 regs) */
	0x04410000, 	/* block tcfc */
	0x010b41c1, 	/* tcfc.weak_enable_vf (1 regs) */
	0x010b41c3, 	/* tcfc.strong_enable_vf (1 regs) */
	0x010b7402, 	/* tcfc.vf_lstate_cnt1 (1 regs) */
	0x010b7404, 	/* tcfc.vf_lstate_cnt2 (1 regs) */
	0x05420000, 	/* block igu */
	0x01060102, 	/* igu.statistic_num_vf_msg_sent (1 regs) */
	0x01060201, 	/* igu.vf_configuration (1 regs) */
	0x02060330, 	/* igu.int_before_mask_sts_vf_lsb .. igu.int_before_mask_sts_vf_msb (2 regs) */
	0x02060340, 	/* igu.int_mask_sts_vf_lsb .. igu.int_mask_sts_vf_msb (2 regs) */
	0x02060350, 	/* igu.pba_sts_vf_lsb .. igu.pba_sts_vf_msb (2 regs) */
	0x08150015, 	/* mode !(bb|k2), block pglcs */
	0x0e000c00, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_ID .. pglcs.pgl_cs_vf_1.PCIEEPVF_CAP_PTR (14 regs) */
	0x01000c0f, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_INT (1 regs) */
	0x05000c1c, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_E_CAP_LIST .. pglcs.pgl_cs_vf_1.PCIEEPVF_LINK_CTL (5 regs) */
	0x04000c25, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_DEV_CAP2 .. pglcs.pgl_cs_vf_1.PCIEEPVF_LINK_CTL2 (4 regs) */
	0x03000c2c, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_MSIX_CAP_CNTRL .. pglcs.pgl_cs_vf_1.PCIEEPVF_MSIX_PBA (3 regs) */
	0x02000c40, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_EXT_CAP .. pglcs.pgl_cs_vf_1.PCIEEPVF_ARI_CAP_CTL (2 regs) */
	0x04000c44, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_TPH_CAP_HDR .. pglcs.pgl_cs_vf_1.PCIEEPVF_TPH_ST_TABLE (4 regs) */
	0x03000c67, 	/* pglcs.pgl_cs_vf_1.PCIEEPVF_ACS_CAP_HDR .. pglcs.pgl_cs_vf_1.PCIEEPVF_ACS_EGR_CTL_VEC (3 regs) */
	0x01420047, 	/* mode !(k2|e5), block igu */
	0x02060206, 	/* igu.pci_vf_msix_en .. igu.pci_vf_msix_func_mask (2 regs) */
	0x08150027, 	/* mode !(bb|e5), block pglcs */
	0x0c000c00, 	/* pglcs.pgl_cs_vf_1.VF_DEVICE_ID_VENDOR_ID_REG .. pglcs.pgl_cs_vf_1.VF_SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG (12 regs) */
	0x01000c0d, 	/* pglcs.pgl_cs_vf_1.VF_PCI_CAP_PTR_REG (1 regs) */
	0x01000c0f, 	/* pglcs.pgl_cs_vf_1.VF_MAX_LATENCY_MIN_GRANT_INTERRUPT_PIN_INTERRUPT_LINE_REG (1 regs) */
	0x05000c1c, 	/* pglcs.pgl_cs_vf_1.VF_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG .. pglcs.pgl_cs_vf_1.VF_LINK_CONTROL_LINK_STATUS_REG (5 regs) */
	0x04000c25, 	/* pglcs.pgl_cs_vf_1.VF_DEVICE_CAPABILITIES2_REG .. pglcs.pgl_cs_vf_1.VF_LINK_CONTROL2_LINK_STATUS2_REG (4 regs) */
	0x03000c2c, 	/* pglcs.pgl_cs_vf_1.VF_PCI_MSIX_CAP_ID_NEXT_CTRL_REG .. pglcs.pgl_cs_vf_1.VF_MSIX_PBA_OFFSET_REG (3 regs) */
	0x02000c40, 	/* pglcs.pgl_cs_vf_1.VF_ARI_BASE .. pglcs.pgl_cs_vf_1.VF_CAP_REG (2 regs) */
	0x04000c44, 	/* pglcs.pgl_cs_vf_1.VF_TPH_EXT_CAP_HDR_REG .. pglcs.pgl_cs_vf_1.VF_TPH_ST_TABLE_REG_0 (4 regs) */
};
/* Data size: 15076 bytes */

#ifndef __PREVENT_DUMP_MEM_ARR__

/* Array of memories to be dumped */
static const u32 dump_mem[] = {
	0x00000304, 	/* split NONE */
	0x14040000, 	/* block pglue_b */
	0x000aa910, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_0, group=PXP_MEM, size=6 regs */
	0x000aa918, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_1, group=PXP_MEM, size=6 regs */
	0x000aa920, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_2, group=PXP_MEM, size=6 regs */
	0x000aa928, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_3, group=PXP_MEM, size=6 regs */
	0x000aa930, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_4, group=PXP_MEM, size=6 regs */
	0x000aa938, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_5, group=PXP_MEM, size=6 regs */
	0x000aa940, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_6, group=PXP_MEM, size=6 regs */
	0x000aa948, 0x00000006, 	/* pglue_b.sdm_inb_int_b_pf_7, group=PXP_MEM, size=6 regs */
	0x000aaa00, 0x010000b0, 	/* pglue_b.write_fifo_queue, group=PXP_MEM, size=176 regs, WB */
	0x000aab00, 0x01000070, 	/* pglue_b.read_fifo_queue, group=PXP_MEM, size=112 regs, WB */
	0x020d0000, 	/* block pswhst */
	0x000a8100, 0x00000048, 	/* pswhst.inbound_int, group=PXP_MEM, size=72 regs */
	0x02160000, 	/* block dmae */
	0x01003200, 0x000001c0, 	/* dmae.cmd_mem, group=DMAE_MEM, size=448 regs */
	0x04180000, 	/* block tcm */
	0x02460538, 0x00000008, 	/* tcm.xx_cbyp_tbl, group=CM_MEM, size=8 regs */
	0x02460600, 0x00000040, 	/* tcm.xx_dscr_tbl, group=CM_MEM, size=64 regs */
	0x04190000, 	/* block mcm */
	0x02480608, 0x00000008, 	/* mcm.xx_cbyp_tbl, group=CM_MEM, size=8 regs */
	0x02480700, 0x00000040, 	/* mcm.xx_dscr_tbl, group=CM_MEM, size=64 regs */
	0x041a0000, 	/* block ucm */
	0x024a05c8, 0x00000008, 	/* ucm.xx_cbyp_tbl, group=CM_MEM, size=8 regs */
	0x024a06c0, 0x00000040, 	/* ucm.xx_dscr_tbl, group=CM_MEM, size=64 regs */
	0x061b0000, 	/* block xcm */
	0x02400608, 0x00000008, 	/* xcm.xx_cbyp_tbl, group=CM_MEM, size=8 regs */
	0x024006c0, 0x00000040, 	/* xcm.xx_dscr_tbl, group=CM_MEM, size=64 regs */
	0x02400800, 0x00000400, 	/* xcm.xx_msg_ram, group=CM_MEM, size=1024 regs */
	0x041c0000, 	/* block ycm */
	0x02420608, 0x00000008, 	/* ycm.xx_cbyp_tbl, group=CM_MEM, size=8 regs */
	0x02420700, 0x00000040, 	/* ycm.xx_dscr_tbl, group=CM_MEM, size=64 regs */
	0x081d0000, 	/* block pcm */
	0x02440540, 0x00000002, 	/* pcm.xx_lcid_cam, group=CM_MEM, size=2 regs */
	0x02440580, 0x00000002, 	/* pcm.xx_tbl, group=CM_MEM, size=2 regs */
	0x024405c0, 0x00000004, 	/* pcm.xx_dscr_tbl, group=CM_MEM, size=4 regs */
	0x02440800, 0x000002c0, 	/* pcm.xx_msg_ram, group=CM_MEM, size=704 regs */
	0x081e0000, 	/* block qm */
	0x030bc700, 0x00000048, 	/* qm.CMIntQMask, group=QM_MEM, size=72 regs */
	0x030bcd00, 0x00000100, 	/* qm.RlGlblIncVal, group=QM_MEM, size=256 regs */
	0x030bcf00, 0x00000100, 	/* qm.RlGlblUpperBound, group=QM_MEM, size=256 regs */
	0x030bd100, 0x00000100, 	/* qm.RlGlblCrd, group=QM_MEM, size=256 regs */
	0x02200000, 	/* block dorq */
	0x04040400, 0x00000280, 	/* dorq.wqe_buf, group=DORQ_MEM, size=640 regs */
	0x0a210000, 	/* block brb */
	0x050d0204, 0x00000004, 	/* brb.free_list_head, group=BRB_RAM, size=4 regs */
	0x050d0208, 0x00000004, 	/* brb.free_list_tail, group=BRB_RAM, size=4 regs */
	0x050d020c, 0x00000004, 	/* brb.free_list_size, group=BRB_RAM, size=4 regs */
	0x060d0600, 0x01000014, 	/* brb.stopped_rd_req, group=BRB_MEM, size=20 regs, WB */
	0x060d0640, 0x01000014, 	/* brb.stopped_rls_req, group=BRB_MEM, size=20 regs, WB */
	0x04230000, 	/* block prs */
	0x0707c400, 0x01000040, 	/* prs.gft_profile_mask_ram, group=PRS_MEM, size=64 regs, WB */
	0x0707c440, 0x0000001f, 	/* prs.gft_cam, group=PRS_MEM, size=31 regs */
	0x042a0000, 	/* block tsem */
	0x085d0100, 0x00000020, 	/* tsem.fast_memory.gpre, group=IOR, size=32 regs */
	0x085d0130, 0x00000001, 	/* tsem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x042b0000, 	/* block msem */
	0x08610100, 0x00000020, 	/* msem.fast_memory.gpre, group=IOR, size=32 regs */
	0x08610130, 0x00000001, 	/* msem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x042c0000, 	/* block usem */
	0x08650100, 0x00000020, 	/* usem.fast_memory.gpre, group=IOR, size=32 regs */
	0x08650130, 0x00000001, 	/* usem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x042d0000, 	/* block xsem */
	0x08510100, 0x00000020, 	/* xsem.fast_memory.gpre, group=IOR, size=32 regs */
	0x08510130, 0x00000001, 	/* xsem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x042e0000, 	/* block ysem */
	0x08550100, 0x00000020, 	/* ysem.fast_memory.gpre, group=IOR, size=32 regs */
	0x08550130, 0x00000001, 	/* ysem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x042f0000, 	/* block psem */
	0x08590100, 0x00000020, 	/* psem.fast_memory.gpre, group=IOR, size=32 regs */
	0x08590130, 0x00000001, 	/* psem.fast_memory.active_reg_set, group=IOR, size=1 regs */
	0x063b0000, 	/* block btb */
	0x0936c204, 0x00000004, 	/* btb.free_list_head, group=BTB_RAM, size=4 regs */
	0x0936c208, 0x00000004, 	/* btb.free_list_tail, group=BTB_RAM, size=4 regs */
	0x0936c20c, 0x00000004, 	/* btb.free_list_size, group=BTB_RAM, size=4 regs */
	0x08400000, 	/* block ccfc */
	0x0a0ba200, 0x00000140, 	/* ccfc.activity_counter, group=CONN_CFC_MEM, size=320 regs */
	0x0a0ba400, 0x00000140, 	/* ccfc.info_state, group=CONN_CFC_MEM, size=320 regs */
	0x0a0ba600, 0x00000140, 	/* ccfc.info_reg, group=CONN_CFC_MEM, size=320 regs */
	0x0a0bac00, 0x01000280, 	/* ccfc.cid_cam, group=CONN_CFC_MEM, size=640 regs, WB */
	0x08410000, 	/* block tcfc */
	0x0b0b6200, 0x00000140, 	/* tcfc.activity_counter, group=TASK_CFC_MEM, size=320 regs */
	0x0b0b6400, 0x00000140, 	/* tcfc.info_state, group=TASK_CFC_MEM, size=320 regs */
	0x0b0b6600, 0x00000140, 	/* tcfc.info_reg, group=TASK_CFC_MEM, size=320 regs */
	0x0b0b6c00, 0x01000280, 	/* tcfc.cid_cam, group=TASK_CFC_MEM, size=640 regs, WB */
	0x0a430000, 	/* block cau */
	0x0c070100, 0x00000001, 	/* cau.num_pi_per_sb, group=CAU_PI, size=1 regs */
	0x0d070880, 0x01000010, 	/* cau.igu_cmd_fifo, group=CAU_MEM, size=16 regs, WB */
	0x0d0708c0, 0x01000020, 	/* cau.pxp_req_fifo, group=CAU_MEM, size=32 regs, WB */
	0x0d070900, 0x01000100, 	/* cau.pxp_wdata_fifo, group=CAU_MEM, size=256 regs, WB */
	0x0d071100, 0x00000100, 	/* cau.fsm_table, group=CAU_MEM, size=256 regs */
	0x020d0005, 	/* mode bb, block pswhst */
	0x000a8200, 0x00000100, 	/* pswhst.zone_permission_table, group=PXP_MEM, size=256 regs */
	0x02140000, 	/* block pswrq2 */
	0x0e098000, 0x01003b60, 	/* pswrq2.ilt_memory, group=PXP_ILT, size=15200 regs, WB */
	0x06180000, 	/* block tcm */
	0x02460580, 0x00000020, 	/* tcm.xx_lcid_cam, group=CM_MEM, size=32 regs */
	0x024605c0, 0x00000020, 	/* tcm.xx_tbl, group=CM_MEM, size=32 regs */
	0x02462000, 0x00001600, 	/* tcm.xx_msg_ram, group=CM_MEM, size=5632 regs */
	0x06190000, 	/* block mcm */
	0x02480680, 0x00000016, 	/* mcm.xx_lcid_cam, group=CM_MEM, size=22 regs */
	0x024806c0, 0x00000016, 	/* mcm.xx_tbl, group=CM_MEM, size=22 regs */
	0x02482000, 0x00001a00, 	/* mcm.xx_msg_ram, group=CM_MEM, size=6656 regs */
	0x061a0000, 	/* block ucm */
	0x024a0640, 0x00000018, 	/* ucm.xx_lcid_cam, group=CM_MEM, size=24 regs */
	0x024a0680, 0x00000018, 	/* ucm.xx_tbl, group=CM_MEM, size=24 regs */
	0x024a2000, 0x00001a00, 	/* ucm.xx_msg_ram, group=CM_MEM, size=6656 regs */
	0x041b0000, 	/* block xcm */
	0x02400640, 0x0000001e, 	/* xcm.xx_lcid_cam, group=CM_MEM, size=30 regs */
	0x02400680, 0x0000001e, 	/* xcm.xx_tbl, group=CM_MEM, size=30 regs */
	0x061c0000, 	/* block ycm */
	0x02420680, 0x00000016, 	/* ycm.xx_lcid_cam, group=CM_MEM, size=22 regs */
	0x024206c0, 0x00000016, 	/* ycm.xx_tbl, group=CM_MEM, size=22 regs */
	0x02422000, 0x00001860, 	/* ycm.xx_msg_ram, group=CM_MEM, size=6240 regs */
	0x261e0000, 	/* block qm */
	0x030bc180, 0x00000040, 	/* qm.BaseAddrOtherPq, group=QM_MEM, size=64 regs */
	0x030bc300, 0x01000080, 	/* qm.PtrTblOther, group=QM_MEM, size=128 regs, WB */
	0x030bc800, 0x00000040, 	/* qm.PqFillLvlOther, group=QM_MEM, size=64 regs */
	0x030bca00, 0x00000040, 	/* qm.PqStsOther, group=QM_MEM, size=64 regs */
	0x030bd320, 0x00000008, 	/* qm.RlPfIncVal, group=QM_MEM, size=8 regs */
	0x030bd340, 0x00000008, 	/* qm.RlPfUpperBound, group=QM_MEM, size=8 regs */
	0x030bd360, 0x00000008, 	/* qm.RlPfCrd, group=QM_MEM, size=8 regs */
	0x030bd3a0, 0x00000008, 	/* qm.WfqPfWeight, group=QM_MEM, size=8 regs */
	0x030bd3c0, 0x00000008, 	/* qm.WfqPfUpperBound, group=QM_MEM, size=8 regs */
	0x030bd500, 0x00000090, 	/* qm.WfqPfCrd, group=QM_MEM, size=144 regs */
	0x030bd800, 0x000001c0, 	/* qm.BaseAddrTxPq, group=QM_MEM, size=448 regs */
	0x030bdc00, 0x000001c0, 	/* qm.PqFillLvlTx, group=QM_MEM, size=448 regs */
	0x030be000, 0x000001c0, 	/* qm.PqStsTx, group=QM_MEM, size=448 regs */
	0x030be400, 0x000001c0, 	/* qm.TxPqMap, group=QM_MEM, size=448 regs */
	0x030be800, 0x000001c0, 	/* qm.WfqVpWeight, group=QM_MEM, size=448 regs */
	0x030bec00, 0x000001c0, 	/* qm.WfqVpUpperBound, group=QM_MEM, size=448 regs */
	0x030bf000, 0x000001c0, 	/* qm.WfqVpCrd, group=QM_MEM, size=448 regs */
	0x030bf400, 0x000001c0, 	/* qm.WfqVpMap, group=QM_MEM, size=448 regs */
	0x030bf800, 0x01000380, 	/* qm.PtrTblTx, group=QM_MEM, size=896 regs, WB */
	0x041f0000, 	/* block tm */
	0x0f0b0800, 0x01000130, 	/* tm.config_task_mem, group=TM_MEM, size=304 regs, WB */
	0x0f0b2000, 0x01000a00, 	/* tm.context_mem, group=TM_MEM, size=2560 regs, WB */
	0x04210000, 	/* block brb */
	0x060d0680, 0x00000022, 	/* brb.per_tc_counters, group=BRB_MEM, size=34 regs */
	0x050d2000, 0x000012c0, 	/* brb.link_list, group=BRB_RAM, size=4800 regs */
	0x04250000, 	/* block msdm */
	0x103f0200, 0x00000020, 	/* msdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103f0280, 0x00000020, 	/* msdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04260000, 	/* block usdm */
	0x103f4200, 0x00000020, 	/* usdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103f4280, 0x00000020, 	/* usdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04270000, 	/* block xsdm */
	0x103e0200, 0x00000020, 	/* xsdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e0280, 0x00000020, 	/* xsdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04280000, 	/* block ysdm */
	0x103e4200, 0x00000020, 	/* ysdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e4280, 0x00000020, 	/* ysdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04290000, 	/* block psdm */
	0x103e8200, 0x00000020, 	/* psdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e8280, 0x00000020, 	/* psdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x042a0000, 	/* block tsem */
	0x115c8000, 0x010010e0, 	/* tsem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x125d8000, 0x00005000, 	/* tsem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042b0000, 	/* block msem */
	0x11608000, 0x010010e0, 	/* msem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x12618000, 0x00005000, 	/* msem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042c0000, 	/* block usem */
	0x11648000, 0x01000b40, 	/* usem.passive_buffer, group=PBUF, size=2880 regs, WB */
	0x12658000, 0x00005000, 	/* usem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042d0000, 	/* block xsem */
	0x11508000, 0x010010e0, 	/* xsem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x12518000, 0x00005000, 	/* xsem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042e0000, 	/* block ysem */
	0x11548000, 0x010009d8, 	/* ysem.passive_buffer, group=PBUF, size=2520 regs, WB */
	0x12558000, 0x00005000, 	/* ysem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042f0000, 	/* block psem */
	0x11588000, 0x010002d0, 	/* psem.passive_buffer, group=PBUF, size=720 regs, WB */
	0x12598000, 0x00005000, 	/* psem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x04320000, 	/* block muld */
	0x13139000, 0x01000800, 	/* muld.bd_db_arr_dw, group=MULD_MEM, size=2048 regs, WB */
	0x1313a000, 0x01000800, 	/* muld.sge_db_arr_dw, group=MULD_MEM, size=2048 regs, WB */
	0x063b0000, 	/* block btb */
	0x1436c400, 0x01000008, 	/* btb.stopped_rd_req, group=BTB_MEM, size=8 regs, WB */
	0x1436c440, 0x01000008, 	/* btb.stopped_rls_req, group=BTB_MEM, size=8 regs, WB */
	0x0936d000, 0x00000b40, 	/* btb.link_list, group=BTB_RAM, size=2880 regs */
	0x023d0000, 	/* block rdif */
	0x150c1000, 0x01000a00, 	/* rdif.l1_task_context, group=RDIF_CTX, size=2560 regs, WB */
	0x023e0000, 	/* block tdif */
	0x160c6000, 0x01001400, 	/* tdif.l1_task_context, group=TDIF_CTX, size=5120 regs, WB */
	0x02400000, 	/* block ccfc */
	0x170b81d0, 0x0000000e, 	/* ccfc.lcreq_credit, group=CFC_MEM, size=14 regs */
	0x02410000, 	/* block tcfc */
	0x170b41d0, 0x0000000e, 	/* tcfc.lcreq_credit, group=CFC_MEM, size=14 regs */
	0x0c420000, 	/* block igu */
	0x18060220, 0x00000009, 	/* igu.pending_bits_status, group=IGU_MEM, size=9 regs */
	0x18060240, 0x00000009, 	/* igu.write_done_pending, group=IGU_MEM, size=9 regs */
	0x18060800, 0x00000128, 	/* igu.producer_memory, group=IGU_MEM, size=296 regs */
	0x18060c00, 0x00000128, 	/* igu.consumer_mem, group=IGU_MEM, size=296 regs */
	0x18061000, 0x00000120, 	/* igu.mapping_memory, group=IGU_MEM, size=288 regs */
	0x19061800, 0x01000480, 	/* igu.msix_memory, group=IGU_MSIX, size=1152 regs, WB */
	0x06430000, 	/* block cau */
	0x1a071800, 0x01000240, 	/* cau.sb_var_memory, group=CAU_SB, size=576 regs, WB */
	0x1a072000, 0x01000240, 	/* cau.sb_addr_memory, group=CAU_SB, size=576 regs, WB */
	0x0c074000, 0x00000d80, 	/* cau.pi_memory, group=CAU_PI, size=3456 regs */
	0x020d001b, 	/* mode k2, block pswhst */
	0x000a8200, 0x00000140, 	/* pswhst.zone_permission_table, group=PXP_MEM, size=320 regs */
	0x02140000, 	/* block pswrq2 */
	0x0e098000, 0x010055f0, 	/* pswrq2.ilt_memory, group=PXP_ILT, size=22000 regs, WB */
	0x06180000, 	/* block tcm */
	0x02460580, 0x00000020, 	/* tcm.xx_lcid_cam, group=CM_MEM, size=32 regs */
	0x024605c0, 0x00000020, 	/* tcm.xx_tbl, group=CM_MEM, size=32 regs */
	0x02462000, 0x00001600, 	/* tcm.xx_msg_ram, group=CM_MEM, size=5632 regs */
	0x06190000, 	/* block mcm */
	0x02480680, 0x00000016, 	/* mcm.xx_lcid_cam, group=CM_MEM, size=22 regs */
	0x024806c0, 0x00000016, 	/* mcm.xx_tbl, group=CM_MEM, size=22 regs */
	0x02482000, 0x00001a00, 	/* mcm.xx_msg_ram, group=CM_MEM, size=6656 regs */
	0x061a0000, 	/* block ucm */
	0x024a0640, 0x00000018, 	/* ucm.xx_lcid_cam, group=CM_MEM, size=24 regs */
	0x024a0680, 0x00000018, 	/* ucm.xx_tbl, group=CM_MEM, size=24 regs */
	0x024a2000, 0x00001a00, 	/* ucm.xx_msg_ram, group=CM_MEM, size=6656 regs */
	0x041b0000, 	/* block xcm */
	0x02400640, 0x0000001e, 	/* xcm.xx_lcid_cam, group=CM_MEM, size=30 regs */
	0x02400680, 0x0000001e, 	/* xcm.xx_tbl, group=CM_MEM, size=30 regs */
	0x061c0000, 	/* block ycm */
	0x02420680, 0x00000016, 	/* ycm.xx_lcid_cam, group=CM_MEM, size=22 regs */
	0x024206c0, 0x00000016, 	/* ycm.xx_tbl, group=CM_MEM, size=22 regs */
	0x02422000, 0x00001860, 	/* ycm.xx_msg_ram, group=CM_MEM, size=6240 regs */
	0x061e0000, 	/* block qm */
	0x030bd500, 0x000000a0, 	/* qm.WfqPfCrd, group=QM_MEM, size=160 regs */
	0x030bfc00, 0x00000048, 	/* qm.CMIntQMask_msb, group=QM_MEM, size=72 regs */
	0x030bfd00, 0x000000a0, 	/* qm.WfqPfCrd_msb, group=QM_MEM, size=160 regs */
	0x061f0000, 	/* block tm */
	0x0f0b0400, 0x010001a0, 	/* tm.config_conn_mem, group=TM_MEM, size=416 regs, WB */
	0x0f0b0800, 0x01000200, 	/* tm.config_task_mem, group=TM_MEM, size=512 regs, WB */
	0x0f0b2000, 0x01000a00, 	/* tm.context_mem, group=TM_MEM, size=2560 regs, WB */
	0x04210000, 	/* block brb */
	0x060d0680, 0x00000022, 	/* brb.per_tc_counters, group=BRB_MEM, size=34 regs */
	0x050d2000, 0x00001e00, 	/* brb.link_list, group=BRB_RAM, size=7680 regs */
	0x04250000, 	/* block msdm */
	0x103f0200, 0x00000020, 	/* msdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103f0280, 0x00000020, 	/* msdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04260000, 	/* block usdm */
	0x103f4200, 0x00000020, 	/* usdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103f4280, 0x00000020, 	/* usdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04270000, 	/* block xsdm */
	0x103e0200, 0x00000020, 	/* xsdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e0280, 0x00000020, 	/* xsdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04280000, 	/* block ysdm */
	0x103e4200, 0x00000020, 	/* ysdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e4280, 0x00000020, 	/* ysdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x04290000, 	/* block psdm */
	0x103e8200, 0x00000020, 	/* psdm.agg_int_ctrl, group=SDM_MEM, size=32 regs */
	0x103e8280, 0x00000020, 	/* psdm.agg_int_state, group=SDM_MEM, size=32 regs */
	0x042a0000, 	/* block tsem */
	0x115c8000, 0x010010e0, 	/* tsem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x125d8000, 0x00005000, 	/* tsem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042b0000, 	/* block msem */
	0x11608000, 0x010010e0, 	/* msem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x12618000, 0x00005000, 	/* msem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042c0000, 	/* block usem */
	0x11648000, 0x01000b40, 	/* usem.passive_buffer, group=PBUF, size=2880 regs, WB */
	0x12658000, 0x00005000, 	/* usem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042d0000, 	/* block xsem */
	0x11508000, 0x010010e0, 	/* xsem.passive_buffer, group=PBUF, size=4320 regs, WB */
	0x12518000, 0x00005000, 	/* xsem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042e0000, 	/* block ysem */
	0x11548000, 0x010009d8, 	/* ysem.passive_buffer, group=PBUF, size=2520 regs, WB */
	0x12558000, 0x00005000, 	/* ysem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x042f0000, 	/* block psem */
	0x11588000, 0x010002d0, 	/* psem.passive_buffer, group=PBUF, size=720 regs, WB */
	0x12598000, 0x00005000, 	/* psem.fast_memory.int_ram, group=RAM, size=20480 regs */
	0x04320000, 	/* block muld */
	0x13139000, 0x01000a00, 	/* muld.bd_db_arr_dw, group=MULD_MEM, size=2560 regs, WB */
	0x1313a000, 0x01000a00, 	/* muld.sge_db_arr_dw, group=MULD_MEM, size=2560 regs, WB */
	0x043b0000, 	/* block btb */
	0x1436c400, 0x01000010, 	/* btb.stopped_rd_req, group=BTB_MEM, size=16 regs, WB */
	0x0936d000, 0x00000e60, 	/* btb.link_list, group=BTB_RAM, size=3680 regs */
	0x023d0000, 	/* block rdif */
	0x150c1000, 0x01000a00, 	/* rdif.l1_task_context, group=RDIF_CTX, size=2560 regs, WB */
	0x023e0000, 	/* block tdif */
	0x160c6000, 0x01001400, 	/* tdif.l1_task_context, group=TDIF_CTX, size=5120 regs, WB */
	0x02400000, 	/* block ccfc */
	0x170b81d0, 0x0000000e, 	/* ccfc.lcreq_credit, group=CFC_MEM, size=14 regs */
	0x02410000, 	/* block tcfc */
	0x170b41d0, 0x0000000e, 	/* tcfc.lcreq_credit, group=CFC_MEM, size=14 regs */
	0x0c420000, 	/* block igu */
	0x18060220, 0x0000000c, 	/* igu.pending_bits_status, group=IGU_MEM, size=12 regs */
	0x18060240, 0x0000000c, 	/* igu.write_done_pending, group=IGU_MEM, size=12 regs */
	0x18060800, 0x00000180, 	/* igu.producer_memory, group=IGU_MEM, size=384 regs */
	0x18060c00, 0x00000180, 	/* igu.consumer_mem, group=IGU_MEM, size=384 regs */
	0x18061000, 0x00000170, 	/* igu.mapping_memory, group=IGU_MEM, size=368 regs */
	0x19061800, 0x010005c0, 	/* igu.msix_memory, group=IGU_MSIX, size=1472 regs, WB */
	0x06430000, 	/* block cau */
	0x1a071800, 0x010002e0, 	/* cau.sb_var_memory, group=CAU_SB, size=736 regs, WB */
	0x1a072000, 0x010002e0, 	/* cau.sb_addr_memory, group=CAU_SB, size=736 regs, WB */
	0x0c074000, 0x00001140, 	/* cau.pi_memory, group=CAU_PI, size=4416 regs */
	0x241e0003, 	/* mode !bb, block qm */
	0x030bc180, 0x00000080, 	/* qm.BaseAddrOtherPq, group=QM_MEM, size=128 regs */
	0x030bc300, 0x01000100, 	/* qm.PtrTblOther, group=QM_MEM, size=256 regs, WB */
	0x030bc800, 0x00000080, 	/* qm.PqFillLvlOther, group=QM_MEM, size=128 regs */
	0x030bca00, 0x00000080, 	/* qm.PqStsOther, group=QM_MEM, size=128 regs */
	0x030bd320, 0x00000010, 	/* qm.RlPfIncVal, group=QM_MEM, size=16 regs */
	0x030bd340, 0x00000010, 	/* qm.RlPfUpperBound, group=QM_MEM, size=16 regs */
	0x030bd360, 0x00000010, 	/* qm.RlPfCrd, group=QM_MEM, size=16 regs */
	0x030bd3a0, 0x00000010, 	/* qm.WfqPfWeight, group=QM_MEM, size=16 regs */
	0x030bd3c0, 0x00000010, 	/* qm.WfqPfUpperBound, group=QM_MEM, size=16 regs */
	0x030bd800, 0x00000200, 	/* qm.BaseAddrTxPq, group=QM_MEM, size=512 regs */
	0x030bdc00, 0x00000200, 	/* qm.PqFillLvlTx, group=QM_MEM, size=512 regs */
	0x030be000, 0x00000200, 	/* qm.PqStsTx, group=QM_MEM, size=512 regs */
	0x030be400, 0x00000200, 	/* qm.TxPqMap, group=QM_MEM, size=512 regs */
	0x030be800, 0x00000200, 	/* qm.WfqVpWeight, group=QM_MEM, size=512 regs */
	0x030bec00, 0x00000200, 	/* qm.WfqVpUpperBound, group=QM_MEM, size=512 regs */
	0x030bf000, 0x00000200, 	/* qm.WfqVpCrd, group=QM_MEM, size=512 regs */
	0x030bf400, 0x00000200, 	/* qm.WfqVpMap, group=QM_MEM, size=512 regs */
	0x030bf800, 0x01000400, 	/* qm.PtrTblTx, group=QM_MEM, size=1024 regs, WB */
	0x023b0000, 	/* block btb */
	0x1436c440, 0x01000010, 	/* btb.stopped_rls_req, group=BTB_MEM, size=16 regs, WB */
	0x020d0015, 	/* mode !(bb|k2), block pswhst */
	0x000a8200, 0x00000200, 	/* pswhst.zone_permission_table, group=PXP_MEM, size=512 regs */
	0x02140000, 	/* block pswrq2 */
	0x0e098000, 0x0100672e, 	/* pswrq2.ilt_memory, group=PXP_ILT, size=26414 regs, WB */
	0x06180000, 	/* block tcm */
	0x02460580, 0x00000040, 	/* tcm.xx_lcid_cam, group=CM_MEM, size=64 regs */
	0x024605c0, 0x00000040, 	/* tcm.xx_tbl, group=CM_MEM, size=64 regs */
	0x02462000, 0x00001800, 	/* tcm.xx_msg_ram, group=CM_MEM, size=6144 regs */
	0x06190000, 	/* block mcm */
	0x02480680, 0x00000040, 	/* mcm.xx_lcid_cam, group=CM_MEM, size=64 regs */
	0x024806c0, 0x00000040, 	/* mcm.xx_tbl, group=CM_MEM, size=64 regs */
	0x02484000, 0x00002400, 	/* mcm.xx_msg_ram, group=CM_MEM, size=9216 regs */
	0x061a0000, 	/* block ucm */
	0x024a0640, 0x00000040, 	/* ucm.xx_lcid_cam, group=CM_MEM, size=64 regs */
	0x024a0680, 0x00000040, 	/* ucm.xx_tbl, group=CM_MEM, size=64 regs */
	0x024a2000, 0x00002000, 	/* ucm.xx_msg_ram, group=CM_MEM, size=8192 regs */
	0x041b0000, 	/* block xcm */
	0x02400640, 0x00000040, 	/* xcm.xx_lcid_cam, group=CM_MEM, size=64 regs */
	0x02400680, 0x00000040, 	/* xcm.xx_tbl, group=CM_MEM, size=64 regs */
	0x061c0000, 	/* block ycm */
	0x02420680, 0x00000040, 	/* ycm.xx_lcid_cam, group=CM_MEM, size=64 regs */
	0x024206c0, 0x00000040, 	/* ycm.xx_tbl, group=CM_MEM, size=64 regs */
	0x02424000, 0x00002c00, 	/* ycm.xx_msg_ram, group=CM_MEM, size=11264 regs */
	0x061e0000, 	/* block qm */
	0x030bd500, 0x00000100, 	/* qm.WfqPfCrd, group=QM_MEM, size=256 regs */
	0x030bfc00, 0x00000140, 	/* qm.WfqPfCrd_msb, group=QM_MEM, size=320 regs */
	0x030bfe00, 0x00000048, 	/* qm.CMIntQMask_msb, group=QM_MEM, size=72 regs */
	0x041f0000, 	/* block tm */
	0x0f0b0800, 0x01000260, 	/* tm.config_task_mem, group=TM_MEM, size=608 regs, WB */
	0x0f0b2000, 0x01000b00, 	/* tm.context_mem, group=TM_MEM, size=2816 regs, WB */
	0x04210000, 	/* block brb */
	0x060d0680, 0x00000024, 	/* brb.per_tc_counters, group=BRB_MEM, size=36 regs */
	0x050d4000, 0x00002280, 	/* brb.link_list, group=BRB_RAM, size=8832 regs */
	0x04250000, 	/* block msdm */
	0x103f0200, 0x00000010, 	/* msdm.agg_int_ctrl, group=SDM_MEM, size=16 regs */
	0x103f0280, 0x00000010, 	/* msdm.agg_int_state, group=SDM_MEM, size=16 regs */
	0x04260000, 	/* block usdm */
	0x103f4200, 0x00000010, 	/* usdm.agg_int_ctrl, group=SDM_MEM, size=16 regs */
	0x103f4280, 0x00000010, 	/* usdm.agg_int_state, group=SDM_MEM, size=16 regs */
	0x04270000, 	/* block xsdm */
	0x103e0200, 0x00000010, 	/* xsdm.agg_int_ctrl, group=SDM_MEM, size=16 regs */
	0x103e0280, 0x00000010, 	/* xsdm.agg_int_state, group=SDM_MEM, size=16 regs */
	0x04280000, 	/* block ysdm */
	0x103e4200, 0x00000010, 	/* ysdm.agg_int_ctrl, group=SDM_MEM, size=16 regs */
	0x103e4280, 0x00000010, 	/* ysdm.agg_int_state, group=SDM_MEM, size=16 regs */
	0x04290000, 	/* block psdm */
	0x103e8200, 0x00000010, 	/* psdm.agg_int_ctrl, group=SDM_MEM, size=16 regs */
	0x103e8280, 0x00000010, 	/* psdm.agg_int_state, group=SDM_MEM, size=16 regs */
	0x042a0000, 	/* block tsem */
	0x115c8000, 0x00003100, 	/* tsem.passive_buffer, group=PBUF, size=12544 regs */
	0x125d8000, 0x000070c8, 	/* tsem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x042b0000, 	/* block msem */
	0x11608000, 0x00003100, 	/* msem.passive_buffer, group=PBUF, size=12544 regs */
	0x12618000, 0x000070c8, 	/* msem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x042c0000, 	/* block usem */
	0x11648000, 0x00003100, 	/* usem.passive_buffer, group=PBUF, size=12544 regs */
	0x12658000, 0x000070c8, 	/* usem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x042d0000, 	/* block xsem */
	0x11508000, 0x00003100, 	/* xsem.passive_buffer, group=PBUF, size=12544 regs */
	0x12518000, 0x000070c8, 	/* xsem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x042e0000, 	/* block ysem */
	0x11548000, 0x00003100, 	/* ysem.passive_buffer, group=PBUF, size=12544 regs */
	0x12558000, 0x000070c8, 	/* ysem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x042f0000, 	/* block psem */
	0x11588000, 0x00003100, 	/* psem.passive_buffer, group=PBUF, size=12544 regs */
	0x12598000, 0x000070c8, 	/* psem.fast_memory.int_ram, group=RAM, size=28872 regs */
	0x04320000, 	/* block muld */
	0x13139000, 0x01001000, 	/* muld.bd_db_arr_dw, group=MULD_MEM, size=4096 regs, WB */
	0x1313a000, 0x01001000, 	/* muld.sge_db_arr_dw, group=MULD_MEM, size=4096 regs, WB */
	0x043b0000, 	/* block btb */
	0x1436c400, 0x01000020, 	/* btb.stopped_rd_req, group=BTB_MEM, size=32 regs, WB */
	0x0936e000, 0x000014a0, 	/* btb.link_list, group=BTB_RAM, size=5280 regs */
	0x023d0000, 	/* block rdif */
	0x150c2000, 0x01001800, 	/* rdif.l1_task_context, group=RDIF_CTX, size=6144 regs, WB */
	0x023e0000, 	/* block tdif */
	0x160c6000, 0x01001800, 	/* tdif.l1_task_context, group=TDIF_CTX, size=6144 regs, WB */
	0x02400000, 	/* block ccfc */
	0x170b81d0, 0x0000000d, 	/* ccfc.lcreq_credit, group=CFC_MEM, size=13 regs */
	0x02410000, 	/* block tcfc */
	0x170b41d0, 0x0000000d, 	/* tcfc.lcreq_credit, group=CFC_MEM, size=13 regs */
	0x0c420000, 	/* block igu */
	0x18060220, 0x00000010, 	/* igu.pending_bits_status, group=IGU_MEM, size=16 regs */
	0x18060240, 0x00000010, 	/* igu.write_done_pending, group=IGU_MEM, size=16 regs */
	0x18060800, 0x00000210, 	/* igu.producer_memory, group=IGU_MEM, size=528 regs */
	0x18060c00, 0x00000210, 	/* igu.consumer_mem, group=IGU_MEM, size=528 regs */
	0x18061000, 0x00000200, 	/* igu.mapping_memory, group=IGU_MEM, size=512 regs */
	0x19061800, 0x01000800, 	/* igu.msix_memory, group=IGU_MSIX, size=2048 regs, WB */
	0x06430000, 	/* block cau */
	0x1a071800, 0x01000400, 	/* cau.sb_var_memory, group=CAU_SB, size=1024 regs, WB */
	0x1a072000, 0x01000400, 	/* cau.sb_addr_memory, group=CAU_SB, size=1024 regs, WB */
	0x0c074000, 0x00001000, 	/* cau.pi_memory, group=CAU_PI, size=4096 regs */
	0x02230083, 	/* mode !e5, block prs */
	0x0707c300, 0x00000080, 	/* prs.last_pkt_list, group=PRS_MEM, size=128 regs */
	0x022a0000, 	/* block tsem */
	0x115c02c4, 0x00000001, 	/* tsem.thread_valid, group=PBUF, size=1 regs */
	0x022b0000, 	/* block msem */
	0x116002c4, 0x00000001, 	/* msem.thread_valid, group=PBUF, size=1 regs */
	0x022c0000, 	/* block usem */
	0x116402c4, 0x00000001, 	/* usem.thread_valid, group=PBUF, size=1 regs */
	0x022d0000, 	/* block xsem */
	0x115002c4, 0x00000001, 	/* xsem.thread_valid, group=PBUF, size=1 regs */
	0x022e0000, 	/* block ysem */
	0x115402c4, 0x00000001, 	/* ysem.thread_valid, group=PBUF, size=1 regs */
	0x022f0000, 	/* block psem */
	0x115802c4, 0x00000001, 	/* psem.thread_valid, group=PBUF, size=1 regs */
	0x04430000, 	/* block cau */
	0x0d070800, 0x01000078, 	/* cau.cqe_fifo, group=CAU_MEM, size=120 regs, WB */
	0x0d077000, 0x01000100, 	/* cau.agg_unit_descriptor, group=CAU_MEM, size=256 regs, WB */
	0x021f01e5, 	/* mode !k2, block tm */
	0x0f0b0400, 0x01000100, 	/* tm.config_conn_mem, group=TM_MEM, size=256 regs, WB */
	0x0809001d, 	/* mode !(emul_reduced|fpga), block bmb */
	0x1b150204, 0x00000004, 	/* bmb.free_list_head, group=BMB_RAM, size=4 regs */
	0x1b150208, 0x00000004, 	/* bmb.free_list_tail, group=BMB_RAM, size=4 regs */
	0x1b15020c, 0x00000004, 	/* bmb.free_list_size, group=BMB_RAM, size=4 regs */
	0x1b150800, 0x00000480, 	/* bmb.link_list, group=BMB_RAM, size=1152 regs */
	0x04090013, 	/* mode (!(bb|k2))&(!(emul_reduced|fpga)), block bmb */
	0x1c150480, 0x01000006, 	/* bmb.stopped_rd_req, group=BMB_MEM, size=6 regs, WB */
	0x1c1504c0, 0x0100000c, 	/* bmb.stopped_rls_req, group=BMB_MEM, size=12 regs, WB */
	0x040900c7, 	/* mode bb&(!(emul_reduced|fpga)), block bmb */
	0x1c150480, 0x01000014, 	/* bmb.stopped_rd_req, group=BMB_MEM, size=20 regs, WB */
	0x1c1504c0, 0x01000028, 	/* bmb.stopped_rls_req, group=BMB_MEM, size=40 regs, WB */
	0x040900f7, 	/* mode k2&(!(emul_reduced|fpga)), block bmb */
	0x1c150480, 0x01000014, 	/* bmb.stopped_rd_req, group=BMB_MEM, size=20 regs, WB */
	0x1c1504c0, 0x01000028, 	/* bmb.stopped_rls_req, group=BMB_MEM, size=40 regs, WB */
	0x081e0147, 	/* mode bb&(!(!asic)), block qm */
	0x030bc560, 0x00000012, 	/* qm.VoqCrdLine, group=QM_MEM, size=18 regs */
	0x030bc5a0, 0x00000012, 	/* qm.VoqInitCrdLine, group=QM_MEM, size=18 regs */
	0x030bc5e0, 0x00000012, 	/* qm.VoqCrdByte, group=QM_MEM, size=18 regs */
	0x030bc620, 0x00000012, 	/* qm.VoqInitCrdByte, group=QM_MEM, size=18 regs */
	0x081e015b, 	/* mode k2&(!(!asic)), block qm */
	0x030bc560, 0x00000014, 	/* qm.VoqCrdLine, group=QM_MEM, size=20 regs */
	0x030bc5a0, 0x00000014, 	/* qm.VoqInitCrdLine, group=QM_MEM, size=20 regs */
	0x030bc5e0, 0x00000014, 	/* qm.VoqCrdByte, group=QM_MEM, size=20 regs */
	0x030bc620, 0x00000014, 	/* qm.VoqInitCrdByte, group=QM_MEM, size=20 regs */
	0x081e0035, 	/* mode (!(bb|k2))&(!(!asic)), block qm */
	0x030bfe80, 0x00000024, 	/* qm.VoqCrdLine, group=QM_MEM, size=36 regs */
	0x030bfec0, 0x00000024, 	/* qm.VoqInitCrdLine, group=QM_MEM, size=36 regs */
	0x030bff00, 0x00000024, 	/* qm.VoqCrdByte, group=QM_MEM, size=36 regs */
	0x030bff40, 0x00000024, 	/* qm.VoqInitCrdByte, group=QM_MEM, size=36 regs */
};
/* Data size: 3092 bytes */

#endif /* __PREVENT_DUMP_MEM_ARR__ */

/* Idle check registers */
static const u32 idle_chk_regs[] = {
	0x02002060, 0x00010001, 	/* cond: misc.INT_STS */
	0x02002061, 0x00010001, 	/* cond: misc.INT_MASK */
	0x020021ed, 0x00010001, 	/* cond: misc.aeu_after_invert_1_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021ee, 0x00010001, 	/* cond: misc.aeu_after_invert_2_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f0, 0x00010001, 	/* cond: misc.aeu_after_invert_4_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f1, 0x00010001, 	/* cond: misc.aeu_after_invert_5_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f2, 0x00010001, 	/* cond: misc.aeu_after_invert_6_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f3, 0x00010001, 	/* cond: misc.aeu_after_invert_7_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f4, 0x00010001, 	/* cond: misc.aeu_after_invert_8_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f5, 0x00010001, 	/* cond: misc.aeu_after_invert_9_igu */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f6, 0x00010001, 	/* cond: misc.aeu_after_invert_1_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f7, 0x00010001, 	/* cond: misc.aeu_after_invert_2_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021f9, 0x00010001, 	/* cond: misc.aeu_after_invert_4_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021fa, 0x00010001, 	/* cond: misc.aeu_after_invert_5_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021fb, 0x00010001, 	/* cond: misc.aeu_after_invert_6_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021fc, 0x00010001, 	/* cond: misc.aeu_after_invert_7_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021fd, 0x00010001, 	/* cond: misc.aeu_after_invert_8_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021fe, 0x00010001, 	/* cond: misc.aeu_after_invert_9_mcp */
	0x0200220f, 0x00000001, 	/* info: misc.attn_num_st mode=all */
	0x020021ff, 0x00010001, 	/* cond: misc.aeu_sys_kill_occurred */
	0x01002460, 0x00010001, 	/* cond: miscs.INT_STS_0 */
	0x01002461, 0x00010001, 	/* cond: miscs.INT_MASK_0 */
	0x010025bf, 0x00010001, 	/* cond: miscs.pcie_hot_reset */
	0x16003012, 0x00010001, 	/* cond: dmae.go_c0 */
	0x16003013, 0x00010001, 	/* cond: dmae.go_c1 */
	0x16003014, 0x00010001, 	/* cond: dmae.go_c2 */
	0x16003015, 0x00010001, 	/* cond: dmae.go_c3 */
	0x16003016, 0x00010001, 	/* cond: dmae.go_c4 */
	0x16003017, 0x00010001, 	/* cond: dmae.go_c5 */
	0x16003018, 0x00010001, 	/* cond: dmae.go_c6 */
	0x16003019, 0x00010001, 	/* cond: dmae.go_c7 */
	0x1600301a, 0x00010001, 	/* cond: dmae.go_c8 */
	0x1600301b, 0x00010001, 	/* cond: dmae.go_c9 */
	0x1600301c, 0x00010001, 	/* cond: dmae.go_c10 */
	0x1600301d, 0x00010001, 	/* cond: dmae.go_c11 */
	0x1600301e, 0x00010001, 	/* cond: dmae.go_c12 */
	0x1600301f, 0x00010001, 	/* cond: dmae.go_c13 */
	0x16003020, 0x00010001, 	/* cond: dmae.go_c14 */
	0x16003021, 0x00010001, 	/* cond: dmae.go_c15 */
	0x16003022, 0x00010001, 	/* cond: dmae.go_c16 */
	0x16003023, 0x00010001, 	/* cond: dmae.go_c17 */
	0x16003024, 0x00010001, 	/* cond: dmae.go_c18 */
	0x16003025, 0x00010001, 	/* cond: dmae.go_c19 */
	0x16003026, 0x00010001, 	/* cond: dmae.go_c20 */
	0x16003027, 0x00010001, 	/* cond: dmae.go_c21 */
	0x16003028, 0x00010001, 	/* cond: dmae.go_c22 */
	0x16003029, 0x00010001, 	/* cond: dmae.go_c23 */
	0x1600302a, 0x00010001, 	/* cond: dmae.go_c24 */
	0x1600302b, 0x00010001, 	/* cond: dmae.go_c25 */
	0x1600302c, 0x00010001, 	/* cond: dmae.go_c26 */
	0x1600302d, 0x00010001, 	/* cond: dmae.go_c27 */
	0x1600302e, 0x00010001, 	/* cond: dmae.go_c28 */
	0x1600302f, 0x00010001, 	/* cond: dmae.go_c29 */
	0x16003030, 0x00010001, 	/* cond: dmae.go_c30 */
	0x16003031, 0x00010001, 	/* cond: dmae.go_c31 */
	0x16003080, 0x00010001, 	/* cond: dmae.PRTY_STS_H_0 */
	0x16003081, 0x00010001, 	/* cond: dmae.PRTY_MASK_H_0 */
	0x4a004080, 0x00010001, 	/* cond: dbg.PRTY_STS_H_0 */
	0x4a004081, 0x00010001, 	/* cond: dbg.PRTY_MASK_H_0 */
	0x00014019, 0x00010001, 	/* cond: grc.trace_fifo_valid_data */
	0x00014060, 0x00010001, 	/* cond: grc.INT_STS_0 */
	0x00014061, 0x00010001, 	/* cond: grc.INT_MASK_0 */
	0x00014080, 0x00010001, 	/* cond: grc.PRTY_STS_H_0 */
	0x00014081, 0x00010001, 	/* cond: grc.PRTY_MASK_H_0 */
	0x20040060, 0x00010001, 	/* cond: dorq.INT_STS */
	0x20040061, 0x00010001, 	/* cond: dorq.INT_MASK */
	0x20040080, 0x00010001, 	/* cond: dorq.PRTY_STS_H_0 */
	0x20040081, 0x00010001, 	/* cond: dorq.PRTY_MASK_H_0 */
	0x20040260, 0x00010001, 	/* cond: dorq.xcm_msg_init_crd */
	0x20040261, 0x00010001, 	/* cond: dorq.tcm_msg_init_crd */
	0x20040262, 0x00010001, 	/* cond: dorq.ucm_msg_init_crd */
	0x20040263, 0x00010001, 	/* cond: dorq.pbf_cmd_init_crd */
	0x20040270, 0x00010001, 	/* cond: dorq.pf_usage_cnt */
	0x20040271, 0x00010001, 	/* cond: dorq.vf_usage_cnt */
	0x20040282, 0x00010001, 	/* cond: dorq.cfc_ld_req_fifo_fill_lvl */
	0x20040283, 0x00010001, 	/* cond: dorq.dorq_fifo_fill_lvl */
	0x20040286, 0x00010001, 	/* cond: dorq.db_drop_cnt */
	0x20040289, 0x00000001, 	/* info: dorq.db_drop_details mode=all */
	0x20040288, 0x00000001, 	/* info: dorq.db_drop_details_reason mode=all */
	0x2004028c, 0x00010001, 	/* cond: dorq.dpm_abort_cnt */
	0x20040293, 0x00000001, 	/* info: dorq.dpm_abort_reason mode=all */
	0x20040291, 0x00000001, 	/* info: dorq.dpm_abort_details_reason mode=all */
	0x20040296, 0x00010001, 	/* cond: dorq.dpm_timer_expir_abort_cnt */
	0x200402a2, 0x00010001, 	/* cond: dorq.dpm_tbl_fill_lvl */
	0x42060064, 0x00010001, 	/* cond: igu.PRTY_STS */
	0x42060065, 0x00010001, 	/* cond: igu.PRTY_MASK */
	0x42060080, 0x00010001, 	/* cond: igu.PRTY_STS_H_0 */
	0x42060081, 0x00010001, 	/* cond: igu.PRTY_MASK_H_0 */
	0x42060545, 0x00010001, 	/* cond: igu.attn_write_done_pending */
	0x42060547, 0x00010001, 	/* cond: igu.Interrupt_status */
	0x4206054c, 0x00010001, 	/* cond: igu.error_handling_data_valid */
	0x4206054d, 0x00010001, 	/* cond: igu.silent_drop */
	0x4206054f, 0x00010001, 	/* cond: igu.sb_ctrl_fsm */
	0x42060550, 0x00010001, 	/* cond: igu.int_handle_fsm */
	0x42060551, 0x00010001, 	/* cond: igu.attn_fsm */
	0x42060555, 0x00010001, 	/* cond: igu.ctrl_fsm */
	0x42060556, 0x00010001, 	/* cond: igu.pxp_arb_fsm */
	0x43070080, 0x00010001, 	/* cond: cau.PRTY_STS_H_0 */
	0x43070081, 0x00010001, 	/* cond: cau.PRTY_MASK_H_0 */
	0x43070260, 0x00010001, 	/* cond: cau.igu_req_credit_status */
	0x43070261, 0x00010001, 	/* cond: cau.igu_cmd_credit_status */
	0x43070320, 0x00010001, 	/* cond: cau.debug_fifo_status */
	0x43070321, 0x00010001, 	/* cond: cau.error_pxp_req */
	0x43070322, 0x00010001, 	/* cond: cau.error_fsm_line */
	0x43070323, 0x00000001, 	/* info: cau.error_fsm_line_pre mode=all */
	0x43070324, 0x00010001, 	/* cond: cau.parity_latch_status */
	0x43070325, 0x00010001, 	/* cond: cau.error_cleanup_cmd_reg */
	0x43070380, 0x00010001, 	/* cond: cau.req_counter */
	0x43070382, 0x00010001, 	/* cond: cau.wdone_counter */
	0x43070380, 0x00010001, 	/* cond: cau.req_counter */
	0x43070381, 0x00010001, 	/* cond: cau.ack_counter */
	0x430703c0, 0x00010001, 	/* cond: cau.main_fsm_status */
	0x430703c1, 0x00010001, 	/* cond: cau.var_read_fsm_status */
	0x430703c2, 0x00010001, 	/* cond: cau.igu_dma_fsm_status */
	0x2307c010, 0x00010001, 	/* cond: prs.INT_STS_0 */
	0x2307c011, 0x00010001, 	/* cond: prs.INT_MASK_0 */
	0x2307c2da, 0x00000001, 	/* info: prs.mini_cache_failed_response mode=all */
	0x2387c2d8, 0x00000002, 	/* info: prs.mini_cache_entry width=2 access=WB mode=all */
	0x00014019, 0x00000001, 	/* info: grc.trace_fifo_valid_data mode=all */
	0x2307c014, 0x00010001, 	/* cond: prs.PRTY_STS */
	0x2307c015, 0x00010001, 	/* cond: prs.PRTY_MASK */
	0x2307c081, 0x00010001, 	/* cond: prs.PRTY_STS_H_0 */
	0x2307c082, 0x00010001, 	/* cond: prs.PRTY_MASK_H_0 */
	0x2307c2d5, 0x00010001, 	/* cond: prs.queue_pkt_avail_status */
	0x2307c2d6, 0x00010001, 	/* cond: prs.storm_bkprs_status */
	0x2307c2d7, 0x00010001, 	/* cond: prs.stop_parsing_status */
	0x2307c3c4, 0x00010001, 	/* cond: prs.ccfc_search_current_credit */
	0x2307c3c5, 0x00010001, 	/* cond: prs.tcfc_search_current_credit */
	0x2307c3c6, 0x00010001, 	/* cond: prs.ccfc_load_current_credit */
	0x2307c3c7, 0x00010001, 	/* cond: prs.tcfc_load_current_credit */
	0x2307c3c8, 0x00010001, 	/* cond: prs.ccfc_search_req_ct */
	0x2307c3c9, 0x00010001, 	/* cond: prs.tcfc_search_req_ct */
	0x2307c3ca, 0x00010001, 	/* cond: prs.ccfc_load_req_ct */
	0x2307c3cb, 0x00010001, 	/* cond: prs.tcfc_load_req_ct */
	0x2307c3cc, 0x00010001, 	/* cond: prs.sop_req_ct */
	0x2307c3cd, 0x00010001, 	/* cond: prs.eop_req_ct */
	0x3708c010, 0x00010001, 	/* cond: prm.INT_STS */
	0x3708c011, 0x00010001, 	/* cond: prm.INT_MASK */
	0x3708c080, 0x00010001, 	/* cond: prm.PRTY_STS_H_0 */
	0x3708c081, 0x00010001, 	/* cond: prm.PRTY_MASK_H_0 */
	0x3008e202, 0x00010001, 	/* cond: rss.rss_init_done */
	0x3008e260, 0x00010001, 	/* cond: rss.INT_STS */
	0x3008e261, 0x00010001, 	/* cond: rss.INT_MASK */
	0x3008e280, 0x00010001, 	/* cond: rss.PRTY_STS_H_0 */
	0x3008e281, 0x00010001, 	/* cond: rss.PRTY_MASK_H_0 */
	0x14090000, 0x00010001, 	/* cond: pswrq2.rbc_done */
	0x14090001, 0x00010001, 	/* cond: pswrq2.cfg_done */
	0x14090060, 0x00010001, 	/* cond: pswrq2.INT_STS */
	0x14090061, 0x00010001, 	/* cond: pswrq2.INT_MASK */
	0x14090080, 0x00010001, 	/* cond: pswrq2.PRTY_STS_H_0 */
	0x14090081, 0x00010001, 	/* cond: pswrq2.PRTY_MASK_H_0 */
	0x14090115, 0x00010020, 	/* cond: pswrq2.vq0_entry_cnt[0:31] */
	0x1409024a, 0x00010001, 	/* cond: pswrq2.l2p_err_add_31_0 */
	0x1409024b, 0x00010001, 	/* cond: pswrq2.l2p_err_add_63_32 */
	0x1409024c, 0x00010001, 	/* cond: pswrq2.l2p_err_details */
	0x1409024d, 0x00010001, 	/* cond: pswrq2.l2p_err_details2 */
	0x14090271, 0x00010001, 	/* cond: pswrq2.sr_cnt */
	0x1409024f, 0x00010001, 	/* cond: pswrq2.sr_num_cfg */
	0x14090271, 0x00010001, 	/* cond: pswrq2.sr_cnt */
	0x1409024f, 0x00010001, 	/* cond: pswrq2.sr_num_cfg */
	0x14090272, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_0 */
	0x140902d1, 0x00010001, 	/* cond: pswrq2.max_srs_vq0 */
	0x14090273, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_1 */
	0x140902d2, 0x00010001, 	/* cond: pswrq2.max_srs_vq1 */
	0x14090274, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_2 */
	0x140902d3, 0x00010001, 	/* cond: pswrq2.max_srs_vq2 */
	0x14090275, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_3 */
	0x140902d4, 0x00010001, 	/* cond: pswrq2.max_srs_vq3 */
	0x14090276, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_4 */
	0x140902d5, 0x00010001, 	/* cond: pswrq2.max_srs_vq4 */
	0x14090277, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_5 */
	0x140902d6, 0x00010001, 	/* cond: pswrq2.max_srs_vq5 */
	0x14090278, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_6 */
	0x140902d7, 0x00010001, 	/* cond: pswrq2.max_srs_vq6 */
	0x1409027b, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_9 */
	0x140902da, 0x00010001, 	/* cond: pswrq2.max_srs_vq9 */
	0x1409027d, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_11 */
	0x140902dc, 0x00010001, 	/* cond: pswrq2.max_srs_vq11 */
	0x1409027e, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_12 */
	0x140902dd, 0x00010001, 	/* cond: pswrq2.max_srs_vq12 */
	0x14090281, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_15 */
	0x140902e0, 0x00010001, 	/* cond: pswrq2.max_srs_vq15 */
	0x14090282, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_16 */
	0x140902e1, 0x00010001, 	/* cond: pswrq2.max_srs_vq16 */
	0x14090283, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_17 */
	0x140902e2, 0x00010001, 	/* cond: pswrq2.max_srs_vq17 */
	0x14090284, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_18 */
	0x140902e3, 0x00010001, 	/* cond: pswrq2.max_srs_vq18 */
	0x14090285, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_19 */
	0x140902e4, 0x00010001, 	/* cond: pswrq2.max_srs_vq19 */
	0x14090287, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_21 */
	0x140902e6, 0x00010001, 	/* cond: pswrq2.max_srs_vq21 */
	0x14090288, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_22 */
	0x140902e7, 0x00010001, 	/* cond: pswrq2.max_srs_vq22 */
	0x14090289, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_23 */
	0x140902e8, 0x00010001, 	/* cond: pswrq2.max_srs_vq23 */
	0x1409028a, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_24 */
	0x140902e9, 0x00010001, 	/* cond: pswrq2.max_srs_vq24 */
	0x1409028c, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_26 */
	0x140902eb, 0x00010001, 	/* cond: pswrq2.max_srs_vq26 */
	0x1409028e, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_28 */
	0x140902ed, 0x00010001, 	/* cond: pswrq2.max_srs_vq28 */
	0x1409028f, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_29 */
	0x140902ee, 0x00010001, 	/* cond: pswrq2.max_srs_vq29 */
	0x14090290, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_30 */
	0x140902ef, 0x00010001, 	/* cond: pswrq2.max_srs_vq30 */
	0x14090292, 0x00010001, 	/* cond: pswrq2.blk_cnt */
	0x14090250, 0x00010001, 	/* cond: pswrq2.blk_num_cfg */
	0x14090292, 0x00010001, 	/* cond: pswrq2.blk_cnt */
	0x14090250, 0x00010001, 	/* cond: pswrq2.blk_num_cfg */
	0x14090293, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_0 */
	0x14090251, 0x00010001, 	/* cond: pswrq2.max_blks_vq0 */
	0x14090294, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_1 */
	0x14090252, 0x00010001, 	/* cond: pswrq2.max_blks_vq1 */
	0x14090295, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_2 */
	0x14090253, 0x00010001, 	/* cond: pswrq2.max_blks_vq2 */
	0x14090296, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_3 */
	0x14090254, 0x00010001, 	/* cond: pswrq2.max_blks_vq3 */
	0x14090297, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_4 */
	0x14090255, 0x00010001, 	/* cond: pswrq2.max_blks_vq4 */
	0x14090298, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_5 */
	0x14090256, 0x00010001, 	/* cond: pswrq2.max_blks_vq5 */
	0x14090299, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_6 */
	0x14090257, 0x00010001, 	/* cond: pswrq2.max_blks_vq6 */
	0x1409029c, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_9 */
	0x1409025a, 0x00010001, 	/* cond: pswrq2.max_blks_vq9 */
	0x1409029e, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_11 */
	0x1409025c, 0x00010001, 	/* cond: pswrq2.max_blks_vq11 */
	0x1409029f, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_12 */
	0x1409025d, 0x00010001, 	/* cond: pswrq2.max_blks_vq12 */
	0x140902a2, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_15 */
	0x14090260, 0x00010001, 	/* cond: pswrq2.max_blks_vq15 */
	0x140902a3, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_16 */
	0x14090261, 0x00010001, 	/* cond: pswrq2.max_blks_vq16 */
	0x140902a4, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_17 */
	0x14090262, 0x00010001, 	/* cond: pswrq2.max_blks_vq17 */
	0x140902a5, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_18 */
	0x14090263, 0x00010001, 	/* cond: pswrq2.max_blks_vq18 */
	0x140902a6, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_19 */
	0x14090264, 0x00010001, 	/* cond: pswrq2.max_blks_vq19 */
	0x140902a8, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_21 */
	0x14090266, 0x00010001, 	/* cond: pswrq2.max_blks_vq21 */
	0x140902a9, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_22 */
	0x14090267, 0x00010001, 	/* cond: pswrq2.max_blks_vq22 */
	0x140902aa, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_23 */
	0x14090268, 0x00010001, 	/* cond: pswrq2.max_blks_vq23 */
	0x140902ab, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_24 */
	0x14090269, 0x00010001, 	/* cond: pswrq2.max_blks_vq24 */
	0x140902ad, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_26 */
	0x1409026b, 0x00010001, 	/* cond: pswrq2.max_blks_vq26 */
	0x140902af, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_28 */
	0x1409026d, 0x00010001, 	/* cond: pswrq2.max_blks_vq28 */
	0x140902b0, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_29 */
	0x1409026e, 0x00010001, 	/* cond: pswrq2.max_blks_vq29 */
	0x140902b1, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_30 */
	0x1409026f, 0x00010001, 	/* cond: pswrq2.max_blks_vq30 */
	0x140902f2, 0x00010001, 	/* cond: pswrq2.l2p_close_gate_sts */
	0x140902f3, 0x00010001, 	/* cond: pswrq2.misc_close_gate_sts */
	0x140902f4, 0x00010001, 	/* cond: pswrq2.misc_stall_mem_sts */
	0x130a0060, 0x00010001, 	/* cond: pswrq.INT_STS */
	0x130a0061, 0x00010001, 	/* cond: pswrq.INT_MASK */
	0x110a6860, 0x00010001, 	/* cond: pswwr.INT_STS */
	0x110a6861, 0x00010001, 	/* cond: pswwr.INT_MASK */
	0x120a6c12, 0x00010001, 	/* cond: pswwr2.pglue_eop_err_details */
	0x120a6c14, 0x00010001, 	/* cond: pswwr2.prm_curr_fill_level */
	0x120a6c16, 0x00010001, 	/* cond: pswwr2.cdu_curr_fill_level */
	0x120a6c60, 0x00010001, 	/* cond: pswwr2.INT_STS */
	0x120a6c61, 0x00010001, 	/* cond: pswwr2.INT_MASK */
	0x120a6c80, 0x00010001, 	/* cond: pswwr2.PRTY_STS_H_0 */
	0x120a6c81, 0x00010001, 	/* cond: pswwr2.PRTY_MASK_H_0 */
	0x120a6c84, 0x00010001, 	/* cond: pswwr2.PRTY_STS_H_1 */
	0x120a6c85, 0x00010001, 	/* cond: pswwr2.PRTY_MASK_H_1 */
	0x120a6c88, 0x00010001, 	/* cond: pswwr2.PRTY_STS_H_2 */
	0x120a6c89, 0x00010001, 	/* cond: pswwr2.PRTY_MASK_H_2 */
	0x120a6c8c, 0x00010001, 	/* cond: pswwr2.PRTY_STS_H_3 */
	0x120a6c8d, 0x00010001, 	/* cond: pswwr2.PRTY_MASK_H_3 */
	0x0f0a7029, 0x00010001, 	/* cond: pswrd.fifo_full_status */
	0x0f0a7060, 0x00010001, 	/* cond: pswrd.INT_STS */
	0x0f0a7061, 0x00010001, 	/* cond: pswrd.INT_MASK */
	0x100a7400, 0x00010001, 	/* cond: pswrd2.start_init */
	0x100a7401, 0x00010001, 	/* cond: pswrd2.init_done */
	0x100a741a, 0x00010001, 	/* cond: pswrd2.cpl_err_details */
	0x100a741b, 0x00010001, 	/* cond: pswrd2.cpl_err_details2 */
	0x100a741f, 0x00010001, 	/* cond: pswrd2.port_is_idle_0 */
	0x100a7420, 0x00010001, 	/* cond: pswrd2.port_is_idle_1 */
	0x100a7438, 0x00010010, 	/* cond: pswrd2.almost_full_0[0:15] */
	0x100a7460, 0x00010001, 	/* cond: pswrd2.INT_STS */
	0x100a7461, 0x00010001, 	/* cond: pswrd2.INT_MASK */
	0x100a7480, 0x00010001, 	/* cond: pswrd2.PRTY_STS_H_0 */
	0x100a7481, 0x00010001, 	/* cond: pswrd2.PRTY_MASK_H_0 */
	0x100a7484, 0x00010001, 	/* cond: pswrd2.PRTY_STS_H_1 */
	0x100a7485, 0x00010001, 	/* cond: pswrd2.PRTY_MASK_H_1 */
	0x100a7518, 0x00010001, 	/* cond: pswrd2.disable_inputs */
	0x0e0a7810, 0x00010001, 	/* cond: pswhst2.header_fifo_status */
	0x0e0a7811, 0x00010001, 	/* cond: pswhst2.data_fifo_status */
	0x0e0a7860, 0x00010001, 	/* cond: pswhst2.INT_STS */
	0x0e0a7861, 0x00010001, 	/* cond: pswhst2.INT_MASK */
	0x0d0a8013, 0x00010001, 	/* cond: pswhst.discard_internal_writes_status */
	0x0d0a8014, 0x00010001, 	/* cond: pswhst.discard_doorbells_status */
	0x0d0a8016, 0x00010001, 	/* cond: pswhst.arb_is_idle */
	0x0d0a801c, 0x00010001, 	/* cond: pswhst.incorrect_access_valid */
	0x0d0a801a, 0x00000001, 	/* info: pswhst.incorrect_access_data mode=all */
	0x0d0a801b, 0x00000001, 	/* info: pswhst.incorrect_access_length mode=all */
	0x0d0a801d, 0x00000001, 	/* info: pswhst.incorrect_access_address mode=all */
	0x0d0a801e, 0x00010001, 	/* cond: pswhst.per_violation_valid */
	0x0d0a801f, 0x00000001, 	/* info: pswhst.per_violation_data mode=all */
	0x0d0a8023, 0x00010001, 	/* cond: pswhst.source_credits_avail */
	0x0d0a8025, 0x00010001, 	/* cond: pswhst.source_credit_viol_valid */
	0x0d0a8024, 0x00000001, 	/* info: pswhst.source_credit_viol_data mode=all */
	0x0d0a802b, 0x00010001, 	/* cond: pswhst.is_in_drain_mode */
	0x0d0a802e, 0x00010001, 	/* cond: pswhst.timeout_valid */
	0x0d0a802f, 0x00000001, 	/* info: pswhst.timeout_address mode=all */
	0x0d0a802e, 0x00010001, 	/* cond: pswhst.timeout_valid */
	0x0d0a802d, 0x00000001, 	/* info: pswhst.timeout_data mode=all */
	0x0d0a8058, 0x00010008, 	/* cond: pswhst.clients_waiting_to_source_arb[0:7] */
	0x0d0a8060, 0x00010001, 	/* cond: pswhst.INT_STS */
	0x0d0a8080, 0x00010001, 	/* cond: pswhst.PRTY_STS_H_0 */
	0x0d0a8081, 0x00010001, 	/* cond: pswhst.PRTY_MASK_H_0 */
	0x040aa060, 0x00010001, 	/* cond: pglue_b.INT_STS */
	0x040aa060, 0x00010001, 	/* cond: pglue_b.INT_STS */
	0x040aa080, 0x00010001, 	/* cond: pglue_b.PRTY_STS_H_0 */
	0x040aa081, 0x00010001, 	/* cond: pglue_b.PRTY_MASK_H_0 */
	0x040aa120, 0x00010001, 	/* cond: pglue_b.pgl_write_blocked */
	0x040aa121, 0x00010001, 	/* cond: pglue_b.pgl_read_blocked */
	0x040aa122, 0x00010001, 	/* cond: pglue_b.read_fifo_occupancy_level */
	0x040aa12b, 0x00010001, 	/* cond: pglue_b.rx_legacy_errors */
	0x040aa159, 0x00010001, 	/* cond: pglue_b.pgl_txw_cdts */
	0x040aa804, 0x00010001, 	/* cond: pglue_b.cfg_space_a_request */
	0x040aa806, 0x00010001, 	/* cond: pglue_b.cfg_space_b_request */
	0x040aa808, 0x00010001, 	/* cond: pglue_b.flr_request_vf_31_0 */
	0x040aa809, 0x00010001, 	/* cond: pglue_b.flr_request_vf_63_32 */
	0x040aa80a, 0x00010001, 	/* cond: pglue_b.flr_request_vf_95_64 */
	0x040aa80b, 0x00010001, 	/* cond: pglue_b.flr_request_vf_127_96 */
	0x040aa80c, 0x00010001, 	/* cond: pglue_b.flr_request_vf_159_128 */
	0x040aa80d, 0x00010001, 	/* cond: pglue_b.flr_request_vf_191_160 */
	0x040aa810, 0x00010001, 	/* cond: pglue_b.flr_request_pf_31_0 */
	0x040aa81b, 0x00010001, 	/* cond: pglue_b.sr_iov_disabled_request */
	0x040aa83d, 0x00010001, 	/* cond: pglue_b.was_error_vf_31_0 */
	0x040aa83e, 0x00010001, 	/* cond: pglue_b.was_error_vf_63_32 */
	0x040aa83f, 0x00010001, 	/* cond: pglue_b.was_error_vf_95_64 */
	0x040aa840, 0x00010001, 	/* cond: pglue_b.was_error_vf_127_96 */
	0x040aa841, 0x00010001, 	/* cond: pglue_b.was_error_vf_159_128 */
	0x040aa842, 0x00010001, 	/* cond: pglue_b.was_error_vf_191_160 */
	0x040aa845, 0x00010001, 	/* cond: pglue_b.was_error_pf_31_0 */
	0x040aa84f, 0x00010001, 	/* cond: pglue_b.rx_err_details */
	0x040aa850, 0x00010001, 	/* cond: pglue_b.rx_tcpl_err_details */
	0x040aa851, 0x00010001, 	/* cond: pglue_b.tx_err_wr_add_31_0 */
	0x040aa852, 0x00010001, 	/* cond: pglue_b.tx_err_wr_add_63_32 */
	0x040aa853, 0x00010001, 	/* cond: pglue_b.tx_err_wr_details */
	0x040aa854, 0x00010001, 	/* cond: pglue_b.tx_err_wr_details2 */
	0x040aa855, 0x00010001, 	/* cond: pglue_b.tx_err_rd_add_31_0 */
	0x040aa856, 0x00010001, 	/* cond: pglue_b.tx_err_rd_add_63_32 */
	0x040aa857, 0x00010001, 	/* cond: pglue_b.tx_err_rd_details */
	0x040aa858, 0x00010001, 	/* cond: pglue_b.tx_err_rd_details2 */
	0x040aa8ec, 0x00010001, 	/* cond: pglue_b.vf_length_violation_details */
	0x040aa8ed, 0x00010001, 	/* cond: pglue_b.vf_length_violation_details2 */
	0x040aa8ee, 0x00010001, 	/* cond: pglue_b.vf_grc_space_violation_details */
	0x040aa951, 0x00010001, 	/* cond: pglue_b.master_zlr_err_add_31_0 */
	0x040aa952, 0x00010001, 	/* cond: pglue_b.master_zlr_err_add_63_32 */
	0x040aa953, 0x00010001, 	/* cond: pglue_b.master_zlr_err_details */
	0x040aa955, 0x00010001, 	/* cond: pglue_b.admin_window_violation_details */
	0x040aa956, 0x00010001, 	/* cond: pglue_b.out_of_range_function_in_pretend_details */
	0x040aa957, 0x00010001, 	/* cond: pglue_b.out_of_range_function_in_pretend_address */
	0x040aab80, 0x00010001, 	/* cond: pglue_b.write_fifo_occupancy_level */
	0x040aab84, 0x00010001, 	/* cond: pglue_b.illegal_address_add_31_0 */
	0x040aab85, 0x00010001, 	/* cond: pglue_b.illegal_address_add_63_32 */
	0x040aab86, 0x00010001, 	/* cond: pglue_b.illegal_address_details */
	0x040aab87, 0x00010001, 	/* cond: pglue_b.illegal_address_details2 */
	0x040aab8e, 0x00010001, 	/* cond: pglue_b.tags_31_0 */
	0x040aab8f, 0x00010001, 	/* cond: pglue_b.tags_63_32 */
	0x040aab90, 0x00010001, 	/* cond: pglue_b.tags_95_64 */
	0x040aab91, 0x00010001, 	/* cond: pglue_b.tags_127_96 */
	0x040aab9d, 0x00010001, 	/* cond: pglue_b.vf_ilt_err_add_31_0 */
	0x040aab9e, 0x00010001, 	/* cond: pglue_b.vf_ilt_err_add_63_32 */
	0x040aab9f, 0x00010001, 	/* cond: pglue_b.vf_ilt_err_details */
	0x040aaba0, 0x00010001, 	/* cond: pglue_b.vf_ilt_err_details2 */
	0x1f0b0060, 0x00010001, 	/* cond: tm.INT_STS_0 */
	0x1f0b0060, 0x00010001, 	/* cond: tm.INT_STS_0 */
	0x1f0b0060, 0x00010001, 	/* cond: tm.INT_STS_0 */
	0x1f0b0064, 0x00010001, 	/* cond: tm.INT_STS_1 */
	0x1f0b0080, 0x00010001, 	/* cond: tm.PRTY_STS_H_0 */
	0x1f0b0081, 0x00010001, 	/* cond: tm.PRTY_MASK_H_0 */
	0x1f0b01c4, 0x00010001, 	/* cond: tm.pxp_read_data_fifo_status */
	0x1f0b01c6, 0x00010001, 	/* cond: tm.pxp_read_ctrl_fifo_status */
	0x1f0b01c8, 0x00010001, 	/* cond: tm.cfc_load_echo_fifo_status */
	0x1f0b01c9, 0x00010001, 	/* cond: tm.client_out_fifo_status */
	0x1f0b01ca, 0x00010001, 	/* cond: tm.client_in_pbf_fifo_status */
	0x1f0b01cb, 0x00010001, 	/* cond: tm.client_in_xcm_fifo_status */
	0x1f0b01cc, 0x00010001, 	/* cond: tm.client_in_tcm_fifo_status */
	0x1f0b01cd, 0x00010001, 	/* cond: tm.client_in_ucm_fifo_status */
	0x1f0b01ce, 0x00010001, 	/* cond: tm.expiration_cmd_fifo_status */
	0x1f0b01cf, 0x00010001, 	/* cond: tm.ac_command_fifo_status */
	0x410b4001, 0x00010001, 	/* cond: tcfc.ll_init_done */
	0x410b4002, 0x00010001, 	/* cond: tcfc.ac_init_done */
	0x410b4003, 0x00010001, 	/* cond: tcfc.cam_init_done */
	0x410b4004, 0x00010001, 	/* cond: tcfc.tidram_init_done */
	0x410b4060, 0x00010001, 	/* cond: tcfc.INT_STS_0 */
	0x410b4061, 0x00010001, 	/* cond: tcfc.INT_MASK_0 */
	0x410b4153, 0x00000001, 	/* info: tcfc.error_vector mode=all */
	0x410b4156, 0x00000001, 	/* info: tcfc.error_data1 mode=all */
	0x410b4157, 0x00000001, 	/* info: tcfc.error_data2 mode=all */
	0x410b4158, 0x00000001, 	/* info: tcfc.error_data3 mode=all */
	0x410b4159, 0x00000001, 	/* info: tcfc.error_data4 mode=all */
	0x410b4080, 0x00010001, 	/* cond: tcfc.PRTY_STS_H_0 */
	0x410b4081, 0x00010001, 	/* cond: tcfc.PRTY_MASK_H_0 */
	0x410b4184, 0x00010001, 	/* cond: tcfc.lstate_arriving */
	0x410b4187, 0x00010001, 	/* cond: tcfc.lstate_leaving */
	0x400b8001, 0x00010001, 	/* cond: ccfc.ll_init_done */
	0x400b8002, 0x00010001, 	/* cond: ccfc.ac_init_done */
	0x400b8003, 0x00010001, 	/* cond: ccfc.cam_init_done */
	0x400b8004, 0x00010001, 	/* cond: ccfc.tidram_init_done */
	0x400b8060, 0x00010001, 	/* cond: ccfc.INT_STS_0 */
	0x400b8061, 0x00010001, 	/* cond: ccfc.INT_MASK_0 */
	0x400b8153, 0x00000001, 	/* info: ccfc.error_vector mode=all */
	0x400b8156, 0x00000001, 	/* info: ccfc.error_data1 mode=all */
	0x400b8157, 0x00000001, 	/* info: ccfc.error_data2 mode=all */
	0x400b8158, 0x00000001, 	/* info: ccfc.error_data3 mode=all */
	0x400b8159, 0x00000001, 	/* info: ccfc.error_data4 mode=all */
	0x400b8080, 0x00010001, 	/* cond: ccfc.PRTY_STS_H_0 */
	0x400b8081, 0x00010001, 	/* cond: ccfc.PRTY_MASK_H_0 */
	0x400b8179, 0x00010001, 	/* cond: ccfc.PRTY_STS */
	0x400b817a, 0x00010001, 	/* cond: ccfc.PRTY_MASK */
	0x400b8184, 0x00010001, 	/* cond: ccfc.lstate_arriving */
	0x400b8187, 0x00010001, 	/* cond: ccfc.lstate_leaving */
	0x1e0bc060, 0x00010001, 	/* cond: qm.INT_STS */
	0x1e0bc061, 0x00010001, 	/* cond: qm.INT_MASK */
	0x1e0bc064, 0x00010001, 	/* cond: qm.PRTY_STS */
	0x1e0bc065, 0x00010001, 	/* cond: qm.PRTY_MASK */
	0x1e0bc080, 0x00010001, 	/* cond: qm.PRTY_STS_H_0 */
	0x1e0bc081, 0x00010001, 	/* cond: qm.PRTY_MASK_H_0 */
	0x1e0bc084, 0x00010001, 	/* cond: qm.PRTY_STS_H_1 */
	0x1e0bc085, 0x00010001, 	/* cond: qm.PRTY_MASK_H_1 */
	0x1e0bc088, 0x00010001, 	/* cond: qm.PRTY_STS_H_2 */
	0x1e0bc089, 0x00010001, 	/* cond: qm.PRTY_MASK_H_2 */
	0x1e0bc106, 0x00010006, 	/* cond: qm.wrc_fifolvl_0[0:5] */
	0x1e0bc202, 0x00010001, 	/* cond: qm.OutLdReqCrdConnTx */
	0x1e0bc200, 0x00010001, 	/* cond: qm.OutLdReqSizeConnTx */
	0x1e0bc203, 0x00010001, 	/* cond: qm.OutLdReqCrdConnOther */
	0x1e0bc201, 0x00010001, 	/* cond: qm.OutLdReqSizeConnOther */
	0x1e0bc54e, 0x00010001, 	/* cond: qm.OvfQNumTx */
	0x1e0bc54f, 0x00010001, 	/* cond: qm.OvfErrorTx */
	0x1e0bc54e, 0x00000001, 	/* info: qm.OvfQNumTx mode=all */
	0x1e0bc550, 0x00010001, 	/* cond: qm.OvfQNumOther */
	0x1e0bc551, 0x00010001, 	/* cond: qm.OvfErrorOther */
	0x1e0bc550, 0x00000001, 	/* info: qm.OvfQNumOther mode=all */
	0x1e0bc68e, 0x00010001, 	/* cond: qm.CmCrd_0 */
	0x1e0bc684, 0x00010001, 	/* cond: qm.CmInitCrd_0 */
	0x1e0bc68f, 0x00010001, 	/* cond: qm.CmCrd_1 */
	0x1e0bc685, 0x00010001, 	/* cond: qm.CmInitCrd_1 */
	0x1e0bc690, 0x00010001, 	/* cond: qm.CmCrd_2 */
	0x1e0bc686, 0x00010001, 	/* cond: qm.CmInitCrd_2 */
	0x1e0bc691, 0x00010001, 	/* cond: qm.CmCrd_3 */
	0x1e0bc687, 0x00010001, 	/* cond: qm.CmInitCrd_3 */
	0x1e0bc692, 0x00010001, 	/* cond: qm.CmCrd_4 */
	0x1e0bc688, 0x00010001, 	/* cond: qm.CmInitCrd_4 */
	0x1e0bc693, 0x00010001, 	/* cond: qm.CmCrd_5 */
	0x1e0bc689, 0x00010001, 	/* cond: qm.CmInitCrd_5 */
	0x1e0bc694, 0x00010001, 	/* cond: qm.CmCrd_6 */
	0x1e0bc68a, 0x00010001, 	/* cond: qm.CmInitCrd_6 */
	0x1e0bc695, 0x00010001, 	/* cond: qm.CmCrd_7 */
	0x1e0bc68b, 0x00010001, 	/* cond: qm.CmInitCrd_7 */
	0x1e0bc696, 0x00010001, 	/* cond: qm.CmCrd_8 */
	0x1e0bc68c, 0x00010001, 	/* cond: qm.CmInitCrd_8 */
	0x1e0bc697, 0x00010001, 	/* cond: qm.CmCrd_9 */
	0x1e0bc68d, 0x00010001, 	/* cond: qm.CmInitCrd_9 */
	0x3d0c0060, 0x00010001, 	/* cond: rdif.INT_STS */
	0x3d0c0061, 0x00010001, 	/* cond: rdif.INT_MASK */
	0x3e0c4060, 0x00010001, 	/* cond: tdif.INT_STS */
	0x3e0c4061, 0x00010001, 	/* cond: tdif.INT_MASK */
	0x3e0c4080, 0x00010001, 	/* cond: tdif.PRTY_STS_H_0 */
	0x3e0c4081, 0x00010001, 	/* cond: tdif.PRTY_MASK_H_0 */
	0x210d0030, 0x00010001, 	/* cond: brb.INT_STS_0 */
	0x210d0031, 0x00010001, 	/* cond: brb.INT_MASK_0 */
	0x218d0600, 0x00000014, 	/* info: brb.stopped_rd_req[0:4] width=3 access=WB mode=all */
	0x218d0640, 0x00000014, 	/* info: brb.stopped_rls_req[0:4] width=3 access=WB mode=all */
	0x210d0036, 0x00010001, 	/* cond: brb.INT_STS_1 */
	0x210d0037, 0x00010001, 	/* cond: brb.INT_MASK_1 */
	0x210d003c, 0x00010001, 	/* cond: brb.INT_STS_2 */
	0x210d003d, 0x00010001, 	/* cond: brb.INT_MASK_2 */
	0x210d0042, 0x00010001, 	/* cond: brb.INT_STS_3 */
	0x210d0043, 0x00010001, 	/* cond: brb.INT_MASK_3 */
	0x210d0048, 0x00010001, 	/* cond: brb.INT_STS_4 */
	0x210d0049, 0x00010001, 	/* cond: brb.INT_MASK_4 */
	0x218d0600, 0x00000014, 	/* info: brb.stopped_rd_req[0:4] width=3 access=WB mode=all */
	0x218d0640, 0x00000014, 	/* info: brb.stopped_rls_req[0:4] width=3 access=WB mode=all */
	0x210d0100, 0x00010001, 	/* cond: brb.PRTY_STS_H_0 */
	0x210d0101, 0x00010001, 	/* cond: brb.PRTY_MASK_H_0 */
	0x210d0104, 0x00010001, 	/* cond: brb.PRTY_STS_H_1 */
	0x210d0105, 0x00010001, 	/* cond: brb.PRTY_MASK_H_1 */
	0x210d03ec, 0x00010001, 	/* cond: brb.wc_bandwidth_if_full */
	0x210d03ed, 0x00010001, 	/* cond: brb.rc_pkt_if_full */
	0x210d03ee, 0x00010005, 	/* cond: brb.rc_pkt_empty_0[0:4] */
	0x210d041b, 0x00010001, 	/* cond: brb.rc_sop_empty */
	0x210d0468, 0x00010001, 	/* cond: brb.stop_packet_counter */
	0x210d0469, 0x00010001, 	/* cond: brb.stop_byte_counter */
	0x210d046a, 0x00010001, 	/* cond: brb.rc_pkt_state */
	0x210d0474, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_0 */
	0x210d0475, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_1 */
	0x210d0476, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_2 */
	0x210d0477, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_3 */
	0x210d0478, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_4 */
	0x210d0479, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_5 */
	0x210d047a, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_6 */
	0x210d047b, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_7 */
	0x210d047c, 0x00010001, 	/* cond: brb.mac0_tc_occupancy_8 */
	0x210d0484, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_0 */
	0x210d0485, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_1 */
	0x210d0486, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_2 */
	0x210d0487, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_3 */
	0x210d0488, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_4 */
	0x210d0489, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_5 */
	0x210d048a, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_6 */
	0x210d048b, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_7 */
	0x210d048c, 0x00010001, 	/* cond: brb.mac1_tc_occupancy_8 */
	0x34130017, 0x00010001, 	/* cond: xyld.pending_msg_to_ext_ev_1_ctr */
	0x34130018, 0x00010001, 	/* cond: xyld.pending_msg_to_ext_ev_2_ctr */
	0x34130019, 0x00010001, 	/* cond: xyld.pending_msg_to_ext_ev_3_ctr */
	0x3413001a, 0x00010001, 	/* cond: xyld.pending_msg_to_ext_ev_4_ctr */
	0x3413001b, 0x00010001, 	/* cond: xyld.pending_msg_to_ext_ev_5_ctr */
	0x3413001c, 0x00010001, 	/* cond: xyld.foc_remain_credits */
	0x34130003, 0x00010001, 	/* cond: xyld.foci_foc_credits */
	0x3413001f, 0x00010001, 	/* cond: xyld.pci_pending_msg_ctr */
	0x34130039, 0x00010001, 	/* cond: xyld.dbg_pending_ccfc_req */
	0x3413003a, 0x00010001, 	/* cond: xyld.dbg_pending_tcfc_req */
	0x34130080, 0x00010001, 	/* cond: xyld.PRTY_STS_H_0 */
	0x34130081, 0x00010001, 	/* cond: xyld.PRTY_MASK_H_0 */
	0x31134016, 0x00010001, 	/* cond: tmld.pending_msg_to_ext_ev_1_ctr */
	0x31134017, 0x00010001, 	/* cond: tmld.pending_msg_to_ext_ev_2_ctr */
	0x31134018, 0x00010001, 	/* cond: tmld.pending_msg_to_ext_ev_3_ctr */
	0x31134019, 0x00010001, 	/* cond: tmld.pending_msg_to_ext_ev_4_ctr */
	0x3113401a, 0x00010001, 	/* cond: tmld.pending_msg_to_ext_ev_5_ctr */
	0x3113401b, 0x00010001, 	/* cond: tmld.foc_remain_credits */
	0x31134003, 0x00010001, 	/* cond: tmld.foci_foc_credits */
	0x31134031, 0x00010001, 	/* cond: tmld.dbg_pending_ccfc_req */
	0x31134032, 0x00010001, 	/* cond: tmld.dbg_pending_tcfc_req */
	0x31134080, 0x00010001, 	/* cond: tmld.PRTY_STS_H_0 */
	0x31134081, 0x00010001, 	/* cond: tmld.PRTY_MASK_H_0 */
	0x3213801c, 0x00010001, 	/* cond: muld.pending_msg_to_ext_ev_1_ctr */
	0x3213801d, 0x00010001, 	/* cond: muld.pending_msg_to_ext_ev_2_ctr */
	0x3213801e, 0x00010001, 	/* cond: muld.pending_msg_to_ext_ev_3_ctr */
	0x3213801f, 0x00010001, 	/* cond: muld.pending_msg_to_ext_ev_4_ctr */
	0x32138020, 0x00010001, 	/* cond: muld.pending_msg_to_ext_ev_5_ctr */
	0x32138021, 0x00010001, 	/* cond: muld.foc_remain_credits */
	0x32138009, 0x00010001, 	/* cond: muld.foci_foc_credits */
	0x32138022, 0x00010001, 	/* cond: muld.bd_pending_msg_ctr */
	0x32138023, 0x00010001, 	/* cond: muld.sge_pending_msg_ctr */
	0x32138026, 0x00010001, 	/* cond: muld.pci_pending_msg_ctr */
	0x3213803a, 0x00010001, 	/* cond: muld.dbg_pending_ccfc_req */
	0x3213803b, 0x00010001, 	/* cond: muld.dbg_pending_tcfc_req */
	0x32138080, 0x00010001, 	/* cond: muld.PRTY_STS_H_0 */
	0x32138081, 0x00010001, 	/* cond: muld.PRTY_MASK_H_0 */
	0x4b140010, 0x00010001, 	/* cond: nig.INT_STS_0 */
	0x4b140011, 0x00010001, 	/* cond: nig.INT_MASK_0 */
	0x4b140014, 0x00010001, 	/* cond: nig.INT_STS_1 */
	0x4b140015, 0x00010001, 	/* cond: nig.INT_MASK_1 */
	0x4b140018, 0x00010001, 	/* cond: nig.INT_STS_2 */
	0x4b140019, 0x00010001, 	/* cond: nig.INT_MASK_2 */
	0x4b14001c, 0x00010001, 	/* cond: nig.INT_STS_3 */
	0x4b14001d, 0x00010001, 	/* cond: nig.INT_MASK_3 */
	0x4b1406e8, 0x00000001, 	/* info: nig.flowctrl_mode mode=all */
	0x4b140713, 0x00000001, 	/* info: nig.rx_flowctrl_status mode=all */
	0x4b140020, 0x00010001, 	/* cond: nig.INT_STS_4 */
	0x4b140021, 0x00010001, 	/* cond: nig.INT_MASK_4 */
	0x4b140024, 0x00010001, 	/* cond: nig.INT_STS_5 */
	0x4b140025, 0x00010001, 	/* cond: nig.INT_MASK_5 */
	0x4b1406e8, 0x00000001, 	/* info: nig.flowctrl_mode mode=all */
	0x4b140713, 0x00000001, 	/* info: nig.rx_flowctrl_status mode=all */
	0x4b140080, 0x00010001, 	/* cond: nig.PRTY_STS_H_0 */
	0x4b140081, 0x00010001, 	/* cond: nig.PRTY_MASK_H_0 */
	0x4b140084, 0x00010001, 	/* cond: nig.PRTY_STS_H_1 */
	0x4b140085, 0x00010001, 	/* cond: nig.PRTY_MASK_H_1 */
	0x4b140088, 0x00010001, 	/* cond: nig.PRTY_STS_H_2 */
	0x4b140089, 0x00010001, 	/* cond: nig.PRTY_MASK_H_2 */
	0x4b14008c, 0x00010001, 	/* cond: nig.PRTY_STS_H_3 */
	0x4b14008d, 0x00010001, 	/* cond: nig.PRTY_MASK_H_3 */
	0x4b140301, 0x00010001, 	/* cond: nig.lb_sopq_empty */
	0x4b140303, 0x00010001, 	/* cond: nig.tx_sopq_empty */
	0x4b1404c6, 0x00010001, 	/* cond: nig.rx_llh_rfifo_empty */
	0x4b1405db, 0x00010001, 	/* cond: nig.lb_btb_fifo_empty */
	0x4b1405e6, 0x00010001, 	/* cond: nig.lb_llh_rfifo_empty */
	0x4b140655, 0x00010001, 	/* cond: nig.rx_ptp_ts_msb_err */
	0x4b140640, 0x00010001, 	/* cond: nig.rx_ptp_en */
	0x4b1407ff, 0x00010001, 	/* cond: nig.tx_btb_fifo_empty */
	0x4b140838, 0x00010001, 	/* cond: nig.debug_fifo_empty */
	0x1715801c, 0x00010001, 	/* cond: ptu.pxp_err_ctr */
	0x1715801d, 0x00010001, 	/* cond: ptu.inv_err_ctr */
	0x17158032, 0x00010001, 	/* cond: ptu.pbf_fill_level */
	0x17158033, 0x00010001, 	/* cond: ptu.prm_fill_level */
	0x17158060, 0x00010001, 	/* cond: ptu.INT_STS */
	0x17158061, 0x00010001, 	/* cond: ptu.INT_MASK */
	0x17158080, 0x00010001, 	/* cond: ptu.PRTY_STS_H_0 */
	0x17158081, 0x00010001, 	/* cond: ptu.PRTY_MASK_H_0 */
	0x3f160070, 0x00010001, 	/* cond: cdu.INT_STS */
	0x3f160073, 0x00010001, 	/* cond: cdu.INT_MASK */
	0x3f160200, 0x00000001, 	/* info: cdu.ccfc_cvld_error_data mode=all */
	0x3f160201, 0x00000001, 	/* info: cdu.tcfc_cvld_error_data mode=all */
	0x3f160202, 0x00000001, 	/* info: cdu.ccfc_ld_l1_num_error_data mode=all */
	0x3f160203, 0x00000001, 	/* info: cdu.tcfc_ld_l1_num_error_data mode=all */
	0x3f160204, 0x00000001, 	/* info: cdu.ccfc_wb_l1_num_error_data mode=all */
	0x3f160205, 0x00000001, 	/* info: cdu.tcfc_wb_l1_num_error_data mode=all */
	0x3f160080, 0x00010001, 	/* cond: cdu.PRTY_STS_H_0 */
	0x3f160081, 0x00010001, 	/* cond: cdu.PRTY_MASK_H_0 */
	0x3c360080, 0x00010001, 	/* cond: pbf.PRTY_STS_H_0 */
	0x3c360081, 0x00010001, 	/* cond: pbf.PRTY_MASK_H_0 */
	0x3c360084, 0x00010001, 	/* cond: pbf.PRTY_STS_H_1 */
	0x3c360085, 0x00010001, 	/* cond: pbf.PRTY_MASK_H_1 */
	0x3c36019a, 0x00010001, 	/* cond: pbf.num_pkts_received_with_error */
	0x3c36019b, 0x00010001, 	/* cond: pbf.num_pkts_sent_with_error_to_btb */
	0x3c36019c, 0x00010001, 	/* cond: pbf.num_pkts_sent_with_drop_to_btb */
	0x3c3601ac, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq0 */
	0x3c3601ae, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq0 */
	0x3c3601b1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq0 */
	0x3c3601bc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq1 */
	0x3c3601be, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq1 */
	0x3c3601c1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq1 */
	0x3c3601cc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq2 */
	0x3c3601ce, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq2 */
	0x3c3601d1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq2 */
	0x3c3601dc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq3 */
	0x3c3601de, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq3 */
	0x3c3601e1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq3 */
	0x3c3601ec, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq4 */
	0x3c3601ee, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq4 */
	0x3c3601f1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq4 */
	0x3c3601fc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq5 */
	0x3c3601fe, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq5 */
	0x3c360201, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq5 */
	0x3c36020c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq6 */
	0x3c36020e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq6 */
	0x3c360211, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq6 */
	0x3c36021c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq7 */
	0x3c36021e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq7 */
	0x3c360221, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq7 */
	0x3c36022c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq8 */
	0x3c36022e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq8 */
	0x3c360231, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq8 */
	0x3c36023c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq9 */
	0x3c36023e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq9 */
	0x3c360241, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq9 */
	0x3c36024c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq10 */
	0x3c36024e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq10 */
	0x3c360251, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq10 */
	0x3c36025c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq11 */
	0x3c36025e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq11 */
	0x3c360261, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq11 */
	0x3c36026c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq12 */
	0x3c36026e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq12 */
	0x3c360271, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq12 */
	0x3c36027c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq13 */
	0x3c36027e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq13 */
	0x3c360281, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq13 */
	0x3c36028c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq14 */
	0x3c36028e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq14 */
	0x3c360291, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq14 */
	0x3c36029c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq15 */
	0x3c36029e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq15 */
	0x3c3602a1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq15 */
	0x3c3602ac, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq16 */
	0x3c3602ae, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq16 */
	0x3c3602b1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq16 */
	0x3c3602bc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq17 */
	0x3c3602be, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq17 */
	0x3c3602c1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq17 */
	0x3c3602cc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq18 */
	0x3c3602ce, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq18 */
	0x3c3602d1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq18 */
	0x3c3602dc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq19 */
	0x3c3602de, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq19 */
	0x3c3602e1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq19 */
	0x3b36c036, 0x00010001, 	/* cond: btb.INT_STS_1 */
	0x3b36c037, 0x00010001, 	/* cond: btb.INT_MASK_1 */
	0x3b36c03c, 0x00010001, 	/* cond: btb.INT_STS_2 */
	0x3b36c03d, 0x00010001, 	/* cond: btb.INT_MASK_2 */
	0x3b36c042, 0x00010001, 	/* cond: btb.INT_STS_3 */
	0x3b36c043, 0x00010001, 	/* cond: btb.INT_MASK_3 */
	0x3b36c100, 0x00010001, 	/* cond: btb.PRTY_STS_H_0 */
	0x3b36c101, 0x00010001, 	/* cond: btb.PRTY_MASK_H_0 */
	0x3b36c244, 0x00010001, 	/* cond: btb.wc_dup_empty */
	0x3b36c246, 0x00010001, 	/* cond: btb.wc_dup_status */
	0x3b36c247, 0x00010001, 	/* cond: btb.wc_empty_0 */
	0x3b36c267, 0x00010001, 	/* cond: btb.wc_bandwidth_if_full */
	0x3b36c268, 0x00010001, 	/* cond: btb.rc_pkt_if_full */
	0x3b36c269, 0x00010001, 	/* cond: btb.rc_pkt_empty_0 */
	0x3b36c26a, 0x00010001, 	/* cond: btb.rc_pkt_empty_1 */
	0x3b36c26b, 0x00010001, 	/* cond: btb.rc_pkt_empty_2 */
	0x3b36c26c, 0x00010001, 	/* cond: btb.rc_pkt_empty_3 */
	0x3b36c296, 0x00010001, 	/* cond: btb.rc_sop_empty */
	0x3b36c29c, 0x00010001, 	/* cond: btb.block_occupancy */
	0x3b36c2ae, 0x00010001, 	/* cond: btb.rc_pkt_state */
	0x3bb6c480, 0x00030001, 	/* cond: btb.wc_status_0 width=3 access=WB */
	0x273e0010, 0x00010001, 	/* cond: xsdm.INT_STS */
	0x273e0011, 0x00010001, 	/* cond: xsdm.INT_MASK */
	0x273e0109, 0x00000001, 	/* info: xsdm.inp_queue_err_vect mode=all */
	0x273e0080, 0x00010001, 	/* cond: xsdm.PRTY_STS_H_0 */
	0x273e0081, 0x00010001, 	/* cond: xsdm.PRTY_MASK_H_0 */
	0x273e0303, 0x00010001, 	/* cond: xsdm.qm_full */
	0x273e030c, 0x00010001, 	/* cond: xsdm.rsp_brb_if_full */
	0x273e030d, 0x00010001, 	/* cond: xsdm.rsp_pxp_if_full */
	0x273e0316, 0x00010001, 	/* cond: xsdm.dst_pxp_if_full */
	0x273e0317, 0x00010001, 	/* cond: xsdm.dst_int_ram_if_full */
	0x273e0318, 0x00010001, 	/* cond: xsdm.dst_pas_buf_if_full */
	0x273e0340, 0x00010001, 	/* cond: xsdm.int_cmpl_pend_empty */
	0x273e0341, 0x00010001, 	/* cond: xsdm.int_cprm_pend_empty */
	0x273e0342, 0x00010001, 	/* cond: xsdm.queue_empty */
	0x273e0343, 0x00010001, 	/* cond: xsdm.delay_fifo_empty */
	0x273e0346, 0x00010001, 	/* cond: xsdm.rsp_pxp_rdata_empty */
	0x273e0347, 0x00010001, 	/* cond: xsdm.rsp_brb_rdata_empty */
	0x273e0348, 0x00010001, 	/* cond: xsdm.rsp_int_ram_rdata_empty */
	0x273e0349, 0x00010001, 	/* cond: xsdm.rsp_brb_pend_empty */
	0x273e034a, 0x00010001, 	/* cond: xsdm.rsp_int_ram_pend_empty */
	0x273e034b, 0x00010001, 	/* cond: xsdm.dst_pxp_immed_empty */
	0x273e034c, 0x00010001, 	/* cond: xsdm.dst_pxp_dst_pend_empty */
	0x273e034d, 0x00010001, 	/* cond: xsdm.dst_pxp_src_pend_empty */
	0x273e034e, 0x00010001, 	/* cond: xsdm.dst_brb_src_pend_empty */
	0x273e034f, 0x00010001, 	/* cond: xsdm.dst_brb_src_addr_empty */
	0x273e0350, 0x00010001, 	/* cond: xsdm.dst_pxp_link_empty */
	0x273e0351, 0x00010001, 	/* cond: xsdm.dst_int_ram_wait_empty */
	0x273e0352, 0x00010001, 	/* cond: xsdm.dst_pas_buf_wait_empty */
	0x273e0353, 0x00010001, 	/* cond: xsdm.sh_delay_empty */
	0x273e0354, 0x00010001, 	/* cond: xsdm.cm_delay_empty */
	0x273e0355, 0x00010001, 	/* cond: xsdm.cmsg_que_empty */
	0x273e0356, 0x00010001, 	/* cond: xsdm.ccfc_load_pend_empty */
	0x273e0357, 0x00010001, 	/* cond: xsdm.tcfc_load_pend_empty */
	0x273e0358, 0x00010001, 	/* cond: xsdm.async_host_empty */
	0x273e0359, 0x00010001, 	/* cond: xsdm.prm_fifo_empty */
	0x283e4010, 0x00010001, 	/* cond: ysdm.INT_STS */
	0x283e4011, 0x00010001, 	/* cond: ysdm.INT_MASK */
	0x283e4109, 0x00000001, 	/* info: ysdm.inp_queue_err_vect mode=all */
	0x283e4080, 0x00010001, 	/* cond: ysdm.PRTY_STS_H_0 */
	0x283e4081, 0x00010001, 	/* cond: ysdm.PRTY_MASK_H_0 */
	0x283e4303, 0x00010001, 	/* cond: ysdm.qm_full */
	0x283e430c, 0x00010001, 	/* cond: ysdm.rsp_brb_if_full */
	0x283e430d, 0x00010001, 	/* cond: ysdm.rsp_pxp_if_full */
	0x283e4316, 0x00010001, 	/* cond: ysdm.dst_pxp_if_full */
	0x283e4317, 0x00010001, 	/* cond: ysdm.dst_int_ram_if_full */
	0x283e4318, 0x00010001, 	/* cond: ysdm.dst_pas_buf_if_full */
	0x283e4340, 0x00010001, 	/* cond: ysdm.int_cmpl_pend_empty */
	0x283e4341, 0x00010001, 	/* cond: ysdm.int_cprm_pend_empty */
	0x283e4342, 0x00010001, 	/* cond: ysdm.queue_empty */
	0x283e4343, 0x00010001, 	/* cond: ysdm.delay_fifo_empty */
	0x283e4346, 0x00010001, 	/* cond: ysdm.rsp_pxp_rdata_empty */
	0x283e4347, 0x00010001, 	/* cond: ysdm.rsp_brb_rdata_empty */
	0x283e4348, 0x00010001, 	/* cond: ysdm.rsp_int_ram_rdata_empty */
	0x283e4349, 0x00010001, 	/* cond: ysdm.rsp_brb_pend_empty */
	0x283e434a, 0x00010001, 	/* cond: ysdm.rsp_int_ram_pend_empty */
	0x283e434b, 0x00010001, 	/* cond: ysdm.dst_pxp_immed_empty */
	0x283e434c, 0x00010001, 	/* cond: ysdm.dst_pxp_dst_pend_empty */
	0x283e434d, 0x00010001, 	/* cond: ysdm.dst_pxp_src_pend_empty */
	0x283e434e, 0x00010001, 	/* cond: ysdm.dst_brb_src_pend_empty */
	0x283e434f, 0x00010001, 	/* cond: ysdm.dst_brb_src_addr_empty */
	0x283e4350, 0x00010001, 	/* cond: ysdm.dst_pxp_link_empty */
	0x283e4351, 0x00010001, 	/* cond: ysdm.dst_int_ram_wait_empty */
	0x283e4352, 0x00010001, 	/* cond: ysdm.dst_pas_buf_wait_empty */
	0x283e4353, 0x00010001, 	/* cond: ysdm.sh_delay_empty */
	0x283e4354, 0x00010001, 	/* cond: ysdm.cm_delay_empty */
	0x283e4355, 0x00010001, 	/* cond: ysdm.cmsg_que_empty */
	0x283e4356, 0x00010001, 	/* cond: ysdm.ccfc_load_pend_empty */
	0x283e4357, 0x00010001, 	/* cond: ysdm.tcfc_load_pend_empty */
	0x283e4358, 0x00010001, 	/* cond: ysdm.async_host_empty */
	0x283e4359, 0x00010001, 	/* cond: ysdm.prm_fifo_empty */
	0x293e8010, 0x00010001, 	/* cond: psdm.INT_STS */
	0x293e8011, 0x00010001, 	/* cond: psdm.INT_MASK */
	0x293e8109, 0x00000001, 	/* info: psdm.inp_queue_err_vect mode=all */
	0x293e8080, 0x00010001, 	/* cond: psdm.PRTY_STS_H_0 */
	0x293e8081, 0x00010001, 	/* cond: psdm.PRTY_MASK_H_0 */
	0x293e8303, 0x00010001, 	/* cond: psdm.qm_full */
	0x293e830c, 0x00010001, 	/* cond: psdm.rsp_brb_if_full */
	0x293e830d, 0x00010001, 	/* cond: psdm.rsp_pxp_if_full */
	0x293e8316, 0x00010001, 	/* cond: psdm.dst_pxp_if_full */
	0x293e8317, 0x00010001, 	/* cond: psdm.dst_int_ram_if_full */
	0x293e8318, 0x00010001, 	/* cond: psdm.dst_pas_buf_if_full */
	0x293e8340, 0x00010001, 	/* cond: psdm.int_cmpl_pend_empty */
	0x293e8341, 0x00010001, 	/* cond: psdm.int_cprm_pend_empty */
	0x293e8342, 0x00010001, 	/* cond: psdm.queue_empty */
	0x293e8343, 0x00010001, 	/* cond: psdm.delay_fifo_empty */
	0x293e8346, 0x00010001, 	/* cond: psdm.rsp_pxp_rdata_empty */
	0x293e8347, 0x00010001, 	/* cond: psdm.rsp_brb_rdata_empty */
	0x293e8348, 0x00010001, 	/* cond: psdm.rsp_int_ram_rdata_empty */
	0x293e8349, 0x00010001, 	/* cond: psdm.rsp_brb_pend_empty */
	0x293e834a, 0x00010001, 	/* cond: psdm.rsp_int_ram_pend_empty */
	0x293e834b, 0x00010001, 	/* cond: psdm.dst_pxp_immed_empty */
	0x293e834c, 0x00010001, 	/* cond: psdm.dst_pxp_dst_pend_empty */
	0x293e834d, 0x00010001, 	/* cond: psdm.dst_pxp_src_pend_empty */
	0x293e834e, 0x00010001, 	/* cond: psdm.dst_brb_src_pend_empty */
	0x293e834f, 0x00010001, 	/* cond: psdm.dst_brb_src_addr_empty */
	0x293e8350, 0x00010001, 	/* cond: psdm.dst_pxp_link_empty */
	0x293e8351, 0x00010001, 	/* cond: psdm.dst_int_ram_wait_empty */
	0x293e8352, 0x00010001, 	/* cond: psdm.dst_pas_buf_wait_empty */
	0x293e8353, 0x00010001, 	/* cond: psdm.sh_delay_empty */
	0x293e8354, 0x00010001, 	/* cond: psdm.cm_delay_empty */
	0x293e8355, 0x00010001, 	/* cond: psdm.cmsg_que_empty */
	0x293e8356, 0x00010001, 	/* cond: psdm.ccfc_load_pend_empty */
	0x293e8357, 0x00010001, 	/* cond: psdm.tcfc_load_pend_empty */
	0x293e8358, 0x00010001, 	/* cond: psdm.async_host_empty */
	0x293e8359, 0x00010001, 	/* cond: psdm.prm_fifo_empty */
	0x243ec010, 0x00010001, 	/* cond: tsdm.INT_STS */
	0x243ec011, 0x00010001, 	/* cond: tsdm.INT_MASK */
	0x243ec109, 0x00000001, 	/* info: tsdm.inp_queue_err_vect mode=all */
	0x243ec080, 0x00010001, 	/* cond: tsdm.PRTY_STS_H_0 */
	0x243ec081, 0x00010001, 	/* cond: tsdm.PRTY_MASK_H_0 */
	0x243ec303, 0x00010001, 	/* cond: tsdm.qm_full */
	0x243ec30c, 0x00010001, 	/* cond: tsdm.rsp_brb_if_full */
	0x243ec30d, 0x00010001, 	/* cond: tsdm.rsp_pxp_if_full */
	0x243ec316, 0x00010001, 	/* cond: tsdm.dst_pxp_if_full */
	0x243ec317, 0x00010001, 	/* cond: tsdm.dst_int_ram_if_full */
	0x243ec318, 0x00010001, 	/* cond: tsdm.dst_pas_buf_if_full */
	0x243ec340, 0x00010001, 	/* cond: tsdm.int_cmpl_pend_empty */
	0x243ec341, 0x00010001, 	/* cond: tsdm.int_cprm_pend_empty */
	0x243ec342, 0x00010001, 	/* cond: tsdm.queue_empty */
	0x243ec343, 0x00010001, 	/* cond: tsdm.delay_fifo_empty */
	0x243ec346, 0x00010001, 	/* cond: tsdm.rsp_pxp_rdata_empty */
	0x243ec347, 0x00010001, 	/* cond: tsdm.rsp_brb_rdata_empty */
	0x243ec348, 0x00010001, 	/* cond: tsdm.rsp_int_ram_rdata_empty */
	0x243ec349, 0x00010001, 	/* cond: tsdm.rsp_brb_pend_empty */
	0x243ec34a, 0x00010001, 	/* cond: tsdm.rsp_int_ram_pend_empty */
	0x243ec34b, 0x00010001, 	/* cond: tsdm.dst_pxp_immed_empty */
	0x243ec34c, 0x00010001, 	/* cond: tsdm.dst_pxp_dst_pend_empty */
	0x243ec34d, 0x00010001, 	/* cond: tsdm.dst_pxp_src_pend_empty */
	0x243ec34e, 0x00010001, 	/* cond: tsdm.dst_brb_src_pend_empty */
	0x243ec34f, 0x00010001, 	/* cond: tsdm.dst_brb_src_addr_empty */
	0x243ec350, 0x00010001, 	/* cond: tsdm.dst_pxp_link_empty */
	0x243ec351, 0x00010001, 	/* cond: tsdm.dst_int_ram_wait_empty */
	0x243ec352, 0x00010001, 	/* cond: tsdm.dst_pas_buf_wait_empty */
	0x243ec353, 0x00010001, 	/* cond: tsdm.sh_delay_empty */
	0x243ec354, 0x00010001, 	/* cond: tsdm.cm_delay_empty */
	0x243ec355, 0x00010001, 	/* cond: tsdm.cmsg_que_empty */
	0x243ec356, 0x00010001, 	/* cond: tsdm.ccfc_load_pend_empty */
	0x243ec357, 0x00010001, 	/* cond: tsdm.tcfc_load_pend_empty */
	0x243ec358, 0x00010001, 	/* cond: tsdm.async_host_empty */
	0x243ec359, 0x00010001, 	/* cond: tsdm.prm_fifo_empty */
	0x253f0010, 0x00010001, 	/* cond: msdm.INT_STS */
	0x253f0011, 0x00010001, 	/* cond: msdm.INT_MASK */
	0x253f0109, 0x00000001, 	/* info: msdm.inp_queue_err_vect mode=all */
	0x253f0080, 0x00010001, 	/* cond: msdm.PRTY_STS_H_0 */
	0x253f0081, 0x00010001, 	/* cond: msdm.PRTY_MASK_H_0 */
	0x253f0303, 0x00010001, 	/* cond: msdm.qm_full */
	0x253f030c, 0x00010001, 	/* cond: msdm.rsp_brb_if_full */
	0x253f030d, 0x00010001, 	/* cond: msdm.rsp_pxp_if_full */
	0x253f0316, 0x00010001, 	/* cond: msdm.dst_pxp_if_full */
	0x253f0317, 0x00010001, 	/* cond: msdm.dst_int_ram_if_full */
	0x253f0318, 0x00010001, 	/* cond: msdm.dst_pas_buf_if_full */
	0x253f0340, 0x00010001, 	/* cond: msdm.int_cmpl_pend_empty */
	0x253f0341, 0x00010001, 	/* cond: msdm.int_cprm_pend_empty */
	0x253f0342, 0x00010001, 	/* cond: msdm.queue_empty */
	0x253f0343, 0x00010001, 	/* cond: msdm.delay_fifo_empty */
	0x253f0346, 0x00010001, 	/* cond: msdm.rsp_pxp_rdata_empty */
	0x253f0347, 0x00010001, 	/* cond: msdm.rsp_brb_rdata_empty */
	0x253f0348, 0x00010001, 	/* cond: msdm.rsp_int_ram_rdata_empty */
	0x253f0349, 0x00010001, 	/* cond: msdm.rsp_brb_pend_empty */
	0x253f034a, 0x00010001, 	/* cond: msdm.rsp_int_ram_pend_empty */
	0x253f034b, 0x00010001, 	/* cond: msdm.dst_pxp_immed_empty */
	0x253f034c, 0x00010001, 	/* cond: msdm.dst_pxp_dst_pend_empty */
	0x253f034d, 0x00010001, 	/* cond: msdm.dst_pxp_src_pend_empty */
	0x253f034e, 0x00010001, 	/* cond: msdm.dst_brb_src_pend_empty */
	0x253f034f, 0x00010001, 	/* cond: msdm.dst_brb_src_addr_empty */
	0x253f0350, 0x00010001, 	/* cond: msdm.dst_pxp_link_empty */
	0x253f0351, 0x00010001, 	/* cond: msdm.dst_int_ram_wait_empty */
	0x253f0352, 0x00010001, 	/* cond: msdm.dst_pas_buf_wait_empty */
	0x253f0353, 0x00010001, 	/* cond: msdm.sh_delay_empty */
	0x253f0354, 0x00010001, 	/* cond: msdm.cm_delay_empty */
	0x253f0355, 0x00010001, 	/* cond: msdm.cmsg_que_empty */
	0x253f0356, 0x00010001, 	/* cond: msdm.ccfc_load_pend_empty */
	0x253f0357, 0x00010001, 	/* cond: msdm.tcfc_load_pend_empty */
	0x253f0358, 0x00010001, 	/* cond: msdm.async_host_empty */
	0x253f0359, 0x00010001, 	/* cond: msdm.prm_fifo_empty */
	0x263f4010, 0x00010001, 	/* cond: usdm.INT_STS */
	0x263f4011, 0x00010001, 	/* cond: usdm.INT_MASK */
	0x263f4109, 0x00000001, 	/* info: usdm.inp_queue_err_vect mode=all */
	0x263f4080, 0x00010001, 	/* cond: usdm.PRTY_STS_H_0 */
	0x263f4081, 0x00010001, 	/* cond: usdm.PRTY_MASK_H_0 */
	0x263f4303, 0x00010001, 	/* cond: usdm.qm_full */
	0x263f430c, 0x00010001, 	/* cond: usdm.rsp_brb_if_full */
	0x263f430d, 0x00010001, 	/* cond: usdm.rsp_pxp_if_full */
	0x263f4316, 0x00010001, 	/* cond: usdm.dst_pxp_if_full */
	0x263f4317, 0x00010001, 	/* cond: usdm.dst_int_ram_if_full */
	0x263f4318, 0x00010001, 	/* cond: usdm.dst_pas_buf_if_full */
	0x263f4340, 0x00010001, 	/* cond: usdm.int_cmpl_pend_empty */
	0x263f4341, 0x00010001, 	/* cond: usdm.int_cprm_pend_empty */
	0x263f4342, 0x00010001, 	/* cond: usdm.queue_empty */
	0x263f4343, 0x00010001, 	/* cond: usdm.delay_fifo_empty */
	0x263f4346, 0x00010001, 	/* cond: usdm.rsp_pxp_rdata_empty */
	0x263f4347, 0x00010001, 	/* cond: usdm.rsp_brb_rdata_empty */
	0x263f4348, 0x00010001, 	/* cond: usdm.rsp_int_ram_rdata_empty */
	0x263f4349, 0x00010001, 	/* cond: usdm.rsp_brb_pend_empty */
	0x263f434a, 0x00010001, 	/* cond: usdm.rsp_int_ram_pend_empty */
	0x263f434b, 0x00010001, 	/* cond: usdm.dst_pxp_immed_empty */
	0x263f434c, 0x00010001, 	/* cond: usdm.dst_pxp_dst_pend_empty */
	0x263f434d, 0x00010001, 	/* cond: usdm.dst_pxp_src_pend_empty */
	0x263f434e, 0x00010001, 	/* cond: usdm.dst_brb_src_pend_empty */
	0x263f434f, 0x00010001, 	/* cond: usdm.dst_brb_src_addr_empty */
	0x263f4350, 0x00010001, 	/* cond: usdm.dst_pxp_link_empty */
	0x263f4351, 0x00010001, 	/* cond: usdm.dst_int_ram_wait_empty */
	0x263f4352, 0x00010001, 	/* cond: usdm.dst_pas_buf_wait_empty */
	0x263f4353, 0x00010001, 	/* cond: usdm.sh_delay_empty */
	0x263f4354, 0x00010001, 	/* cond: usdm.cm_delay_empty */
	0x263f4355, 0x00010001, 	/* cond: usdm.cmsg_que_empty */
	0x263f4356, 0x00010001, 	/* cond: usdm.ccfc_load_pend_empty */
	0x263f4357, 0x00010001, 	/* cond: usdm.tcfc_load_pend_empty */
	0x263f4358, 0x00010001, 	/* cond: usdm.async_host_empty */
	0x263f4359, 0x00010001, 	/* cond: usdm.prm_fifo_empty */
	0x1b400060, 0x00010001, 	/* cond: xcm.INT_STS_0 */
	0x1b400061, 0x00010001, 	/* cond: xcm.INT_MASK_0 */
	0x1b400064, 0x00010001, 	/* cond: xcm.INT_STS_1 */
	0x1b400065, 0x00010001, 	/* cond: xcm.INT_MASK_1 */
	0x1b400068, 0x00010001, 	/* cond: xcm.INT_STS_2 */
	0x1b400069, 0x00010001, 	/* cond: xcm.INT_MASK_2 */
	0x1b4002d4, 0x00000001, 	/* info: xcm.qm_act_st_cnt_err_details mode=all */
	0x1b400080, 0x00010001, 	/* cond: xcm.PRTY_STS_H_0 */
	0x1b400081, 0x00010001, 	/* cond: xcm.PRTY_MASK_H_0 */
	0x1b400084, 0x00010001, 	/* cond: xcm.PRTY_STS_H_1 */
	0x1b400085, 0x00010001, 	/* cond: xcm.PRTY_MASK_H_1 */
	0x1b4001a2, 0x00010001, 	/* cond: xcm.fi_desc_input_violate */
	0x1b4001a3, 0x00010001, 	/* cond: xcm.ia_agg_con_part_fill_lvl */
	0x1b4001a4, 0x00010001, 	/* cond: xcm.ia_sm_con_part_fill_lvl */
	0x1b4001a5, 0x00010001, 	/* cond: xcm.ia_trans_part_fill_lvl */
	0x1b4001c4, 0x00010001, 	/* cond: xcm.xx_free_cnt */
	0x1b4001c5, 0x00010001, 	/* cond: xcm.xx_lcid_cam_fill_lvl */
	0x1b4001ce, 0x00010001, 	/* cond: xcm.xx_non_lock_cnt */
	0x1b4001cf, 0x00010001, 	/* cond: xcm.xx_lock_cnt */
	0x1b4001d6, 0x00010001, 	/* cond: xcm.xx_cbyp_tbl_fill_lvl */
	0x1b4001e2, 0x00010001, 	/* cond: xcm.xx_pref_dir_fill_lvl */
	0x1b4001e3, 0x00010001, 	/* cond: xcm.xx_pref_aggst_fill_lvl */
	0x1b4001e4, 0x00010001, 	/* cond: xcm.xx_pref_byp_fill_lvl */
	0x1b400201, 0x00010001, 	/* cond: xcm.prcs_agg_con_curr_st */
	0x1b400202, 0x00010001, 	/* cond: xcm.prcs_sm_con_curr_st */
	0x1b40020b, 0x00010001, 	/* cond: xcm.agg_con_fic_buf_fill_lvl */
	0x1b40020c, 0x00010001, 	/* cond: xcm.sm_con_fic_buf_fill_lvl */
	0x1b400283, 0x00010001, 	/* cond: xcm.in_prcs_tbl_fill_lvl */
	0x1b400288, 0x00010001, 	/* cond: xcm.cmpl_dir_curr_st */
	0x1b4002a1, 0x00010001, 	/* cond: xcm.ccfc_init_crd */
	0x1b4002a2, 0x00010001, 	/* cond: xcm.qm_init_crd0 */
	0x1b4002a3, 0x00010001, 	/* cond: xcm.qm_init_crd1 */
	0x1b4002a4, 0x00010001, 	/* cond: xcm.tm_init_crd */
	0x1b4002ab, 0x00010001, 	/* cond: xcm.xsdm_length_mis */
	0x1b4002ac, 0x00010001, 	/* cond: xcm.ysdm_length_mis */
	0x1b4002ae, 0x00010001, 	/* cond: xcm.dorq_length_mis */
	0x1b4002af, 0x00010001, 	/* cond: xcm.pbf_length_mis */
	0x1b4002c0, 0x00010001, 	/* cond: xcm.is_qm_p_fill_lvl */
	0x1b4002c1, 0x00010001, 	/* cond: xcm.is_qm_s_fill_lvl */
	0x1b4002c2, 0x00010001, 	/* cond: xcm.is_tm_fill_lvl */
	0x1b4002c3, 0x00010001, 	/* cond: xcm.is_storm_fill_lvl */
	0x1b4002c5, 0x00010001, 	/* cond: xcm.is_xsdm_fill_lvl */
	0x1b4002c6, 0x00010001, 	/* cond: xcm.is_ysdm_fill_lvl */
	0x1b4002c8, 0x00010001, 	/* cond: xcm.is_msem_fill_lvl */
	0x1b4002c9, 0x00010001, 	/* cond: xcm.is_usem_fill_lvl */
	0x1b4002cb, 0x00010001, 	/* cond: xcm.is_dorq_fill_lvl */
	0x1b4002cc, 0x00010001, 	/* cond: xcm.is_pbf_fill_lvl */
	0x1c420060, 0x00010001, 	/* cond: ycm.INT_STS_0 */
	0x1c420061, 0x00010001, 	/* cond: ycm.INT_MASK_0 */
	0x1c420064, 0x00010001, 	/* cond: ycm.INT_STS_1 */
	0x1c420065, 0x00010001, 	/* cond: ycm.INT_MASK_1 */
	0x1c420080, 0x00010001, 	/* cond: ycm.PRTY_STS_H_0 */
	0x1c420081, 0x00010001, 	/* cond: ycm.PRTY_MASK_H_0 */
	0x1c420084, 0x00010001, 	/* cond: ycm.PRTY_STS_H_1 */
	0x1c420085, 0x00010001, 	/* cond: ycm.PRTY_MASK_H_1 */
	0x1c42019b, 0x00010001, 	/* cond: ycm.fi_desc_input_violate */
	0x1c42019c, 0x00010001, 	/* cond: ycm.se_desc_input_violate */
	0x1c42019e, 0x00010001, 	/* cond: ycm.ia_sm_con_part_fill_lvl */
	0x1c42019f, 0x00010001, 	/* cond: ycm.ia_agg_task_part_fill_lvl */
	0x1c4201a0, 0x00010001, 	/* cond: ycm.ia_sm_task_part_fill_lvl */
	0x1c4201a1, 0x00010001, 	/* cond: ycm.ia_trans_part_fill_lvl */
	0x1c4201c4, 0x00010001, 	/* cond: ycm.xx_free_cnt */
	0x1c4201c5, 0x00010001, 	/* cond: ycm.xx_lcid_cam_fill_lvl */
	0x1c4201ce, 0x00010001, 	/* cond: ycm.xx_non_lock_cnt */
	0x1c4201cf, 0x00010001, 	/* cond: ycm.xx_lock_cnt */
	0x1c4201d6, 0x00010001, 	/* cond: ycm.xx_cbyp_tbl_fill_lvl */
	0x1c4201d9, 0x00010001, 	/* cond: ycm.xx_tbyp_tbl_fill_lvl */
	0x1c4201d9, 0x00010001, 	/* cond: ycm.xx_tbyp_tbl_fill_lvl */
	0x1c4201e5, 0x00010001, 	/* cond: ycm.xx_pref_dir_fill_lvl */
	0x1c4201e6, 0x00010001, 	/* cond: ycm.xx_pref_aggst_fill_lvl */
	0x1c4201e7, 0x00010001, 	/* cond: ycm.xx_pref_byp_fill_lvl */
	0x1c420201, 0x00010001, 	/* cond: ycm.prcs_agg_con_curr_st */
	0x1c420202, 0x00010001, 	/* cond: ycm.prcs_sm_con_curr_st */
	0x1c420203, 0x00010001, 	/* cond: ycm.prcs_agg_task_curr_st */
	0x1c420204, 0x00010001, 	/* cond: ycm.prcs_sm_task_curr_st */
	0x1c420216, 0x00010001, 	/* cond: ycm.sm_con_fic_buf_fill_lvl */
	0x1c42021e, 0x00010001, 	/* cond: ycm.agg_task_fic_buf_fill_lvl */
	0x1c42021f, 0x00010001, 	/* cond: ycm.sm_task_fic_buf_fill_lvl */
	0x1c420283, 0x00010001, 	/* cond: ycm.in_prcs_tbl_fill_lvl */
	0x1c420289, 0x00010001, 	/* cond: ycm.cmpl_dir_curr_st */
	0x1c4202a1, 0x00010001, 	/* cond: ycm.ccfc_init_crd */
	0x1c4202a2, 0x00010001, 	/* cond: ycm.tcfc_init_crd */
	0x1c4202a3, 0x00010001, 	/* cond: ycm.qm_init_crd0 */
	0x1c4202ab, 0x00010001, 	/* cond: ycm.ysdm_length_mis */
	0x1c4202ac, 0x00010001, 	/* cond: ycm.pbf_length_mis */
	0x1c4202ad, 0x00010001, 	/* cond: ycm.xyld_length_mis */
	0x1c4202ba, 0x00010001, 	/* cond: ycm.is_qm_p_fill_lvl */
	0x1c4202bb, 0x00010001, 	/* cond: ycm.is_qm_s_fill_lvl */
	0x1c4202bc, 0x00010001, 	/* cond: ycm.is_storm_fill_lvl */
	0x1c4202be, 0x00010001, 	/* cond: ycm.is_ysdm_fill_lvl */
	0x1c4202bf, 0x00010001, 	/* cond: ycm.is_xyld_fill_lvl */
	0x1c4202c0, 0x00010001, 	/* cond: ycm.is_msem_fill_lvl */
	0x1c4202c1, 0x00010001, 	/* cond: ycm.is_usem_fill_lvl */
	0x1c4202c2, 0x00010001, 	/* cond: ycm.is_pbf_fill_lvl */
	0x1d440060, 0x00010001, 	/* cond: pcm.INT_STS_0 */
	0x1d440061, 0x00010001, 	/* cond: pcm.INT_MASK_0 */
	0x1d440064, 0x00010001, 	/* cond: pcm.INT_STS_1 */
	0x1d440065, 0x00010001, 	/* cond: pcm.INT_MASK_1 */
	0x1d440080, 0x00010001, 	/* cond: pcm.PRTY_STS_H_0 */
	0x1d440081, 0x00010001, 	/* cond: pcm.PRTY_MASK_H_0 */
	0x1d440191, 0x00010001, 	/* cond: pcm.fi_desc_input_violate */
	0x1d440192, 0x00010001, 	/* cond: pcm.ia_sm_con_part_fill_lvl */
	0x1d440193, 0x00010001, 	/* cond: pcm.ia_trans_part_fill_lvl */
	0x1d4401c4, 0x00010001, 	/* cond: pcm.xx_free_cnt */
	0x1d4401c5, 0x00010001, 	/* cond: pcm.xx_lcid_cam_fill_lvl */
	0x1d4401ce, 0x00010001, 	/* cond: pcm.xx_non_lock_cnt */
	0x1d4401cf, 0x00010001, 	/* cond: pcm.xx_lock_cnt */
	0x1d4401d6, 0x00010001, 	/* cond: pcm.xx_pref_dir_fill_lvl */
	0x1d4401d7, 0x00010001, 	/* cond: pcm.xx_pref_aggst_fill_lvl */
	0x1d440201, 0x00010001, 	/* cond: pcm.prcs_sm_con_curr_st */
	0x1d44020a, 0x00010001, 	/* cond: pcm.sm_con_fic_buf_fill_lvl */
	0x1d440283, 0x00010001, 	/* cond: pcm.in_prcs_tbl_fill_lvl */
	0x1d440286, 0x00010001, 	/* cond: pcm.cmpl_dir_curr_st */
	0x1d4402a1, 0x00010001, 	/* cond: pcm.ccfc_init_crd */
	0x1d4402b2, 0x00010001, 	/* cond: pcm.is_storm_fill_lvl */
	0x18460060, 0x00010001, 	/* cond: tcm.INT_STS_0 */
	0x18460061, 0x00010001, 	/* cond: tcm.INT_MASK_0 */
	0x18460064, 0x00010001, 	/* cond: tcm.INT_STS_1 */
	0x18460065, 0x00010001, 	/* cond: tcm.INT_MASK_1 */
	0x18460080, 0x00010001, 	/* cond: tcm.PRTY_STS_H_0 */
	0x18460081, 0x00010001, 	/* cond: tcm.PRTY_MASK_H_0 */
	0x18460084, 0x00010001, 	/* cond: tcm.PRTY_STS_H_1 */
	0x18460085, 0x00010001, 	/* cond: tcm.PRTY_MASK_H_1 */
	0x1846019b, 0x00010001, 	/* cond: tcm.fi_desc_input_violate */
	0x1846019c, 0x00010001, 	/* cond: tcm.se_desc_input_violate */
	0x1846019d, 0x00010001, 	/* cond: tcm.ia_agg_con_part_fill_lvl */
	0x1846019e, 0x00010001, 	/* cond: tcm.ia_sm_con_part_fill_lvl */
	0x1846019f, 0x00010001, 	/* cond: tcm.ia_agg_task_part_fill_lvl */
	0x184601a0, 0x00010001, 	/* cond: tcm.ia_sm_task_part_fill_lvl */
	0x184601a1, 0x00010001, 	/* cond: tcm.ia_trans_part_fill_lvl */
	0x184601c4, 0x00010001, 	/* cond: tcm.xx_free_cnt */
	0x184601c5, 0x00010001, 	/* cond: tcm.xx_lcid_cam_fill_lvl */
	0x184601ce, 0x00010001, 	/* cond: tcm.xx_non_lock_cnt */
	0x184601cf, 0x00010001, 	/* cond: tcm.xx_lock_cnt */
	0x184601d6, 0x00010001, 	/* cond: tcm.xx_cbyp_tbl_fill_lvl */
	0x184601d9, 0x00010001, 	/* cond: tcm.xx_tbyp_tbl_fill_lvl */
	0x184601d9, 0x00010001, 	/* cond: tcm.xx_tbyp_tbl_fill_lvl */
	0x184601e5, 0x00010001, 	/* cond: tcm.xx_pref_dir_fill_lvl */
	0x184601e6, 0x00010001, 	/* cond: tcm.xx_pref_aggst_fill_lvl */
	0x184601e7, 0x00010001, 	/* cond: tcm.xx_pref_byp_fill_lvl */
	0x18460201, 0x00010001, 	/* cond: tcm.prcs_agg_con_curr_st */
	0x18460202, 0x00010001, 	/* cond: tcm.prcs_sm_con_curr_st */
	0x18460203, 0x00010001, 	/* cond: tcm.prcs_agg_task_curr_st */
	0x18460204, 0x00010001, 	/* cond: tcm.prcs_sm_task_curr_st */
	0x18460215, 0x00010001, 	/* cond: tcm.agg_con_fic_buf_fill_lvl */
	0x18460216, 0x00010001, 	/* cond: tcm.sm_con_fic_buf_fill_lvl */
	0x1846021e, 0x00010001, 	/* cond: tcm.agg_task_fic_buf_fill_lvl */
	0x1846021f, 0x00010001, 	/* cond: tcm.sm_task_fic_buf_fill_lvl */
	0x18460283, 0x00010001, 	/* cond: tcm.in_prcs_tbl_fill_lvl */
	0x1846028b, 0x00010001, 	/* cond: tcm.cmpl_dir_curr_st */
	0x184602a1, 0x00010001, 	/* cond: tcm.ccfc_init_crd */
	0x184602a2, 0x00010001, 	/* cond: tcm.tcfc_init_crd */
	0x184602a3, 0x00010001, 	/* cond: tcm.qm_init_crd0 */
	0x184602a4, 0x00010001, 	/* cond: tcm.tm_init_crd */
	0x184602ab, 0x00010001, 	/* cond: tcm.dorq_length_mis */
	0x184602ac, 0x00010001, 	/* cond: tcm.pbf_length_mis */
	0x184602bb, 0x00010001, 	/* cond: tcm.is_qm_p_fill_lvl */
	0x184602bc, 0x00010001, 	/* cond: tcm.is_qm_s_fill_lvl */
	0x184602bd, 0x00010001, 	/* cond: tcm.is_tm_fill_lvl */
	0x184602be, 0x00010001, 	/* cond: tcm.is_storm_fill_lvl */
	0x184602c0, 0x00010001, 	/* cond: tcm.is_msem_fill_lvl */
	0x184602c2, 0x00010001, 	/* cond: tcm.is_dorq_fill_lvl */
	0x184602c3, 0x00010001, 	/* cond: tcm.is_pbf_fill_lvl */
	0x19480060, 0x00010001, 	/* cond: mcm.INT_STS_0 */
	0x19480061, 0x00010001, 	/* cond: mcm.INT_MASK_0 */
	0x19480064, 0x00010001, 	/* cond: mcm.INT_STS_1 */
	0x19480065, 0x00010001, 	/* cond: mcm.INT_MASK_1 */
	0x19480080, 0x00010001, 	/* cond: mcm.PRTY_STS_H_0 */
	0x19480081, 0x00010001, 	/* cond: mcm.PRTY_MASK_H_0 */
	0x19480084, 0x00010001, 	/* cond: mcm.PRTY_STS_H_1 */
	0x19480085, 0x00010001, 	/* cond: mcm.PRTY_MASK_H_1 */
	0x1948019d, 0x00010001, 	/* cond: mcm.fi_desc_input_violate */
	0x1948019e, 0x00010001, 	/* cond: mcm.se_desc_input_violate */
	0x1948019f, 0x00010001, 	/* cond: mcm.ia_agg_con_part_fill_lvl */
	0x194801a0, 0x00010001, 	/* cond: mcm.ia_sm_con_part_fill_lvl */
	0x194801a1, 0x00010001, 	/* cond: mcm.ia_agg_task_part_fill_lvl */
	0x194801a2, 0x00010001, 	/* cond: mcm.ia_sm_task_part_fill_lvl */
	0x194801a3, 0x00010001, 	/* cond: mcm.ia_trans_part_fill_lvl */
	0x194801c4, 0x00010001, 	/* cond: mcm.xx_free_cnt */
	0x194801c5, 0x00010001, 	/* cond: mcm.xx_lcid_cam_fill_lvl */
	0x194801ce, 0x00010001, 	/* cond: mcm.xx_non_lock_cnt */
	0x194801cf, 0x00010001, 	/* cond: mcm.xx_lock_cnt */
	0x194801d6, 0x00010001, 	/* cond: mcm.xx_cbyp_tbl_fill_lvl */
	0x194801d9, 0x00010001, 	/* cond: mcm.xx_tbyp_tbl_fill_lvl */
	0x194801d9, 0x00010001, 	/* cond: mcm.xx_tbyp_tbl_fill_lvl */
	0x194801e5, 0x00010001, 	/* cond: mcm.xx_pref_dir_fill_lvl */
	0x194801e6, 0x00010001, 	/* cond: mcm.xx_pref_aggst_fill_lvl */
	0x194801e7, 0x00010001, 	/* cond: mcm.xx_pref_byp_fill_lvl */
	0x19480201, 0x00010001, 	/* cond: mcm.prcs_agg_con_curr_st */
	0x19480202, 0x00010001, 	/* cond: mcm.prcs_sm_con_curr_st */
	0x19480203, 0x00010001, 	/* cond: mcm.prcs_agg_task_curr_st */
	0x19480204, 0x00010001, 	/* cond: mcm.prcs_sm_task_curr_st */
	0x19480215, 0x00010001, 	/* cond: mcm.agg_con_fic_buf_fill_lvl */
	0x19480216, 0x00010001, 	/* cond: mcm.sm_con_fic_buf_fill_lvl */
	0x1948021e, 0x00010001, 	/* cond: mcm.agg_task_fic_buf_fill_lvl */
	0x1948021f, 0x00010001, 	/* cond: mcm.sm_task_fic_buf_fill_lvl */
	0x19480283, 0x00010001, 	/* cond: mcm.in_prcs_tbl_fill_lvl */
	0x19480289, 0x00010001, 	/* cond: mcm.cmpl_dir_curr_st */
	0x194802a1, 0x00010001, 	/* cond: mcm.ccfc_init_crd */
	0x194802a2, 0x00010001, 	/* cond: mcm.tcfc_init_crd */
	0x194802a3, 0x00010001, 	/* cond: mcm.qm_init_crd0 */
	0x194802ab, 0x00010001, 	/* cond: mcm.ysdm_length_mis */
	0x194802ac, 0x00010001, 	/* cond: mcm.usdm_length_mis */
	0x194802ad, 0x00010001, 	/* cond: mcm.pbf_length_mis */
	0x194802ae, 0x00010001, 	/* cond: mcm.tmld_length_mis */
	0x194802bc, 0x00010001, 	/* cond: mcm.is_qm_p_fill_lvl */
	0x194802bd, 0x00010001, 	/* cond: mcm.is_qm_s_fill_lvl */
	0x194802be, 0x00010001, 	/* cond: mcm.is_storm_fill_lvl */
	0x194802c0, 0x00010001, 	/* cond: mcm.is_ysdm_fill_lvl */
	0x194802c1, 0x00010001, 	/* cond: mcm.is_usdm_fill_lvl */
	0x194802c2, 0x00010001, 	/* cond: mcm.is_tmld_fill_lvl */
	0x194802c3, 0x00010001, 	/* cond: mcm.is_usem_fill_lvl */
	0x194802c5, 0x00010001, 	/* cond: mcm.is_pbf_fill_lvl */
	0x1a4a0060, 0x00010001, 	/* cond: ucm.INT_STS_0 */
	0x1a4a0061, 0x00010001, 	/* cond: ucm.INT_MASK_0 */
	0x1a4a0064, 0x00010001, 	/* cond: ucm.INT_STS_1 */
	0x1a4a0065, 0x00010001, 	/* cond: ucm.INT_MASK_1 */
	0x1a4a0080, 0x00010001, 	/* cond: ucm.PRTY_STS_H_0 */
	0x1a4a0081, 0x00010001, 	/* cond: ucm.PRTY_MASK_H_0 */
	0x1a4a0084, 0x00010001, 	/* cond: ucm.PRTY_STS_H_1 */
	0x1a4a0085, 0x00010001, 	/* cond: ucm.PRTY_MASK_H_1 */
	0x1a4a01a3, 0x00010001, 	/* cond: ucm.fi_desc_input_violate */
	0x1a4a01a4, 0x00010001, 	/* cond: ucm.se_desc_input_violate */
	0x1a4a01a5, 0x00010001, 	/* cond: ucm.ia_agg_con_part_fill_lvl */
	0x1a4a01a6, 0x00010001, 	/* cond: ucm.ia_sm_con_part_fill_lvl */
	0x1a4a01a7, 0x00010001, 	/* cond: ucm.ia_agg_task_part_fill_lvl */
	0x1a4a01a8, 0x00010001, 	/* cond: ucm.ia_sm_task_part_fill_lvl */
	0x1a4a01a9, 0x00010001, 	/* cond: ucm.ia_trans_part_fill_lvl */
	0x1a4a01c4, 0x00010001, 	/* cond: ucm.xx_free_cnt */
	0x1a4a01c5, 0x00010001, 	/* cond: ucm.xx_lcid_cam_fill_lvl */
	0x1a4a01ce, 0x00010001, 	/* cond: ucm.xx_non_lock_cnt */
	0x1a4a01cf, 0x00010001, 	/* cond: ucm.xx_lock_cnt */
	0x1a4a01d6, 0x00010001, 	/* cond: ucm.xx_cbyp_tbl_fill_lvl */
	0x1a4a01d9, 0x00010001, 	/* cond: ucm.xx_tbyp_tbl_fill_lvl */
	0x1a4a01d9, 0x00010001, 	/* cond: ucm.xx_tbyp_tbl_fill_lvl */
	0x1a4a01e5, 0x00010001, 	/* cond: ucm.xx_pref_dir_fill_lvl */
	0x1a4a01e6, 0x00010001, 	/* cond: ucm.xx_pref_aggst_fill_lvl */
	0x1a4a01e7, 0x00010001, 	/* cond: ucm.xx_pref_byp_fill_lvl */
	0x1a4a0201, 0x00010001, 	/* cond: ucm.prcs_agg_con_curr_st */
	0x1a4a0202, 0x00010001, 	/* cond: ucm.prcs_sm_con_curr_st */
	0x1a4a0203, 0x00010001, 	/* cond: ucm.prcs_agg_task_curr_st */
	0x1a4a0204, 0x00010001, 	/* cond: ucm.prcs_sm_task_curr_st */
	0x1a4a0215, 0x00010001, 	/* cond: ucm.agg_con_fic_buf_fill_lvl */
	0x1a4a0216, 0x00010001, 	/* cond: ucm.sm_con_fic_buf_fill_lvl */
	0x1a4a021e, 0x00010001, 	/* cond: ucm.agg_task_fic_buf_fill_lvl */
	0x1a4a021f, 0x00010001, 	/* cond: ucm.sm_task_fic_buf_fill_lvl */
	0x1a4a0283, 0x00010001, 	/* cond: ucm.in_prcs_tbl_fill_lvl */
	0x1a4a028b, 0x00010001, 	/* cond: ucm.cmpl_dir_curr_st */
	0x1a4a02a1, 0x00010001, 	/* cond: ucm.ccfc_init_crd */
	0x1a4a02a2, 0x00010001, 	/* cond: ucm.tcfc_init_crd */
	0x1a4a02a3, 0x00010001, 	/* cond: ucm.qm_init_crd0 */
	0x1a4a02a4, 0x00010001, 	/* cond: ucm.tm_init_crd */
	0x1a4a02ab, 0x00010001, 	/* cond: ucm.ysdm_length_mis */
	0x1a4a02ac, 0x00010001, 	/* cond: ucm.usdm_length_mis */
	0x1a4a02ad, 0x00010001, 	/* cond: ucm.dorq_length_mis */
	0x1a4a02ae, 0x00010001, 	/* cond: ucm.pbf_length_mis */
	0x1a4a02af, 0x00010001, 	/* cond: ucm.rdif_length_mis */
	0x1a4a02b0, 0x00010001, 	/* cond: ucm.tdif_length_mis */
	0x1a4a02b1, 0x00010001, 	/* cond: ucm.muld_length_mis */
	0x1a4a02c3, 0x00010001, 	/* cond: ucm.is_qm_p_fill_lvl */
	0x1a4a02c4, 0x00010001, 	/* cond: ucm.is_qm_s_fill_lvl */
	0x1a4a02c5, 0x00010001, 	/* cond: ucm.is_tm_fill_lvl */
	0x1a4a02c6, 0x00010001, 	/* cond: ucm.is_storm_fill_lvl */
	0x1a4a02c8, 0x00010001, 	/* cond: ucm.is_ysdm_fill_lvl */
	0x1a4a02c9, 0x00010001, 	/* cond: ucm.is_usdm_fill_lvl */
	0x1a4a02ca, 0x00010001, 	/* cond: ucm.is_rdif_fill_lvl */
	0x1a4a02cb, 0x00010001, 	/* cond: ucm.is_tdif_fill_lvl */
	0x1a4a02cc, 0x00010001, 	/* cond: ucm.is_muld_fill_lvl */
	0x1a4a02ce, 0x00010001, 	/* cond: ucm.is_dorq_fill_lvl */
	0x1a4a02cf, 0x00010001, 	/* cond: ucm.is_pbf_fill_lvl */
	0x2d500010, 0x00010001, 	/* cond: xsem.INT_STS_0 */
	0x2d500011, 0x00010001, 	/* cond: xsem.INT_MASK_0 */
	0x2d500014, 0x00010001, 	/* cond: xsem.INT_STS_1 */
	0x2d500015, 0x00010001, 	/* cond: xsem.INT_MASK_1 */
	0x2d500032, 0x00010001, 	/* cond: xsem.PRTY_STS */
	0x2d500033, 0x00010001, 	/* cond: xsem.PRTY_MASK */
	0x2d500110, 0x00010001, 	/* cond: xsem.pf_err_vector */
	0x2d5001a0, 0x01010001, 	/* cond: xsem.foc_credit[1] */
	0x2d5001a0, 0x00010001, 	/* cond: xsem.foc_credit */
	0x2d500441, 0x00010001, 	/* cond: xsem.ext_pas_empty */
	0x2d500448, 0x00010002, 	/* cond: xsem.fic_empty[0:1] */
	0x2d500454, 0x00010001, 	/* cond: xsem.slow_ext_store_empty */
	0x2d500455, 0x00010001, 	/* cond: xsem.slow_ext_load_empty */
	0x2d500458, 0x00010001, 	/* cond: xsem.sync_dbg_empty */
	0x2d500481, 0x00010001, 	/* cond: xsem.ext_store_if_full */
	0x2d500491, 0x00010001, 	/* cond: xsem.ram_if_full */
	0x2e540010, 0x00010001, 	/* cond: ysem.INT_STS_0 */
	0x2e540011, 0x00010001, 	/* cond: ysem.INT_MASK_0 */
	0x2e540014, 0x00010001, 	/* cond: ysem.INT_STS_1 */
	0x2e540015, 0x00010001, 	/* cond: ysem.INT_MASK_1 */
	0x2e540032, 0x00010001, 	/* cond: ysem.PRTY_STS */
	0x2e540033, 0x00010001, 	/* cond: ysem.PRTY_MASK */
	0x2e540110, 0x00010001, 	/* cond: ysem.pf_err_vector */
	0x2e5401a0, 0x00010001, 	/* cond: ysem.foc_credit */
	0x2e5401a0, 0x01010001, 	/* cond: ysem.foc_credit[1] */
	0x2e5401a0, 0x02010001, 	/* cond: ysem.foc_credit[2] */
	0x2e5401a0, 0x03010001, 	/* cond: ysem.foc_credit[3] */
	0x2e5401a0, 0x04010001, 	/* cond: ysem.foc_credit[4] */
	0x2e5401a0, 0x05010001, 	/* cond: ysem.foc_credit[5] */
	0x2e540441, 0x00010001, 	/* cond: ysem.ext_pas_empty */
	0x2e540448, 0x00010002, 	/* cond: ysem.fic_empty[0:1] */
	0x2e540454, 0x00010001, 	/* cond: ysem.slow_ext_store_empty */
	0x2e540455, 0x00010001, 	/* cond: ysem.slow_ext_load_empty */
	0x2e540458, 0x00010001, 	/* cond: ysem.sync_dbg_empty */
	0x2e540481, 0x00010001, 	/* cond: ysem.ext_store_if_full */
	0x2e540491, 0x00010001, 	/* cond: ysem.ram_if_full */
	0x2f580010, 0x00010001, 	/* cond: psem.INT_STS_0 */
	0x2f580011, 0x00010001, 	/* cond: psem.INT_MASK_0 */
	0x2f580014, 0x00010001, 	/* cond: psem.INT_STS_1 */
	0x2f580015, 0x00010001, 	/* cond: psem.INT_MASK_1 */
	0x2f580032, 0x00010001, 	/* cond: psem.PRTY_STS */
	0x2f580033, 0x00010001, 	/* cond: psem.PRTY_MASK */
	0x2f580110, 0x00010001, 	/* cond: psem.pf_err_vector */
	0x2f5801a0, 0x00010001, 	/* cond: psem.foc_credit */
	0x2f5801a0, 0x01010001, 	/* cond: psem.foc_credit[1] */
	0x2f580441, 0x00010001, 	/* cond: psem.ext_pas_empty */
	0x2f580448, 0x00010001, 	/* cond: psem.fic_empty */
	0x2f580454, 0x00010001, 	/* cond: psem.slow_ext_store_empty */
	0x2f580455, 0x00010001, 	/* cond: psem.slow_ext_load_empty */
	0x2f580458, 0x00010001, 	/* cond: psem.sync_dbg_empty */
	0x2f580481, 0x00010001, 	/* cond: psem.ext_store_if_full */
	0x2f580491, 0x00010001, 	/* cond: psem.ram_if_full */
	0x2a5c0010, 0x00010001, 	/* cond: tsem.INT_STS_0 */
	0x2a5c0011, 0x00010001, 	/* cond: tsem.INT_MASK_0 */
	0x2a5c0014, 0x00010001, 	/* cond: tsem.INT_STS_1 */
	0x2a5c0015, 0x00010001, 	/* cond: tsem.INT_MASK_1 */
	0x2a5c0032, 0x00010001, 	/* cond: tsem.PRTY_STS */
	0x2a5c0033, 0x00010001, 	/* cond: tsem.PRTY_MASK */
	0x2a5c0110, 0x00010001, 	/* cond: tsem.pf_err_vector */
	0x2a5c01a0, 0x01010001, 	/* cond: tsem.foc_credit[1] */
	0x2a5c01a0, 0x00010001, 	/* cond: tsem.foc_credit */
	0x2a5c0441, 0x00010001, 	/* cond: tsem.ext_pas_empty */
	0x2a5c0448, 0x00010001, 	/* cond: tsem.fic_empty */
	0x2a5c0454, 0x00010001, 	/* cond: tsem.slow_ext_store_empty */
	0x2a5c0455, 0x00010001, 	/* cond: tsem.slow_ext_load_empty */
	0x2a5c0458, 0x00010001, 	/* cond: tsem.sync_dbg_empty */
	0x2a5c0481, 0x00010001, 	/* cond: tsem.ext_store_if_full */
	0x2a5c0491, 0x00010001, 	/* cond: tsem.ram_if_full */
	0x2b600010, 0x00010001, 	/* cond: msem.INT_STS_0 */
	0x2b600011, 0x00010001, 	/* cond: msem.INT_MASK_0 */
	0x2b600014, 0x00010001, 	/* cond: msem.INT_STS_1 */
	0x2b600015, 0x00010001, 	/* cond: msem.INT_MASK_1 */
	0x2b600032, 0x00010001, 	/* cond: msem.PRTY_STS */
	0x2b600033, 0x00010001, 	/* cond: msem.PRTY_MASK */
	0x2b600080, 0x00010001, 	/* cond: msem.PRTY_STS_H_0 */
	0x2b600081, 0x00010001, 	/* cond: msem.PRTY_MASK_H_0 */
	0x2b600110, 0x00010001, 	/* cond: msem.pf_err_vector */
	0x2b6001a0, 0x03010001, 	/* cond: msem.foc_credit[3] */
	0x2b6001a0, 0x04010001, 	/* cond: msem.foc_credit[4] */
	0x2b6001a0, 0x02010001, 	/* cond: msem.foc_credit[2] */
	0x2b6001a0, 0x01010001, 	/* cond: msem.foc_credit[1] */
	0x2b6001a0, 0x00010001, 	/* cond: msem.foc_credit */
	0x2b6001a0, 0x05010001, 	/* cond: msem.foc_credit[5] */
	0x2b600441, 0x00010001, 	/* cond: msem.ext_pas_empty */
	0x2b600448, 0x00010001, 	/* cond: msem.fic_empty */
	0x2b600454, 0x00010001, 	/* cond: msem.slow_ext_store_empty */
	0x2b600455, 0x00010001, 	/* cond: msem.slow_ext_load_empty */
	0x2b600458, 0x00010001, 	/* cond: msem.sync_dbg_empty */
	0x2b600481, 0x00010001, 	/* cond: msem.ext_store_if_full */
	0x2b600491, 0x00010001, 	/* cond: msem.ram_if_full */
	0x2c640010, 0x00010001, 	/* cond: usem.INT_STS_0 */
	0x2c640011, 0x00010001, 	/* cond: usem.INT_MASK_0 */
	0x2c640014, 0x00010001, 	/* cond: usem.INT_STS_1 */
	0x2c640015, 0x00010001, 	/* cond: usem.INT_MASK_1 */
	0x2c640032, 0x00010001, 	/* cond: usem.PRTY_STS */
	0x2c640033, 0x00010001, 	/* cond: usem.PRTY_MASK */
	0x2c640110, 0x00010001, 	/* cond: usem.pf_err_vector */
	0x2c6401a0, 0x00010001, 	/* cond: usem.foc_credit */
	0x2c6401a0, 0x01010001, 	/* cond: usem.foc_credit[1] */
	0x2c6401a0, 0x02010001, 	/* cond: usem.foc_credit[2] */
	0x2c6401a0, 0x03010001, 	/* cond: usem.foc_credit[3] */
	0x2c6401a0, 0x04010001, 	/* cond: usem.foc_credit[4] */
	0x2c640441, 0x00010001, 	/* cond: usem.ext_pas_empty */
	0x2c640448, 0x00010001, 	/* cond: usem.fic_empty */
	0x2c640454, 0x00010001, 	/* cond: usem.slow_ext_store_empty */
	0x2c640455, 0x00010001, 	/* cond: usem.slow_ext_load_empty */
	0x2c640458, 0x00010001, 	/* cond: usem.sync_dbg_empty */
	0x2c640481, 0x00010001, 	/* cond: usem.ext_store_if_full */
	0x2c640491, 0x00010001, 	/* cond: usem.ram_if_full */
	0x0a015000, 0x00010001, 	/* cond: pcie.PRTY_STS_H_0 */
	0x42060220, 0x00010009, 	/* cond: igu.pending_bits_status[0:8] */
	0x42060240, 0x00010009, 	/* cond: igu.write_done_pending[0:8] */
	0x050860ba, 0x00010001, 	/* cond: cnig.INT_STS */
	0x050860bb, 0x00010001, 	/* cond: cnig.INT_MASK */
	0x050860a7, 0x00470001, 	/* info: cnig.cnig_dbg_fifo_error mode=!(k2|e5) */
	0x050860d2, 0x00010001, 	/* cond: cnig.PRTY_STS */
	0x050860d3, 0x00010001, 	/* cond: cnig.PRTY_MASK */
	0x3008e301, 0x00010001, 	/* cond: rss.tmld_credit */
	0x140901c5, 0x00010001, 	/* cond: pswrq2.BW_CREDIT */
	0x0d0a8018, 0x00010001, 	/* cond: pswhst.vf_disabled_error_valid */
	0x0d0a8017, 0x00000001, 	/* info: pswhst.vf_disabled_error_data mode=all */
	0x0d0a8029, 0x00010001, 	/* cond: pswhst.dest_credits_avail */
	0x040aa80e, 0x00010001, 	/* cond: pglue_b.flr_request_vf_223_192 */
	0x040aa80f, 0x00010001, 	/* cond: pglue_b.flr_request_vf_255_224 */
	0x040aa83c, 0x00010001, 	/* cond: pglue_b.incorrect_rcv_details */
	0x040aa843, 0x00010001, 	/* cond: pglue_b.was_error_vf_223_192 */
	0x040aa844, 0x00010001, 	/* cond: pglue_b.was_error_vf_255_224 */
	0x040aab92, 0x00010001, 	/* cond: pglue_b.tags_159_128 */
	0x040aab93, 0x00010001, 	/* cond: pglue_b.tags_191_160 */
	0x040aab94, 0x00010001, 	/* cond: pglue_b.tags_223_192 */
	0x040aab95, 0x00010001, 	/* cond: pglue_b.tags_255_224 */
	0x410b41e2, 0x00010001, 	/* cond: tcfc.cduld_credit */
	0x410b41e3, 0x00010001, 	/* cond: tcfc.cduwb_credit */
	0x400b81e2, 0x00010001, 	/* cond: ccfc.cduld_credit */
	0x400b81e3, 0x00010001, 	/* cond: ccfc.cduwb_credit */
	0x1e8bc300, 0x00020040, 	/* cond: qm.PtrTblOther[0:63] width=2 access=WB */
	0x1e8bc300, 0x00020040, 	/* cond: qm.PtrTblOther[0:63] width=2 access=WB */
	0x1e0bc410, 0x0001000e, 	/* cond: qm.QstatusTx_0[0:13] */
	0x1e0bc430, 0x00010002, 	/* cond: qm.QstatusOther_0[0:1] */
	0x1e0bc560, 0x00010012, 	/* cond: qm.VoqCrdLine[0:17] */
	0x1e0bc5a0, 0x00010012, 	/* cond: qm.VoqInitCrdLine[0:17] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc560, 0x10010001, 	/* cond: qm.VoqCrdLine[16] */
	0x1e0bc5a0, 0x10010001, 	/* cond: qm.VoqInitCrdLine[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc560, 0x00010008, 	/* cond: qm.VoqCrdLine[0:7] */
	0x1e0bc5a0, 0x00010008, 	/* cond: qm.VoqInitCrdLine[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x10010001, 	/* cond: qm.VoqCrdByte[16] */
	0x1e0bc620, 0x10010001, 	/* cond: qm.VoqInitCrdByte[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x00010008, 	/* cond: qm.VoqCrdByte[0:7] */
	0x1e0bc620, 0x00010008, 	/* cond: qm.VoqInitCrdByte[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x00010012, 	/* cond: qm.VoqCrdByte[0:17] */
	0x1e0bc620, 0x00010012, 	/* cond: qm.VoqInitCrdByte[0:17] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e8bf800, 0x000201c0, 	/* cond: qm.PtrTblTx[0:447] width=2 access=WB */
	0x1e8bf800, 0x000201c0, 	/* cond: qm.PtrTblTx[0:447] width=2 access=WB */
	0x210d03cc, 0x00010004, 	/* cond: brb.wc_empty_0[0:3] */
	0x210d041e, 0x00010001, 	/* cond: brb.rc_eop_empty */
	0x218d06c0, 0x00030004, 	/* cond: brb.wc_status_0[0:3] width=3 access=WB */
	0x4b140028, 0x00010001, 	/* cond: nig.PRTY_STS */
	0x4b140029, 0x00010001, 	/* cond: nig.PRTY_MASK */
	0x4b1422db, 0x00000001, 	/* info: nig.rx_parity_err mode=all */
	0x4b1422dc, 0x00000001, 	/* info: nig.tx_parity_err mode=all */
	0x4b1422dd, 0x00000001, 	/* info: nig.lb_parity_err mode=all */
	0x3b36c030, 0x00010001, 	/* cond: btb.INT_STS_0 */
	0x3b36c031, 0x00010001, 	/* cond: btb.INT_MASK_0 */
	0x3bb6c400, 0x00000008, 	/* info: btb.stopped_rd_req[0:3] width=2 access=WB mode=all */
	0x3bb6c440, 0x00000008, 	/* info: btb.stopped_rls_req[0:3] width=2 access=WB mode=all */
	0x3b36c048, 0x00010001, 	/* cond: btb.INT_STS_4 */
	0x3b36c049, 0x00010001, 	/* cond: btb.INT_MASK_4 */
	0x3bb6c400, 0x00000008, 	/* info: btb.stopped_rd_req[0:3] width=2 access=WB mode=all */
	0x3bb6c440, 0x00000008, 	/* info: btb.stopped_rls_req[0:3] width=2 access=WB mode=all */
	0x1b4002a5, 0x00010001, 	/* cond: xcm.fic_init_crd */
	0x1b4002aa, 0x00010001, 	/* cond: xcm.msdm_length_mis */
	0x1b4002c4, 0x00010001, 	/* cond: xcm.is_msdm_fill_lvl */
	0x1b4002ca, 0x00010001, 	/* cond: xcm.is_ysem_fill_lvl */
	0x1b401000, 0x000101c0, 	/* cond: xcm.qm_act_st_cnt[0:447] */
	0x1c4202a6, 0x00010001, 	/* cond: ycm.fic_init_crd */
	0x1c4202aa, 0x00010001, 	/* cond: ycm.msdm_length_mis */
	0x1c4202bd, 0x00010001, 	/* cond: ycm.is_msdm_fill_lvl */
	0x1d4402a2, 0x00010001, 	/* cond: pcm.fic_init_crd */
	0x1d4402aa, 0x00010001, 	/* cond: pcm.psdm_length_mis */
	0x1d4402b3, 0x00010001, 	/* cond: pcm.is_psdm_fill_lvl */
	0x184602a5, 0x00010001, 	/* cond: tcm.fic_init_crd */
	0x184602aa, 0x00010001, 	/* cond: tcm.tsdm_length_mis */
	0x184602bf, 0x00010001, 	/* cond: tcm.is_tsdm_fill_lvl */
	0x194802a6, 0x00010001, 	/* cond: mcm.fic_init_crd */
	0x194802aa, 0x00010001, 	/* cond: mcm.msdm_length_mis */
	0x194802bf, 0x00010001, 	/* cond: mcm.is_msdm_fill_lvl */
	0x194802c4, 0x00010001, 	/* cond: mcm.is_ysem_fill_lvl */
	0x1a4a02a5, 0x00010001, 	/* cond: ucm.fic_init_crd */
	0x2dd00108, 0x00040001, 	/* cond: xsem.vf_err_vector width=4 access=WB */
	0x2ed40108, 0x00040001, 	/* cond: ysem.vf_err_vector width=4 access=WB */
	0x2fd80108, 0x00040001, 	/* cond: psem.vf_err_vector width=4 access=WB */
	0x2adc0108, 0x00040001, 	/* cond: tsem.vf_err_vector width=4 access=WB */
	0x2be00108, 0x00040001, 	/* cond: msem.vf_err_vector width=4 access=WB */
	0x2ce40108, 0x00040001, 	/* cond: usem.vf_err_vector width=4 access=WB */
	0x0a015000, 0x00010001, 	/* cond: pcie.PRTY_STS_H_0 */
	0x0a015001, 0x00010001, 	/* cond: pcie.PRTY_MASK_H_0 */
	0x42060220, 0x0001000c, 	/* cond: igu.pending_bits_status[0:11] */
	0x42060240, 0x0001000c, 	/* cond: igu.write_done_pending[0:11] */
	0x3008e301, 0x00010001, 	/* cond: rss.tmld_credit */
	0x140901c5, 0x00010001, 	/* cond: pswrq2.BW_CREDIT */
	0x0d0a8018, 0x00010001, 	/* cond: pswhst.vf_disabled_error_valid */
	0x0d0a8017, 0x00000001, 	/* info: pswhst.vf_disabled_error_data mode=all */
	0x0d0a8029, 0x00010001, 	/* cond: pswhst.dest_credits_avail */
	0x410b41e2, 0x00010001, 	/* cond: tcfc.cduld_credit */
	0x410b41e3, 0x00010001, 	/* cond: tcfc.cduwb_credit */
	0x400b81e2, 0x00010001, 	/* cond: ccfc.cduld_credit */
	0x400b81e3, 0x00010001, 	/* cond: ccfc.cduwb_credit */
	0x1e0bc410, 0x00010010, 	/* cond: qm.QstatusTx_0[0:15] */
	0x1e0bc430, 0x00010004, 	/* cond: qm.QstatusOther_0[0:3] */
	0x1e0bc560, 0x00010008, 	/* cond: qm.VoqCrdLine[0:7] */
	0x1e0bc5a0, 0x00010008, 	/* cond: qm.VoqInitCrdLine[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc560, 0x10010001, 	/* cond: qm.VoqCrdLine[16] */
	0x1e0bc5a0, 0x10010001, 	/* cond: qm.VoqInitCrdLine[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc560, 0x00010014, 	/* cond: qm.VoqCrdLine[0:19] */
	0x1e0bc5a0, 0x00010014, 	/* cond: qm.VoqInitCrdLine[0:19] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x00010014, 	/* cond: qm.VoqCrdByte[0:19] */
	0x1e0bc620, 0x00010014, 	/* cond: qm.VoqInitCrdByte[0:19] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x10010001, 	/* cond: qm.VoqCrdByte[16] */
	0x1e0bc620, 0x10010001, 	/* cond: qm.VoqInitCrdByte[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bc5e0, 0x00010008, 	/* cond: qm.VoqCrdByte[0:7] */
	0x1e0bc620, 0x00010008, 	/* cond: qm.VoqInitCrdByte[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x210d03cc, 0x00010008, 	/* cond: brb.wc_empty_0[0:7] */
	0x210d041e, 0x00010001, 	/* cond: brb.rc_eop_empty */
	0x218d06c0, 0x00030008, 	/* cond: brb.wc_status_0[0:7] width=3 access=WB */
	0x3b36c030, 0x00010001, 	/* cond: btb.INT_STS_0 */
	0x3b36c031, 0x00010001, 	/* cond: btb.INT_MASK_0 */
	0x3bb6c400, 0x00000010, 	/* info: btb.stopped_rd_req[0:7] width=2 access=WB mode=all */
	0x3bb6c440, 0x00000010, 	/* info: btb.stopped_rls_req[0:7] width=2 access=WB mode=all */
	0x3b36c048, 0x00010001, 	/* cond: btb.INT_STS_4 */
	0x3b36c049, 0x00010001, 	/* cond: btb.INT_MASK_4 */
	0x3bb6c400, 0x00000010, 	/* info: btb.stopped_rd_req[0:7] width=2 access=WB mode=all */
	0x3bb6c440, 0x00000010, 	/* info: btb.stopped_rls_req[0:7] width=2 access=WB mode=all */
	0x1b4002a5, 0x00010001, 	/* cond: xcm.fic_init_crd */
	0x1b4002aa, 0x00010001, 	/* cond: xcm.msdm_length_mis */
	0x1b4002c4, 0x00010001, 	/* cond: xcm.is_msdm_fill_lvl */
	0x1b4002ca, 0x00010001, 	/* cond: xcm.is_ysem_fill_lvl */
	0x1c4202a6, 0x00010001, 	/* cond: ycm.fic_init_crd */
	0x1c4202aa, 0x00010001, 	/* cond: ycm.msdm_length_mis */
	0x1c4202bd, 0x00010001, 	/* cond: ycm.is_msdm_fill_lvl */
	0x1d4402a2, 0x00010001, 	/* cond: pcm.fic_init_crd */
	0x1d4402aa, 0x00010001, 	/* cond: pcm.psdm_length_mis */
	0x1d4402b3, 0x00010001, 	/* cond: pcm.is_psdm_fill_lvl */
	0x184602a5, 0x00010001, 	/* cond: tcm.fic_init_crd */
	0x184602aa, 0x00010001, 	/* cond: tcm.tsdm_length_mis */
	0x184602bf, 0x00010001, 	/* cond: tcm.is_tsdm_fill_lvl */
	0x194802a6, 0x00010001, 	/* cond: mcm.fic_init_crd */
	0x194802aa, 0x00010001, 	/* cond: mcm.msdm_length_mis */
	0x194802bf, 0x00010001, 	/* cond: mcm.is_msdm_fill_lvl */
	0x194802c4, 0x00010001, 	/* cond: mcm.is_ysem_fill_lvl */
	0x1a4a02a5, 0x00010001, 	/* cond: ucm.fic_init_crd */
	0x14090279, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_7 */
	0x140902d8, 0x00010001, 	/* cond: pswrq2.max_srs_vq7 */
	0x1409027a, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_8 */
	0x140902d9, 0x00010001, 	/* cond: pswrq2.max_srs_vq8 */
	0x1409027c, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_10 */
	0x140902db, 0x00010001, 	/* cond: pswrq2.max_srs_vq10 */
	0x1409027f, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_13 */
	0x140902de, 0x00010001, 	/* cond: pswrq2.max_srs_vq13 */
	0x14090280, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_14 */
	0x140902df, 0x00010001, 	/* cond: pswrq2.max_srs_vq14 */
	0x14090286, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_20 */
	0x140902e5, 0x00010001, 	/* cond: pswrq2.max_srs_vq20 */
	0x1409028b, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_25 */
	0x140902ea, 0x00010001, 	/* cond: pswrq2.max_srs_vq25 */
	0x1409028d, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_27 */
	0x140902ec, 0x00010001, 	/* cond: pswrq2.max_srs_vq27 */
	0x14090291, 0x00010001, 	/* cond: pswrq2.sr_cnt_per_vq_31 */
	0x140902f0, 0x00010001, 	/* cond: pswrq2.max_srs_vq31 */
	0x1409029a, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_7 */
	0x14090258, 0x00010001, 	/* cond: pswrq2.max_blks_vq7 */
	0x1409029b, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_8 */
	0x14090259, 0x00010001, 	/* cond: pswrq2.max_blks_vq8 */
	0x1409029d, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_10 */
	0x1409025b, 0x00010001, 	/* cond: pswrq2.max_blks_vq10 */
	0x140902a0, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_13 */
	0x1409025e, 0x00010001, 	/* cond: pswrq2.max_blks_vq13 */
	0x140902a1, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_14 */
	0x1409025f, 0x00010001, 	/* cond: pswrq2.max_blks_vq14 */
	0x140902a7, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_20 */
	0x14090265, 0x00010001, 	/* cond: pswrq2.max_blks_vq20 */
	0x140902ac, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_25 */
	0x1409026a, 0x00010001, 	/* cond: pswrq2.max_blks_vq25 */
	0x140902ae, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_27 */
	0x1409026c, 0x00010001, 	/* cond: pswrq2.max_blks_vq27 */
	0x140902b2, 0x00010001, 	/* cond: pswrq2.blk_cnt_per_vq_31 */
	0x14090270, 0x00010001, 	/* cond: pswrq2.max_blks_vq31 */
	0x1f0b0064, 0x00010001, 	/* cond: tm.INT_STS_1 */
	0x09150100, 0x00010001, 	/* cond: bmb.PRTY_STS_H_0 */
	0x09150101, 0x00010001, 	/* cond: bmb.PRTY_MASK_H_0 */
	0x09150104, 0x00010001, 	/* cond: bmb.PRTY_STS_H_1 */
	0x09150105, 0x00010001, 	/* cond: bmb.PRTY_MASK_H_1 */
	0x210d0421, 0x00010001, 	/* cond: brb.ll_arb_empty */
	0x3b36c299, 0x00010001, 	/* cond: btb.ll_arb_empty */
	0x1f0b0064, 0x00010001, 	/* cond: tm.INT_STS_1 */
	0x15000e0c, 0x00010001, 	/* cond: pglcs.syncfifo_pop_underflow */
	0x15000e0d, 0x00010001, 	/* cond: pglcs.syncfifo_push_overflow */
	0x15000e0e, 0x00010001, 	/* cond: pglcs.tx_syncfifo_pop_status */
	0x05086086, 0x00010001, 	/* cond: cnig.INT_STS */
	0x05086087, 0x00010001, 	/* cond: cnig.INT_MASK */
	0x050860a7, 0x00470001, 	/* info: cnig.cnig_dbg_fifo_error mode=!(k2|e5) */
	0x0508608b, 0x00010001, 	/* cond: cnig.PRTY_STS */
	0x0508608c, 0x00010001, 	/* cond: cnig.PRTY_MASK */
	0x040aabf1, 0x00010001, 	/* cond: pglue_b.syncfifo_push_overflow */
	0x040aabf2, 0x00010001, 	/* cond: pglue_b.syncfifo_pop_underflow */
	0x040aabf3, 0x00010001, 	/* cond: pglue_b.rxh_syncfifo_pop_status */
	0x040aabf4, 0x00010001, 	/* cond: pglue_b.rxd_syncfifo_pop_status */
	0x040aabf3, 0x00030001, 	/* info: pglue_b.rxh_syncfifo_pop_status mode=!bb */
	0x1e8bc300, 0x00020080, 	/* cond: qm.PtrTblOther[0:127] width=2 access=WB */
	0x1e8bc300, 0x00020080, 	/* cond: qm.PtrTblOther[0:127] width=2 access=WB */
	0x1e8bf800, 0x00020200, 	/* cond: qm.PtrTblTx[0:511] width=2 access=WB */
	0x1e8bf800, 0x00020200, 	/* cond: qm.PtrTblTx[0:511] width=2 access=WB */
	0x210d0494, 0x00010001, 	/* cond: brb.mac2_tc_occupancy_0 */
	0x210d0495, 0x00010001, 	/* cond: brb.mac2_tc_occupancy_1 */
	0x210d0496, 0x00010001, 	/* cond: brb.mac2_tc_occupancy_2 */
	0x210d0497, 0x00010001, 	/* cond: brb.mac2_tc_occupancy_3 */
	0x210d0498, 0x00010001, 	/* cond: brb.mac2_tc_occupancy_4 */
	0x210d04a4, 0x00010001, 	/* cond: brb.mac3_tc_occupancy_0 */
	0x210d04a5, 0x00010001, 	/* cond: brb.mac3_tc_occupancy_1 */
	0x210d04a6, 0x00010001, 	/* cond: brb.mac3_tc_occupancy_2 */
	0x210d04a7, 0x00010001, 	/* cond: brb.mac3_tc_occupancy_3 */
	0x210d04a8, 0x00010001, 	/* cond: brb.mac3_tc_occupancy_4 */
	0x4b140028, 0x00010001, 	/* cond: nig.INT_STS_6 */
	0x4b140029, 0x00010001, 	/* cond: nig.INT_MASK_6 */
	0x4b14002c, 0x00010001, 	/* cond: nig.INT_STS_7 */
	0x4b14002d, 0x00010001, 	/* cond: nig.INT_MASK_7 */
	0x4b1406e8, 0x00000001, 	/* info: nig.flowctrl_mode mode=all */
	0x4b140713, 0x00000001, 	/* info: nig.rx_flowctrl_status mode=all */
	0x4b140030, 0x00010001, 	/* cond: nig.INT_STS_8 */
	0x4b140031, 0x00010001, 	/* cond: nig.INT_MASK_8 */
	0x4b140034, 0x00010001, 	/* cond: nig.INT_STS_9 */
	0x4b140035, 0x00010001, 	/* cond: nig.INT_MASK_9 */
	0x4b1406e8, 0x00000001, 	/* info: nig.flowctrl_mode mode=all */
	0x4b140713, 0x00000001, 	/* info: nig.rx_flowctrl_status mode=all */
	0x4b140038, 0x00010001, 	/* cond: nig.PRTY_STS */
	0x4b140039, 0x00010001, 	/* cond: nig.PRTY_MASK */
	0x4b1422db, 0x00000001, 	/* info: nig.rx_parity_err mode=all */
	0x4b1422dc, 0x00000001, 	/* info: nig.tx_parity_err mode=all */
	0x4b1422dd, 0x00000001, 	/* info: nig.lb_parity_err mode=all */
	0x501c0060, 0x00010001, 	/* cond: nws.INT_STS_0 */
	0x501c0080, 0x00010001, 	/* cond: nws.PRTY_STS_H_0 */
	0x501c0081, 0x00010001, 	/* cond: nws.PRTY_MASK_H_0 */
	0x4f200001, 0x00010001, 	/* cond: nwm.INT_STS */
	0x4f200001, 0x00010001, 	/* cond: nwm.INT_STS */
	0x3b36c26d, 0x00010001, 	/* cond: btb.rc_pkt_empty_4 */
	0x3b36c26e, 0x00010001, 	/* cond: btb.rc_pkt_empty_5 */
	0x3b36c26f, 0x00010001, 	/* cond: btb.rc_pkt_empty_6 */
	0x3b36c270, 0x00010001, 	/* cond: btb.rc_pkt_empty_7 */
	0x1b401000, 0x00010200, 	/* cond: xcm.qm_act_st_cnt[0:511] */
	0x2dd00108, 0x00080001, 	/* cond: xsem.vf_err_vector width=8 access=WB */
	0x2ed40108, 0x00080001, 	/* cond: ysem.vf_err_vector width=8 access=WB */
	0x2fd80108, 0x00080001, 	/* cond: psem.vf_err_vector width=8 access=WB */
	0x2adc0108, 0x00080001, 	/* cond: tsem.vf_err_vector width=8 access=WB */
	0x2be00108, 0x00080001, 	/* cond: msem.vf_err_vector width=8 access=WB */
	0x2ce40108, 0x00080001, 	/* cond: usem.vf_err_vector width=8 access=WB */
	0x210d0421, 0x00010001, 	/* cond: brb.ll_arb_empty */
	0x3b36c299, 0x00010001, 	/* cond: btb.ll_arb_empty */
	0x20040b01, 0x00010001, 	/* cond: dorq.iedpm_timer_expir_abort_cnt */
	0x42060220, 0x00010010, 	/* cond: igu.pending_bits_status[0:15] */
	0x42060240, 0x00010010, 	/* cond: igu.write_done_pending[0:15] */
	0x3008e301, 0x00010001, 	/* cond: rss.tmld_credit */
	0x3008e326, 0x00010001, 	/* cond: rss.fifo_full_status1 */
	0x3008e327, 0x00010001, 	/* cond: rss.fifo_empty_status1 */
	0x3008e328, 0x00010001, 	/* cond: rss.counter_status1 */
	0x3008e329, 0x00010001, 	/* cond: rss.state_machines1 */
	0x140901c5, 0x00010001, 	/* cond: pswrq2.BW_CREDIT */
	0x100a7456, 0x00010002, 	/* cond: pswrd2.almost_full_16[0:1] */
	0x0d0a8029, 0x00010001, 	/* cond: pswhst.dest_credits_avail */
	0x410b41e2, 0x00010001, 	/* cond: tcfc.cduld_credit */
	0x410b41e3, 0x00010001, 	/* cond: tcfc.cduwb_credit */
	0x400b81e2, 0x00010001, 	/* cond: ccfc.cduld_credit */
	0x400b81e3, 0x00010001, 	/* cond: ccfc.cduwb_credit */
	0x1e0bfe80, 0x00010008, 	/* cond: qm.VoqCrdLine[0:7] */
	0x1e0bfec0, 0x00010008, 	/* cond: qm.VoqInitCrdLine[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bfe80, 0x00010024, 	/* cond: qm.VoqCrdLine[0:35] */
	0x1e0bfec0, 0x00010024, 	/* cond: qm.VoqInitCrdLine[0:35] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bfe80, 0x10010001, 	/* cond: qm.VoqCrdLine[16] */
	0x1e0bfec0, 0x10010001, 	/* cond: qm.VoqInitCrdLine[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bff00, 0x00010024, 	/* cond: qm.VoqCrdByte[0:35] */
	0x1e0bff40, 0x00010024, 	/* cond: qm.VoqInitCrdByte[0:35] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bff00, 0x10010001, 	/* cond: qm.VoqCrdByte[16] */
	0x1e0bff40, 0x10010001, 	/* cond: qm.VoqInitCrdByte[16] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x1e0bff00, 0x00010008, 	/* cond: qm.VoqCrdByte[0:7] */
	0x1e0bff40, 0x00010008, 	/* cond: qm.VoqInitCrdByte[0:7] */
	0x02002300, 0x00010001, 	/* cond: misc.port_mode */
	0x210d041e, 0x00010001, 	/* cond: brb.rc_eop_empty */
	0x3c3602ec, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq20 */
	0x3c3602ee, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq20 */
	0x3c3602f1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq20 */
	0x3c3602fc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq21 */
	0x3c3602fe, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq21 */
	0x3c360301, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq21 */
	0x3c36030c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq22 */
	0x3c36030e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq22 */
	0x3c360311, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq22 */
	0x3c36031c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq23 */
	0x3c36031e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq23 */
	0x3c360321, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq23 */
	0x3c36032c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq24 */
	0x3c36032e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq24 */
	0x3c360331, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq24 */
	0x3c36033c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq25 */
	0x3c36033e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq25 */
	0x3c360341, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq25 */
	0x3c36034c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq26 */
	0x3c36034e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq26 */
	0x3c360351, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq26 */
	0x3c36035c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq27 */
	0x3c36035e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq27 */
	0x3c360361, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq27 */
	0x3c36036c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq28 */
	0x3c36036e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq28 */
	0x3c360371, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq28 */
	0x3c36037c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq29 */
	0x3c36037e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq29 */
	0x3c360381, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq29 */
	0x3c36038c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq30 */
	0x3c36038e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq30 */
	0x3c360391, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq30 */
	0x3c36039c, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq31 */
	0x3c36039e, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq31 */
	0x3c3603a1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq31 */
	0x3c3603ac, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq32 */
	0x3c3603ae, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq32 */
	0x3c3603b1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq32 */
	0x3c3603bc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq33 */
	0x3c3603be, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq33 */
	0x3c3603c1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq33 */
	0x3c3603cc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq34 */
	0x3c3603ce, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq34 */
	0x3c3603d1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq34 */
	0x3c3603dc, 0x00010001, 	/* cond: pbf.ycmd_qs_cmd_cnt_voq35 */
	0x3c3603de, 0x00010001, 	/* cond: pbf.ycmd_qs_occupancy_voq35 */
	0x3c3603e1, 0x00010001, 	/* cond: pbf.btb_allocated_blocks_voq35 */
	0x3b36c030, 0x00010001, 	/* cond: btb.INT_STS_0 */
	0x3b36c031, 0x00010001, 	/* cond: btb.INT_MASK_0 */
	0x3bb6c400, 0x00000020, 	/* info: btb.stopped_rd_req[0:7] width=3 access=WB mode=all */
	0x3bb6c440, 0x00000010, 	/* info: btb.stopped_rls_req[0:7] width=2 access=WB mode=all */
	0x3b36c048, 0x00010001, 	/* cond: btb.INT_STS_4 */
	0x3b36c049, 0x00010001, 	/* cond: btb.INT_MASK_4 */
	0x3bb6c400, 0x00000020, 	/* info: btb.stopped_rd_req[0:7] width=3 access=WB mode=all */
	0x3bb6c440, 0x00000010, 	/* info: btb.stopped_rls_req[0:7] width=2 access=WB mode=all */
	0x1b4001a6, 0x00010001, 	/* cond: xcm.ext_rd_fill_lvl */
	0x1b4002a5, 0x00010001, 	/* cond: xcm.fic_init_crd */
	0x1b400791, 0x00010001, 	/* cond: xcm.msdm_length_mis */
	0x1b400793, 0x00010001, 	/* cond: xcm.is_msdm_fill_lvl */
	0x1b4007c2, 0x00010001, 	/* cond: xcm.is_ysem_fill_lvl */
	0x1c4201a2, 0x00010001, 	/* cond: ycm.ext_rd_fill_lvl */
	0x1c4202a6, 0x00010001, 	/* cond: ycm.fic_init_crd */
	0x1c4207b1, 0x00010001, 	/* cond: ycm.msdm_length_mis */
	0x1c4207b3, 0x00010001, 	/* cond: ycm.is_msdm_fill_lvl */
	0x1d440194, 0x00010001, 	/* cond: pcm.ext_rd_fill_lvl */
	0x1d4402a2, 0x00010001, 	/* cond: pcm.fic_init_crd */
	0x1d4405d5, 0x00010001, 	/* cond: pcm.psdm_length_mis */
	0x1d4405d7, 0x00010001, 	/* cond: pcm.is_psdm_fill_lvl */
	0x1d4405f3, 0x00010001, 	/* cond: pcm.is_ypld_fill_lvl */
	0x184601a2, 0x00010001, 	/* cond: tcm.ext_rd_fill_lvl */
	0x184602a5, 0x00010001, 	/* cond: tcm.fic_init_crd */
	0x184606c1, 0x00010001, 	/* cond: tcm.tsdm_length_mis */
	0x184606c3, 0x00010001, 	/* cond: tcm.is_tsdm_fill_lvl */
	0x184606e3, 0x00010001, 	/* cond: tcm.is_psdm_fill_lvl */
	0x18460703, 0x00010001, 	/* cond: tcm.is_msdm_fill_lvl */
	0x18460783, 0x00010001, 	/* cond: tcm.is_ptld_fill_lvl */
	0x194801a4, 0x00010001, 	/* cond: mcm.ext_rd_fill_lvl */
	0x194802a6, 0x00010001, 	/* cond: mcm.fic_init_crd */
	0x19480a03, 0x00010001, 	/* cond: mcm.is_tsdm_fill_lvl */
	0x19480a23, 0x00010001, 	/* cond: mcm.is_psdm_fill_lvl */
	0x19480a41, 0x00010001, 	/* cond: mcm.msdm_length_mis */
	0x19480a43, 0x00010001, 	/* cond: mcm.is_msdm_fill_lvl */
	0x19480a82, 0x00010001, 	/* cond: mcm.is_ysem_fill_lvl */
	0x1a4a01aa, 0x00010001, 	/* cond: ucm.ext_rd_fill_lvl */
	0x1a4a02a5, 0x00010001, 	/* cond: ucm.fic_init_crd */
	0x1a4a0a04, 0x00010001, 	/* cond: ucm.is_ysem_fill_lvl */
	0x2d500018, 0x00010001, 	/* cond: xsem.INT_STS_2 */
	0x2d500019, 0x00010001, 	/* cond: xsem.INT_MASK_2 */
	0x2d5002c1, 0x00010001, 	/* cond: xsem.thread_error_low */
	0x2d5002c7, 0x00010001, 	/* cond: xsem.thread_error_high */
	0x2e540018, 0x00010001, 	/* cond: ysem.INT_STS_2 */
	0x2e540019, 0x00010001, 	/* cond: ysem.INT_MASK_2 */
	0x2e5402c1, 0x00010001, 	/* cond: ysem.thread_error_low */
	0x2e5402c7, 0x00010001, 	/* cond: ysem.thread_error_high */
	0x2f580018, 0x00010001, 	/* cond: psem.INT_STS_2 */
	0x2f580019, 0x00010001, 	/* cond: psem.INT_MASK_2 */
	0x2f5802c1, 0x00010001, 	/* cond: psem.thread_error_low */
	0x2f5802c7, 0x00010001, 	/* cond: psem.thread_error_high */
	0x2a5c0018, 0x00010001, 	/* cond: tsem.INT_STS_2 */
	0x2a5c0019, 0x00010001, 	/* cond: tsem.INT_MASK_2 */
	0x2a5c02c1, 0x00010001, 	/* cond: tsem.thread_error_low */
	0x2a5c02c7, 0x00010001, 	/* cond: tsem.thread_error_high */
	0x2b600018, 0x00010001, 	/* cond: msem.INT_STS_2 */
	0x2b600019, 0x00010001, 	/* cond: msem.INT_MASK_2 */
	0x2b6002c1, 0x00010001, 	/* cond: msem.thread_error_low */
	0x2b6002c7, 0x00010001, 	/* cond: msem.thread_error_high */
	0x2c640018, 0x00010001, 	/* cond: usem.INT_STS_2 */
	0x2c640019, 0x00010001, 	/* cond: usem.INT_MASK_2 */
	0x2c6402c1, 0x00010001, 	/* cond: usem.thread_error_low */
	0x2c6402c7, 0x00010001, 	/* cond: usem.thread_error_high */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.uc_err_status */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.uc_err_status */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.uc_err_status */
	0x15000847, 0x00010004, 	/* cond: pglcs.pgl_cs.header_log1[0:3] */
	0x15000a05, 0x00010001, 	/* cond: pglcs.pgl_cs.tl_ctrlstat_5 */
	0x15000a05, 0x00010001, 	/* cond: pglcs.pgl_cs.tl_ctrlstat_5 */
	0x15000a15, 0x00010001, 	/* cond: pglcs.pgl_cs.tl_func345_stat */
	0x15000a17, 0x00010001, 	/* cond: pglcs.pgl_cs.tl_func678_stat */
	0x15000c10, 0x00010001, 	/* cond: pglcs.pgl_cs.DLATTN_VEC */
	0x01002464, 0x00010001, 	/* cond: miscs.INT_STS_1 */
	0x01002465, 0x00010001, 	/* cond: miscs.INT_MASK_1 */
	0x4b140391, 0x00010001, 	/* cond: nig.rx_macfifo_empty */
	0x08014c00, 0x00010001, 	/* cond: opte.PRTY_STS_H_0 */
	0x08014c01, 0x00010001, 	/* cond: opte.PRTY_MASK_H_0 */
	0x43070327, 0x00010001, 	/* cond: cau.agg_units_0to15_state */
	0x43070328, 0x00010001, 	/* cond: cau.agg_units_16to31_state */
	0x43070329, 0x00010001, 	/* cond: cau.agg_units_32to47_state */
	0x4307032a, 0x00010001, 	/* cond: cau.agg_units_48to63_state */
	0x430703c3, 0x00010001, 	/* cond: cau.igu_cqe_cmd_fsm_status */
	0x430703c4, 0x00010001, 	/* cond: cau.igu_cqe_agg_fsm_status */
	0x2307c085, 0x00010001, 	/* cond: prs.PRTY_STS_H_1 */
	0x2307c086, 0x00010001, 	/* cond: prs.PRTY_MASK_H_1 */
	0x2307c2d4, 0x00010001, 	/* cond: prs.prs_pkt_ct */
	0x2307c3c3, 0x00010001, 	/* cond: prs.tcm_current_credit */
	0x3008e30e, 0x00010001, 	/* cond: rss.empty_status */
	0x3008e30f, 0x00010001, 	/* cond: rss.full_status */
	0x3008e310, 0x00010001, 	/* cond: rss.counters_status */
	0x3008e311, 0x00010001, 	/* cond: rss.state_machines */
	0x14090206, 0x00010001, 	/* cond: pswrq2.treq_fifo_fill_lvl */
	0x14090207, 0x00010001, 	/* cond: pswrq2.icpl_fifo_fill_lvl */
	0x33132013, 0x00010001, 	/* cond: yuld.pending_msg_to_ext_ev_1_ctr */
	0x33132014, 0x00010001, 	/* cond: yuld.pending_msg_to_ext_ev_2_ctr */
	0x33132015, 0x00010001, 	/* cond: yuld.pending_msg_to_ext_ev_3_ctr */
	0x33132016, 0x00010001, 	/* cond: yuld.pending_msg_to_ext_ev_4_ctr */
	0x33132017, 0x00010001, 	/* cond: yuld.pending_msg_to_ext_ev_5_ctr */
	0x33132018, 0x00010001, 	/* cond: yuld.foc_remain_credits */
	0x33132003, 0x00010001, 	/* cond: yuld.foci_foc_credits */
	0x3313202c, 0x00010001, 	/* cond: yuld.dbg_pending_ccfc_req */
	0x3313202d, 0x00010001, 	/* cond: yuld.dbg_pending_tcfc_req */
	0x33132080, 0x00010001, 	/* cond: yuld.PRTY_STS_H_0 */
	0x33132081, 0x00010001, 	/* cond: yuld.PRTY_MASK_H_0 */
	0x4b140394, 0x00010001, 	/* cond: nig.tx_macfifo_empty */
	0x1d4402ab, 0x00010001, 	/* cond: pcm.pbf_length_mis */
	0x184602ad, 0x00010001, 	/* cond: tcm.prs_length_mis */
	0x1a4a02b2, 0x00010001, 	/* cond: ucm.yuld_length_mis */
	0x2d500080, 0x00010001, 	/* cond: xsem.PRTY_STS_H_0 */
	0x2d500081, 0x00010001, 	/* cond: xsem.PRTY_MASK_H_0 */
	0x2e540080, 0x00010001, 	/* cond: ysem.PRTY_STS_H_0 */
	0x2e540081, 0x00010001, 	/* cond: ysem.PRTY_MASK_H_0 */
	0x2f580080, 0x00010001, 	/* cond: psem.PRTY_STS_H_0 */
	0x2f580081, 0x00010001, 	/* cond: psem.PRTY_MASK_H_0 */
	0x2a5c0080, 0x00010001, 	/* cond: tsem.PRTY_STS_H_0 */
	0x2a5c0081, 0x00010001, 	/* cond: tsem.PRTY_MASK_H_0 */
	0x2c640080, 0x00010001, 	/* cond: usem.PRTY_STS_H_0 */
	0x2c640081, 0x00010001, 	/* cond: usem.PRTY_MASK_H_0 */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF */
	0x15000841, 0x00010001, 	/* cond: pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF */
	0x15000847, 0x00010004, 	/* cond: pglcs.pgl_cs.HDR_LOG_0_OFF[0:3] */
	0x501c0003, 0x00010001, 	/* cond: nws.common_status */
	0x501c0003, 0x00010001, 	/* cond: nws.common_status */
	0x501c0003, 0x00010001, 	/* cond: nws.common_status */
	0x42060084, 0x00010001, 	/* cond: igu.PRTY_STS_H_1 */
	0x42060085, 0x00010001, 	/* cond: igu.PRTY_MASK_H_1 */
	0x0c014810, 0x00010001, 	/* cond: mcp2.PRTY_STS */
	0x0c014811, 0x00010001, 	/* cond: mcp2.PRTY_MASK */
	0x0c014881, 0x00010001, 	/* cond: mcp2.PRTY_STS_H_0 */
	0x0c014882, 0x00010001, 	/* cond: mcp2.PRTY_MASK_H_0 */
	0x07010000, 0x00010001, 	/* cond: ncsi.PRTY_STS_H_0 */
	0x07010001, 0x00010001, 	/* cond: ncsi.PRTY_MASK_H_0 */
	0x48014460, 0x00010001, 	/* cond: umac.INT_STS */
	0x48014460, 0x00010001, 	/* cond: umac.INT_STS */
	0x0a0151e8, 0x00010001, 	/* cond: pcie.INT_STS */
	0x501c8180, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_err_ctrl0 */
	0x501c8181, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_err_ctrl1 */
	0x501c8182, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_err_ctrl2 */
	0x501c8188, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status0 */
	0x501c8188, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status0 */
	0x501c8188, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status0 */
	0x501c8188, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status0 */
	0x501c8189, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status1 */
	0x501c818a, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status2 */
	0x501c818b, 0x00010001, 	/* cond: nws.nws_cmu.phy0_top_regbus_err_info_status3 */
	0x501c8852, 0x00010001, 	/* cond: nws.nws_cmu.cmu_lc0_top_phy_if_status */
	0x501c8880, 0x00010001, 	/* cond: nws.nws_cmu.cmu_lc0_top_err_ctrl1 */
	0x501c8881, 0x00010001, 	/* cond: nws.nws_cmu.cmu_lc0_top_err_ctrl2 */
	0x501c8882, 0x00010001, 	/* cond: nws.nws_cmu.cmu_lc0_top_err_ctrl3 */
	0x501c8c52, 0x00010001, 	/* cond: nws.nws_cmu.cmu_r0_top_phy_if_status */
	0x501c8c80, 0x00010001, 	/* cond: nws.nws_cmu.cmu_r0_top_err_ctrl1 */
	0x501c8c81, 0x00010001, 	/* cond: nws.nws_cmu.cmu_r0_top_err_ctrl2 */
	0x501c8c82, 0x00010001, 	/* cond: nws.nws_cmu.cmu_r0_top_err_ctrl3 */
	0x501c9850, 0x00010001, 	/* cond: nws.nws_cmu.ln0_top_err_ctrl1 */
	0x501c9851, 0x00010001, 	/* cond: nws.nws_cmu.ln0_top_err_ctrl2 */
	0x501c9852, 0x00010001, 	/* cond: nws.nws_cmu.ln0_top_err_ctrl3 */
	0x501ca050, 0x00010001, 	/* cond: nws.nws_cmu.ln1_top_err_ctrl1 */
	0x501ca051, 0x00010001, 	/* cond: nws.nws_cmu.ln1_top_err_ctrl2 */
	0x501ca052, 0x00010001, 	/* cond: nws.nws_cmu.ln1_top_err_ctrl3 */
	0x501ca850, 0x00010001, 	/* cond: nws.nws_cmu.ln2_top_err_ctrl1 */
	0x501ca851, 0x00010001, 	/* cond: nws.nws_cmu.ln2_top_err_ctrl2 */
	0x501ca852, 0x00010001, 	/* cond: nws.nws_cmu.ln2_top_err_ctrl3 */
	0x501cb050, 0x00010001, 	/* cond: nws.nws_cmu.ln3_top_err_ctrl1 */
	0x501cb051, 0x00010001, 	/* cond: nws.nws_cmu.ln3_top_err_ctrl2 */
	0x501cb052, 0x00010001, 	/* cond: nws.nws_cmu.ln3_top_err_ctrl3 */
	0x4f200080, 0x00010001, 	/* cond: nwm.PRTY_STS_H_0 */
	0x4f200081, 0x00010001, 	/* cond: nwm.PRTY_MASK_H_0 */
	0x4f200084, 0x00010001, 	/* cond: nwm.PRTY_STS_H_1 */
	0x4f200085, 0x00010001, 	/* cond: nwm.PRTY_MASK_H_1 */
	0x4f200088, 0x00010001, 	/* cond: nwm.PRTY_STS_H_2 */
	0x4f200089, 0x00010001, 	/* cond: nwm.PRTY_MASK_H_2 */
	0x4e008147, 0x00010001, 	/* cond: ipc.PRTY_STS */
	0x4e008148, 0x00010001, 	/* cond: ipc.PRTY_MASK */
};
/* Data size: 14472 bytes */

/* Idle check immediates */
static const u32 idle_chk_imms[] = {
	0x00000000, 0x000000b0, 0x00000000, 0x0000000f, 0x00000000, 0x34000000, 
	0x00000000, 0x00000060, 0x00000000, 0x00000005, 0x00000010, 0x00000002, 
	0x00000000, 0x00000001, 0x00000000, 0x00000004, 0x00000008, 0x00000003, 
	0x00022aab, 0x0076417c, 0x00000000, 0x0001bc01, 0x00000000, 0x00000011, 
	0x00000001, 0x00000000, 0xffffffff, 0x80000000, 0x007fffff, 0x7f800000, 
	0x0000041e, 0x000000ff, 0x000fffff, 0x0000ffff, 0x000000ff, 0x00000000, 
	0x00000007, 0x00000000, 0x00001ffe, 0x0000004a, 0x000001ff, 0x00000040, 
	0x0000001d, 0x0000003f, 0x0000000e, 0x0000001a, 0x0000000a, 0x00000026, 
	0x00000028, 0x00000017, 0x0000003c, 0x0000002c, 0x00000020, 0x00000032, 
	0x00003f02, 0x00000000, 0x000000b7, 0x0000eaaa, 0x00000030, 0x3fffffc0, 
	0x00000006, 0xc0000000, 0x0000001e, 0x003fffff, 0x00000002, 0x00000030, 
	0x00000004, 0x00000003, 0x0000002a, 0x00000034, 0x000003e1, 0x00000381, 
	0x000001fe, 0x00000000, 0x00001fff, 0x00000118, 0x0001eaaa, 0x000fd010, 
	0x00000000, 0x00100000, 0x00000000, 0x00002000, 0x00000000, 0x02040902, 
	0x00000000, 0x02000000, 0x00000000, 0x10240902, 0x00000000, 0x0000001f, 
	0x00000002, 0x00000002, 0x00000004, 0x00000004, 0x0000e001, 0x00000000, 
	0x00000004, 0x00000000, 
};
/* Data size: 392 bytes */

/* Idle check rules */
static const u32 idle_chk_rules[] = {
	0x0c0f0000, 	/* mode all */
	0x00000000, 0x00010002, 0x00000000, 	/* ((r1&~r2)!=0), r1=misc.INT_STS, r2=misc.INT_MASK,  */
	0x01000001, 0x00010101, 0x00000002, 	/* (r1!=0), r1=misc.aeu_after_invert_1_igu,  */
	0x04000002, 0x00020101, 0x00010004, 	/* ((r1&~0xB0)!=0), r1=misc.aeu_after_invert_2_igu,  */
	0x04000003, 0x00020101, 0x00030006, 	/* ((r1&~0xF)!=0), r1=misc.aeu_after_invert_4_igu,  */
	0x01000004, 0x00010101, 0x00000008, 	/* (r1!=0), r1=misc.aeu_after_invert_5_igu,  */
	0x01000005, 0x00010101, 0x0000000a, 	/* (r1!=0), r1=misc.aeu_after_invert_6_igu,  */
	0x01000006, 0x00010101, 0x0000000c, 	/* (r1!=0), r1=misc.aeu_after_invert_7_igu,  */
	0x04000007, 0x00020101, 0x0005000e, 	/* ((r1&~0x34000000)!=0), r1=misc.aeu_after_invert_8_igu,  */
	0x04000008, 0x00020101, 0x00070010, 	/* ((r1&~0x60)!=0), r1=misc.aeu_after_invert_9_igu,  */
	0x01000009, 0x00010101, 0x00000012, 	/* (r1!=0), r1=misc.aeu_after_invert_1_mcp,  */
	0x0400000a, 0x00020101, 0x00010014, 	/* ((r1&~0xB0)!=0), r1=misc.aeu_after_invert_2_mcp,  */
	0x0400000b, 0x00020101, 0x00030016, 	/* ((r1&~0xF)!=0), r1=misc.aeu_after_invert_4_mcp,  */
	0x0100000c, 0x00010101, 0x00000018, 	/* (r1!=0), r1=misc.aeu_after_invert_5_mcp,  */
	0x0100000d, 0x00010101, 0x0000001a, 	/* (r1!=0), r1=misc.aeu_after_invert_6_mcp,  */
	0x0100000e, 0x00010101, 0x0000001c, 	/* (r1!=0), r1=misc.aeu_after_invert_7_mcp,  */
	0x0400000f, 0x00020101, 0x0005001e, 	/* ((r1&~0x34000000)!=0), r1=misc.aeu_after_invert_8_mcp,  */
	0x04000010, 0x00020101, 0x00070020, 	/* ((r1&~0x60)!=0), r1=misc.aeu_after_invert_9_mcp,  */
	0x01000011, 0x00010001, 0x00000022, 	/* (r1!=0), r1=misc.aeu_sys_kill_occurred,  */
	0x00000012, 0x00010002, 0x00000023, 	/* ((r1&~r2)!=0), r1=miscs.INT_STS_0, r2=miscs.INT_MASK_0,  */
	0x01020013, 0x00010001, 0x00000025, 	/* (r1!=0), r1=miscs.pcie_hot_reset,  */
	0x01010014, 0x00010001, 0x00000026, 	/* (r1!=0), r1=dmae.go_c0,  */
	0x01010015, 0x00010001, 0x00000027, 	/* (r1!=0), r1=dmae.go_c1,  */
	0x01010016, 0x00010001, 0x00000028, 	/* (r1!=0), r1=dmae.go_c2,  */
	0x01010017, 0x00010001, 0x00000029, 	/* (r1!=0), r1=dmae.go_c3,  */
	0x01010018, 0x00010001, 0x0000002a, 	/* (r1!=0), r1=dmae.go_c4,  */
	0x01010019, 0x00010001, 0x0000002b, 	/* (r1!=0), r1=dmae.go_c5,  */
	0x0101001a, 0x00010001, 0x0000002c, 	/* (r1!=0), r1=dmae.go_c6,  */
	0x0101001b, 0x00010001, 0x0000002d, 	/* (r1!=0), r1=dmae.go_c7,  */
	0x0101001c, 0x00010001, 0x0000002e, 	/* (r1!=0), r1=dmae.go_c8,  */
	0x0101001d, 0x00010001, 0x0000002f, 	/* (r1!=0), r1=dmae.go_c9,  */
	0x0101001e, 0x00010001, 0x00000030, 	/* (r1!=0), r1=dmae.go_c10,  */
	0x0101001f, 0x00010001, 0x00000031, 	/* (r1!=0), r1=dmae.go_c11,  */
	0x01010020, 0x00010001, 0x00000032, 	/* (r1!=0), r1=dmae.go_c12,  */
	0x01010021, 0x00010001, 0x00000033, 	/* (r1!=0), r1=dmae.go_c13,  */
	0x01010022, 0x00010001, 0x00000034, 	/* (r1!=0), r1=dmae.go_c14,  */
	0x01010023, 0x00010001, 0x00000035, 	/* (r1!=0), r1=dmae.go_c15,  */
	0x01010024, 0x00010001, 0x00000036, 	/* (r1!=0), r1=dmae.go_c16,  */
	0x01010025, 0x00010001, 0x00000037, 	/* (r1!=0), r1=dmae.go_c17,  */
	0x01010026, 0x00010001, 0x00000038, 	/* (r1!=0), r1=dmae.go_c18,  */
	0x01010027, 0x00010001, 0x00000039, 	/* (r1!=0), r1=dmae.go_c19,  */
	0x01010028, 0x00010001, 0x0000003a, 	/* (r1!=0), r1=dmae.go_c20,  */
	0x01010029, 0x00010001, 0x0000003b, 	/* (r1!=0), r1=dmae.go_c21,  */
	0x0101002a, 0x00010001, 0x0000003c, 	/* (r1!=0), r1=dmae.go_c22,  */
	0x0101002b, 0x00010001, 0x0000003d, 	/* (r1!=0), r1=dmae.go_c23,  */
	0x0101002c, 0x00010001, 0x0000003e, 	/* (r1!=0), r1=dmae.go_c24,  */
	0x0101002d, 0x00010001, 0x0000003f, 	/* (r1!=0), r1=dmae.go_c25,  */
	0x0101002e, 0x00010001, 0x00000040, 	/* (r1!=0), r1=dmae.go_c26,  */
	0x0101002f, 0x00010001, 0x00000041, 	/* (r1!=0), r1=dmae.go_c27,  */
	0x01010030, 0x00010001, 0x00000042, 	/* (r1!=0), r1=dmae.go_c28,  */
	0x01010031, 0x00010001, 0x00000043, 	/* (r1!=0), r1=dmae.go_c29,  */
	0x01010032, 0x00010001, 0x00000044, 	/* (r1!=0), r1=dmae.go_c30,  */
	0x01010033, 0x00010001, 0x00000045, 	/* (r1!=0), r1=dmae.go_c31,  */
	0x00000034, 0x00010002, 0x00000046, 	/* ((r1&~r2)!=0), r1=dmae.PRTY_STS_H_0, r2=dmae.PRTY_MASK_H_0,  */
	0x00000035, 0x00010002, 0x00000048, 	/* ((r1&~r2)!=0), r1=dbg.PRTY_STS_H_0, r2=dbg.PRTY_MASK_H_0,  */
	0x01020036, 0x00010001, 0x0000004a, 	/* (r1!=0), r1=grc.trace_fifo_valid_data,  */
	0x00000037, 0x00010002, 0x0000004b, 	/* ((r1&~r2)!=0), r1=grc.INT_STS_0, r2=grc.INT_MASK_0,  */
	0x00000038, 0x00010002, 0x0000004d, 	/* ((r1&~r2)!=0), r1=grc.PRTY_STS_H_0, r2=grc.PRTY_MASK_H_0,  */
	0x00000039, 0x00010002, 0x0000004f, 	/* ((r1&~r2)!=0), r1=dorq.INT_STS, r2=dorq.INT_MASK,  */
	0x0000003a, 0x00010002, 0x00000051, 	/* ((r1&~r2)!=0), r1=dorq.PRTY_STS_H_0, r2=dorq.PRTY_MASK_H_0,  */
	0x0100003b, 0x00010001, 0x00090053, 	/* (r1!=reset1), r1=dorq.xcm_msg_init_crd,  */
	0x0100003c, 0x00010001, 0x00090054, 	/* (r1!=reset1), r1=dorq.tcm_msg_init_crd,  */
	0x0100003d, 0x00010001, 0x00090055, 	/* (r1!=reset1), r1=dorq.ucm_msg_init_crd,  */
	0x0100003e, 0x00010001, 0x000a0056, 	/* (r1!=reset1), r1=dorq.pbf_cmd_init_crd,  */
	0x0100003f, 0x00010001, 0x00000057, 	/* (r1!=0), r1=dorq.pf_usage_cnt,  */
	0x01000040, 0x00010001, 0x00000058, 	/* (r1!=0), r1=dorq.vf_usage_cnt,  */
	0x01000041, 0x00010001, 0x00000059, 	/* (r1!=0), r1=dorq.cfc_ld_req_fifo_fill_lvl,  */
	0x01000042, 0x00010001, 0x0000005a, 	/* (r1!=0), r1=dorq.dorq_fifo_fill_lvl,  */
	0x01020043, 0x00010201, 0x0000005b, 	/* (r1!=0), r1=dorq.db_drop_cnt,  */
	0x01020044, 0x00010201, 0x0000005e, 	/* (r1!=0), r1=dorq.dpm_abort_cnt,  */
	0x01000045, 0x00010001, 0x00000061, 	/* (r1!=0), r1=dorq.dpm_timer_expir_abort_cnt,  */
	0x01000046, 0x00010001, 0x00000062, 	/* (r1!=0), r1=dorq.dpm_tbl_fill_lvl,  */
	0x00000047, 0x00010002, 0x00000063, 	/* ((r1&~r2)!=0), r1=igu.PRTY_STS, r2=igu.PRTY_MASK,  */
	0x00000048, 0x00010002, 0x00000065, 	/* ((r1&~r2)!=0), r1=igu.PRTY_STS_H_0, r2=igu.PRTY_MASK_H_0,  */
	0x01010049, 0x00010001, 0x00000067, 	/* (r1!=0), r1=igu.attn_write_done_pending,  */
	0x0102004a, 0x00010001, 0x00000068, 	/* (r1!=0), r1=igu.Interrupt_status,  */
	0x0100004b, 0x00010001, 0x00000069, 	/* (r1!=0), r1=igu.error_handling_data_valid,  */
	0x0100004c, 0x00010001, 0x0000006a, 	/* (r1!=0), r1=igu.silent_drop,  */
	0x0102004d, 0x00010001, 0x0000006b, 	/* (r1!=0), r1=igu.sb_ctrl_fsm,  */
	0x0102004e, 0x00010001, 0x0000006c, 	/* (r1!=0), r1=igu.int_handle_fsm,  */
	0x0402004f, 0x00020001, 0x000b006d, 	/* ((r1&~0x2)!=0), r1=igu.attn_fsm,  */
	0x04020050, 0x00020001, 0x000d006e, 	/* ((r1&~0x1)!=0), r1=igu.ctrl_fsm,  */
	0x04020051, 0x00020001, 0x000d006f, 	/* ((r1&~0x1)!=0), r1=igu.pxp_arb_fsm,  */
	0x00000052, 0x00010002, 0x00000070, 	/* ((r1&~r2)!=0), r1=cau.PRTY_STS_H_0, r2=cau.PRTY_MASK_H_0,  */
	0x01010053, 0x00010001, 0x000d0072, 	/* (r1!=1), r1=cau.igu_req_credit_status,  */
	0x01010054, 0x00010001, 0x000d0073, 	/* (r1!=1), r1=cau.igu_cmd_credit_status,  */
	0x01010055, 0x00010001, 0x00000074, 	/* (r1!=0), r1=cau.debug_fifo_status,  */
	0x01000056, 0x00010001, 0x00000075, 	/* (r1!=0), r1=cau.error_pxp_req,  */
	0x01000057, 0x00010101, 0x00000076, 	/* (r1!=0), r1=cau.error_fsm_line,  */
	0x01000058, 0x00010001, 0x00000078, 	/* (r1!=0), r1=cau.parity_latch_status,  */
	0x01000059, 0x00010001, 0x00000079, 	/* (r1!=0), r1=cau.error_cleanup_cmd_reg,  */
	0x0301005a, 0x00000002, 0x0000007a, 	/* (r1!=r2), r1=cau.req_counter, r2=cau.wdone_counter,  */
	0x0301005b, 0x00000002, 0x0000007c, 	/* (r1!=r2), r1=cau.req_counter, r2=cau.ack_counter,  */
	0x0102005c, 0x00010001, 0x0000007e, 	/* (r1!=0), r1=cau.main_fsm_status,  */
	0x0102005d, 0x00010001, 0x0000007f, 	/* (r1!=0), r1=cau.var_read_fsm_status,  */
	0x0102005e, 0x00010001, 0x00000080, 	/* (r1!=0), r1=cau.igu_dma_fsm_status,  */
	0x0000005f, 0x00010302, 0x00000081, 	/* ((r1&~r2)!=0), r1=prs.INT_STS_0, r2=prs.INT_MASK_0,  */
	0x00000060, 0x00010002, 0x00000086, 	/* ((r1&~r2)!=0), r1=prs.PRTY_STS, r2=prs.PRTY_MASK,  */
	0x00000061, 0x00010002, 0x00000088, 	/* ((r1&~r2)!=0), r1=prs.PRTY_STS_H_0, r2=prs.PRTY_MASK_H_0,  */
	0x01010062, 0x00010001, 0x0000008a, 	/* (r1!=0), r1=prs.queue_pkt_avail_status,  */
	0x01010063, 0x00010001, 0x0000008b, 	/* (r1!=0), r1=prs.storm_bkprs_status,  */
	0x01010064, 0x00010001, 0x0000008c, 	/* (r1!=0), r1=prs.stop_parsing_status,  */
	0x01010065, 0x00010001, 0x0000008d, 	/* (r1!=0), r1=prs.ccfc_search_current_credit,  */
	0x01010066, 0x00010001, 0x0000008e, 	/* (r1!=0), r1=prs.tcfc_search_current_credit,  */
	0x01010067, 0x00010001, 0x0000008f, 	/* (r1!=0), r1=prs.ccfc_load_current_credit,  */
	0x01010068, 0x00010001, 0x00000090, 	/* (r1!=0), r1=prs.tcfc_load_current_credit,  */
	0x01010069, 0x00010001, 0x00000091, 	/* (r1!=0), r1=prs.ccfc_search_req_ct,  */
	0x0101006a, 0x00010001, 0x00000092, 	/* (r1!=0), r1=prs.tcfc_search_req_ct,  */
	0x0101006b, 0x00010001, 0x00000093, 	/* (r1!=0), r1=prs.ccfc_load_req_ct,  */
	0x0101006c, 0x00010001, 0x00000094, 	/* (r1!=0), r1=prs.tcfc_load_req_ct,  */
	0x0101006d, 0x00010001, 0x00000095, 	/* (r1!=0), r1=prs.sop_req_ct,  */
	0x0101006e, 0x00010001, 0x00000096, 	/* (r1!=0), r1=prs.eop_req_ct,  */
	0x0000006f, 0x00010002, 0x00000097, 	/* ((r1&~r2)!=0), r1=prm.INT_STS, r2=prm.INT_MASK,  */
	0x00000070, 0x00010002, 0x00000099, 	/* ((r1&~r2)!=0), r1=prm.PRTY_STS_H_0, r2=prm.PRTY_MASK_H_0,  */
	0x01000071, 0x00010001, 0x000d009b, 	/* (r1!=1), r1=rss.rss_init_done,  */
	0x00000072, 0x00010002, 0x0000009c, 	/* ((r1&~r2)!=0), r1=rss.INT_STS, r2=rss.INT_MASK,  */
	0x00000073, 0x00010002, 0x0000009e, 	/* ((r1&~r2)!=0), r1=rss.PRTY_STS_H_0, r2=rss.PRTY_MASK_H_0,  */
	0x01000074, 0x00010001, 0x000d00a0, 	/* (r1!=1), r1=pswrq2.rbc_done,  */
	0x01000075, 0x00010001, 0x000d00a1, 	/* (r1!=1), r1=pswrq2.cfg_done,  */
	0x00020076, 0x00010002, 0x000000a2, 	/* ((r1&~r2)!=0), r1=pswrq2.INT_STS, r2=pswrq2.INT_MASK,  */
	0x00000077, 0x00010002, 0x000000a4, 	/* ((r1&~r2)!=0), r1=pswrq2.PRTY_STS_H_0, r2=pswrq2.PRTY_MASK_H_0,  */
	0x01010078, 0x00010001, 0x000000a6, 	/* (r1!=0), r1=pswrq2.vq0_entry_cnt[0:31],  */
	0x01000079, 0x00010001, 0x000000a7, 	/* (r1!=0), r1=pswrq2.l2p_err_add_31_0,  */
	0x0100007a, 0x00010001, 0x000000a8, 	/* (r1!=0), r1=pswrq2.l2p_err_add_63_32,  */
	0x0100007b, 0x00010001, 0x000000a9, 	/* (r1!=0), r1=pswrq2.l2p_err_details,  */
	0x0100007c, 0x00010001, 0x000000aa, 	/* (r1!=0), r1=pswrq2.l2p_err_details2,  */
	0x0301007d, 0x00000002, 0x000000ab, 	/* (r1!=r2), r1=pswrq2.sr_cnt, r2=pswrq2.sr_num_cfg,  */
	0x0801007e, 0x00010002, 0x000f00ad, 	/* (r1<(r2-4)), r1=pswrq2.sr_cnt, r2=pswrq2.sr_num_cfg,  */
	0x0301007f, 0x00000002, 0x000000af, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_0, r2=pswrq2.max_srs_vq0,  */
	0x03010080, 0x00000002, 0x000000b1, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_1, r2=pswrq2.max_srs_vq1,  */
	0x03010081, 0x00000002, 0x000000b3, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_2, r2=pswrq2.max_srs_vq2,  */
	0x03010082, 0x00000002, 0x000000b5, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_3, r2=pswrq2.max_srs_vq3,  */
	0x03010083, 0x00000002, 0x000000b7, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_4, r2=pswrq2.max_srs_vq4,  */
	0x03010084, 0x00000002, 0x000000b9, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_5, r2=pswrq2.max_srs_vq5,  */
	0x03010085, 0x00000002, 0x000000bb, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_6, r2=pswrq2.max_srs_vq6,  */
	0x03010086, 0x00000002, 0x000000bd, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_9, r2=pswrq2.max_srs_vq9,  */
	0x03010087, 0x00000002, 0x000000bf, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_11, r2=pswrq2.max_srs_vq11,  */
	0x03010088, 0x00000002, 0x000000c1, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_12, r2=pswrq2.max_srs_vq12,  */
	0x03010089, 0x00000002, 0x000000c3, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_15, r2=pswrq2.max_srs_vq15,  */
	0x0301008a, 0x00000002, 0x000000c5, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_16, r2=pswrq2.max_srs_vq16,  */
	0x0301008b, 0x00000002, 0x000000c7, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_17, r2=pswrq2.max_srs_vq17,  */
	0x0301008c, 0x00000002, 0x000000c9, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_18, r2=pswrq2.max_srs_vq18,  */
	0x0301008d, 0x00000002, 0x000000cb, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_19, r2=pswrq2.max_srs_vq19,  */
	0x0301008e, 0x00000002, 0x000000cd, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_21, r2=pswrq2.max_srs_vq21,  */
	0x0301008f, 0x00000002, 0x000000cf, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_22, r2=pswrq2.max_srs_vq22,  */
	0x03010090, 0x00000002, 0x000000d1, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_23, r2=pswrq2.max_srs_vq23,  */
	0x03010091, 0x00000002, 0x000000d3, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_24, r2=pswrq2.max_srs_vq24,  */
	0x03010092, 0x00000002, 0x000000d5, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_26, r2=pswrq2.max_srs_vq26,  */
	0x03010093, 0x00000002, 0x000000d7, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_28, r2=pswrq2.max_srs_vq28,  */
	0x03010094, 0x00000002, 0x000000d9, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_29, r2=pswrq2.max_srs_vq29,  */
	0x03010095, 0x00000002, 0x000000db, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_30, r2=pswrq2.max_srs_vq30,  */
	0x03010096, 0x00000002, 0x000000dd, 	/* (r1!=r2), r1=pswrq2.blk_cnt, r2=pswrq2.blk_num_cfg,  */
	0x08010097, 0x00010002, 0x001000df, 	/* (r1<(r2-8)), r1=pswrq2.blk_cnt, r2=pswrq2.blk_num_cfg,  */
	0x03010098, 0x00000002, 0x000000e1, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_0, r2=pswrq2.max_blks_vq0,  */
	0x03010099, 0x00000002, 0x000000e3, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_1, r2=pswrq2.max_blks_vq1,  */
	0x0301009a, 0x00000002, 0x000000e5, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_2, r2=pswrq2.max_blks_vq2,  */
	0x0301009b, 0x00000002, 0x000000e7, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_3, r2=pswrq2.max_blks_vq3,  */
	0x0301009c, 0x00000002, 0x000000e9, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_4, r2=pswrq2.max_blks_vq4,  */
	0x0301009d, 0x00000002, 0x000000eb, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_5, r2=pswrq2.max_blks_vq5,  */
	0x0301009e, 0x00000002, 0x000000ed, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_6, r2=pswrq2.max_blks_vq6,  */
	0x0301009f, 0x00000002, 0x000000ef, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_9, r2=pswrq2.max_blks_vq9,  */
	0x030100a0, 0x00000002, 0x000000f1, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_11, r2=pswrq2.max_blks_vq11,  */
	0x030100a1, 0x00000002, 0x000000f3, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_12, r2=pswrq2.max_blks_vq12,  */
	0x030100a2, 0x00000002, 0x000000f5, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_15, r2=pswrq2.max_blks_vq15,  */
	0x030100a3, 0x00000002, 0x000000f7, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_16, r2=pswrq2.max_blks_vq16,  */
	0x030100a4, 0x00000002, 0x000000f9, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_17, r2=pswrq2.max_blks_vq17,  */
	0x030100a5, 0x00000002, 0x000000fb, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_18, r2=pswrq2.max_blks_vq18,  */
	0x030100a6, 0x00000002, 0x000000fd, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_19, r2=pswrq2.max_blks_vq19,  */
	0x030100a7, 0x00000002, 0x000000ff, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_21, r2=pswrq2.max_blks_vq21,  */
	0x030100a8, 0x00000002, 0x00000101, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_22, r2=pswrq2.max_blks_vq22,  */
	0x030100a9, 0x00000002, 0x00000103, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_23, r2=pswrq2.max_blks_vq23,  */
	0x030100aa, 0x00000002, 0x00000105, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_24, r2=pswrq2.max_blks_vq24,  */
	0x030100ab, 0x00000002, 0x00000107, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_26, r2=pswrq2.max_blks_vq26,  */
	0x030100ac, 0x00000002, 0x00000109, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_28, r2=pswrq2.max_blks_vq28,  */
	0x030100ad, 0x00000002, 0x0000010b, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_29, r2=pswrq2.max_blks_vq29,  */
	0x030100ae, 0x00000002, 0x0000010d, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_30, r2=pswrq2.max_blks_vq30,  */
	0x010000af, 0x00010001, 0x0000010f, 	/* (r1!=0), r1=pswrq2.l2p_close_gate_sts,  */
	0x010000b0, 0x00010001, 0x00000110, 	/* (r1!=0), r1=pswrq2.misc_close_gate_sts,  */
	0x010200b1, 0x00010001, 0x00000111, 	/* (r1!=0), r1=pswrq2.misc_stall_mem_sts,  */
	0x000200b2, 0x00010002, 0x00000112, 	/* ((r1&~r2)!=0), r1=pswrq.INT_STS, r2=pswrq.INT_MASK,  */
	0x000200b3, 0x00010002, 0x00000114, 	/* ((r1&~r2)!=0), r1=pswwr.INT_STS, r2=pswwr.INT_MASK,  */
	0x010000b4, 0x00010001, 0x00000116, 	/* (r1!=0), r1=pswwr2.pglue_eop_err_details,  */
	0x010100b5, 0x00010001, 0x00000117, 	/* (r1!=0), r1=pswwr2.prm_curr_fill_level,  */
	0x010100b6, 0x00010001, 0x00000118, 	/* (r1!=0), r1=pswwr2.cdu_curr_fill_level,  */
	0x000200b7, 0x00010002, 0x00000119, 	/* ((r1&~r2)!=0), r1=pswwr2.INT_STS, r2=pswwr2.INT_MASK,  */
	0x000000b8, 0x00010002, 0x0000011b, 	/* ((r1&~r2)!=0), r1=pswwr2.PRTY_STS_H_0, r2=pswwr2.PRTY_MASK_H_0,  */
	0x000000b9, 0x00010002, 0x0000011d, 	/* ((r1&~r2)!=0), r1=pswwr2.PRTY_STS_H_1, r2=pswwr2.PRTY_MASK_H_1,  */
	0x000000ba, 0x00010002, 0x0000011f, 	/* ((r1&~r2)!=0), r1=pswwr2.PRTY_STS_H_2, r2=pswwr2.PRTY_MASK_H_2,  */
	0x000000bb, 0x00010002, 0x00000121, 	/* ((r1&~r2)!=0), r1=pswwr2.PRTY_STS_H_3, r2=pswwr2.PRTY_MASK_H_3,  */
	0x010000bc, 0x00010001, 0x00000123, 	/* (r1!=0), r1=pswrd.fifo_full_status,  */
	0x000200bd, 0x00010002, 0x00000124, 	/* ((r1&~r2)!=0), r1=pswrd.INT_STS, r2=pswrd.INT_MASK,  */
	0x010000be, 0x00010001, 0x000d0126, 	/* (r1!=1), r1=pswrd2.start_init,  */
	0x010000bf, 0x00010001, 0x000d0127, 	/* (r1!=1), r1=pswrd2.init_done,  */
	0x010200c0, 0x00010001, 0x00000128, 	/* (r1!=0), r1=pswrd2.cpl_err_details,  */
	0x010200c1, 0x00010001, 0x00000129, 	/* (r1!=0), r1=pswrd2.cpl_err_details2,  */
	0x010100c2, 0x00010001, 0x000d012a, 	/* (r1!=1), r1=pswrd2.port_is_idle_0,  */
	0x010100c3, 0x00010001, 0x000d012b, 	/* (r1!=1), r1=pswrd2.port_is_idle_1,  */
	0x010000c4, 0x00010001, 0x0000012c, 	/* (r1!=0), r1=pswrd2.almost_full_0[0:15],  */
	0x000200c5, 0x00010002, 0x0000012d, 	/* ((r1&~r2)!=0), r1=pswrd2.INT_STS, r2=pswrd2.INT_MASK,  */
	0x000000c6, 0x00010002, 0x0000012f, 	/* ((r1&~r2)!=0), r1=pswrd2.PRTY_STS_H_0, r2=pswrd2.PRTY_MASK_H_0,  */
	0x000000c7, 0x00010002, 0x00000131, 	/* ((r1&~r2)!=0), r1=pswrd2.PRTY_STS_H_1, r2=pswrd2.PRTY_MASK_H_1,  */
	0x010000c8, 0x00010001, 0x00000133, 	/* (r1!=0), r1=pswrd2.disable_inputs,  */
	0x010100c9, 0x00010001, 0x00000134, 	/* (r1!=0), r1=pswhst2.header_fifo_status,  */
	0x010100ca, 0x00010001, 0x00000135, 	/* (r1!=0), r1=pswhst2.data_fifo_status,  */
	0x000200cb, 0x00010002, 0x00000136, 	/* ((r1&~r2)!=0), r1=pswhst2.INT_STS, r2=pswhst2.INT_MASK,  */
	0x010200cc, 0x00010001, 0x00000138, 	/* (r1!=0), r1=pswhst.discard_internal_writes_status,  */
	0x010200cd, 0x00010001, 0x00000139, 	/* (r1!=0), r1=pswhst.discard_doorbells_status,  */
	0x010200ce, 0x00010001, 0x0011013a, 	/* (r1!=3), r1=pswhst.arb_is_idle,  */
	0x010200cf, 0x00010301, 0x0000013b, 	/* (r1!=0), r1=pswhst.incorrect_access_valid,  */
	0x010200d0, 0x00010101, 0x0000013f, 	/* (r1!=0), r1=pswhst.per_violation_valid,  */
	0x010200d1, 0x00010001, 0x00120141, 	/* (r1!=reset1), r1=pswhst.source_credits_avail,  */
	0x010200d2, 0x00010101, 0x00000142, 	/* (r1!=0), r1=pswhst.source_credit_viol_valid,  */
	0x010200d3, 0x00010001, 0x00000144, 	/* (r1!=0), r1=pswhst.is_in_drain_mode,  */
	0x010200d4, 0x00010101, 0x00000145, 	/* (r1!=0), r1=pswhst.timeout_valid,  */
	0x010200d5, 0x00010101, 0x00000147, 	/* (r1!=0), r1=pswhst.timeout_valid,  */
	0x010200d6, 0x00010001, 0x00000149, 	/* (r1!=0), r1=pswhst.clients_waiting_to_source_arb[0:7],  */
	0x010200d7, 0x00010001, 0x0000014a, 	/* (r1!=0), r1=pswhst.INT_STS,  */
	0x000000d8, 0x00010002, 0x0000014b, 	/* ((r1&~r2)!=0), r1=pswhst.PRTY_STS_H_0, r2=pswhst.PRTY_MASK_H_0,  */
	0x060200d9, 0x00020001, 0x0013014d, 	/* ((r1&0x76417C)!=0), r1=pglue_b.INT_STS,  */
	0x060000da, 0x00020001, 0x0015014e, 	/* ((r1&0x01BC01)!=0), r1=pglue_b.INT_STS,  */
	0x000000db, 0x00010002, 0x0000014f, 	/* ((r1&~r2)!=0), r1=pglue_b.PRTY_STS_H_0, r2=pglue_b.PRTY_MASK_H_0,  */
	0x010000dc, 0x00010001, 0x00000151, 	/* (r1!=0), r1=pglue_b.pgl_write_blocked,  */
	0x010000dd, 0x00010001, 0x00000152, 	/* (r1!=0), r1=pglue_b.pgl_read_blocked,  */
	0x010100de, 0x00010001, 0x00000153, 	/* (r1!=0), r1=pglue_b.read_fifo_occupancy_level,  */
	0x010200df, 0x00010001, 0x00000154, 	/* (r1!=0), r1=pglue_b.rx_legacy_errors,  */
	0x070100e0, 0x00030001, 0x00170155, 	/* (((r1>>17)&1)!=0), r1=pglue_b.pgl_txw_cdts,  */
	0x010200e1, 0x00010001, 0x00000156, 	/* (r1!=0), r1=pglue_b.cfg_space_a_request,  */
	0x010200e2, 0x00010001, 0x00000157, 	/* (r1!=0), r1=pglue_b.cfg_space_b_request,  */
	0x010200e3, 0x00010001, 0x00000158, 	/* (r1!=0), r1=pglue_b.flr_request_vf_31_0,  */
	0x010200e4, 0x00010001, 0x00000159, 	/* (r1!=0), r1=pglue_b.flr_request_vf_63_32,  */
	0x010200e5, 0x00010001, 0x0000015a, 	/* (r1!=0), r1=pglue_b.flr_request_vf_95_64,  */
	0x010200e6, 0x00010001, 0x0000015b, 	/* (r1!=0), r1=pglue_b.flr_request_vf_127_96,  */
	0x010200e7, 0x00010001, 0x0000015c, 	/* (r1!=0), r1=pglue_b.flr_request_vf_159_128,  */
	0x010200e8, 0x00010001, 0x0000015d, 	/* (r1!=0), r1=pglue_b.flr_request_vf_191_160,  */
	0x010200e9, 0x00010001, 0x0000015e, 	/* (r1!=0), r1=pglue_b.flr_request_pf_31_0,  */
	0x010200ea, 0x00010001, 0x0000015f, 	/* (r1!=0), r1=pglue_b.sr_iov_disabled_request,  */
	0x010200eb, 0x00010001, 0x00000160, 	/* (r1!=0), r1=pglue_b.was_error_vf_31_0,  */
	0x010200ec, 0x00010001, 0x00000161, 	/* (r1!=0), r1=pglue_b.was_error_vf_63_32,  */
	0x010200ed, 0x00010001, 0x00000162, 	/* (r1!=0), r1=pglue_b.was_error_vf_95_64,  */
	0x010200ee, 0x00010001, 0x00000163, 	/* (r1!=0), r1=pglue_b.was_error_vf_127_96,  */
	0x010200ef, 0x00010001, 0x00000164, 	/* (r1!=0), r1=pglue_b.was_error_vf_159_128,  */
	0x010200f0, 0x00010001, 0x00000165, 	/* (r1!=0), r1=pglue_b.was_error_vf_191_160,  */
	0x010200f1, 0x00010001, 0x00000166, 	/* (r1!=0), r1=pglue_b.was_error_pf_31_0,  */
	0x010200f2, 0x00010001, 0x00000167, 	/* (r1!=0), r1=pglue_b.rx_err_details,  */
	0x010200f3, 0x00010001, 0x00000168, 	/* (r1!=0), r1=pglue_b.rx_tcpl_err_details,  */
	0x010200f4, 0x00010001, 0x00000169, 	/* (r1!=0), r1=pglue_b.tx_err_wr_add_31_0,  */
	0x010200f5, 0x00010001, 0x0000016a, 	/* (r1!=0), r1=pglue_b.tx_err_wr_add_63_32,  */
	0x010200f6, 0x00010001, 0x0000016b, 	/* (r1!=0), r1=pglue_b.tx_err_wr_details,  */
	0x010200f7, 0x00010001, 0x0000016c, 	/* (r1!=0), r1=pglue_b.tx_err_wr_details2,  */
	0x010200f8, 0x00010001, 0x0000016d, 	/* (r1!=0), r1=pglue_b.tx_err_rd_add_31_0,  */
	0x010200f9, 0x00010001, 0x0000016e, 	/* (r1!=0), r1=pglue_b.tx_err_rd_add_63_32,  */
	0x010200fa, 0x00010001, 0x0000016f, 	/* (r1!=0), r1=pglue_b.tx_err_rd_details,  */
	0x010200fb, 0x00010001, 0x00000170, 	/* (r1!=0), r1=pglue_b.tx_err_rd_details2,  */
	0x010200fc, 0x00010001, 0x00000171, 	/* (r1!=0), r1=pglue_b.vf_length_violation_details,  */
	0x010200fd, 0x00010001, 0x00000172, 	/* (r1!=0), r1=pglue_b.vf_length_violation_details2,  */
	0x010200fe, 0x00010001, 0x00000173, 	/* (r1!=0), r1=pglue_b.vf_grc_space_violation_details,  */
	0x010200ff, 0x00010001, 0x00000174, 	/* (r1!=0), r1=pglue_b.master_zlr_err_add_31_0,  */
	0x01020100, 0x00010001, 0x00000175, 	/* (r1!=0), r1=pglue_b.master_zlr_err_add_63_32,  */
	0x01020101, 0x00010001, 0x00000176, 	/* (r1!=0), r1=pglue_b.master_zlr_err_details,  */
	0x01020102, 0x00010001, 0x00000177, 	/* (r1!=0), r1=pglue_b.admin_window_violation_details,  */
	0x01000103, 0x00010001, 0x00000178, 	/* (r1!=0), r1=pglue_b.out_of_range_function_in_pretend_details,  */
	0x01000104, 0x00010001, 0x00000179, 	/* (r1!=0), r1=pglue_b.out_of_range_function_in_pretend_address,  */
	0x01010105, 0x00010001, 0x0000017a, 	/* (r1!=0), r1=pglue_b.write_fifo_occupancy_level,  */
	0x01020106, 0x00010001, 0x0000017b, 	/* (r1!=0), r1=pglue_b.illegal_address_add_31_0,  */
	0x01020107, 0x00010001, 0x0000017c, 	/* (r1!=0), r1=pglue_b.illegal_address_add_63_32,  */
	0x01020108, 0x00010001, 0x0000017d, 	/* (r1!=0), r1=pglue_b.illegal_address_details,  */
	0x01020109, 0x00010001, 0x0000017e, 	/* (r1!=0), r1=pglue_b.illegal_address_details2,  */
	0x0102010a, 0x00010001, 0x001a017f, 	/* (r1!=0xffffffff), r1=pglue_b.tags_31_0,  */
	0x0102010b, 0x00010001, 0x001a0180, 	/* (r1!=0xffffffff), r1=pglue_b.tags_63_32,  */
	0x0102010c, 0x00010001, 0x001a0181, 	/* (r1!=0xffffffff), r1=pglue_b.tags_95_64,  */
	0x0102010d, 0x00010001, 0x001a0182, 	/* (r1!=0xffffffff), r1=pglue_b.tags_127_96,  */
	0x0102010e, 0x00010001, 0x00000183, 	/* (r1!=0), r1=pglue_b.vf_ilt_err_add_31_0,  */
	0x0102010f, 0x00010001, 0x00000184, 	/* (r1!=0), r1=pglue_b.vf_ilt_err_add_63_32,  */
	0x01020110, 0x00010001, 0x00000185, 	/* (r1!=0), r1=pglue_b.vf_ilt_err_details,  */
	0x01020111, 0x00010001, 0x00000186, 	/* (r1!=0), r1=pglue_b.vf_ilt_err_details2,  */
	0x0d020112, 0x00010001, 0x001b0187, 	/* (r1&0x80000000), r1=tm.INT_STS_0,  */
	0x0d000113, 0x00010001, 0x001c0188, 	/* (r1&0x7FFFFF), r1=tm.INT_STS_0,  */
	0x0d000114, 0x00010001, 0x001d0189, 	/* (r1&0x7F800000), r1=tm.INT_STS_0,  */
	0x0d000115, 0x00010001, 0x001e018a, 	/* (r1&0x41E), r1=tm.INT_STS_1,  */
	0x00000116, 0x00010002, 0x0000018b, 	/* ((r1&~r2)!=0), r1=tm.PRTY_STS_H_0, r2=tm.PRTY_MASK_H_0,  */
	0x01010117, 0x00010001, 0x0000018d, 	/* (r1!=0), r1=tm.pxp_read_data_fifo_status,  */
	0x01010118, 0x00010001, 0x0000018e, 	/* (r1!=0), r1=tm.pxp_read_ctrl_fifo_status,  */
	0x01010119, 0x00010001, 0x0000018f, 	/* (r1!=0), r1=tm.cfc_load_echo_fifo_status,  */
	0x0101011a, 0x00010001, 0x00000190, 	/* (r1!=0), r1=tm.client_out_fifo_status,  */
	0x0101011b, 0x00010001, 0x00000191, 	/* (r1!=0), r1=tm.client_in_pbf_fifo_status,  */
	0x0101011c, 0x00010001, 0x00000192, 	/* (r1!=0), r1=tm.client_in_xcm_fifo_status,  */
	0x0101011d, 0x00010001, 0x00000193, 	/* (r1!=0), r1=tm.client_in_tcm_fifo_status,  */
	0x0101011e, 0x00010001, 0x00000194, 	/* (r1!=0), r1=tm.client_in_ucm_fifo_status,  */
	0x0101011f, 0x00010001, 0x00000195, 	/* (r1!=0), r1=tm.expiration_cmd_fifo_status,  */
	0x01010120, 0x00010001, 0x00000196, 	/* (r1!=0), r1=tm.ac_command_fifo_status,  */
	0x01000121, 0x00010001, 0x000d0197, 	/* (r1!=1), r1=tcfc.ll_init_done,  */
	0x01000122, 0x00010001, 0x000d0198, 	/* (r1!=1), r1=tcfc.ac_init_done,  */
	0x01000123, 0x00010001, 0x000d0199, 	/* (r1!=1), r1=tcfc.cam_init_done,  */
	0x01000124, 0x00010001, 0x000d019a, 	/* (r1!=1), r1=tcfc.tidram_init_done,  */
	0x00000125, 0x00010502, 0x0000019b, 	/* ((r1&~r2)!=0), r1=tcfc.INT_STS_0, r2=tcfc.INT_MASK_0,  */
	0x00000126, 0x00010002, 0x000001a2, 	/* ((r1&~r2)!=0), r1=tcfc.PRTY_STS_H_0, r2=tcfc.PRTY_MASK_H_0,  */
	0x01010127, 0x00010001, 0x000001a4, 	/* (r1!=0x0), r1=tcfc.lstate_arriving,  */
	0x01010128, 0x00010001, 0x000001a5, 	/* (r1!=0x0), r1=tcfc.lstate_leaving,  */
	0x01000129, 0x00010001, 0x000d01a6, 	/* (r1!=1), r1=ccfc.ll_init_done,  */
	0x0100012a, 0x00010001, 0x000d01a7, 	/* (r1!=1), r1=ccfc.ac_init_done,  */
	0x0100012b, 0x00010001, 0x000d01a8, 	/* (r1!=1), r1=ccfc.cam_init_done,  */
	0x0100012c, 0x00010001, 0x000d01a9, 	/* (r1!=1), r1=ccfc.tidram_init_done,  */
	0x0000012d, 0x00010502, 0x000001aa, 	/* ((r1&~r2)!=0), r1=ccfc.INT_STS_0, r2=ccfc.INT_MASK_0,  */
	0x0000012e, 0x00010002, 0x000001b1, 	/* ((r1&~r2)!=0), r1=ccfc.PRTY_STS_H_0, r2=ccfc.PRTY_MASK_H_0,  */
	0x0000012f, 0x00010002, 0x000001b3, 	/* ((r1&~r2)!=0), r1=ccfc.PRTY_STS, r2=ccfc.PRTY_MASK,  */
	0x01010130, 0x00010001, 0x000001b5, 	/* (r1!=0x0), r1=ccfc.lstate_arriving,  */
	0x01010131, 0x00010001, 0x000001b6, 	/* (r1!=0x0), r1=ccfc.lstate_leaving,  */
	0x00000132, 0x00010002, 0x000001b7, 	/* ((r1&~r2)!=0), r1=qm.INT_STS, r2=qm.INT_MASK,  */
	0x00000133, 0x00010002, 0x000001b9, 	/* ((r1&~r2)!=0), r1=qm.PRTY_STS, r2=qm.PRTY_MASK,  */
	0x00000134, 0x00010002, 0x000001bb, 	/* ((r1&~r2)!=0), r1=qm.PRTY_STS_H_0, r2=qm.PRTY_MASK_H_0,  */
	0x00000135, 0x00010002, 0x000001bd, 	/* ((r1&~r2)!=0), r1=qm.PRTY_STS_H_1, r2=qm.PRTY_MASK_H_1,  */
	0x00000136, 0x00010002, 0x000001bf, 	/* ((r1&~r2)!=0), r1=qm.PRTY_STS_H_2, r2=qm.PRTY_MASK_H_2,  */
	0x01000137, 0x00010001, 0x000001c1, 	/* (r1!=0), r1=qm.wrc_fifolvl_0[0:5],  */
	0x03000138, 0x00000002, 0x000001c2, 	/* (r1!=r2), r1=qm.OutLdReqCrdConnTx, r2=qm.OutLdReqSizeConnTx,  */
	0x03000139, 0x00000002, 0x000001c4, 	/* (r1!=r2), r1=qm.OutLdReqCrdConnOther, r2=qm.OutLdReqSizeConnOther,  */
	0x0100013a, 0x00010001, 0x000001c6, 	/* (r1!=0), r1=qm.OvfQNumTx,  */
	0x0100013b, 0x00010101, 0x000001c7, 	/* (r1!=0), r1=qm.OvfErrorTx,  */
	0x0100013c, 0x00010001, 0x000001c9, 	/* (r1!=0), r1=qm.OvfQNumOther,  */
	0x0100013d, 0x00010101, 0x000001ca, 	/* (r1!=0), r1=qm.OvfErrorOther,  */
	0x0301013e, 0x00000002, 0x000001cc, 	/* (r1!=r2), r1=qm.CmCrd_0, r2=qm.CmInitCrd_0,  */
	0x0301013f, 0x00000002, 0x000001ce, 	/* (r1!=r2), r1=qm.CmCrd_1, r2=qm.CmInitCrd_1,  */
	0x03010140, 0x00000002, 0x000001d0, 	/* (r1!=r2), r1=qm.CmCrd_2, r2=qm.CmInitCrd_2,  */
	0x03010141, 0x00000002, 0x000001d2, 	/* (r1!=r2), r1=qm.CmCrd_3, r2=qm.CmInitCrd_3,  */
	0x03010142, 0x00000002, 0x000001d4, 	/* (r1!=r2), r1=qm.CmCrd_4, r2=qm.CmInitCrd_4,  */
	0x03010143, 0x00000002, 0x000001d6, 	/* (r1!=r2), r1=qm.CmCrd_5, r2=qm.CmInitCrd_5,  */
	0x03010144, 0x00000002, 0x000001d8, 	/* (r1!=r2), r1=qm.CmCrd_6, r2=qm.CmInitCrd_6,  */
	0x03010145, 0x00000002, 0x000001da, 	/* (r1!=r2), r1=qm.CmCrd_7, r2=qm.CmInitCrd_7,  */
	0x03010146, 0x00000002, 0x000001dc, 	/* (r1!=r2), r1=qm.CmCrd_8, r2=qm.CmInitCrd_8,  */
	0x03010147, 0x00000002, 0x000001de, 	/* (r1!=r2), r1=qm.CmCrd_9, r2=qm.CmInitCrd_9,  */
	0x00000148, 0x00010002, 0x000001e0, 	/* ((r1&~r2)!=0), r1=rdif.INT_STS, r2=rdif.INT_MASK,  */
	0x00000149, 0x00010002, 0x000001e2, 	/* ((r1&~r2)!=0), r1=tdif.INT_STS, r2=tdif.INT_MASK,  */
	0x0000014a, 0x00010002, 0x000001e4, 	/* ((r1&~r2)!=0), r1=tdif.PRTY_STS_H_0, r2=tdif.PRTY_MASK_H_0,  */
	0x0000014b, 0x00010202, 0x000001e6, 	/* ((r1&~r2)!=0), r1=brb.INT_STS_0, r2=brb.INT_MASK_0,  */
	0x0000014c, 0x00010002, 0x000001ea, 	/* ((r1&~r2)!=0), r1=brb.INT_STS_1, r2=brb.INT_MASK_1,  */
	0x0000014d, 0x00010002, 0x000001ec, 	/* ((r1&~r2)!=0), r1=brb.INT_STS_2, r2=brb.INT_MASK_2,  */
	0x0000014e, 0x00010002, 0x000001ee, 	/* ((r1&~r2)!=0), r1=brb.INT_STS_3, r2=brb.INT_MASK_3,  */
	0x0000014f, 0x00010202, 0x000001f0, 	/* ((r1&~r2)!=0), r1=brb.INT_STS_4, r2=brb.INT_MASK_4,  */
	0x00000150, 0x00010002, 0x000001f4, 	/* ((r1&~r2)!=0), r1=brb.PRTY_STS_H_0, r2=brb.PRTY_MASK_H_0,  */
	0x00000151, 0x00010002, 0x000001f6, 	/* ((r1&~r2)!=0), r1=brb.PRTY_STS_H_1, r2=brb.PRTY_MASK_H_1,  */
	0x01010152, 0x00010001, 0x000001f8, 	/* (r1!=0), r1=brb.wc_bandwidth_if_full,  */
	0x01010153, 0x00010001, 0x000001f9, 	/* (r1!=0), r1=brb.rc_pkt_if_full,  */
	0x01010154, 0x00010001, 0x001f01fa, 	/* (r1!=255), r1=brb.rc_pkt_empty_0[0:4],  */
	0x01010155, 0x00010001, 0x000301fb, 	/* (r1!=15), r1=brb.rc_sop_empty,  */
	0x01010156, 0x00010001, 0x000001fc, 	/* (r1!=0), r1=brb.stop_packet_counter,  */
	0x01010157, 0x00010001, 0x000001fd, 	/* (r1!=0), r1=brb.stop_byte_counter,  */
	0x01010158, 0x00010001, 0x000001fe, 	/* (r1!=0), r1=brb.rc_pkt_state,  */
	0x01010159, 0x00010001, 0x000001ff, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_0,  */
	0x0101015a, 0x00010001, 0x00000200, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_1,  */
	0x0101015b, 0x00010001, 0x00000201, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_2,  */
	0x0101015c, 0x00010001, 0x00000202, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_3,  */
	0x0101015d, 0x00010001, 0x00000203, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_4,  */
	0x0101015e, 0x00010001, 0x00000204, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_5,  */
	0x0101015f, 0x00010001, 0x00000205, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_6,  */
	0x01010160, 0x00010001, 0x00000206, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_7,  */
	0x01010161, 0x00010001, 0x00000207, 	/* (r1!=0), r1=brb.mac0_tc_occupancy_8,  */
	0x01010162, 0x00010001, 0x00000208, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_0,  */
	0x01010163, 0x00010001, 0x00000209, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_1,  */
	0x01010164, 0x00010001, 0x0000020a, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_2,  */
	0x01010165, 0x00010001, 0x0000020b, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_3,  */
	0x01010166, 0x00010001, 0x0000020c, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_4,  */
	0x01010167, 0x00010001, 0x0000020d, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_5,  */
	0x01010168, 0x00010001, 0x0000020e, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_6,  */
	0x01010169, 0x00010001, 0x0000020f, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_7,  */
	0x0101016a, 0x00010001, 0x00000210, 	/* (r1!=0), r1=brb.mac1_tc_occupancy_8,  */
	0x0101016b, 0x00010001, 0x00000211, 	/* (r1!=0), r1=xyld.pending_msg_to_ext_ev_1_ctr,  */
	0x0101016c, 0x00010001, 0x00000212, 	/* (r1!=0), r1=xyld.pending_msg_to_ext_ev_2_ctr,  */
	0x0101016d, 0x00010001, 0x00000213, 	/* (r1!=0), r1=xyld.pending_msg_to_ext_ev_3_ctr,  */
	0x0101016e, 0x00010001, 0x00000214, 	/* (r1!=0), r1=xyld.pending_msg_to_ext_ev_4_ctr,  */
	0x0101016f, 0x00010001, 0x00000215, 	/* (r1!=0), r1=xyld.pending_msg_to_ext_ev_5_ctr,  */
	0x03010170, 0x00000002, 0x00000216, 	/* (r1!=r2), r1=xyld.foc_remain_credits, r2=xyld.foci_foc_credits,  */
	0x01010171, 0x00010001, 0x00000218, 	/* (r1!=0), r1=xyld.pci_pending_msg_ctr,  */
	0x01010172, 0x00010001, 0x00000219, 	/* (r1!=0), r1=xyld.dbg_pending_ccfc_req,  */
	0x01010173, 0x00010001, 0x0000021a, 	/* (r1!=0), r1=xyld.dbg_pending_tcfc_req,  */
	0x00000174, 0x00010002, 0x0000021b, 	/* ((r1&~r2)!=0), r1=xyld.PRTY_STS_H_0, r2=xyld.PRTY_MASK_H_0,  */
	0x01010175, 0x00010001, 0x0000021d, 	/* (r1!=0), r1=tmld.pending_msg_to_ext_ev_1_ctr,  */
	0x01010176, 0x00010001, 0x0000021e, 	/* (r1!=0), r1=tmld.pending_msg_to_ext_ev_2_ctr,  */
	0x01010177, 0x00010001, 0x0000021f, 	/* (r1!=0), r1=tmld.pending_msg_to_ext_ev_3_ctr,  */
	0x01010178, 0x00010001, 0x00000220, 	/* (r1!=0), r1=tmld.pending_msg_to_ext_ev_4_ctr,  */
	0x01010179, 0x00010001, 0x00000221, 	/* (r1!=0), r1=tmld.pending_msg_to_ext_ev_5_ctr,  */
	0x0301017a, 0x00000002, 0x00000222, 	/* (r1!=r2), r1=tmld.foc_remain_credits, r2=tmld.foci_foc_credits,  */
	0x0101017b, 0x00010001, 0x00000224, 	/* (r1!=0), r1=tmld.dbg_pending_ccfc_req,  */
	0x0101017c, 0x00010001, 0x00000225, 	/* (r1!=0), r1=tmld.dbg_pending_tcfc_req,  */
	0x0000017d, 0x00010002, 0x00000226, 	/* ((r1&~r2)!=0), r1=tmld.PRTY_STS_H_0, r2=tmld.PRTY_MASK_H_0,  */
	0x0101017e, 0x00010001, 0x00000228, 	/* (r1!=0), r1=muld.pending_msg_to_ext_ev_1_ctr,  */
	0x0101017f, 0x00010001, 0x00000229, 	/* (r1!=0), r1=muld.pending_msg_to_ext_ev_2_ctr,  */
	0x01010180, 0x00010001, 0x0000022a, 	/* (r1!=0), r1=muld.pending_msg_to_ext_ev_3_ctr,  */
	0x01010181, 0x00010001, 0x0000022b, 	/* (r1!=0), r1=muld.pending_msg_to_ext_ev_4_ctr,  */
	0x01010182, 0x00010001, 0x0000022c, 	/* (r1!=0), r1=muld.pending_msg_to_ext_ev_5_ctr,  */
	0x03010183, 0x00000002, 0x0000022d, 	/* (r1!=r2), r1=muld.foc_remain_credits, r2=muld.foci_foc_credits,  */
	0x01010184, 0x00010001, 0x0000022f, 	/* (r1!=0), r1=muld.bd_pending_msg_ctr,  */
	0x01010185, 0x00010001, 0x00000230, 	/* (r1!=0), r1=muld.sge_pending_msg_ctr,  */
	0x01010186, 0x00010001, 0x00000231, 	/* (r1!=0), r1=muld.pci_pending_msg_ctr,  */
	0x01010187, 0x00010001, 0x00000232, 	/* (r1!=0), r1=muld.dbg_pending_ccfc_req,  */
	0x01010188, 0x00010001, 0x00000233, 	/* (r1!=0), r1=muld.dbg_pending_tcfc_req,  */
	0x00000189, 0x00010002, 0x00000234, 	/* ((r1&~r2)!=0), r1=muld.PRTY_STS_H_0, r2=muld.PRTY_MASK_H_0,  */
	0x0000018a, 0x00010002, 0x00000236, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_0, r2=nig.INT_MASK_0,  */
	0x0000018b, 0x00010002, 0x00000238, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_1, r2=nig.INT_MASK_1,  */
	0x0000018c, 0x00010002, 0x0000023a, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_2, r2=nig.INT_MASK_2,  */
	0x0002018d, 0x00010202, 0x0000023c, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_3, r2=nig.INT_MASK_3,  */
	0x0000018e, 0x00010002, 0x00000240, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_4, r2=nig.INT_MASK_4,  */
	0x0002018f, 0x00010202, 0x00000242, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_5, r2=nig.INT_MASK_5,  */
	0x00000190, 0x00010002, 0x00000246, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS_H_0, r2=nig.PRTY_MASK_H_0,  */
	0x00000191, 0x00010002, 0x00000248, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS_H_1, r2=nig.PRTY_MASK_H_1,  */
	0x00000192, 0x00010002, 0x0000024a, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS_H_2, r2=nig.PRTY_MASK_H_2,  */
	0x00000193, 0x00010002, 0x0000024c, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS_H_3, r2=nig.PRTY_MASK_H_3,  */
	0x01010194, 0x00010001, 0x0020024e, 	/* (r1!=0x000fffff), r1=nig.lb_sopq_empty,  */
	0x01010195, 0x00010001, 0x0021024f, 	/* (r1!=0x0000ffff), r1=nig.tx_sopq_empty,  */
	0x01010196, 0x00010001, 0x000d0250, 	/* (r1!=1), r1=nig.rx_llh_rfifo_empty,  */
	0x01010197, 0x00010001, 0x000d0251, 	/* (r1!=1), r1=nig.lb_btb_fifo_empty,  */
	0x01010198, 0x00010001, 0x000d0252, 	/* (r1!=1), r1=nig.lb_llh_rfifo_empty,  */
	0x05000199, 0x00040002, 0x00220253, 	/* (((r1&0xff)!=0)&&((r2&0x7)!=0)), r1=nig.rx_ptp_ts_msb_err, r2=nig.rx_ptp_en,  */
	0x0101019a, 0x00010001, 0x000d0255, 	/* (r1!=1), r1=nig.tx_btb_fifo_empty,  */
	0x0101019b, 0x00010001, 0x000d0256, 	/* (r1!=1), r1=nig.debug_fifo_empty,  */
	0x0100019c, 0x00010001, 0x00000257, 	/* (r1!=0), r1=ptu.pxp_err_ctr,  */
	0x0100019d, 0x00010001, 0x00000258, 	/* (r1!=0), r1=ptu.inv_err_ctr,  */
	0x0101019e, 0x00010001, 0x00000259, 	/* (r1!=0), r1=ptu.pbf_fill_level,  */
	0x0101019f, 0x00010001, 0x0000025a, 	/* (r1!=0), r1=ptu.prm_fill_level,  */
	0x000001a0, 0x00010002, 0x0000025b, 	/* ((r1&~r2)!=0), r1=ptu.INT_STS, r2=ptu.INT_MASK,  */
	0x000001a1, 0x00010002, 0x0000025d, 	/* ((r1&~r2)!=0), r1=ptu.PRTY_STS_H_0, r2=ptu.PRTY_MASK_H_0,  */
	0x000001a2, 0x00010602, 0x0000025f, 	/* ((r1&~r2)!=0), r1=cdu.INT_STS, r2=cdu.INT_MASK,  */
	0x000001a3, 0x00010002, 0x00000267, 	/* ((r1&~r2)!=0), r1=cdu.PRTY_STS_H_0, r2=cdu.PRTY_MASK_H_0,  */
	0x000001a4, 0x00010002, 0x00000269, 	/* ((r1&~r2)!=0), r1=pbf.PRTY_STS_H_0, r2=pbf.PRTY_MASK_H_0,  */
	0x000001a5, 0x00010002, 0x0000026b, 	/* ((r1&~r2)!=0), r1=pbf.PRTY_STS_H_1, r2=pbf.PRTY_MASK_H_1,  */
	0x010201a6, 0x00010001, 0x0000026d, 	/* (r1!=0), r1=pbf.num_pkts_received_with_error,  */
	0x010201a7, 0x00010001, 0x0000026e, 	/* (r1!=0), r1=pbf.num_pkts_sent_with_error_to_btb,  */
	0x010201a8, 0x00010001, 0x0000026f, 	/* (r1!=0), r1=pbf.num_pkts_sent_with_drop_to_btb,  */
	0x010101a9, 0x00010001, 0x00000270, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq0,  */
	0x010101aa, 0x00010001, 0x00000271, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq0,  */
	0x010101ab, 0x00010001, 0x00000272, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq0,  */
	0x010101ac, 0x00010001, 0x00000273, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq1,  */
	0x010101ad, 0x00010001, 0x00000274, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq1,  */
	0x010101ae, 0x00010001, 0x00000275, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq1,  */
	0x010101af, 0x00010001, 0x00000276, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq2,  */
	0x010101b0, 0x00010001, 0x00000277, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq2,  */
	0x010101b1, 0x00010001, 0x00000278, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq2,  */
	0x010101b2, 0x00010001, 0x00000279, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq3,  */
	0x010101b3, 0x00010001, 0x0000027a, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq3,  */
	0x010101b4, 0x00010001, 0x0000027b, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq3,  */
	0x010101b5, 0x00010001, 0x0000027c, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq4,  */
	0x010101b6, 0x00010001, 0x0000027d, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq4,  */
	0x010101b7, 0x00010001, 0x0000027e, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq4,  */
	0x010101b8, 0x00010001, 0x0000027f, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq5,  */
	0x010101b9, 0x00010001, 0x00000280, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq5,  */
	0x010101ba, 0x00010001, 0x00000281, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq5,  */
	0x010101bb, 0x00010001, 0x00000282, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq6,  */
	0x010101bc, 0x00010001, 0x00000283, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq6,  */
	0x010101bd, 0x00010001, 0x00000284, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq6,  */
	0x010101be, 0x00010001, 0x00000285, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq7,  */
	0x010101bf, 0x00010001, 0x00000286, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq7,  */
	0x010101c0, 0x00010001, 0x00000287, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq7,  */
	0x010101c1, 0x00010001, 0x00000288, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq8,  */
	0x010101c2, 0x00010001, 0x00000289, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq8,  */
	0x010101c3, 0x00010001, 0x0000028a, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq8,  */
	0x010101c4, 0x00010001, 0x0000028b, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq9,  */
	0x010101c5, 0x00010001, 0x0000028c, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq9,  */
	0x010101c6, 0x00010001, 0x0000028d, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq9,  */
	0x010101c7, 0x00010001, 0x0000028e, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq10,  */
	0x010101c8, 0x00010001, 0x0000028f, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq10,  */
	0x010101c9, 0x00010001, 0x00000290, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq10,  */
	0x010101ca, 0x00010001, 0x00000291, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq11,  */
	0x010101cb, 0x00010001, 0x00000292, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq11,  */
	0x010101cc, 0x00010001, 0x00000293, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq11,  */
	0x010101cd, 0x00010001, 0x00000294, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq12,  */
	0x010101ce, 0x00010001, 0x00000295, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq12,  */
	0x010101cf, 0x00010001, 0x00000296, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq12,  */
	0x010101d0, 0x00010001, 0x00000297, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq13,  */
	0x010101d1, 0x00010001, 0x00000298, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq13,  */
	0x010101d2, 0x00010001, 0x00000299, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq13,  */
	0x010101d3, 0x00010001, 0x0000029a, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq14,  */
	0x010101d4, 0x00010001, 0x0000029b, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq14,  */
	0x010101d5, 0x00010001, 0x0000029c, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq14,  */
	0x010101d6, 0x00010001, 0x0000029d, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq15,  */
	0x010101d7, 0x00010001, 0x0000029e, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq15,  */
	0x010101d8, 0x00010001, 0x0000029f, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq15,  */
	0x010101d9, 0x00010001, 0x000002a0, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq16,  */
	0x010101da, 0x00010001, 0x000002a1, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq16,  */
	0x010101db, 0x00010001, 0x000002a2, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq16,  */
	0x010101dc, 0x00010001, 0x000002a3, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq17,  */
	0x010101dd, 0x00010001, 0x000002a4, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq17,  */
	0x010101de, 0x00010001, 0x000002a5, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq17,  */
	0x010101df, 0x00010001, 0x000002a6, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq18,  */
	0x010101e0, 0x00010001, 0x000002a7, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq18,  */
	0x010101e1, 0x00010001, 0x000002a8, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq18,  */
	0x010101e2, 0x00010001, 0x000002a9, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq19,  */
	0x010101e3, 0x00010001, 0x000002aa, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq19,  */
	0x010101e4, 0x00010001, 0x000002ab, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq19,  */
	0x000001e5, 0x00010002, 0x000002ac, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_1, r2=btb.INT_MASK_1,  */
	0x000001e6, 0x00010002, 0x000002ae, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_2, r2=btb.INT_MASK_2,  */
	0x000001e7, 0x00010002, 0x000002b0, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_3, r2=btb.INT_MASK_3,  */
	0x000001e8, 0x00010002, 0x000002b2, 	/* ((r1&~r2)!=0), r1=btb.PRTY_STS_H_0, r2=btb.PRTY_MASK_H_0,  */
	0x010101e9, 0x00010001, 0x000302b4, 	/* (r1!=15), r1=btb.wc_dup_empty,  */
	0x010101ea, 0x00010001, 0x000002b5, 	/* (r1!=0), r1=btb.wc_dup_status,  */
	0x010101eb, 0x00010001, 0x002602b6, 	/* (r1!=8190), r1=btb.wc_empty_0,  */
	0x010201ec, 0x00010001, 0x000002b7, 	/* (r1!=0), r1=btb.wc_bandwidth_if_full,  */
	0x010201ed, 0x00010001, 0x000002b8, 	/* (r1!=0), r1=btb.rc_pkt_if_full,  */
	0x010101ee, 0x00010001, 0x001f02b9, 	/* (r1!=255), r1=btb.rc_pkt_empty_0,  */
	0x010101ef, 0x00010001, 0x001f02ba, 	/* (r1!=255), r1=btb.rc_pkt_empty_1,  */
	0x010101f0, 0x00010001, 0x001f02bb, 	/* (r1!=255), r1=btb.rc_pkt_empty_2,  */
	0x010101f1, 0x00010001, 0x001f02bc, 	/* (r1!=255), r1=btb.rc_pkt_empty_3,  */
	0x010101f2, 0x00010001, 0x000302bd, 	/* (r1!=15), r1=btb.rc_sop_empty,  */
	0x020101f3, 0x00010001, 0x002702be, 	/* (r1>74), r1=btb.block_occupancy,  */
	0x010101f4, 0x00010001, 0x000002bf, 	/* (r1!=0), r1=btb.rc_pkt_state,  */
	0x010101f5, 0x00010001, 0x000b02c0, 	/* (r1!=2), r1=btb.wc_status_0 width=3 access=WB,  */
	0x000001f6, 0x00010102, 0x000002c1, 	/* ((r1&~r2)!=0), r1=xsdm.INT_STS, r2=xsdm.INT_MASK,  */
	0x000001f7, 0x00010002, 0x000002c4, 	/* ((r1&~r2)!=0), r1=xsdm.PRTY_STS_H_0, r2=xsdm.PRTY_MASK_H_0,  */
	0x010101f8, 0x00010001, 0x000002c6, 	/* (r1!=0), r1=xsdm.qm_full,  */
	0x010101f9, 0x00010001, 0x000002c7, 	/* (r1!=0), r1=xsdm.rsp_brb_if_full,  */
	0x010101fa, 0x00010001, 0x000002c8, 	/* (r1!=0), r1=xsdm.rsp_pxp_if_full,  */
	0x010101fb, 0x00010001, 0x000002c9, 	/* (r1!=0), r1=xsdm.dst_pxp_if_full,  */
	0x010101fc, 0x00010001, 0x000002ca, 	/* (r1!=0), r1=xsdm.dst_int_ram_if_full,  */
	0x010101fd, 0x00010001, 0x000002cb, 	/* (r1!=0), r1=xsdm.dst_pas_buf_if_full,  */
	0x010101fe, 0x00010001, 0x000d02cc, 	/* (r1!=1), r1=xsdm.int_cmpl_pend_empty,  */
	0x010101ff, 0x00010001, 0x000d02cd, 	/* (r1!=1), r1=xsdm.int_cprm_pend_empty,  */
	0x01010200, 0x00010001, 0x002802ce, 	/* (r1!=511), r1=xsdm.queue_empty,  */
	0x01010201, 0x00010001, 0x000d02cf, 	/* (r1!=1), r1=xsdm.delay_fifo_empty,  */
	0x01010202, 0x00010001, 0x000d02d0, 	/* (r1!=1), r1=xsdm.rsp_pxp_rdata_empty,  */
	0x01010203, 0x00010001, 0x000d02d1, 	/* (r1!=1), r1=xsdm.rsp_brb_rdata_empty,  */
	0x01010204, 0x00010001, 0x000d02d2, 	/* (r1!=1), r1=xsdm.rsp_int_ram_rdata_empty,  */
	0x01010205, 0x00010001, 0x000d02d3, 	/* (r1!=1), r1=xsdm.rsp_brb_pend_empty,  */
	0x01010206, 0x00010001, 0x000d02d4, 	/* (r1!=1), r1=xsdm.rsp_int_ram_pend_empty,  */
	0x01010207, 0x00010001, 0x000d02d5, 	/* (r1!=1), r1=xsdm.dst_pxp_immed_empty,  */
	0x01010208, 0x00010001, 0x000d02d6, 	/* (r1!=1), r1=xsdm.dst_pxp_dst_pend_empty,  */
	0x01010209, 0x00010001, 0x000d02d7, 	/* (r1!=1), r1=xsdm.dst_pxp_src_pend_empty,  */
	0x0101020a, 0x00010001, 0x000d02d8, 	/* (r1!=1), r1=xsdm.dst_brb_src_pend_empty,  */
	0x0101020b, 0x00010001, 0x000d02d9, 	/* (r1!=1), r1=xsdm.dst_brb_src_addr_empty,  */
	0x0101020c, 0x00010001, 0x000d02da, 	/* (r1!=1), r1=xsdm.dst_pxp_link_empty,  */
	0x0101020d, 0x00010001, 0x000d02db, 	/* (r1!=1), r1=xsdm.dst_int_ram_wait_empty,  */
	0x0101020e, 0x00010001, 0x000d02dc, 	/* (r1!=1), r1=xsdm.dst_pas_buf_wait_empty,  */
	0x0101020f, 0x00010001, 0x000d02dd, 	/* (r1!=1), r1=xsdm.sh_delay_empty,  */
	0x01010210, 0x00010001, 0x000d02de, 	/* (r1!=1), r1=xsdm.cm_delay_empty,  */
	0x01010211, 0x00010001, 0x000d02df, 	/* (r1!=1), r1=xsdm.cmsg_que_empty,  */
	0x01010212, 0x00010001, 0x000d02e0, 	/* (r1!=1), r1=xsdm.ccfc_load_pend_empty,  */
	0x01010213, 0x00010001, 0x000d02e1, 	/* (r1!=1), r1=xsdm.tcfc_load_pend_empty,  */
	0x01010214, 0x00010001, 0x000d02e2, 	/* (r1!=1), r1=xsdm.async_host_empty,  */
	0x01010215, 0x00010001, 0x000d02e3, 	/* (r1!=1), r1=xsdm.prm_fifo_empty,  */
	0x00000216, 0x00010102, 0x000002e4, 	/* ((r1&~r2)!=0), r1=ysdm.INT_STS, r2=ysdm.INT_MASK,  */
	0x00000217, 0x00010002, 0x000002e7, 	/* ((r1&~r2)!=0), r1=ysdm.PRTY_STS_H_0, r2=ysdm.PRTY_MASK_H_0,  */
	0x01010218, 0x00010001, 0x000002e9, 	/* (r1!=0), r1=ysdm.qm_full,  */
	0x01010219, 0x00010001, 0x000002ea, 	/* (r1!=0), r1=ysdm.rsp_brb_if_full,  */
	0x0101021a, 0x00010001, 0x000002eb, 	/* (r1!=0), r1=ysdm.rsp_pxp_if_full,  */
	0x0101021b, 0x00010001, 0x000002ec, 	/* (r1!=0), r1=ysdm.dst_pxp_if_full,  */
	0x0101021c, 0x00010001, 0x000002ed, 	/* (r1!=0), r1=ysdm.dst_int_ram_if_full,  */
	0x0101021d, 0x00010001, 0x000002ee, 	/* (r1!=0), r1=ysdm.dst_pas_buf_if_full,  */
	0x0101021e, 0x00010001, 0x000d02ef, 	/* (r1!=1), r1=ysdm.int_cmpl_pend_empty,  */
	0x0101021f, 0x00010001, 0x000d02f0, 	/* (r1!=1), r1=ysdm.int_cprm_pend_empty,  */
	0x01010220, 0x00010001, 0x002802f1, 	/* (r1!=511), r1=ysdm.queue_empty,  */
	0x01010221, 0x00010001, 0x000d02f2, 	/* (r1!=1), r1=ysdm.delay_fifo_empty,  */
	0x01010222, 0x00010001, 0x000d02f3, 	/* (r1!=1), r1=ysdm.rsp_pxp_rdata_empty,  */
	0x01010223, 0x00010001, 0x000d02f4, 	/* (r1!=1), r1=ysdm.rsp_brb_rdata_empty,  */
	0x01010224, 0x00010001, 0x000d02f5, 	/* (r1!=1), r1=ysdm.rsp_int_ram_rdata_empty,  */
	0x01010225, 0x00010001, 0x000d02f6, 	/* (r1!=1), r1=ysdm.rsp_brb_pend_empty,  */
	0x01010226, 0x00010001, 0x000d02f7, 	/* (r1!=1), r1=ysdm.rsp_int_ram_pend_empty,  */
	0x01010227, 0x00010001, 0x000d02f8, 	/* (r1!=1), r1=ysdm.dst_pxp_immed_empty,  */
	0x01010228, 0x00010001, 0x000d02f9, 	/* (r1!=1), r1=ysdm.dst_pxp_dst_pend_empty,  */
	0x01010229, 0x00010001, 0x000d02fa, 	/* (r1!=1), r1=ysdm.dst_pxp_src_pend_empty,  */
	0x0101022a, 0x00010001, 0x000d02fb, 	/* (r1!=1), r1=ysdm.dst_brb_src_pend_empty,  */
	0x0101022b, 0x00010001, 0x000d02fc, 	/* (r1!=1), r1=ysdm.dst_brb_src_addr_empty,  */
	0x0101022c, 0x00010001, 0x000d02fd, 	/* (r1!=1), r1=ysdm.dst_pxp_link_empty,  */
	0x0101022d, 0x00010001, 0x000d02fe, 	/* (r1!=1), r1=ysdm.dst_int_ram_wait_empty,  */
	0x0101022e, 0x00010001, 0x000d02ff, 	/* (r1!=1), r1=ysdm.dst_pas_buf_wait_empty,  */
	0x0101022f, 0x00010001, 0x000d0300, 	/* (r1!=1), r1=ysdm.sh_delay_empty,  */
	0x01010230, 0x00010001, 0x000d0301, 	/* (r1!=1), r1=ysdm.cm_delay_empty,  */
	0x01010231, 0x00010001, 0x000d0302, 	/* (r1!=1), r1=ysdm.cmsg_que_empty,  */
	0x01010232, 0x00010001, 0x000d0303, 	/* (r1!=1), r1=ysdm.ccfc_load_pend_empty,  */
	0x01010233, 0x00010001, 0x000d0304, 	/* (r1!=1), r1=ysdm.tcfc_load_pend_empty,  */
	0x01010234, 0x00010001, 0x000d0305, 	/* (r1!=1), r1=ysdm.async_host_empty,  */
	0x01010235, 0x00010001, 0x000d0306, 	/* (r1!=1), r1=ysdm.prm_fifo_empty,  */
	0x00000236, 0x00010102, 0x00000307, 	/* ((r1&~r2)!=0), r1=psdm.INT_STS, r2=psdm.INT_MASK,  */
	0x00000237, 0x00010002, 0x0000030a, 	/* ((r1&~r2)!=0), r1=psdm.PRTY_STS_H_0, r2=psdm.PRTY_MASK_H_0,  */
	0x01010238, 0x00010001, 0x0000030c, 	/* (r1!=0), r1=psdm.qm_full,  */
	0x01010239, 0x00010001, 0x0000030d, 	/* (r1!=0), r1=psdm.rsp_brb_if_full,  */
	0x0101023a, 0x00010001, 0x0000030e, 	/* (r1!=0), r1=psdm.rsp_pxp_if_full,  */
	0x0101023b, 0x00010001, 0x0000030f, 	/* (r1!=0), r1=psdm.dst_pxp_if_full,  */
	0x0101023c, 0x00010001, 0x00000310, 	/* (r1!=0), r1=psdm.dst_int_ram_if_full,  */
	0x0101023d, 0x00010001, 0x00000311, 	/* (r1!=0), r1=psdm.dst_pas_buf_if_full,  */
	0x0101023e, 0x00010001, 0x000d0312, 	/* (r1!=1), r1=psdm.int_cmpl_pend_empty,  */
	0x0101023f, 0x00010001, 0x000d0313, 	/* (r1!=1), r1=psdm.int_cprm_pend_empty,  */
	0x01010240, 0x00010001, 0x00280314, 	/* (r1!=511), r1=psdm.queue_empty,  */
	0x01010241, 0x00010001, 0x000d0315, 	/* (r1!=1), r1=psdm.delay_fifo_empty,  */
	0x01010242, 0x00010001, 0x000d0316, 	/* (r1!=1), r1=psdm.rsp_pxp_rdata_empty,  */
	0x01010243, 0x00010001, 0x000d0317, 	/* (r1!=1), r1=psdm.rsp_brb_rdata_empty,  */
	0x01010244, 0x00010001, 0x000d0318, 	/* (r1!=1), r1=psdm.rsp_int_ram_rdata_empty,  */
	0x01010245, 0x00010001, 0x000d0319, 	/* (r1!=1), r1=psdm.rsp_brb_pend_empty,  */
	0x01010246, 0x00010001, 0x000d031a, 	/* (r1!=1), r1=psdm.rsp_int_ram_pend_empty,  */
	0x01010247, 0x00010001, 0x000d031b, 	/* (r1!=1), r1=psdm.dst_pxp_immed_empty,  */
	0x01010248, 0x00010001, 0x000d031c, 	/* (r1!=1), r1=psdm.dst_pxp_dst_pend_empty,  */
	0x01010249, 0x00010001, 0x000d031d, 	/* (r1!=1), r1=psdm.dst_pxp_src_pend_empty,  */
	0x0101024a, 0x00010001, 0x000d031e, 	/* (r1!=1), r1=psdm.dst_brb_src_pend_empty,  */
	0x0101024b, 0x00010001, 0x000d031f, 	/* (r1!=1), r1=psdm.dst_brb_src_addr_empty,  */
	0x0101024c, 0x00010001, 0x000d0320, 	/* (r1!=1), r1=psdm.dst_pxp_link_empty,  */
	0x0101024d, 0x00010001, 0x000d0321, 	/* (r1!=1), r1=psdm.dst_int_ram_wait_empty,  */
	0x0101024e, 0x00010001, 0x000d0322, 	/* (r1!=1), r1=psdm.dst_pas_buf_wait_empty,  */
	0x0101024f, 0x00010001, 0x000d0323, 	/* (r1!=1), r1=psdm.sh_delay_empty,  */
	0x01010250, 0x00010001, 0x000d0324, 	/* (r1!=1), r1=psdm.cm_delay_empty,  */
	0x01010251, 0x00010001, 0x000d0325, 	/* (r1!=1), r1=psdm.cmsg_que_empty,  */
	0x01010252, 0x00010001, 0x000d0326, 	/* (r1!=1), r1=psdm.ccfc_load_pend_empty,  */
	0x01010253, 0x00010001, 0x000d0327, 	/* (r1!=1), r1=psdm.tcfc_load_pend_empty,  */
	0x01010254, 0x00010001, 0x000d0328, 	/* (r1!=1), r1=psdm.async_host_empty,  */
	0x01010255, 0x00010001, 0x000d0329, 	/* (r1!=1), r1=psdm.prm_fifo_empty,  */
	0x00000256, 0x00010102, 0x0000032a, 	/* ((r1&~r2)!=0), r1=tsdm.INT_STS, r2=tsdm.INT_MASK,  */
	0x00000257, 0x00010002, 0x0000032d, 	/* ((r1&~r2)!=0), r1=tsdm.PRTY_STS_H_0, r2=tsdm.PRTY_MASK_H_0,  */
	0x01010258, 0x00010001, 0x0000032f, 	/* (r1!=0), r1=tsdm.qm_full,  */
	0x01010259, 0x00010001, 0x00000330, 	/* (r1!=0), r1=tsdm.rsp_brb_if_full,  */
	0x0101025a, 0x00010001, 0x00000331, 	/* (r1!=0), r1=tsdm.rsp_pxp_if_full,  */
	0x0101025b, 0x00010001, 0x00000332, 	/* (r1!=0), r1=tsdm.dst_pxp_if_full,  */
	0x0101025c, 0x00010001, 0x00000333, 	/* (r1!=0), r1=tsdm.dst_int_ram_if_full,  */
	0x0101025d, 0x00010001, 0x00000334, 	/* (r1!=0), r1=tsdm.dst_pas_buf_if_full,  */
	0x0101025e, 0x00010001, 0x000d0335, 	/* (r1!=1), r1=tsdm.int_cmpl_pend_empty,  */
	0x0101025f, 0x00010001, 0x000d0336, 	/* (r1!=1), r1=tsdm.int_cprm_pend_empty,  */
	0x01010260, 0x00010001, 0x00280337, 	/* (r1!=511), r1=tsdm.queue_empty,  */
	0x01010261, 0x00010001, 0x000d0338, 	/* (r1!=1), r1=tsdm.delay_fifo_empty,  */
	0x01010262, 0x00010001, 0x000d0339, 	/* (r1!=1), r1=tsdm.rsp_pxp_rdata_empty,  */
	0x01010263, 0x00010001, 0x000d033a, 	/* (r1!=1), r1=tsdm.rsp_brb_rdata_empty,  */
	0x01010264, 0x00010001, 0x000d033b, 	/* (r1!=1), r1=tsdm.rsp_int_ram_rdata_empty,  */
	0x01010265, 0x00010001, 0x000d033c, 	/* (r1!=1), r1=tsdm.rsp_brb_pend_empty,  */
	0x01010266, 0x00010001, 0x000d033d, 	/* (r1!=1), r1=tsdm.rsp_int_ram_pend_empty,  */
	0x01010267, 0x00010001, 0x000d033e, 	/* (r1!=1), r1=tsdm.dst_pxp_immed_empty,  */
	0x01010268, 0x00010001, 0x000d033f, 	/* (r1!=1), r1=tsdm.dst_pxp_dst_pend_empty,  */
	0x01010269, 0x00010001, 0x000d0340, 	/* (r1!=1), r1=tsdm.dst_pxp_src_pend_empty,  */
	0x0101026a, 0x00010001, 0x000d0341, 	/* (r1!=1), r1=tsdm.dst_brb_src_pend_empty,  */
	0x0101026b, 0x00010001, 0x000d0342, 	/* (r1!=1), r1=tsdm.dst_brb_src_addr_empty,  */
	0x0101026c, 0x00010001, 0x000d0343, 	/* (r1!=1), r1=tsdm.dst_pxp_link_empty,  */
	0x0101026d, 0x00010001, 0x000d0344, 	/* (r1!=1), r1=tsdm.dst_int_ram_wait_empty,  */
	0x0101026e, 0x00010001, 0x000d0345, 	/* (r1!=1), r1=tsdm.dst_pas_buf_wait_empty,  */
	0x0101026f, 0x00010001, 0x000d0346, 	/* (r1!=1), r1=tsdm.sh_delay_empty,  */
	0x01010270, 0x00010001, 0x000d0347, 	/* (r1!=1), r1=tsdm.cm_delay_empty,  */
	0x01010271, 0x00010001, 0x000d0348, 	/* (r1!=1), r1=tsdm.cmsg_que_empty,  */
	0x01010272, 0x00010001, 0x000d0349, 	/* (r1!=1), r1=tsdm.ccfc_load_pend_empty,  */
	0x01010273, 0x00010001, 0x000d034a, 	/* (r1!=1), r1=tsdm.tcfc_load_pend_empty,  */
	0x01010274, 0x00010001, 0x000d034b, 	/* (r1!=1), r1=tsdm.async_host_empty,  */
	0x01010275, 0x00010001, 0x000d034c, 	/* (r1!=1), r1=tsdm.prm_fifo_empty,  */
	0x00000276, 0x00010102, 0x0000034d, 	/* ((r1&~r2)!=0), r1=msdm.INT_STS, r2=msdm.INT_MASK,  */
	0x00000277, 0x00010002, 0x00000350, 	/* ((r1&~r2)!=0), r1=msdm.PRTY_STS_H_0, r2=msdm.PRTY_MASK_H_0,  */
	0x01010278, 0x00010001, 0x00000352, 	/* (r1!=0), r1=msdm.qm_full,  */
	0x01010279, 0x00010001, 0x00000353, 	/* (r1!=0), r1=msdm.rsp_brb_if_full,  */
	0x0101027a, 0x00010001, 0x00000354, 	/* (r1!=0), r1=msdm.rsp_pxp_if_full,  */
	0x0101027b, 0x00010001, 0x00000355, 	/* (r1!=0), r1=msdm.dst_pxp_if_full,  */
	0x0101027c, 0x00010001, 0x00000356, 	/* (r1!=0), r1=msdm.dst_int_ram_if_full,  */
	0x0101027d, 0x00010001, 0x00000357, 	/* (r1!=0), r1=msdm.dst_pas_buf_if_full,  */
	0x0101027e, 0x00010001, 0x000d0358, 	/* (r1!=1), r1=msdm.int_cmpl_pend_empty,  */
	0x0101027f, 0x00010001, 0x000d0359, 	/* (r1!=1), r1=msdm.int_cprm_pend_empty,  */
	0x01010280, 0x00010001, 0x0028035a, 	/* (r1!=511), r1=msdm.queue_empty,  */
	0x01010281, 0x00010001, 0x000d035b, 	/* (r1!=1), r1=msdm.delay_fifo_empty,  */
	0x01010282, 0x00010001, 0x000d035c, 	/* (r1!=1), r1=msdm.rsp_pxp_rdata_empty,  */
	0x01010283, 0x00010001, 0x000d035d, 	/* (r1!=1), r1=msdm.rsp_brb_rdata_empty,  */
	0x01010284, 0x00010001, 0x000d035e, 	/* (r1!=1), r1=msdm.rsp_int_ram_rdata_empty,  */
	0x01010285, 0x00010001, 0x000d035f, 	/* (r1!=1), r1=msdm.rsp_brb_pend_empty,  */
	0x01010286, 0x00010001, 0x000d0360, 	/* (r1!=1), r1=msdm.rsp_int_ram_pend_empty,  */
	0x01010287, 0x00010001, 0x000d0361, 	/* (r1!=1), r1=msdm.dst_pxp_immed_empty,  */
	0x01010288, 0x00010001, 0x000d0362, 	/* (r1!=1), r1=msdm.dst_pxp_dst_pend_empty,  */
	0x01010289, 0x00010001, 0x000d0363, 	/* (r1!=1), r1=msdm.dst_pxp_src_pend_empty,  */
	0x0101028a, 0x00010001, 0x000d0364, 	/* (r1!=1), r1=msdm.dst_brb_src_pend_empty,  */
	0x0101028b, 0x00010001, 0x000d0365, 	/* (r1!=1), r1=msdm.dst_brb_src_addr_empty,  */
	0x0101028c, 0x00010001, 0x000d0366, 	/* (r1!=1), r1=msdm.dst_pxp_link_empty,  */
	0x0101028d, 0x00010001, 0x000d0367, 	/* (r1!=1), r1=msdm.dst_int_ram_wait_empty,  */
	0x0101028e, 0x00010001, 0x000d0368, 	/* (r1!=1), r1=msdm.dst_pas_buf_wait_empty,  */
	0x0101028f, 0x00010001, 0x000d0369, 	/* (r1!=1), r1=msdm.sh_delay_empty,  */
	0x01010290, 0x00010001, 0x000d036a, 	/* (r1!=1), r1=msdm.cm_delay_empty,  */
	0x01010291, 0x00010001, 0x000d036b, 	/* (r1!=1), r1=msdm.cmsg_que_empty,  */
	0x01010292, 0x00010001, 0x000d036c, 	/* (r1!=1), r1=msdm.ccfc_load_pend_empty,  */
	0x01010293, 0x00010001, 0x000d036d, 	/* (r1!=1), r1=msdm.tcfc_load_pend_empty,  */
	0x01010294, 0x00010001, 0x000d036e, 	/* (r1!=1), r1=msdm.async_host_empty,  */
	0x01010295, 0x00010001, 0x000d036f, 	/* (r1!=1), r1=msdm.prm_fifo_empty,  */
	0x00000296, 0x00010102, 0x00000370, 	/* ((r1&~r2)!=0), r1=usdm.INT_STS, r2=usdm.INT_MASK,  */
	0x00000297, 0x00010002, 0x00000373, 	/* ((r1&~r2)!=0), r1=usdm.PRTY_STS_H_0, r2=usdm.PRTY_MASK_H_0,  */
	0x01010298, 0x00010001, 0x00000375, 	/* (r1!=0), r1=usdm.qm_full,  */
	0x01010299, 0x00010001, 0x00000376, 	/* (r1!=0), r1=usdm.rsp_brb_if_full,  */
	0x0101029a, 0x00010001, 0x00000377, 	/* (r1!=0), r1=usdm.rsp_pxp_if_full,  */
	0x0101029b, 0x00010001, 0x00000378, 	/* (r1!=0), r1=usdm.dst_pxp_if_full,  */
	0x0101029c, 0x00010001, 0x00000379, 	/* (r1!=0), r1=usdm.dst_int_ram_if_full,  */
	0x0101029d, 0x00010001, 0x0000037a, 	/* (r1!=0), r1=usdm.dst_pas_buf_if_full,  */
	0x0101029e, 0x00010001, 0x000d037b, 	/* (r1!=1), r1=usdm.int_cmpl_pend_empty,  */
	0x0101029f, 0x00010001, 0x000d037c, 	/* (r1!=1), r1=usdm.int_cprm_pend_empty,  */
	0x010102a0, 0x00010001, 0x0028037d, 	/* (r1!=511), r1=usdm.queue_empty,  */
	0x010102a1, 0x00010001, 0x000d037e, 	/* (r1!=1), r1=usdm.delay_fifo_empty,  */
	0x010102a2, 0x00010001, 0x000d037f, 	/* (r1!=1), r1=usdm.rsp_pxp_rdata_empty,  */
	0x010102a3, 0x00010001, 0x000d0380, 	/* (r1!=1), r1=usdm.rsp_brb_rdata_empty,  */
	0x010102a4, 0x00010001, 0x000d0381, 	/* (r1!=1), r1=usdm.rsp_int_ram_rdata_empty,  */
	0x010102a5, 0x00010001, 0x000d0382, 	/* (r1!=1), r1=usdm.rsp_brb_pend_empty,  */
	0x010102a6, 0x00010001, 0x000d0383, 	/* (r1!=1), r1=usdm.rsp_int_ram_pend_empty,  */
	0x010102a7, 0x00010001, 0x000d0384, 	/* (r1!=1), r1=usdm.dst_pxp_immed_empty,  */
	0x010102a8, 0x00010001, 0x000d0385, 	/* (r1!=1), r1=usdm.dst_pxp_dst_pend_empty,  */
	0x010102a9, 0x00010001, 0x000d0386, 	/* (r1!=1), r1=usdm.dst_pxp_src_pend_empty,  */
	0x010102aa, 0x00010001, 0x000d0387, 	/* (r1!=1), r1=usdm.dst_brb_src_pend_empty,  */
	0x010102ab, 0x00010001, 0x000d0388, 	/* (r1!=1), r1=usdm.dst_brb_src_addr_empty,  */
	0x010102ac, 0x00010001, 0x000d0389, 	/* (r1!=1), r1=usdm.dst_pxp_link_empty,  */
	0x010102ad, 0x00010001, 0x000d038a, 	/* (r1!=1), r1=usdm.dst_int_ram_wait_empty,  */
	0x010102ae, 0x00010001, 0x000d038b, 	/* (r1!=1), r1=usdm.dst_pas_buf_wait_empty,  */
	0x010102af, 0x00010001, 0x000d038c, 	/* (r1!=1), r1=usdm.sh_delay_empty,  */
	0x010102b0, 0x00010001, 0x000d038d, 	/* (r1!=1), r1=usdm.cm_delay_empty,  */
	0x010102b1, 0x00010001, 0x000d038e, 	/* (r1!=1), r1=usdm.cmsg_que_empty,  */
	0x010102b2, 0x00010001, 0x000d038f, 	/* (r1!=1), r1=usdm.ccfc_load_pend_empty,  */
	0x010102b3, 0x00010001, 0x000d0390, 	/* (r1!=1), r1=usdm.tcfc_load_pend_empty,  */
	0x010102b4, 0x00010001, 0x000d0391, 	/* (r1!=1), r1=usdm.async_host_empty,  */
	0x010102b5, 0x00010001, 0x000d0392, 	/* (r1!=1), r1=usdm.prm_fifo_empty,  */
	0x000002b6, 0x00010002, 0x00000393, 	/* ((r1&~r2)!=0), r1=xcm.INT_STS_0, r2=xcm.INT_MASK_0,  */
	0x000002b7, 0x00010002, 0x00000395, 	/* ((r1&~r2)!=0), r1=xcm.INT_STS_1, r2=xcm.INT_MASK_1,  */
	0x000002b8, 0x00010102, 0x00000397, 	/* ((r1&~r2)!=0), r1=xcm.INT_STS_2, r2=xcm.INT_MASK_2,  */
	0x000002b9, 0x00010002, 0x0000039a, 	/* ((r1&~r2)!=0), r1=xcm.PRTY_STS_H_0, r2=xcm.PRTY_MASK_H_0,  */
	0x000002ba, 0x00010002, 0x0000039c, 	/* ((r1&~r2)!=0), r1=xcm.PRTY_STS_H_1, r2=xcm.PRTY_MASK_H_1,  */
	0x010002bb, 0x00010001, 0x0000039e, 	/* (r1!=0), r1=xcm.fi_desc_input_violate,  */
	0x010102bc, 0x00010001, 0x0000039f, 	/* (r1!=0), r1=xcm.ia_agg_con_part_fill_lvl,  */
	0x010102bd, 0x00010001, 0x000003a0, 	/* (r1!=0), r1=xcm.ia_sm_con_part_fill_lvl,  */
	0x010102be, 0x00010001, 0x000003a1, 	/* (r1!=0), r1=xcm.ia_trans_part_fill_lvl,  */
	0x010102bf, 0x00010001, 0x002903a2, 	/* (r1!=reset1), r1=xcm.xx_free_cnt,  */
	0x010102c0, 0x00010001, 0x000003a3, 	/* (r1!=0), r1=xcm.xx_lcid_cam_fill_lvl,  */
	0x010102c1, 0x00010001, 0x000003a4, 	/* (r1!=0), r1=xcm.xx_non_lock_cnt,  */
	0x010102c2, 0x00010001, 0x000003a5, 	/* (r1!=0), r1=xcm.xx_lock_cnt,  */
	0x010102c3, 0x00010001, 0x000003a6, 	/* (r1!=0), r1=xcm.xx_cbyp_tbl_fill_lvl,  */
	0x010102c4, 0x00010001, 0x000003a7, 	/* (r1!=0), r1=xcm.xx_pref_dir_fill_lvl,  */
	0x010102c5, 0x00010001, 0x000003a8, 	/* (r1!=0), r1=xcm.xx_pref_aggst_fill_lvl,  */
	0x010102c6, 0x00010001, 0x000003a9, 	/* (r1!=0), r1=xcm.xx_pref_byp_fill_lvl,  */
	0x010002c7, 0x00010001, 0x000003aa, 	/* (r1!=0), r1=xcm.prcs_agg_con_curr_st,  */
	0x010002c8, 0x00010001, 0x000003ab, 	/* (r1!=0), r1=xcm.prcs_sm_con_curr_st,  */
	0x010102c9, 0x00010001, 0x000003ac, 	/* (r1!=0), r1=xcm.agg_con_fic_buf_fill_lvl,  */
	0x010102ca, 0x00010001, 0x000003ad, 	/* (r1!=0), r1=xcm.sm_con_fic_buf_fill_lvl,  */
	0x010102cb, 0x00010001, 0x000003ae, 	/* (r1!=0), r1=xcm.in_prcs_tbl_fill_lvl,  */
	0x010002cc, 0x00010001, 0x000003af, 	/* (r1!=0), r1=xcm.cmpl_dir_curr_st,  */
	0x010102cd, 0x00010001, 0x000d03b0, 	/* (r1!=reset1), r1=xcm.ccfc_init_crd,  */
	0x010102ce, 0x00010001, 0x000a03b1, 	/* (r1!=reset1), r1=xcm.qm_init_crd0,  */
	0x010102cf, 0x00010001, 0x000a03b2, 	/* (r1!=reset1), r1=xcm.qm_init_crd1,  */
	0x010102d0, 0x00010001, 0x000f03b3, 	/* (r1!=reset1), r1=xcm.tm_init_crd,  */
	0x010002d1, 0x00010001, 0x000003b4, 	/* (r1!=0), r1=xcm.xsdm_length_mis,  */
	0x010002d2, 0x00010001, 0x000003b5, 	/* (r1!=0), r1=xcm.ysdm_length_mis,  */
	0x010002d3, 0x00010001, 0x000003b6, 	/* (r1!=0), r1=xcm.dorq_length_mis,  */
	0x010002d4, 0x00010001, 0x000003b7, 	/* (r1!=0), r1=xcm.pbf_length_mis,  */
	0x010102d5, 0x00010001, 0x000003b8, 	/* (r1!=0), r1=xcm.is_qm_p_fill_lvl,  */
	0x010102d6, 0x00010001, 0x000003b9, 	/* (r1!=0), r1=xcm.is_qm_s_fill_lvl,  */
	0x010102d7, 0x00010001, 0x000003ba, 	/* (r1!=0), r1=xcm.is_tm_fill_lvl,  */
	0x010102d8, 0x00010001, 0x000003bb, 	/* (r1!=0), r1=xcm.is_storm_fill_lvl,  */
	0x010102d9, 0x00010001, 0x000003bc, 	/* (r1!=0), r1=xcm.is_xsdm_fill_lvl,  */
	0x010102da, 0x00010001, 0x000003bd, 	/* (r1!=0), r1=xcm.is_ysdm_fill_lvl,  */
	0x010102db, 0x00010001, 0x000003be, 	/* (r1!=0), r1=xcm.is_msem_fill_lvl,  */
	0x010102dc, 0x00010001, 0x000003bf, 	/* (r1!=0), r1=xcm.is_usem_fill_lvl,  */
	0x010102dd, 0x00010001, 0x000003c0, 	/* (r1!=0), r1=xcm.is_dorq_fill_lvl,  */
	0x010102de, 0x00010001, 0x000003c1, 	/* (r1!=0), r1=xcm.is_pbf_fill_lvl,  */
	0x000002df, 0x00010002, 0x000003c2, 	/* ((r1&~r2)!=0), r1=ycm.INT_STS_0, r2=ycm.INT_MASK_0,  */
	0x000002e0, 0x00010002, 0x000003c4, 	/* ((r1&~r2)!=0), r1=ycm.INT_STS_1, r2=ycm.INT_MASK_1,  */
	0x000002e1, 0x00010002, 0x000003c6, 	/* ((r1&~r2)!=0), r1=ycm.PRTY_STS_H_0, r2=ycm.PRTY_MASK_H_0,  */
	0x000002e2, 0x00010002, 0x000003c8, 	/* ((r1&~r2)!=0), r1=ycm.PRTY_STS_H_1, r2=ycm.PRTY_MASK_H_1,  */
	0x010002e3, 0x00010001, 0x000003ca, 	/* (r1!=0), r1=ycm.fi_desc_input_violate,  */
	0x010002e4, 0x00010001, 0x000003cb, 	/* (r1!=0), r1=ycm.se_desc_input_violate,  */
	0x010102e5, 0x00010001, 0x000003cc, 	/* (r1!=0), r1=ycm.ia_sm_con_part_fill_lvl,  */
	0x010102e6, 0x00010001, 0x000003cd, 	/* (r1!=0), r1=ycm.ia_agg_task_part_fill_lvl,  */
	0x010102e7, 0x00010001, 0x000003ce, 	/* (r1!=0), r1=ycm.ia_sm_task_part_fill_lvl,  */
	0x010102e8, 0x00010001, 0x000003cf, 	/* (r1!=0), r1=ycm.ia_trans_part_fill_lvl,  */
	0x010102e9, 0x00010001, 0x002903d0, 	/* (r1!=reset1), r1=ycm.xx_free_cnt,  */
	0x010102ea, 0x00010001, 0x000003d1, 	/* (r1!=0), r1=ycm.xx_lcid_cam_fill_lvl,  */
	0x010102eb, 0x00010001, 0x000003d2, 	/* (r1!=0), r1=ycm.xx_non_lock_cnt,  */
	0x010102ec, 0x00010001, 0x000003d3, 	/* (r1!=0), r1=ycm.xx_lock_cnt,  */
	0x010102ed, 0x00010001, 0x000003d4, 	/* (r1!=0), r1=ycm.xx_cbyp_tbl_fill_lvl,  */
	0x010102ee, 0x00010001, 0x000003d5, 	/* (r1!=0), r1=ycm.xx_tbyp_tbl_fill_lvl,  */
	0x010102ef, 0x00010001, 0x000003d6, 	/* (r1!=0), r1=ycm.xx_tbyp_tbl_fill_lvl,  */
	0x010102f0, 0x00010001, 0x000003d7, 	/* (r1!=0), r1=ycm.xx_pref_dir_fill_lvl,  */
	0x010102f1, 0x00010001, 0x000003d8, 	/* (r1!=0), r1=ycm.xx_pref_aggst_fill_lvl,  */
	0x010102f2, 0x00010001, 0x000003d9, 	/* (r1!=0), r1=ycm.xx_pref_byp_fill_lvl,  */
	0x010002f3, 0x00010001, 0x000003da, 	/* (r1!=0), r1=ycm.prcs_agg_con_curr_st,  */
	0x010002f4, 0x00010001, 0x000003db, 	/* (r1!=0), r1=ycm.prcs_sm_con_curr_st,  */
	0x010002f5, 0x00010001, 0x000003dc, 	/* (r1!=0), r1=ycm.prcs_agg_task_curr_st,  */
	0x010002f6, 0x00010001, 0x000003dd, 	/* (r1!=0), r1=ycm.prcs_sm_task_curr_st,  */
	0x010102f7, 0x00010001, 0x000003de, 	/* (r1!=0), r1=ycm.sm_con_fic_buf_fill_lvl,  */
	0x010102f8, 0x00010001, 0x000003df, 	/* (r1!=0), r1=ycm.agg_task_fic_buf_fill_lvl,  */
	0x010102f9, 0x00010001, 0x000003e0, 	/* (r1!=0), r1=ycm.sm_task_fic_buf_fill_lvl,  */
	0x010102fa, 0x00010001, 0x000003e1, 	/* (r1!=0), r1=ycm.in_prcs_tbl_fill_lvl,  */
	0x010002fb, 0x00010001, 0x000003e2, 	/* (r1!=0), r1=ycm.cmpl_dir_curr_st,  */
	0x010102fc, 0x00010001, 0x000d03e3, 	/* (r1!=reset1), r1=ycm.ccfc_init_crd,  */
	0x010102fd, 0x00010001, 0x000d03e4, 	/* (r1!=reset1), r1=ycm.tcfc_init_crd,  */
	0x010102fe, 0x00010001, 0x000a03e5, 	/* (r1!=reset1), r1=ycm.qm_init_crd0,  */
	0x010002ff, 0x00010001, 0x000003e6, 	/* (r1!=0), r1=ycm.ysdm_length_mis,  */
	0x01000300, 0x00010001, 0x000003e7, 	/* (r1!=0), r1=ycm.pbf_length_mis,  */
	0x01000301, 0x00010001, 0x000003e8, 	/* (r1!=0), r1=ycm.xyld_length_mis,  */
	0x01010302, 0x00010001, 0x000003e9, 	/* (r1!=0), r1=ycm.is_qm_p_fill_lvl,  */
	0x01010303, 0x00010001, 0x000003ea, 	/* (r1!=0), r1=ycm.is_qm_s_fill_lvl,  */
	0x01010304, 0x00010001, 0x000003eb, 	/* (r1!=0), r1=ycm.is_storm_fill_lvl,  */
	0x01010305, 0x00010001, 0x000003ec, 	/* (r1!=0), r1=ycm.is_ysdm_fill_lvl,  */
	0x01010306, 0x00010001, 0x000003ed, 	/* (r1!=0), r1=ycm.is_xyld_fill_lvl,  */
	0x01010307, 0x00010001, 0x000003ee, 	/* (r1!=0), r1=ycm.is_msem_fill_lvl,  */
	0x01010308, 0x00010001, 0x000003ef, 	/* (r1!=0), r1=ycm.is_usem_fill_lvl,  */
	0x01010309, 0x00010001, 0x000003f0, 	/* (r1!=0), r1=ycm.is_pbf_fill_lvl,  */
	0x0000030a, 0x00010002, 0x000003f1, 	/* ((r1&~r2)!=0), r1=pcm.INT_STS_0, r2=pcm.INT_MASK_0,  */
	0x0000030b, 0x00010002, 0x000003f3, 	/* ((r1&~r2)!=0), r1=pcm.INT_STS_1, r2=pcm.INT_MASK_1,  */
	0x0000030c, 0x00010002, 0x000003f5, 	/* ((r1&~r2)!=0), r1=pcm.PRTY_STS_H_0, r2=pcm.PRTY_MASK_H_0,  */
	0x0100030d, 0x00010001, 0x000003f7, 	/* (r1!=0), r1=pcm.fi_desc_input_violate,  */
	0x0101030e, 0x00010001, 0x000003f8, 	/* (r1!=0), r1=pcm.ia_sm_con_part_fill_lvl,  */
	0x0101030f, 0x00010001, 0x000003f9, 	/* (r1!=0), r1=pcm.ia_trans_part_fill_lvl,  */
	0x01010310, 0x00010001, 0x000f03fa, 	/* (r1!=reset1), r1=pcm.xx_free_cnt,  */
	0x01010311, 0x00010001, 0x000003fb, 	/* (r1!=0), r1=pcm.xx_lcid_cam_fill_lvl,  */
	0x01010312, 0x00010001, 0x000003fc, 	/* (r1!=0), r1=pcm.xx_non_lock_cnt,  */
	0x01010313, 0x00010001, 0x000003fd, 	/* (r1!=0), r1=pcm.xx_lock_cnt,  */
	0x01010314, 0x00010001, 0x000003fe, 	/* (r1!=0), r1=pcm.xx_pref_dir_fill_lvl,  */
	0x01010315, 0x00010001, 0x000003ff, 	/* (r1!=0), r1=pcm.xx_pref_aggst_fill_lvl,  */
	0x01000316, 0x00010001, 0x00000400, 	/* (r1!=0), r1=pcm.prcs_sm_con_curr_st,  */
	0x01010317, 0x00010001, 0x00000401, 	/* (r1!=0), r1=pcm.sm_con_fic_buf_fill_lvl,  */
	0x01010318, 0x00010001, 0x00000402, 	/* (r1!=0), r1=pcm.in_prcs_tbl_fill_lvl,  */
	0x01000319, 0x00010001, 0x00000403, 	/* (r1!=0), r1=pcm.cmpl_dir_curr_st,  */
	0x0101031a, 0x00010001, 0x000d0404, 	/* (r1!=reset1), r1=pcm.ccfc_init_crd,  */
	0x0101031b, 0x00010001, 0x00000405, 	/* (r1!=0), r1=pcm.is_storm_fill_lvl,  */
	0x0000031c, 0x00010002, 0x00000406, 	/* ((r1&~r2)!=0), r1=tcm.INT_STS_0, r2=tcm.INT_MASK_0,  */
	0x0000031d, 0x00010002, 0x00000408, 	/* ((r1&~r2)!=0), r1=tcm.INT_STS_1, r2=tcm.INT_MASK_1,  */
	0x0000031e, 0x00010002, 0x0000040a, 	/* ((r1&~r2)!=0), r1=tcm.PRTY_STS_H_0, r2=tcm.PRTY_MASK_H_0,  */
	0x0000031f, 0x00010002, 0x0000040c, 	/* ((r1&~r2)!=0), r1=tcm.PRTY_STS_H_1, r2=tcm.PRTY_MASK_H_1,  */
	0x01000320, 0x00010001, 0x0000040e, 	/* (r1!=0), r1=tcm.fi_desc_input_violate,  */
	0x01000321, 0x00010001, 0x0000040f, 	/* (r1!=0), r1=tcm.se_desc_input_violate,  */
	0x01010322, 0x00010001, 0x00000410, 	/* (r1!=0), r1=tcm.ia_agg_con_part_fill_lvl,  */
	0x01010323, 0x00010001, 0x00000411, 	/* (r1!=0), r1=tcm.ia_sm_con_part_fill_lvl,  */
	0x01010324, 0x00010001, 0x00000412, 	/* (r1!=0), r1=tcm.ia_agg_task_part_fill_lvl,  */
	0x01010325, 0x00010001, 0x00000413, 	/* (r1!=0), r1=tcm.ia_sm_task_part_fill_lvl,  */
	0x01010326, 0x00010001, 0x00000414, 	/* (r1!=0), r1=tcm.ia_trans_part_fill_lvl,  */
	0x01010327, 0x00010001, 0x00290415, 	/* (r1!=reset1), r1=tcm.xx_free_cnt,  */
	0x01010328, 0x00010001, 0x00000416, 	/* (r1!=0), r1=tcm.xx_lcid_cam_fill_lvl,  */
	0x01010329, 0x00010001, 0x00000417, 	/* (r1!=0), r1=tcm.xx_non_lock_cnt,  */
	0x0101032a, 0x00010001, 0x00000418, 	/* (r1!=0), r1=tcm.xx_lock_cnt,  */
	0x0101032b, 0x00010001, 0x00000419, 	/* (r1!=0), r1=tcm.xx_cbyp_tbl_fill_lvl,  */
	0x0101032c, 0x00010001, 0x0000041a, 	/* (r1!=0), r1=tcm.xx_tbyp_tbl_fill_lvl,  */
	0x0101032d, 0x00010001, 0x0000041b, 	/* (r1!=0), r1=tcm.xx_tbyp_tbl_fill_lvl,  */
	0x0101032e, 0x00010001, 0x0000041c, 	/* (r1!=0), r1=tcm.xx_pref_dir_fill_lvl,  */
	0x0101032f, 0x00010001, 0x0000041d, 	/* (r1!=0), r1=tcm.xx_pref_aggst_fill_lvl,  */
	0x01010330, 0x00010001, 0x0000041e, 	/* (r1!=0), r1=tcm.xx_pref_byp_fill_lvl,  */
	0x01000331, 0x00010001, 0x0000041f, 	/* (r1!=0), r1=tcm.prcs_agg_con_curr_st,  */
	0x01000332, 0x00010001, 0x00000420, 	/* (r1!=0), r1=tcm.prcs_sm_con_curr_st,  */
	0x01000333, 0x00010001, 0x00000421, 	/* (r1!=0), r1=tcm.prcs_agg_task_curr_st,  */
	0x01000334, 0x00010001, 0x00000422, 	/* (r1!=0), r1=tcm.prcs_sm_task_curr_st,  */
	0x01010335, 0x00010001, 0x00000423, 	/* (r1!=0), r1=tcm.agg_con_fic_buf_fill_lvl,  */
	0x01010336, 0x00010001, 0x00000424, 	/* (r1!=0), r1=tcm.sm_con_fic_buf_fill_lvl,  */
	0x01010337, 0x00010001, 0x00000425, 	/* (r1!=0), r1=tcm.agg_task_fic_buf_fill_lvl,  */
	0x01010338, 0x00010001, 0x00000426, 	/* (r1!=0), r1=tcm.sm_task_fic_buf_fill_lvl,  */
	0x01010339, 0x00010001, 0x00000427, 	/* (r1!=0), r1=tcm.in_prcs_tbl_fill_lvl,  */
	0x0100033a, 0x00010001, 0x00000428, 	/* (r1!=0), r1=tcm.cmpl_dir_curr_st,  */
	0x0101033b, 0x00010001, 0x000d0429, 	/* (r1!=reset1), r1=tcm.ccfc_init_crd,  */
	0x0101033c, 0x00010001, 0x000d042a, 	/* (r1!=reset1), r1=tcm.tcfc_init_crd,  */
	0x0101033d, 0x00010001, 0x000a042b, 	/* (r1!=reset1), r1=tcm.qm_init_crd0,  */
	0x0101033e, 0x00010001, 0x000f042c, 	/* (r1!=reset1), r1=tcm.tm_init_crd,  */
	0x0100033f, 0x00010001, 0x0000042d, 	/* (r1!=0), r1=tcm.dorq_length_mis,  */
	0x01000340, 0x00010001, 0x0000042e, 	/* (r1!=0), r1=tcm.pbf_length_mis,  */
	0x01010341, 0x00010001, 0x0000042f, 	/* (r1!=0), r1=tcm.is_qm_p_fill_lvl,  */
	0x01010342, 0x00010001, 0x00000430, 	/* (r1!=0), r1=tcm.is_qm_s_fill_lvl,  */
	0x01010343, 0x00010001, 0x00000431, 	/* (r1!=0), r1=tcm.is_tm_fill_lvl,  */
	0x01010344, 0x00010001, 0x00000432, 	/* (r1!=0), r1=tcm.is_storm_fill_lvl,  */
	0x01010345, 0x00010001, 0x00000433, 	/* (r1!=0), r1=tcm.is_msem_fill_lvl,  */
	0x01010346, 0x00010001, 0x00000434, 	/* (r1!=0), r1=tcm.is_dorq_fill_lvl,  */
	0x01010347, 0x00010001, 0x00000435, 	/* (r1!=0), r1=tcm.is_pbf_fill_lvl,  */
	0x00000348, 0x00010002, 0x00000436, 	/* ((r1&~r2)!=0), r1=mcm.INT_STS_0, r2=mcm.INT_MASK_0,  */
	0x00000349, 0x00010002, 0x00000438, 	/* ((r1&~r2)!=0), r1=mcm.INT_STS_1, r2=mcm.INT_MASK_1,  */
	0x0000034a, 0x00010002, 0x0000043a, 	/* ((r1&~r2)!=0), r1=mcm.PRTY_STS_H_0, r2=mcm.PRTY_MASK_H_0,  */
	0x0000034b, 0x00010002, 0x0000043c, 	/* ((r1&~r2)!=0), r1=mcm.PRTY_STS_H_1, r2=mcm.PRTY_MASK_H_1,  */
	0x0100034c, 0x00010001, 0x0000043e, 	/* (r1!=0), r1=mcm.fi_desc_input_violate,  */
	0x0100034d, 0x00010001, 0x0000043f, 	/* (r1!=0), r1=mcm.se_desc_input_violate,  */
	0x0101034e, 0x00010001, 0x00000440, 	/* (r1!=0), r1=mcm.ia_agg_con_part_fill_lvl,  */
	0x0101034f, 0x00010001, 0x00000441, 	/* (r1!=0), r1=mcm.ia_sm_con_part_fill_lvl,  */
	0x01010350, 0x00010001, 0x00000442, 	/* (r1!=0), r1=mcm.ia_agg_task_part_fill_lvl,  */
	0x01010351, 0x00010001, 0x00000443, 	/* (r1!=0), r1=mcm.ia_sm_task_part_fill_lvl,  */
	0x01010352, 0x00010001, 0x00000444, 	/* (r1!=0), r1=mcm.ia_trans_part_fill_lvl,  */
	0x01010353, 0x00010001, 0x00290445, 	/* (r1!=reset1), r1=mcm.xx_free_cnt,  */
	0x01010354, 0x00010001, 0x00000446, 	/* (r1!=0), r1=mcm.xx_lcid_cam_fill_lvl,  */
	0x01010355, 0x00010001, 0x00000447, 	/* (r1!=0), r1=mcm.xx_non_lock_cnt,  */
	0x01010356, 0x00010001, 0x00000448, 	/* (r1!=0), r1=mcm.xx_lock_cnt,  */
	0x01010357, 0x00010001, 0x00000449, 	/* (r1!=0), r1=mcm.xx_cbyp_tbl_fill_lvl,  */
	0x01010358, 0x00010001, 0x0000044a, 	/* (r1!=0), r1=mcm.xx_tbyp_tbl_fill_lvl,  */
	0x01010359, 0x00010001, 0x0000044b, 	/* (r1!=0), r1=mcm.xx_tbyp_tbl_fill_lvl,  */
	0x0101035a, 0x00010001, 0x0000044c, 	/* (r1!=0), r1=mcm.xx_pref_dir_fill_lvl,  */
	0x0101035b, 0x00010001, 0x0000044d, 	/* (r1!=0), r1=mcm.xx_pref_aggst_fill_lvl,  */
	0x0101035c, 0x00010001, 0x0000044e, 	/* (r1!=0), r1=mcm.xx_pref_byp_fill_lvl,  */
	0x0100035d, 0x00010001, 0x0000044f, 	/* (r1!=0), r1=mcm.prcs_agg_con_curr_st,  */
	0x0100035e, 0x00010001, 0x00000450, 	/* (r1!=0), r1=mcm.prcs_sm_con_curr_st,  */
	0x0100035f, 0x00010001, 0x00000451, 	/* (r1!=0), r1=mcm.prcs_agg_task_curr_st,  */
	0x01000360, 0x00010001, 0x00000452, 	/* (r1!=0), r1=mcm.prcs_sm_task_curr_st,  */
	0x01010361, 0x00010001, 0x00000453, 	/* (r1!=0), r1=mcm.agg_con_fic_buf_fill_lvl,  */
	0x01010362, 0x00010001, 0x00000454, 	/* (r1!=0), r1=mcm.sm_con_fic_buf_fill_lvl,  */
	0x01010363, 0x00010001, 0x00000455, 	/* (r1!=0), r1=mcm.agg_task_fic_buf_fill_lvl,  */
	0x01010364, 0x00010001, 0x00000456, 	/* (r1!=0), r1=mcm.sm_task_fic_buf_fill_lvl,  */
	0x01010365, 0x00010001, 0x00000457, 	/* (r1!=0), r1=mcm.in_prcs_tbl_fill_lvl,  */
	0x01000366, 0x00010001, 0x00000458, 	/* (r1!=0), r1=mcm.cmpl_dir_curr_st,  */
	0x01010367, 0x00010001, 0x000d0459, 	/* (r1!=reset1), r1=mcm.ccfc_init_crd,  */
	0x01010368, 0x00010001, 0x000d045a, 	/* (r1!=reset1), r1=mcm.tcfc_init_crd,  */
	0x01010369, 0x00010001, 0x000a045b, 	/* (r1!=reset1), r1=mcm.qm_init_crd0,  */
	0x0100036a, 0x00010001, 0x0000045c, 	/* (r1!=0), r1=mcm.ysdm_length_mis,  */
	0x0100036b, 0x00010001, 0x0000045d, 	/* (r1!=0), r1=mcm.usdm_length_mis,  */
	0x0100036c, 0x00010001, 0x0000045e, 	/* (r1!=0), r1=mcm.pbf_length_mis,  */
	0x0100036d, 0x00010001, 0x0000045f, 	/* (r1!=0), r1=mcm.tmld_length_mis,  */
	0x0101036e, 0x00010001, 0x00000460, 	/* (r1!=0), r1=mcm.is_qm_p_fill_lvl,  */
	0x0101036f, 0x00010001, 0x00000461, 	/* (r1!=0), r1=mcm.is_qm_s_fill_lvl,  */
	0x01010370, 0x00010001, 0x00000462, 	/* (r1!=0), r1=mcm.is_storm_fill_lvl,  */
	0x01010371, 0x00010001, 0x00000463, 	/* (r1!=0), r1=mcm.is_ysdm_fill_lvl,  */
	0x01010372, 0x00010001, 0x00000464, 	/* (r1!=0), r1=mcm.is_usdm_fill_lvl,  */
	0x01010373, 0x00010001, 0x00000465, 	/* (r1!=0), r1=mcm.is_tmld_fill_lvl,  */
	0x01010374, 0x00010001, 0x00000466, 	/* (r1!=0), r1=mcm.is_usem_fill_lvl,  */
	0x01010375, 0x00010001, 0x00000467, 	/* (r1!=0), r1=mcm.is_pbf_fill_lvl,  */
	0x00000376, 0x00010002, 0x00000468, 	/* ((r1&~r2)!=0), r1=ucm.INT_STS_0, r2=ucm.INT_MASK_0,  */
	0x00000377, 0x00010002, 0x0000046a, 	/* ((r1&~r2)!=0), r1=ucm.INT_STS_1, r2=ucm.INT_MASK_1,  */
	0x00000378, 0x00010002, 0x0000046c, 	/* ((r1&~r2)!=0), r1=ucm.PRTY_STS_H_0, r2=ucm.PRTY_MASK_H_0,  */
	0x00000379, 0x00010002, 0x0000046e, 	/* ((r1&~r2)!=0), r1=ucm.PRTY_STS_H_1, r2=ucm.PRTY_MASK_H_1,  */
	0x0100037a, 0x00010001, 0x00000470, 	/* (r1!=0), r1=ucm.fi_desc_input_violate,  */
	0x0100037b, 0x00010001, 0x00000471, 	/* (r1!=0), r1=ucm.se_desc_input_violate,  */
	0x0101037c, 0x00010001, 0x00000472, 	/* (r1!=0), r1=ucm.ia_agg_con_part_fill_lvl,  */
	0x0101037d, 0x00010001, 0x00000473, 	/* (r1!=0), r1=ucm.ia_sm_con_part_fill_lvl,  */
	0x0101037e, 0x00010001, 0x00000474, 	/* (r1!=0), r1=ucm.ia_agg_task_part_fill_lvl,  */
	0x0101037f, 0x00010001, 0x00000475, 	/* (r1!=0), r1=ucm.ia_sm_task_part_fill_lvl,  */
	0x01010380, 0x00010001, 0x00000476, 	/* (r1!=0), r1=ucm.ia_trans_part_fill_lvl,  */
	0x01010381, 0x00010001, 0x00290477, 	/* (r1!=reset1), r1=ucm.xx_free_cnt,  */
	0x01010382, 0x00010001, 0x00000478, 	/* (r1!=0), r1=ucm.xx_lcid_cam_fill_lvl,  */
	0x01010383, 0x00010001, 0x00000479, 	/* (r1!=0), r1=ucm.xx_non_lock_cnt,  */
	0x01010384, 0x00010001, 0x0000047a, 	/* (r1!=0), r1=ucm.xx_lock_cnt,  */
	0x01010385, 0x00010001, 0x0000047b, 	/* (r1!=0), r1=ucm.xx_cbyp_tbl_fill_lvl,  */
	0x01010386, 0x00010001, 0x0000047c, 	/* (r1!=0), r1=ucm.xx_tbyp_tbl_fill_lvl,  */
	0x01010387, 0x00010001, 0x0000047d, 	/* (r1!=0), r1=ucm.xx_tbyp_tbl_fill_lvl,  */
	0x01010388, 0x00010001, 0x0000047e, 	/* (r1!=0), r1=ucm.xx_pref_dir_fill_lvl,  */
	0x01010389, 0x00010001, 0x0000047f, 	/* (r1!=0), r1=ucm.xx_pref_aggst_fill_lvl,  */
	0x0101038a, 0x00010001, 0x00000480, 	/* (r1!=0), r1=ucm.xx_pref_byp_fill_lvl,  */
	0x0100038b, 0x00010001, 0x00000481, 	/* (r1!=0), r1=ucm.prcs_agg_con_curr_st,  */
	0x0100038c, 0x00010001, 0x00000482, 	/* (r1!=0), r1=ucm.prcs_sm_con_curr_st,  */
	0x0100038d, 0x00010001, 0x00000483, 	/* (r1!=0), r1=ucm.prcs_agg_task_curr_st,  */
	0x0100038e, 0x00010001, 0x00000484, 	/* (r1!=0), r1=ucm.prcs_sm_task_curr_st,  */
	0x0101038f, 0x00010001, 0x00000485, 	/* (r1!=0), r1=ucm.agg_con_fic_buf_fill_lvl,  */
	0x01010390, 0x00010001, 0x00000486, 	/* (r1!=0), r1=ucm.sm_con_fic_buf_fill_lvl,  */
	0x01010391, 0x00010001, 0x00000487, 	/* (r1!=0), r1=ucm.agg_task_fic_buf_fill_lvl,  */
	0x01010392, 0x00010001, 0x00000488, 	/* (r1!=0), r1=ucm.sm_task_fic_buf_fill_lvl,  */
	0x01010393, 0x00010001, 0x00000489, 	/* (r1!=0), r1=ucm.in_prcs_tbl_fill_lvl,  */
	0x01000394, 0x00010001, 0x0000048a, 	/* (r1!=0), r1=ucm.cmpl_dir_curr_st,  */
	0x01010395, 0x00010001, 0x000d048b, 	/* (r1!=reset1), r1=ucm.ccfc_init_crd,  */
	0x01010396, 0x00010001, 0x000d048c, 	/* (r1!=reset1), r1=ucm.tcfc_init_crd,  */
	0x01010397, 0x00010001, 0x000a048d, 	/* (r1!=reset1), r1=ucm.qm_init_crd0,  */
	0x01010398, 0x00010001, 0x000f048e, 	/* (r1!=reset1), r1=ucm.tm_init_crd,  */
	0x01000399, 0x00010001, 0x0000048f, 	/* (r1!=0), r1=ucm.ysdm_length_mis,  */
	0x0100039a, 0x00010001, 0x00000490, 	/* (r1!=0), r1=ucm.usdm_length_mis,  */
	0x0100039b, 0x00010001, 0x00000491, 	/* (r1!=0), r1=ucm.dorq_length_mis,  */
	0x0100039c, 0x00010001, 0x00000492, 	/* (r1!=0), r1=ucm.pbf_length_mis,  */
	0x0100039d, 0x00010001, 0x00000493, 	/* (r1!=0), r1=ucm.rdif_length_mis,  */
	0x0100039e, 0x00010001, 0x00000494, 	/* (r1!=0), r1=ucm.tdif_length_mis,  */
	0x0100039f, 0x00010001, 0x00000495, 	/* (r1!=0), r1=ucm.muld_length_mis,  */
	0x010103a0, 0x00010001, 0x00000496, 	/* (r1!=0), r1=ucm.is_qm_p_fill_lvl,  */
	0x010103a1, 0x00010001, 0x00000497, 	/* (r1!=0), r1=ucm.is_qm_s_fill_lvl,  */
	0x010103a2, 0x00010001, 0x00000498, 	/* (r1!=0), r1=ucm.is_tm_fill_lvl,  */
	0x010103a3, 0x00010001, 0x00000499, 	/* (r1!=0), r1=ucm.is_storm_fill_lvl,  */
	0x010103a4, 0x00010001, 0x0000049a, 	/* (r1!=0), r1=ucm.is_ysdm_fill_lvl,  */
	0x010103a5, 0x00010001, 0x0000049b, 	/* (r1!=0), r1=ucm.is_usdm_fill_lvl,  */
	0x010103a6, 0x00010001, 0x0000049c, 	/* (r1!=0), r1=ucm.is_rdif_fill_lvl,  */
	0x010103a7, 0x00010001, 0x0000049d, 	/* (r1!=0), r1=ucm.is_tdif_fill_lvl,  */
	0x010103a8, 0x00010001, 0x0000049e, 	/* (r1!=0), r1=ucm.is_muld_fill_lvl,  */
	0x010103a9, 0x00010001, 0x0000049f, 	/* (r1!=0), r1=ucm.is_dorq_fill_lvl,  */
	0x010103aa, 0x00010001, 0x000004a0, 	/* (r1!=0), r1=ucm.is_pbf_fill_lvl,  */
	0x000003ab, 0x00010002, 0x000004a1, 	/* ((r1&~r2)!=0), r1=xsem.INT_STS_0, r2=xsem.INT_MASK_0,  */
	0x000003ac, 0x00010002, 0x000004a3, 	/* ((r1&~r2)!=0), r1=xsem.INT_STS_1, r2=xsem.INT_MASK_1,  */
	0x000003ad, 0x00010002, 0x000004a5, 	/* ((r1&~r2)!=0), r1=xsem.PRTY_STS, r2=xsem.PRTY_MASK,  */
	0x010203ae, 0x00010001, 0x000004a7, 	/* (r1!=0), r1=xsem.pf_err_vector,  */
	0x010103af, 0x00010001, 0x002a04a8, 	/* (r1!=0x1D), r1=xsem.foc_credit[1],  */
	0x010103b0, 0x00010001, 0x002b04a9, 	/* (r1!=0x3F), r1=xsem.foc_credit,  */
	0x010103b1, 0x00010001, 0x000d04aa, 	/* (r1!=1), r1=xsem.ext_pas_empty,  */
	0x010103b2, 0x00010001, 0x000d04ab, 	/* (r1!=1), r1=xsem.fic_empty[0:1],  */
	0x010103b3, 0x00010001, 0x000d04ac, 	/* (r1!=1), r1=xsem.slow_ext_store_empty,  */
	0x010103b4, 0x00010001, 0x000d04ad, 	/* (r1!=1), r1=xsem.slow_ext_load_empty,  */
	0x010103b5, 0x00010001, 0x000d04ae, 	/* (r1!=1), r1=xsem.sync_dbg_empty,  */
	0x010103b6, 0x00010001, 0x000004af, 	/* (r1!=0), r1=xsem.ext_store_if_full,  */
	0x010103b7, 0x00010001, 0x000004b0, 	/* (r1!=0), r1=xsem.ram_if_full,  */
	0x000003b8, 0x00010002, 0x000004b1, 	/* ((r1&~r2)!=0), r1=ysem.INT_STS_0, r2=ysem.INT_MASK_0,  */
	0x000003b9, 0x00010002, 0x000004b3, 	/* ((r1&~r2)!=0), r1=ysem.INT_STS_1, r2=ysem.INT_MASK_1,  */
	0x000003ba, 0x00010002, 0x000004b5, 	/* ((r1&~r2)!=0), r1=ysem.PRTY_STS, r2=ysem.PRTY_MASK,  */
	0x010203bb, 0x00010001, 0x000004b7, 	/* (r1!=0), r1=ysem.pf_err_vector,  */
	0x010103bc, 0x00010001, 0x002c04b8, 	/* (r1!=0xE), r1=ysem.foc_credit,  */
	0x010103bd, 0x00010001, 0x002d04b9, 	/* (r1!=0x1A), r1=ysem.foc_credit[1],  */
	0x010103be, 0x00010001, 0x002e04ba, 	/* (r1!=0xA), r1=ysem.foc_credit[2],  */
	0x010103bf, 0x00010001, 0x002404bb, 	/* (r1!=0x7), r1=ysem.foc_credit[3],  */
	0x010103c0, 0x00010001, 0x002f04bc, 	/* (r1!=0x26), r1=ysem.foc_credit[4],  */
	0x010103c1, 0x00010001, 0x003004bd, 	/* (r1!=0x28), r1=ysem.foc_credit[5],  */
	0x010103c2, 0x00010001, 0x000d04be, 	/* (r1!=1), r1=ysem.ext_pas_empty,  */
	0x010103c3, 0x00010001, 0x000d04bf, 	/* (r1!=1), r1=ysem.fic_empty[0:1],  */
	0x010103c4, 0x00010001, 0x000d04c0, 	/* (r1!=1), r1=ysem.slow_ext_store_empty,  */
	0x010103c5, 0x00010001, 0x000d04c1, 	/* (r1!=1), r1=ysem.slow_ext_load_empty,  */
	0x010103c6, 0x00010001, 0x000d04c2, 	/* (r1!=1), r1=ysem.sync_dbg_empty,  */
	0x010103c7, 0x00010001, 0x000004c3, 	/* (r1!=0), r1=ysem.ext_store_if_full,  */
	0x010103c8, 0x00010001, 0x000004c4, 	/* (r1!=0), r1=ysem.ram_if_full,  */
	0x000003c9, 0x00010002, 0x000004c5, 	/* ((r1&~r2)!=0), r1=psem.INT_STS_0, r2=psem.INT_MASK_0,  */
	0x000003ca, 0x00010002, 0x000004c7, 	/* ((r1&~r2)!=0), r1=psem.INT_STS_1, r2=psem.INT_MASK_1,  */
	0x000003cb, 0x00010002, 0x000004c9, 	/* ((r1&~r2)!=0), r1=psem.PRTY_STS, r2=psem.PRTY_MASK,  */
	0x010203cc, 0x00010001, 0x000004cb, 	/* (r1!=0), r1=psem.pf_err_vector,  */
	0x010103cd, 0x00010001, 0x003104cc, 	/* (r1!=0x17), r1=psem.foc_credit,  */
	0x010103ce, 0x00010001, 0x002904cd, 	/* (r1!=0x40), r1=psem.foc_credit[1],  */
	0x010103cf, 0x00010001, 0x000d04ce, 	/* (r1!=1), r1=psem.ext_pas_empty,  */
	0x010103d0, 0x00010001, 0x000d04cf, 	/* (r1!=1), r1=psem.fic_empty,  */
	0x010103d1, 0x00010001, 0x000d04d0, 	/* (r1!=1), r1=psem.slow_ext_store_empty,  */
	0x010103d2, 0x00010001, 0x000d04d1, 	/* (r1!=1), r1=psem.slow_ext_load_empty,  */
	0x010103d3, 0x00010001, 0x000d04d2, 	/* (r1!=1), r1=psem.sync_dbg_empty,  */
	0x010103d4, 0x00010001, 0x000004d3, 	/* (r1!=0), r1=psem.ext_store_if_full,  */
	0x010103d5, 0x00010001, 0x000004d4, 	/* (r1!=0), r1=psem.ram_if_full,  */
	0x000003d6, 0x00010002, 0x000004d5, 	/* ((r1&~r2)!=0), r1=tsem.INT_STS_0, r2=tsem.INT_MASK_0,  */
	0x000003d7, 0x00010002, 0x000004d7, 	/* ((r1&~r2)!=0), r1=tsem.INT_STS_1, r2=tsem.INT_MASK_1,  */
	0x000003d8, 0x00010002, 0x000004d9, 	/* ((r1&~r2)!=0), r1=tsem.PRTY_STS, r2=tsem.PRTY_MASK,  */
	0x010203d9, 0x00010001, 0x000004db, 	/* (r1!=0), r1=tsem.pf_err_vector,  */
	0x010103da, 0x00010001, 0x003204dc, 	/* (r1!=0x3C), r1=tsem.foc_credit[1],  */
	0x010103db, 0x00010001, 0x003304dd, 	/* (r1!=0x2C), r1=tsem.foc_credit,  */
	0x010103dc, 0x00010001, 0x000d04de, 	/* (r1!=1), r1=tsem.ext_pas_empty,  */
	0x010103dd, 0x00010001, 0x000d04df, 	/* (r1!=1), r1=tsem.fic_empty,  */
	0x010103de, 0x00010001, 0x000d04e0, 	/* (r1!=1), r1=tsem.slow_ext_store_empty,  */
	0x010103df, 0x00010001, 0x000d04e1, 	/* (r1!=1), r1=tsem.slow_ext_load_empty,  */
	0x010103e0, 0x00010001, 0x000d04e2, 	/* (r1!=1), r1=tsem.sync_dbg_empty,  */
	0x010103e1, 0x00010001, 0x000004e3, 	/* (r1!=0), r1=tsem.ext_store_if_full,  */
	0x010103e2, 0x00010001, 0x000004e4, 	/* (r1!=0), r1=tsem.ram_if_full,  */
	0x000003e3, 0x00010002, 0x000004e5, 	/* ((r1&~r2)!=0), r1=msem.INT_STS_0, r2=msem.INT_MASK_0,  */
	0x000003e4, 0x00010002, 0x000004e7, 	/* ((r1&~r2)!=0), r1=msem.INT_STS_1, r2=msem.INT_MASK_1,  */
	0x000003e5, 0x00010002, 0x000004e9, 	/* ((r1&~r2)!=0), r1=msem.PRTY_STS, r2=msem.PRTY_MASK,  */
	0x000003e6, 0x00010002, 0x000004eb, 	/* ((r1&~r2)!=0), r1=msem.PRTY_STS_H_0, r2=msem.PRTY_MASK_H_0,  */
	0x010203e7, 0x00010001, 0x000004ed, 	/* (r1!=0), r1=msem.pf_err_vector,  */
	0x010103e8, 0x00010001, 0x001704ee, 	/* (r1!=0x11), r1=msem.foc_credit[3],  */
	0x010103e9, 0x00010001, 0x002404ef, 	/* (r1!=0x7), r1=msem.foc_credit[4],  */
	0x010103ea, 0x00010001, 0x003104f0, 	/* (r1!=0x17), r1=msem.foc_credit[2],  */
	0x010103eb, 0x00010001, 0x003304f1, 	/* (r1!=0x2C), r1=msem.foc_credit[1],  */
	0x010103ec, 0x00010001, 0x000904f2, 	/* (r1!=0x5), r1=msem.foc_credit,  */
	0x010103ed, 0x00010001, 0x003404f3, 	/* (r1!=0x20), r1=msem.foc_credit[5],  */
	0x010103ee, 0x00010001, 0x000d04f4, 	/* (r1!=1), r1=msem.ext_pas_empty,  */
	0x010103ef, 0x00010001, 0x000d04f5, 	/* (r1!=1), r1=msem.fic_empty,  */
	0x010103f0, 0x00010001, 0x000d04f6, 	/* (r1!=1), r1=msem.slow_ext_store_empty,  */
	0x010103f1, 0x00010001, 0x000d04f7, 	/* (r1!=1), r1=msem.slow_ext_load_empty,  */
	0x010103f2, 0x00010001, 0x000d04f8, 	/* (r1!=1), r1=msem.sync_dbg_empty,  */
	0x010103f3, 0x00010001, 0x000004f9, 	/* (r1!=0), r1=msem.ext_store_if_full,  */
	0x010103f4, 0x00010001, 0x000004fa, 	/* (r1!=0), r1=msem.ram_if_full,  */
	0x000003f5, 0x00010002, 0x000004fb, 	/* ((r1&~r2)!=0), r1=usem.INT_STS_0, r2=usem.INT_MASK_0,  */
	0x000003f6, 0x00010002, 0x000004fd, 	/* ((r1&~r2)!=0), r1=usem.INT_STS_1, r2=usem.INT_MASK_1,  */
	0x000003f7, 0x00010002, 0x000004ff, 	/* ((r1&~r2)!=0), r1=usem.PRTY_STS, r2=usem.PRTY_MASK,  */
	0x010203f8, 0x00010001, 0x00000501, 	/* (r1!=0), r1=usem.pf_err_vector,  */
	0x010103f9, 0x00010001, 0x00090502, 	/* (r1!=0x5), r1=usem.foc_credit,  */
	0x010103fa, 0x00010001, 0x00350503, 	/* (r1!=0x32), r1=usem.foc_credit[1],  */
	0x010103fb, 0x00010001, 0x002e0504, 	/* (r1!=0xA), r1=usem.foc_credit[2],  */
	0x010103fc, 0x00010001, 0x00110505, 	/* (r1!=0x3), r1=usem.foc_credit[3],  */
	0x010103fd, 0x00010001, 0x00340506, 	/* (r1!=0x20), r1=usem.foc_credit[4],  */
	0x010103fe, 0x00010001, 0x000d0507, 	/* (r1!=1), r1=usem.ext_pas_empty,  */
	0x010103ff, 0x00010001, 0x000d0508, 	/* (r1!=1), r1=usem.fic_empty,  */
	0x01010400, 0x00010001, 0x000d0509, 	/* (r1!=1), r1=usem.slow_ext_store_empty,  */
	0x01010401, 0x00010001, 0x000d050a, 	/* (r1!=1), r1=usem.slow_ext_load_empty,  */
	0x01010402, 0x00010001, 0x000d050b, 	/* (r1!=1), r1=usem.sync_dbg_empty,  */
	0x01010403, 0x00010001, 0x0000050c, 	/* (r1!=0), r1=usem.ext_store_if_full,  */
	0x01010404, 0x00010001, 0x0000050d, 	/* (r1!=0), r1=usem.ram_if_full,  */
	0x00c30005, 	/* mode bb */
	0x04000405, 0x00020001, 0x0036050e, 	/* ((r1&~0x3f02)!=0), r1=pcie.PRTY_STS_H_0,  */
	0x01020406, 0x00010001, 0x0000050f, 	/* (r1!=0), r1=igu.pending_bits_status[0:8],  */
	0x01010407, 0x00010001, 0x00000510, 	/* (r1!=0), r1=igu.write_done_pending[0:8],  */
	0x00000408, 0x00010102, 0x00000511, 	/* ((r1&~r2)!=0), r1=cnig.INT_STS, r2=cnig.INT_MASK,  */
	0x00000409, 0x00010002, 0x00000514, 	/* ((r1&~r2)!=0), r1=cnig.PRTY_STS, r2=cnig.PRTY_MASK,  */
	0x0101040a, 0x00010001, 0x00340516, 	/* (r1!=reset1), r1=rss.tmld_credit,  */
	0x0100040b, 0x00010001, 0x00380517, 	/* (r1!=reset1), r1=pswrq2.BW_CREDIT,  */
	0x0102040c, 0x00010101, 0x00000518, 	/* (r1!=0), r1=pswhst.vf_disabled_error_valid,  */
	0x0102040d, 0x00010001, 0x0039051a, 	/* (r1!=reset1), r1=pswhst.dest_credits_avail,  */
	0x0102040e, 0x00010001, 0x0000051b, 	/* (r1!=0), r1=pglue_b.flr_request_vf_223_192,  */
	0x0102040f, 0x00010001, 0x0000051c, 	/* (r1!=0), r1=pglue_b.flr_request_vf_255_224,  */
	0x01000410, 0x00010001, 0x0000051d, 	/* (r1!=0), r1=pglue_b.incorrect_rcv_details,  */
	0x01020411, 0x00010001, 0x0000051e, 	/* (r1!=0), r1=pglue_b.was_error_vf_223_192,  */
	0x01020412, 0x00010001, 0x0000051f, 	/* (r1!=0), r1=pglue_b.was_error_vf_255_224,  */
	0x01020413, 0x00010001, 0x001a0520, 	/* (r1!=0xffffffff), r1=pglue_b.tags_159_128,  */
	0x01020414, 0x00010001, 0x001a0521, 	/* (r1!=0xffffffff), r1=pglue_b.tags_191_160,  */
	0x01020415, 0x00010001, 0x001a0522, 	/* (r1!=0xffffffff), r1=pglue_b.tags_223_192,  */
	0x01020416, 0x00010001, 0x001a0523, 	/* (r1!=0xffffffff), r1=pglue_b.tags_255_224,  */
	0x01010417, 0x00010001, 0x003a0524, 	/* (r1!=reset1), r1=tcfc.cduld_credit,  */
	0x01010418, 0x00010001, 0x003a0525, 	/* (r1!=reset1), r1=tcfc.cduwb_credit,  */
	0x01010419, 0x00010001, 0x003a0526, 	/* (r1!=reset1), r1=ccfc.cduld_credit,  */
	0x0101041a, 0x00010001, 0x000a0527, 	/* (r1!=reset1), r1=ccfc.cduwb_credit,  */
	0x0901041b, 0x00060001, 0x003b0528, 	/* ((r1[0]&0x3FFFFFC0)>>6)!=(((r1[0]&0xC0000000)>>30)|((r1[1]&0x3FFFFF)<<2)), r1=qm.PtrTblOther[0:63] width=2 access=WB,  */
	0x0a01041c, 0x00030001, 0x00410529, 	/* ((r1&0x30)>>4)!=(r1&0x03), r1=qm.PtrTblOther[0:63] width=2 access=WB,  */
	0x0100041d, 0x00010001, 0x0000052a, 	/* (r1!=0), r1=qm.QstatusTx_0[0:13],  */
	0x0100041e, 0x00010001, 0x0000052b, 	/* (r1!=0), r1=qm.QstatusOther_0[0:1],  */
	0x0c01041f, 0x00010003, 0x0000052c, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdLine[0:17], r2=qm.VoqInitCrdLine[0:17], r3=misc.port_mode,  */
	0x0b010420, 0x00010003, 0x0000052f, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[16], r2=qm.VoqInitCrdLine[16], r3=misc.port_mode,  */
	0x0b010421, 0x00010003, 0x00000532, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[0:7], r2=qm.VoqInitCrdLine[0:7], r3=misc.port_mode,  */
	0x0b010422, 0x00010003, 0x00000535, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[16], r2=qm.VoqInitCrdByte[16], r3=misc.port_mode,  */
	0x0b010423, 0x00010003, 0x00000538, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[0:7], r2=qm.VoqInitCrdByte[0:7], r3=misc.port_mode,  */
	0x0c010424, 0x00010003, 0x0000053b, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdByte[0:17], r2=qm.VoqInitCrdByte[0:17], r3=misc.port_mode,  */
	0x09010425, 0x00060001, 0x003b053e, 	/* ((r1[0]&0x3FFFFFC0)>>6)!=(((r1[0]&0xC0000000)>>30)|((r1[1]&0x3FFFFF)<<2)), r1=qm.PtrTblTx[0:447] width=2 access=WB,  */
	0x0a010426, 0x00030001, 0x0041053f, 	/* ((r1&0x30)>>4)!=(r1&0x03), r1=qm.PtrTblTx[0:447] width=2 access=WB,  */
	0x01010427, 0x00010001, 0x00260540, 	/* (r1!=8190), r1=brb.wc_empty_0[0:3],  */
	0x01010428, 0x00010001, 0x00110541, 	/* (r1!=reset1), r1=brb.rc_eop_empty,  */
	0x01010429, 0x00010001, 0x000b0542, 	/* (r1!=2), r1=brb.wc_status_0[0:3] width=3 access=WB,  */
	0x0000042a, 0x00010302, 0x00000543, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS, r2=nig.PRTY_MASK,  */
	0x0000042b, 0x00010202, 0x00000548, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_0, r2=btb.INT_MASK_0,  */
	0x0000042c, 0x00010202, 0x0000054c, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_4, r2=btb.INT_MASK_4,  */
	0x0101042d, 0x00010001, 0x00300550, 	/* (r1!=reset1), r1=xcm.fic_init_crd,  */
	0x0100042e, 0x00010001, 0x00000551, 	/* (r1!=0), r1=xcm.msdm_length_mis,  */
	0x0101042f, 0x00010001, 0x00000552, 	/* (r1!=0), r1=xcm.is_msdm_fill_lvl,  */
	0x01010430, 0x00010001, 0x00000553, 	/* (r1!=0), r1=xcm.is_ysem_fill_lvl,  */
	0x01010431, 0x00010001, 0x00000554, 	/* (r1!=0), r1=xcm.qm_act_st_cnt[0:447],  */
	0x01010432, 0x00010001, 0x00440555, 	/* (r1!=reset1), r1=ycm.fic_init_crd,  */
	0x01000433, 0x00010001, 0x00000556, 	/* (r1!=0), r1=ycm.msdm_length_mis,  */
	0x01010434, 0x00010001, 0x00000557, 	/* (r1!=0), r1=ycm.is_msdm_fill_lvl,  */
	0x01010435, 0x00010001, 0x00300558, 	/* (r1!=reset1), r1=pcm.fic_init_crd,  */
	0x01000436, 0x00010001, 0x00000559, 	/* (r1!=0), r1=pcm.psdm_length_mis,  */
	0x01010437, 0x00010001, 0x0000055a, 	/* (r1!=0), r1=pcm.is_psdm_fill_lvl,  */
	0x01010438, 0x00010001, 0x0045055b, 	/* (r1!=reset1), r1=tcm.fic_init_crd,  */
	0x01000439, 0x00010001, 0x0000055c, 	/* (r1!=0), r1=tcm.tsdm_length_mis,  */
	0x0101043a, 0x00010001, 0x0000055d, 	/* (r1!=0), r1=tcm.is_tsdm_fill_lvl,  */
	0x0101043b, 0x00010001, 0x0045055e, 	/* (r1!=reset1), r1=mcm.fic_init_crd,  */
	0x0100043c, 0x00010001, 0x0000055f, 	/* (r1!=0), r1=mcm.msdm_length_mis,  */
	0x0101043d, 0x00010001, 0x00000560, 	/* (r1!=0), r1=mcm.is_msdm_fill_lvl,  */
	0x0101043e, 0x00010001, 0x00000561, 	/* (r1!=0), r1=mcm.is_ysem_fill_lvl,  */
	0x0101043f, 0x00010001, 0x00440562, 	/* (r1!=reset1), r1=ucm.fic_init_crd,  */
	0x01020440, 0x00010001, 0x00000563, 	/* (r1!=0), r1=xsem.vf_err_vector width=4 access=WB,  */
	0x01020441, 0x00010001, 0x00000564, 	/* (r1!=0), r1=ysem.vf_err_vector width=4 access=WB,  */
	0x01020442, 0x00010001, 0x00000565, 	/* (r1!=0), r1=psem.vf_err_vector width=4 access=WB,  */
	0x01020443, 0x00010001, 0x00000566, 	/* (r1!=0), r1=tsem.vf_err_vector width=4 access=WB,  */
	0x01020444, 0x00010001, 0x00000567, 	/* (r1!=0), r1=msem.vf_err_vector width=4 access=WB,  */
	0x01020445, 0x00010001, 0x00000568, 	/* (r1!=0), r1=usem.vf_err_vector width=4 access=WB,  */
	0x007e001b, 	/* mode k2 */
	0x00000446, 0x00010002, 0x00000569, 	/* ((r1&~r2)!=0), r1=pcie.PRTY_STS_H_0, r2=pcie.PRTY_MASK_H_0,  */
	0x01020447, 0x00010001, 0x0000056b, 	/* (r1!=0), r1=igu.pending_bits_status[0:11],  */
	0x01010448, 0x00010001, 0x0000056c, 	/* (r1!=0), r1=igu.write_done_pending[0:11],  */
	0x01010449, 0x00010001, 0x0034056d, 	/* (r1!=reset1), r1=rss.tmld_credit,  */
	0x0100044a, 0x00010001, 0x0038056e, 	/* (r1!=reset1), r1=pswrq2.BW_CREDIT,  */
	0x0102044b, 0x00010101, 0x0000056f, 	/* (r1!=0), r1=pswhst.vf_disabled_error_valid,  */
	0x0102044c, 0x00010001, 0x00390571, 	/* (r1!=reset1), r1=pswhst.dest_credits_avail,  */
	0x0101044d, 0x00010001, 0x003a0572, 	/* (r1!=reset1), r1=tcfc.cduld_credit,  */
	0x0101044e, 0x00010001, 0x003a0573, 	/* (r1!=reset1), r1=tcfc.cduwb_credit,  */
	0x0101044f, 0x00010001, 0x003a0574, 	/* (r1!=reset1), r1=ccfc.cduld_credit,  */
	0x01010450, 0x00010001, 0x000a0575, 	/* (r1!=reset1), r1=ccfc.cduwb_credit,  */
	0x01000451, 0x00010001, 0x00000576, 	/* (r1!=0), r1=qm.QstatusTx_0[0:15],  */
	0x01000452, 0x00010001, 0x00000577, 	/* (r1!=0), r1=qm.QstatusOther_0[0:3],  */
	0x0b010453, 0x00010003, 0x00000578, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[0:7], r2=qm.VoqInitCrdLine[0:7], r3=misc.port_mode,  */
	0x0b010454, 0x00010003, 0x0000057b, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[16], r2=qm.VoqInitCrdLine[16], r3=misc.port_mode,  */
	0x0c010455, 0x00010003, 0x0000057e, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdLine[0:19], r2=qm.VoqInitCrdLine[0:19], r3=misc.port_mode,  */
	0x0c010456, 0x00010003, 0x00000581, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdByte[0:19], r2=qm.VoqInitCrdByte[0:19], r3=misc.port_mode,  */
	0x0b010457, 0x00010003, 0x00000584, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[16], r2=qm.VoqInitCrdByte[16], r3=misc.port_mode,  */
	0x0b010458, 0x00010003, 0x00000587, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[0:7], r2=qm.VoqInitCrdByte[0:7], r3=misc.port_mode,  */
	0x01010459, 0x00010001, 0x0026058a, 	/* (r1!=8190), r1=brb.wc_empty_0[0:7],  */
	0x0101045a, 0x00010001, 0x0003058b, 	/* (r1!=reset1), r1=brb.rc_eop_empty,  */
	0x0101045b, 0x00010001, 0x000b058c, 	/* (r1!=2), r1=brb.wc_status_0[0:7] width=3 access=WB,  */
	0x0000045c, 0x00010202, 0x0000058d, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_0, r2=btb.INT_MASK_0,  */
	0x0000045d, 0x00010202, 0x00000591, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_4, r2=btb.INT_MASK_4,  */
	0x0101045e, 0x00010001, 0x00300595, 	/* (r1!=reset1), r1=xcm.fic_init_crd,  */
	0x0100045f, 0x00010001, 0x00000596, 	/* (r1!=0), r1=xcm.msdm_length_mis,  */
	0x01010460, 0x00010001, 0x00000597, 	/* (r1!=0), r1=xcm.is_msdm_fill_lvl,  */
	0x01010461, 0x00010001, 0x00000598, 	/* (r1!=0), r1=xcm.is_ysem_fill_lvl,  */
	0x01010462, 0x00010001, 0x00440599, 	/* (r1!=reset1), r1=ycm.fic_init_crd,  */
	0x01000463, 0x00010001, 0x0000059a, 	/* (r1!=0), r1=ycm.msdm_length_mis,  */
	0x01010464, 0x00010001, 0x0000059b, 	/* (r1!=0), r1=ycm.is_msdm_fill_lvl,  */
	0x01010465, 0x00010001, 0x0030059c, 	/* (r1!=reset1), r1=pcm.fic_init_crd,  */
	0x01000466, 0x00010001, 0x0000059d, 	/* (r1!=0), r1=pcm.psdm_length_mis,  */
	0x01010467, 0x00010001, 0x0000059e, 	/* (r1!=0), r1=pcm.is_psdm_fill_lvl,  */
	0x01010468, 0x00010001, 0x0045059f, 	/* (r1!=reset1), r1=tcm.fic_init_crd,  */
	0x01000469, 0x00010001, 0x000005a0, 	/* (r1!=0), r1=tcm.tsdm_length_mis,  */
	0x0101046a, 0x00010001, 0x000005a1, 	/* (r1!=0), r1=tcm.is_tsdm_fill_lvl,  */
	0x0101046b, 0x00010001, 0x004505a2, 	/* (r1!=reset1), r1=mcm.fic_init_crd,  */
	0x0100046c, 0x00010001, 0x000005a3, 	/* (r1!=0), r1=mcm.msdm_length_mis,  */
	0x0101046d, 0x00010001, 0x000005a4, 	/* (r1!=0), r1=mcm.is_msdm_fill_lvl,  */
	0x0101046e, 0x00010001, 0x000005a5, 	/* (r1!=0), r1=mcm.is_ysem_fill_lvl,  */
	0x0101046f, 0x00010001, 0x004405a6, 	/* (r1!=reset1), r1=ucm.fic_init_crd,  */
	0x003f0033, 	/* mode asic */
	0x03010470, 0x00000002, 0x000005a7, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_7, r2=pswrq2.max_srs_vq7,  */
	0x03010471, 0x00000002, 0x000005a9, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_8, r2=pswrq2.max_srs_vq8,  */
	0x03010472, 0x00000002, 0x000005ab, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_10, r2=pswrq2.max_srs_vq10,  */
	0x03010473, 0x00000002, 0x000005ad, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_13, r2=pswrq2.max_srs_vq13,  */
	0x03010474, 0x00000002, 0x000005af, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_14, r2=pswrq2.max_srs_vq14,  */
	0x03010475, 0x00000002, 0x000005b1, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_20, r2=pswrq2.max_srs_vq20,  */
	0x03010476, 0x00000002, 0x000005b3, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_25, r2=pswrq2.max_srs_vq25,  */
	0x03010477, 0x00000002, 0x000005b5, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_27, r2=pswrq2.max_srs_vq27,  */
	0x03010478, 0x00000002, 0x000005b7, 	/* (r1!=r2), r1=pswrq2.sr_cnt_per_vq_31, r2=pswrq2.max_srs_vq31,  */
	0x03010479, 0x00000002, 0x000005b9, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_7, r2=pswrq2.max_blks_vq7,  */
	0x0301047a, 0x00000002, 0x000005bb, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_8, r2=pswrq2.max_blks_vq8,  */
	0x0301047b, 0x00000002, 0x000005bd, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_10, r2=pswrq2.max_blks_vq10,  */
	0x0301047c, 0x00000002, 0x000005bf, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_13, r2=pswrq2.max_blks_vq13,  */
	0x0301047d, 0x00000002, 0x000005c1, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_14, r2=pswrq2.max_blks_vq14,  */
	0x0301047e, 0x00000002, 0x000005c3, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_20, r2=pswrq2.max_blks_vq20,  */
	0x0301047f, 0x00000002, 0x000005c5, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_25, r2=pswrq2.max_blks_vq25,  */
	0x03010480, 0x00000002, 0x000005c7, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_27, r2=pswrq2.max_blks_vq27,  */
	0x03010481, 0x00000002, 0x000005c9, 	/* (r1!=r2), r1=pswrq2.blk_cnt_per_vq_31, r2=pswrq2.max_blks_vq31,  */
	0x0d020482, 0x00010001, 0x004605cb, 	/* (r1&0x3E1), r1=tm.INT_STS_1,  */
	0x00000483, 0x00010002, 0x000005cc, 	/* ((r1&~r2)!=0), r1=bmb.PRTY_STS_H_0, r2=bmb.PRTY_MASK_H_0,  */
	0x00000484, 0x00010002, 0x000005ce, 	/* ((r1&~r2)!=0), r1=bmb.PRTY_STS_H_1, r2=bmb.PRTY_MASK_H_1,  */
	0x0006002d, 	/* mode e5 */
	0x01010485, 0x00010001, 0x000f05d0, 	/* (r1!=4), r1=brb.ll_arb_empty,  */
	0x01010486, 0x00010001, 0x000f05d1, 	/* (r1!=4), r1=btb.ll_arb_empty,  */
	0x00030031, 	/* mode !asic */
	0x0d020487, 0x00010001, 0x004705d2, 	/* (r1&0x381), r1=tm.INT_STS_1,  */
	0x00810003, 	/* mode !bb */
	0x01000488, 0x00010001, 0x000005d3, 	/* (r1!=0), r1=pglcs.syncfifo_pop_underflow,  */
	0x01000489, 0x00010001, 0x000005d4, 	/* (r1!=0), r1=pglcs.syncfifo_push_overflow,  */
	0x0101048a, 0x00010001, 0x000005d5, 	/* (r1!=0), r1=pglcs.tx_syncfifo_pop_status,  */
	0x0000048b, 0x00010102, 0x000005d6, 	/* ((r1&~r2)!=0), r1=cnig.INT_STS, r2=cnig.INT_MASK,  */
	0x0000048c, 0x00010002, 0x000005d9, 	/* ((r1&~r2)!=0), r1=cnig.PRTY_STS, r2=cnig.PRTY_MASK,  */
	0x0100048d, 0x00010001, 0x000005db, 	/* (r1!=0), r1=pglue_b.syncfifo_push_overflow,  */
	0x0100048e, 0x00010001, 0x000005dc, 	/* (r1!=0), r1=pglue_b.syncfifo_pop_underflow,  */
	0x0101048f, 0x00010001, 0x000005dd, 	/* (r1!=0), r1=pglue_b.rxh_syncfifo_pop_status,  */
	0x01010490, 0x00010101, 0x000005de, 	/* (r1!=0), r1=pglue_b.rxd_syncfifo_pop_status,  */
	0x09010491, 0x00060001, 0x003b05e0, 	/* ((r1[0]&0x3FFFFFC0)>>6)!=(((r1[0]&0xC0000000)>>30)|((r1[1]&0x3FFFFF)<<2)), r1=qm.PtrTblOther[0:127] width=2 access=WB,  */
	0x0a010492, 0x00030001, 0x004105e1, 	/* ((r1&0x30)>>4)!=(r1&0x03), r1=qm.PtrTblOther[0:127] width=2 access=WB,  */
	0x09010493, 0x00060001, 0x003b05e2, 	/* ((r1[0]&0x3FFFFFC0)>>6)!=(((r1[0]&0xC0000000)>>30)|((r1[1]&0x3FFFFF)<<2)), r1=qm.PtrTblTx[0:511] width=2 access=WB,  */
	0x0a010494, 0x00030001, 0x004105e3, 	/* ((r1&0x30)>>4)!=(r1&0x03), r1=qm.PtrTblTx[0:511] width=2 access=WB,  */
	0x01010495, 0x00010001, 0x000005e4, 	/* (r1!=0), r1=brb.mac2_tc_occupancy_0,  */
	0x01010496, 0x00010001, 0x000005e5, 	/* (r1!=0), r1=brb.mac2_tc_occupancy_1,  */
	0x01010497, 0x00010001, 0x000005e6, 	/* (r1!=0), r1=brb.mac2_tc_occupancy_2,  */
	0x01010498, 0x00010001, 0x000005e7, 	/* (r1!=0), r1=brb.mac2_tc_occupancy_3,  */
	0x01010499, 0x00010001, 0x000005e8, 	/* (r1!=0), r1=brb.mac2_tc_occupancy_4,  */
	0x0101049a, 0x00010001, 0x000005e9, 	/* (r1!=0), r1=brb.mac3_tc_occupancy_0,  */
	0x0101049b, 0x00010001, 0x000005ea, 	/* (r1!=0), r1=brb.mac3_tc_occupancy_1,  */
	0x0101049c, 0x00010001, 0x000005eb, 	/* (r1!=0), r1=brb.mac3_tc_occupancy_2,  */
	0x0101049d, 0x00010001, 0x000005ec, 	/* (r1!=0), r1=brb.mac3_tc_occupancy_3,  */
	0x0101049e, 0x00010001, 0x000005ed, 	/* (r1!=0), r1=brb.mac3_tc_occupancy_4,  */
	0x0000049f, 0x00010002, 0x000005ee, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_6, r2=nig.INT_MASK_6,  */
	0x000204a0, 0x00010202, 0x000005f0, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_7, r2=nig.INT_MASK_7,  */
	0x000004a1, 0x00010002, 0x000005f4, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_8, r2=nig.INT_MASK_8,  */
	0x000204a2, 0x00010202, 0x000005f6, 	/* ((r1&~r2)!=0), r1=nig.INT_STS_9, r2=nig.INT_MASK_9,  */
	0x000004a3, 0x00010302, 0x000005fa, 	/* ((r1&~r2)!=0), r1=nig.PRTY_STS, r2=nig.PRTY_MASK,  */
	0x060004a4, 0x00020001, 0x000d05ff, 	/* ((r1&0x00000001)!=0), r1=nws.INT_STS_0,  */
	0x000004a5, 0x00010002, 0x00000600, 	/* ((r1&~r2)!=0), r1=nws.PRTY_STS_H_0, r2=nws.PRTY_MASK_H_0,  */
	0x060004a6, 0x00020001, 0x00480602, 	/* ((r1&0x000001FE)!=0), r1=nwm.INT_STS,  */
	0x060004a7, 0x00020001, 0x000d0603, 	/* ((r1&0x00000001)!=0), r1=nwm.INT_STS,  */
	0x010104a8, 0x00010001, 0x001f0604, 	/* (r1!=255), r1=btb.rc_pkt_empty_4,  */
	0x010104a9, 0x00010001, 0x001f0605, 	/* (r1!=255), r1=btb.rc_pkt_empty_5,  */
	0x010104aa, 0x00010001, 0x001f0606, 	/* (r1!=255), r1=btb.rc_pkt_empty_6,  */
	0x010104ab, 0x00010001, 0x001f0607, 	/* (r1!=255), r1=btb.rc_pkt_empty_7,  */
	0x010104ac, 0x00010001, 0x00000608, 	/* (r1!=0), r1=xcm.qm_act_st_cnt[0:511],  */
	0x010204ad, 0x00010001, 0x00000609, 	/* (r1!=0), r1=xsem.vf_err_vector width=8 access=WB,  */
	0x010204ae, 0x00010001, 0x0000060a, 	/* (r1!=0), r1=ysem.vf_err_vector width=8 access=WB,  */
	0x010204af, 0x00010001, 0x0000060b, 	/* (r1!=0), r1=psem.vf_err_vector width=8 access=WB,  */
	0x010204b0, 0x00010001, 0x0000060c, 	/* (r1!=0), r1=tsem.vf_err_vector width=8 access=WB,  */
	0x010204b1, 0x00010001, 0x0000060d, 	/* (r1!=0), r1=msem.vf_err_vector width=8 access=WB,  */
	0x010204b2, 0x00010001, 0x0000060e, 	/* (r1!=0), r1=usem.vf_err_vector width=8 access=WB,  */
	0x00060017, 	/* mode bb|k2 */
	0x010104b3, 0x00010001, 0x000b060f, 	/* (r1!=2), r1=brb.ll_arb_empty,  */
	0x010104b4, 0x00010001, 0x000b0610, 	/* (r1!=2), r1=btb.ll_arb_empty,  */
	0x016b0015, 	/* mode !(bb|k2) */
	0x010004b5, 0x00010001, 0x00000611, 	/* (r1!=0), r1=dorq.iedpm_timer_expir_abort_cnt,  */
	0x010204b6, 0x00010001, 0x00000612, 	/* (r1!=0), r1=igu.pending_bits_status[0:15],  */
	0x010104b7, 0x00010001, 0x00000613, 	/* (r1!=0), r1=igu.write_done_pending[0:15],  */
	0x010104b8, 0x00010001, 0x003e0614, 	/* (r1!=reset1), r1=rss.tmld_credit,  */
	0x010204b9, 0x00010001, 0x00000615, 	/* (r1!=0), r1=rss.fifo_full_status1,  */
	0x010204ba, 0x00010001, 0x004a0616, 	/* (r1!=0x1fff), r1=rss.fifo_empty_status1,  */
	0x010204bb, 0x00010001, 0x00000617, 	/* (r1!=0), r1=rss.counter_status1,  */
	0x010104bc, 0x00010001, 0x00000618, 	/* (r1!=0), r1=rss.state_machines1,  */
	0x010004bd, 0x00010001, 0x004b0619, 	/* (r1!=reset1), r1=pswrq2.BW_CREDIT,  */
	0x010004be, 0x00010001, 0x0000061a, 	/* (r1!=0), r1=pswrd2.almost_full_16[0:1],  */
	0x010204bf, 0x00010001, 0x004c061b, 	/* (r1!=reset1), r1=pswhst.dest_credits_avail,  */
	0x010104c0, 0x00010001, 0x0029061c, 	/* (r1!=reset1), r1=tcfc.cduld_credit,  */
	0x010104c1, 0x00010001, 0x0029061d, 	/* (r1!=reset1), r1=tcfc.cduwb_credit,  */
	0x010104c2, 0x00010001, 0x0029061e, 	/* (r1!=reset1), r1=ccfc.cduld_credit,  */
	0x010104c3, 0x00010001, 0x0029061f, 	/* (r1!=reset1), r1=ccfc.cduwb_credit,  */
	0x0b0104c4, 0x00010003, 0x00000620, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[0:7], r2=qm.VoqInitCrdLine[0:7], r3=misc.port_mode,  */
	0x0c0104c5, 0x00010003, 0x00000623, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdLine[0:35], r2=qm.VoqInitCrdLine[0:35], r3=misc.port_mode,  */
	0x0b0104c6, 0x00010003, 0x00000626, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdLine[16], r2=qm.VoqInitCrdLine[16], r3=misc.port_mode,  */
	0x0c0104c7, 0x00010003, 0x00000629, 	/* (r1!=r2&&r3>0), r1=qm.VoqCrdByte[0:35], r2=qm.VoqInitCrdByte[0:35], r3=misc.port_mode,  */
	0x0b0104c8, 0x00010003, 0x0000062c, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[16], r2=qm.VoqInitCrdByte[16], r3=misc.port_mode,  */
	0x0b0104c9, 0x00010003, 0x0000062f, 	/* (r1!=r2&&r3==0), r1=qm.VoqCrdByte[0:7], r2=qm.VoqInitCrdByte[0:7], r3=misc.port_mode,  */
	0x010104ca, 0x00010001, 0x000d0632, 	/* (r1!=reset1), r1=brb.rc_eop_empty,  */
	0x010104cb, 0x00010001, 0x00000633, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq20,  */
	0x010104cc, 0x00010001, 0x00000634, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq20,  */
	0x010104cd, 0x00010001, 0x00000635, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq20,  */
	0x010104ce, 0x00010001, 0x00000636, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq21,  */
	0x010104cf, 0x00010001, 0x00000637, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq21,  */
	0x010104d0, 0x00010001, 0x00000638, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq21,  */
	0x010104d1, 0x00010001, 0x00000639, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq22,  */
	0x010104d2, 0x00010001, 0x0000063a, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq22,  */
	0x010104d3, 0x00010001, 0x0000063b, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq22,  */
	0x010104d4, 0x00010001, 0x0000063c, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq23,  */
	0x010104d5, 0x00010001, 0x0000063d, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq23,  */
	0x010104d6, 0x00010001, 0x0000063e, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq23,  */
	0x010104d7, 0x00010001, 0x0000063f, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq24,  */
	0x010104d8, 0x00010001, 0x00000640, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq24,  */
	0x010104d9, 0x00010001, 0x00000641, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq24,  */
	0x010104da, 0x00010001, 0x00000642, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq25,  */
	0x010104db, 0x00010001, 0x00000643, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq25,  */
	0x010104dc, 0x00010001, 0x00000644, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq25,  */
	0x010104dd, 0x00010001, 0x00000645, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq26,  */
	0x010104de, 0x00010001, 0x00000646, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq26,  */
	0x010104df, 0x00010001, 0x00000647, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq26,  */
	0x010104e0, 0x00010001, 0x00000648, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq27,  */
	0x010104e1, 0x00010001, 0x00000649, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq27,  */
	0x010104e2, 0x00010001, 0x0000064a, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq27,  */
	0x010104e3, 0x00010001, 0x0000064b, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq28,  */
	0x010104e4, 0x00010001, 0x0000064c, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq28,  */
	0x010104e5, 0x00010001, 0x0000064d, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq28,  */
	0x010104e6, 0x00010001, 0x0000064e, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq29,  */
	0x010104e7, 0x00010001, 0x0000064f, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq29,  */
	0x010104e8, 0x00010001, 0x00000650, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq29,  */
	0x010104e9, 0x00010001, 0x00000651, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq30,  */
	0x010104ea, 0x00010001, 0x00000652, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq30,  */
	0x010104eb, 0x00010001, 0x00000653, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq30,  */
	0x010104ec, 0x00010001, 0x00000654, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq31,  */
	0x010104ed, 0x00010001, 0x00000655, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq31,  */
	0x010104ee, 0x00010001, 0x00000656, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq31,  */
	0x010104ef, 0x00010001, 0x00000657, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq32,  */
	0x010104f0, 0x00010001, 0x00000658, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq32,  */
	0x010104f1, 0x00010001, 0x00000659, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq32,  */
	0x010104f2, 0x00010001, 0x0000065a, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq33,  */
	0x010104f3, 0x00010001, 0x0000065b, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq33,  */
	0x010104f4, 0x00010001, 0x0000065c, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq33,  */
	0x010104f5, 0x00010001, 0x0000065d, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq34,  */
	0x010104f6, 0x00010001, 0x0000065e, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq34,  */
	0x010104f7, 0x00010001, 0x0000065f, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq34,  */
	0x010104f8, 0x00010001, 0x00000660, 	/* (r1!=0), r1=pbf.ycmd_qs_cmd_cnt_voq35,  */
	0x010104f9, 0x00010001, 0x00000661, 	/* (r1!=0), r1=pbf.ycmd_qs_occupancy_voq35,  */
	0x010104fa, 0x00010001, 0x00000662, 	/* (r1!=0), r1=pbf.btb_allocated_blocks_voq35,  */
	0x000004fb, 0x00010202, 0x00000663, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_0, r2=btb.INT_MASK_0,  */
	0x000004fc, 0x00010202, 0x00000667, 	/* ((r1&~r2)!=0), r1=btb.INT_STS_4, r2=btb.INT_MASK_4,  */
	0x010104fd, 0x00010001, 0x0000066b, 	/* (r1!=0), r1=xcm.ext_rd_fill_lvl,  */
	0x010104fe, 0x00010001, 0x000a066c, 	/* (r1!=reset1), r1=xcm.fic_init_crd,  */
	0x010004ff, 0x00010001, 0x0000066d, 	/* (r1!=0), r1=xcm.msdm_length_mis,  */
	0x01010500, 0x00010001, 0x0000066e, 	/* (r1!=0), r1=xcm.is_msdm_fill_lvl,  */
	0x01010501, 0x00010001, 0x0000066f, 	/* (r1!=0), r1=xcm.is_ysem_fill_lvl,  */
	0x01010502, 0x00010001, 0x00000670, 	/* (r1!=0), r1=ycm.ext_rd_fill_lvl,  */
	0x01010503, 0x00010001, 0x000a0671, 	/* (r1!=reset1), r1=ycm.fic_init_crd,  */
	0x01000504, 0x00010001, 0x00000672, 	/* (r1!=0), r1=ycm.msdm_length_mis,  */
	0x01010505, 0x00010001, 0x00000673, 	/* (r1!=0), r1=ycm.is_msdm_fill_lvl,  */
	0x01010506, 0x00010001, 0x00000674, 	/* (r1!=0), r1=pcm.ext_rd_fill_lvl,  */
	0x01010507, 0x00010001, 0x000a0675, 	/* (r1!=reset1), r1=pcm.fic_init_crd,  */
	0x01000508, 0x00010001, 0x00000676, 	/* (r1!=0), r1=pcm.psdm_length_mis,  */
	0x01010509, 0x00010001, 0x00000677, 	/* (r1!=0), r1=pcm.is_psdm_fill_lvl,  */
	0x0101050a, 0x00010001, 0x00000678, 	/* (r1!=0), r1=pcm.is_ypld_fill_lvl,  */
	0x0101050b, 0x00010001, 0x00000679, 	/* (r1!=0), r1=tcm.ext_rd_fill_lvl,  */
	0x0101050c, 0x00010001, 0x000a067a, 	/* (r1!=reset1), r1=tcm.fic_init_crd,  */
	0x0100050d, 0x00010001, 0x0000067b, 	/* (r1!=0), r1=tcm.tsdm_length_mis,  */
	0x0101050e, 0x00010001, 0x0000067c, 	/* (r1!=0), r1=tcm.is_tsdm_fill_lvl,  */
	0x0101050f, 0x00010001, 0x0000067d, 	/* (r1!=0), r1=tcm.is_psdm_fill_lvl,  */
	0x01010510, 0x00010001, 0x0000067e, 	/* (r1!=0), r1=tcm.is_msdm_fill_lvl,  */
	0x01010511, 0x00010001, 0x0000067f, 	/* (r1!=0), r1=tcm.is_ptld_fill_lvl,  */
	0x01010512, 0x00010001, 0x00000680, 	/* (r1!=0), r1=mcm.ext_rd_fill_lvl,  */
	0x01010513, 0x00010001, 0x000a0681, 	/* (r1!=reset1), r1=mcm.fic_init_crd,  */
	0x01010514, 0x00010001, 0x00000682, 	/* (r1!=0), r1=mcm.is_tsdm_fill_lvl,  */
	0x01010515, 0x00010001, 0x00000683, 	/* (r1!=0), r1=mcm.is_psdm_fill_lvl,  */
	0x01000516, 0x00010001, 0x00000684, 	/* (r1!=0), r1=mcm.msdm_length_mis,  */
	0x01010517, 0x00010001, 0x00000685, 	/* (r1!=0), r1=mcm.is_msdm_fill_lvl,  */
	0x01010518, 0x00010001, 0x00000686, 	/* (r1!=0), r1=mcm.is_ysem_fill_lvl,  */
	0x01010519, 0x00010001, 0x00000687, 	/* (r1!=0), r1=ucm.ext_rd_fill_lvl,  */
	0x0101051a, 0x00010001, 0x000a0688, 	/* (r1!=reset1), r1=ucm.fic_init_crd,  */
	0x0101051b, 0x00010001, 0x00000689, 	/* (r1!=0), r1=ucm.is_ysem_fill_lvl,  */
	0x0000051c, 0x00010002, 0x0000068a, 	/* ((r1&~r2)!=0), r1=xsem.INT_STS_2, r2=xsem.INT_MASK_2,  */
	0x0102051d, 0x00010001, 0x0000068c, 	/* (r1!=0), r1=xsem.thread_error_low,  */
	0x0102051e, 0x00010001, 0x0000068d, 	/* (r1!=0), r1=xsem.thread_error_high,  */
	0x0000051f, 0x00010002, 0x0000068e, 	/* ((r1&~r2)!=0), r1=ysem.INT_STS_2, r2=ysem.INT_MASK_2,  */
	0x01020520, 0x00010001, 0x00000690, 	/* (r1!=0), r1=ysem.thread_error_low,  */
	0x01020521, 0x00010001, 0x00000691, 	/* (r1!=0), r1=ysem.thread_error_high,  */
	0x00000522, 0x00010002, 0x00000692, 	/* ((r1&~r2)!=0), r1=psem.INT_STS_2, r2=psem.INT_MASK_2,  */
	0x01020523, 0x00010001, 0x00000694, 	/* (r1!=0), r1=psem.thread_error_low,  */
	0x01020524, 0x00010001, 0x00000695, 	/* (r1!=0), r1=psem.thread_error_high,  */
	0x00000525, 0x00010002, 0x00000696, 	/* ((r1&~r2)!=0), r1=tsem.INT_STS_2, r2=tsem.INT_MASK_2,  */
	0x01020526, 0x00010001, 0x00000698, 	/* (r1!=0), r1=tsem.thread_error_low,  */
	0x01020527, 0x00010001, 0x00000699, 	/* (r1!=0), r1=tsem.thread_error_high,  */
	0x00000528, 0x00010002, 0x0000069a, 	/* ((r1&~r2)!=0), r1=msem.INT_STS_2, r2=msem.INT_MASK_2,  */
	0x01020529, 0x00010001, 0x0000069c, 	/* (r1!=0), r1=msem.thread_error_low,  */
	0x0102052a, 0x00010001, 0x0000069d, 	/* (r1!=0), r1=msem.thread_error_high,  */
	0x0000052b, 0x00010002, 0x0000069e, 	/* ((r1&~r2)!=0), r1=usem.INT_STS_2, r2=usem.INT_MASK_2,  */
	0x0102052c, 0x00010001, 0x000006a0, 	/* (r1!=0), r1=usem.thread_error_low,  */
	0x0102052d, 0x00010001, 0x000006a1, 	/* (r1!=0), r1=usem.thread_error_high,  */
	0x00210047, 	/* mode !(k2|e5) */
	0x0600052e, 0x00020001, 0x004d06a2, 	/* ((r1&0x0FD010)!=0), r1=pglcs.pgl_cs.uc_err_status,  */
	0x0602052f, 0x00020001, 0x004f06a3, 	/* ((r1&0x100000)!=0), r1=pglcs.pgl_cs.uc_err_status,  */
	0x06020530, 0x00020001, 0x005106a4, 	/* ((r1&0x2000)!=0), r1=pglcs.pgl_cs.uc_err_status,  */
	0x01020531, 0x00010001, 0x000006a5, 	/* (r1!=0), r1=pglcs.pgl_cs.header_log1[0:3],  */
	0x04000532, 0x00020001, 0x005306a6, 	/* ((r1&~0x2040902)!=0), r1=pglcs.pgl_cs.tl_ctrlstat_5,  */
	0x06020533, 0x00020001, 0x005506a7, 	/* ((r1&0x2000000)!=0), r1=pglcs.pgl_cs.tl_ctrlstat_5,  */
	0x04000534, 0x00020001, 0x005706a8, 	/* ((r1&~0x10240902)!=0), r1=pglcs.pgl_cs.tl_func345_stat,  */
	0x04000535, 0x00020001, 0x005706a9, 	/* ((r1&~0x10240902)!=0), r1=pglcs.pgl_cs.tl_func678_stat,  */
	0x06000536, 0x00020001, 0x000b06aa, 	/* ((r1&0x2)!=0), r1=pglcs.pgl_cs.DLATTN_VEC,  */
	0x00000537, 0x00010002, 0x000006ab, 	/* ((r1&~r2)!=0), r1=miscs.INT_STS_1, r2=miscs.INT_MASK_1,  */
	0x01010538, 0x00010001, 0x000d06ad, 	/* (r1!=1), r1=nig.rx_macfifo_empty,  */
	0x00660083, 	/* mode !e5 */
	0x00000539, 0x00010002, 0x000006ae, 	/* ((r1&~r2)!=0), r1=opte.PRTY_STS_H_0, r2=opte.PRTY_MASK_H_0,  */
	0x0102053a, 0x00010001, 0x000006b0, 	/* (r1!=0), r1=cau.agg_units_0to15_state,  */
	0x0102053b, 0x00010001, 0x000006b1, 	/* (r1!=0), r1=cau.agg_units_16to31_state,  */
	0x0102053c, 0x00010001, 0x000006b2, 	/* (r1!=0), r1=cau.agg_units_32to47_state,  */
	0x0102053d, 0x00010001, 0x000006b3, 	/* (r1!=0), r1=cau.agg_units_48to63_state,  */
	0x0102053e, 0x00010001, 0x000006b4, 	/* (r1!=0), r1=cau.igu_cqe_cmd_fsm_status,  */
	0x0102053f, 0x00010001, 0x000006b5, 	/* (r1!=0), r1=cau.igu_cqe_agg_fsm_status,  */
	0x00000540, 0x00010002, 0x000006b6, 	/* ((r1&~r2)!=0), r1=prs.PRTY_STS_H_1, r2=prs.PRTY_MASK_H_1,  */
	0x01010541, 0x00010001, 0x000006b8, 	/* (r1!=0), r1=prs.prs_pkt_ct,  */
	0x01010542, 0x00010001, 0x000006b9, 	/* (r1!=0), r1=prs.tcm_current_credit,  */
	0x01020543, 0x00010001, 0x005906ba, 	/* (r1!=31), r1=rss.empty_status,  */
	0x01020544, 0x00010001, 0x000006bb, 	/* (r1!=0), r1=rss.full_status,  */
	0x01020545, 0x00010001, 0x000006bc, 	/* (r1!=0), r1=rss.counters_status,  */
	0x01010546, 0x00010001, 0x000006bd, 	/* (r1!=0), r1=rss.state_machines,  */
	0x01010547, 0x00010001, 0x000006be, 	/* (r1!=0), r1=pswrq2.treq_fifo_fill_lvl,  */
	0x01010548, 0x00010001, 0x000006bf, 	/* (r1!=0), r1=pswrq2.icpl_fifo_fill_lvl,  */
	0x01010549, 0x00010001, 0x000006c0, 	/* (r1!=0), r1=yuld.pending_msg_to_ext_ev_1_ctr,  */
	0x0101054a, 0x00010001, 0x000006c1, 	/* (r1!=0), r1=yuld.pending_msg_to_ext_ev_2_ctr,  */
	0x0101054b, 0x00010001, 0x000006c2, 	/* (r1!=0), r1=yuld.pending_msg_to_ext_ev_3_ctr,  */
	0x0101054c, 0x00010001, 0x000006c3, 	/* (r1!=0), r1=yuld.pending_msg_to_ext_ev_4_ctr,  */
	0x0101054d, 0x00010001, 0x000006c4, 	/* (r1!=0), r1=yuld.pending_msg_to_ext_ev_5_ctr,  */
	0x0301054e, 0x00000002, 0x000006c5, 	/* (r1!=r2), r1=yuld.foc_remain_credits, r2=yuld.foci_foc_credits,  */
	0x0101054f, 0x00010001, 0x000006c7, 	/* (r1!=0), r1=yuld.dbg_pending_ccfc_req,  */
	0x01010550, 0x00010001, 0x000006c8, 	/* (r1!=0), r1=yuld.dbg_pending_tcfc_req,  */
	0x00000551, 0x00010002, 0x000006c9, 	/* ((r1&~r2)!=0), r1=yuld.PRTY_STS_H_0, r2=yuld.PRTY_MASK_H_0,  */
	0x01010552, 0x00010001, 0x000d06cb, 	/* (r1!=1), r1=nig.tx_macfifo_empty,  */
	0x01000553, 0x00010001, 0x000006cc, 	/* (r1!=0), r1=pcm.pbf_length_mis,  */
	0x01000554, 0x00010001, 0x000006cd, 	/* (r1!=0), r1=tcm.prs_length_mis,  */
	0x01000555, 0x00010001, 0x000006ce, 	/* (r1!=0), r1=ucm.yuld_length_mis,  */
	0x00000556, 0x00010002, 0x000006cf, 	/* ((r1&~r2)!=0), r1=xsem.PRTY_STS_H_0, r2=xsem.PRTY_MASK_H_0,  */
	0x00000557, 0x00010002, 0x000006d1, 	/* ((r1&~r2)!=0), r1=ysem.PRTY_STS_H_0, r2=ysem.PRTY_MASK_H_0,  */
	0x00000558, 0x00010002, 0x000006d3, 	/* ((r1&~r2)!=0), r1=psem.PRTY_STS_H_0, r2=psem.PRTY_MASK_H_0,  */
	0x00000559, 0x00010002, 0x000006d5, 	/* ((r1&~r2)!=0), r1=tsem.PRTY_STS_H_0, r2=tsem.PRTY_MASK_H_0,  */
	0x0000055a, 0x00010002, 0x000006d7, 	/* ((r1&~r2)!=0), r1=usem.PRTY_STS_H_0, r2=usem.PRTY_MASK_H_0,  */
	0x00150027, 	/* mode !(bb|e5) */
	0x0602055b, 0x00020001, 0x005106d9, 	/* ((r1&0x2000)!=0), r1=pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF,  */
	0x0602055c, 0x00020001, 0x004f06da, 	/* ((r1&0x100000)!=0), r1=pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF,  */
	0x0600055d, 0x00020001, 0x004d06db, 	/* ((r1&0x0FD010)!=0), r1=pglcs.pgl_cs.UNCORR_ERR_STATUS_OFF,  */
	0x0102055e, 0x00010001, 0x000006dc, 	/* (r1!=0), r1=pglcs.pgl_cs.HDR_LOG_0_OFF[0:3],  */
	0x0600055f, 0x00020001, 0x000d06dd, 	/* ((r1&0x00000001)!=0), r1=nws.common_status,  */
	0x06000560, 0x00020001, 0x005a06de, 	/* ((r1&0x00000002)!=2), r1=nws.common_status,  */
	0x06000561, 0x00020001, 0x005c06df, 	/* ((r1&0x00000004)!=4), r1=nws.common_status,  */
	0x000301e5, 	/* mode !k2 */
	0x00000562, 0x00010002, 0x000006e0, 	/* ((r1&~r2)!=0), r1=igu.PRTY_STS_H_1, r2=igu.PRTY_MASK_H_1,  */
	0x0006002f, 	/* mode !(!asic) */
	0x00000563, 0x00010002, 0x000006e2, 	/* ((r1&~r2)!=0), r1=mcp2.PRTY_STS, r2=mcp2.PRTY_MASK,  */
	0x00000564, 0x00010002, 0x000006e4, 	/* ((r1&~r2)!=0), r1=mcp2.PRTY_STS_H_0, r2=mcp2.PRTY_MASK_H_0,  */
	0x0003001d, 	/* mode !(emul_reduced|fpga) */
	0x00000565, 0x00010002, 0x000006e6, 	/* ((r1&~r2)!=0), r1=ncsi.PRTY_STS_H_0, r2=ncsi.PRTY_MASK_H_0,  */
	0x000900df, 	/* mode (!bb)&(!(!asic)) */
	0x06000566, 0x00020001, 0x000b06e8, 	/* ((r1&0x00000002)!=0), r1=umac.INT_STS,  */
	0x06000567, 0x00020001, 0x000d06e9, 	/* ((r1&0x00000001)!=0), r1=umac.INT_STS,  */
	0x06000568, 0x00020001, 0x005e06ea, 	/* ((r1&0xE001)!=0), r1=pcie.INT_STS,  */
	0x005a0025, 	/* mode (!(bb|e5))&(!(!asic)) */
	0x01000569, 0x00010001, 0x000006eb, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_err_ctrl0,  */
	0x0100056a, 0x00010001, 0x000006ec, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_err_ctrl1,  */
	0x0100056b, 0x00010001, 0x000006ed, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_err_ctrl2,  */
	0x0600056c, 0x00020001, 0x006006ee, 	/* ((r1&0x00000004)!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status0,  */
	0x0100056d, 0x00010001, 0x000006ef, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status0,  */
	0x0600056e, 0x00020001, 0x000d06f0, 	/* ((r1&0x00000001)!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status0,  */
	0x0600056f, 0x00020001, 0x000b06f1, 	/* ((r1&0x00000002)!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status0,  */
	0x01000570, 0x00010001, 0x000006f2, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status1,  */
	0x01000571, 0x00010001, 0x000006f3, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status2,  */
	0x01000572, 0x00010001, 0x000006f4, 	/* (r1!=0), r1=nws.nws_cmu.phy0_top_regbus_err_info_status3,  */
	0x01000573, 0x00010001, 0x000d06f5, 	/* (r1!=1), r1=nws.nws_cmu.cmu_lc0_top_phy_if_status,  */
	0x01000574, 0x00010001, 0x000006f6, 	/* (r1!=0), r1=nws.nws_cmu.cmu_lc0_top_err_ctrl1,  */
	0x01000575, 0x00010001, 0x000006f7, 	/* (r1!=0), r1=nws.nws_cmu.cmu_lc0_top_err_ctrl2,  */
	0x01000576, 0x00010001, 0x000006f8, 	/* (r1!=0), r1=nws.nws_cmu.cmu_lc0_top_err_ctrl3,  */
	0x01000577, 0x00010001, 0x000d06f9, 	/* (r1!=1), r1=nws.nws_cmu.cmu_r0_top_phy_if_status,  */
	0x01000578, 0x00010001, 0x000006fa, 	/* (r1!=0), r1=nws.nws_cmu.cmu_r0_top_err_ctrl1,  */
	0x01000579, 0x00010001, 0x000006fb, 	/* (r1!=0), r1=nws.nws_cmu.cmu_r0_top_err_ctrl2,  */
	0x0100057a, 0x00010001, 0x000006fc, 	/* (r1!=0), r1=nws.nws_cmu.cmu_r0_top_err_ctrl3,  */
	0x0100057b, 0x00010001, 0x000006fd, 	/* (r1!=0), r1=nws.nws_cmu.ln0_top_err_ctrl1,  */
	0x0100057c, 0x00010001, 0x000006fe, 	/* (r1!=0), r1=nws.nws_cmu.ln0_top_err_ctrl2,  */
	0x0100057d, 0x00010001, 0x000006ff, 	/* (r1!=0), r1=nws.nws_cmu.ln0_top_err_ctrl3,  */
	0x0100057e, 0x00010001, 0x00000700, 	/* (r1!=0), r1=nws.nws_cmu.ln1_top_err_ctrl1,  */
	0x0100057f, 0x00010001, 0x00000701, 	/* (r1!=0), r1=nws.nws_cmu.ln1_top_err_ctrl2,  */
	0x01000580, 0x00010001, 0x00000702, 	/* (r1!=0), r1=nws.nws_cmu.ln1_top_err_ctrl3,  */
	0x01000581, 0x00010001, 0x00000703, 	/* (r1!=0), r1=nws.nws_cmu.ln2_top_err_ctrl1,  */
	0x01000582, 0x00010001, 0x00000704, 	/* (r1!=0), r1=nws.nws_cmu.ln2_top_err_ctrl2,  */
	0x01000583, 0x00010001, 0x00000705, 	/* (r1!=0), r1=nws.nws_cmu.ln2_top_err_ctrl3,  */
	0x01000584, 0x00010001, 0x00000706, 	/* (r1!=0), r1=nws.nws_cmu.ln3_top_err_ctrl1,  */
	0x01000585, 0x00010001, 0x00000707, 	/* (r1!=0), r1=nws.nws_cmu.ln3_top_err_ctrl2,  */
	0x01000586, 0x00010001, 0x00000708, 	/* (r1!=0), r1=nws.nws_cmu.ln3_top_err_ctrl3,  */
	0x0009008f, 	/* mode (!bb)&(!(emul_full|emul_reduced)) */
	0x00000587, 0x00010002, 0x00000709, 	/* ((r1&~r2)!=0), r1=nwm.PRTY_STS_H_0, r2=nwm.PRTY_MASK_H_0,  */
	0x00000588, 0x00010002, 0x0000070b, 	/* ((r1&~r2)!=0), r1=nwm.PRTY_STS_H_1, r2=nwm.PRTY_MASK_H_1,  */
	0x00000589, 0x00010002, 0x0000070d, 	/* ((r1&~r2)!=0), r1=nwm.PRTY_STS_H_2, r2=nwm.PRTY_MASK_H_2,  */
	0x000301c3, 	/* mode bb&asic */
	0x0000058a, 0x00010002, 0x0000070f, 	/* ((r1&~r2)!=0), r1=ipc.PRTY_STS, r2=ipc.PRTY_MASK,  */
};
/* Data size: 17104 bytes */

/* Array of attentions data per register */
static const u32 attn_reg[] = {
	0x00000000, 0x05014060, 0x00014063, 0x00014061, 	/* grc.INT_STS_0 */
	0x00000000, 0x02014080, 0x00014083, 0x00014081, 	/* grc.PRTY_STS_H_0 */
	0x00000000, 0x03002460, 0x00002463, 0x00002461, 	/* miscs.INT_STS_0 */
	0x00030047, 0x0b002464, 0x00002467, 0x00002465, 	/* miscs.INT_STS_1, mode !(k2|e5) */
	0x00000000, 0x01002468, 0x0000246b, 0x00002469, 	/* miscs.PRTY_STS_0 */
	0x00000000, 0x01002060, 0x00002063, 0x00002061, 	/* misc.INT_STS */
	0x00000000, 0x180aa060, 0x000aa063, 0x000aa061, 	/* pglue_b.INT_STS */
	0x00000000, 0x010aa064, 0x000aa067, 0x000aa065, 	/* pglue_b.PRTY_STS */
	0x012a0005, 0x160aa080, 0x000aa083, 0x000aa081, 	/* pglue_b.PRTY_STS_H_0, mode bb */
	0x00010003, 0x1f0aa080, 0x000aa083, 0x000aa081, 	/* pglue_b.PRTY_STS_H_0, mode !bb */
	0x00200003, 0x030aa084, 0x000aa087, 0x000aa085, 	/* pglue_b.PRTY_STS_H_1, mode !bb */
	0x01400005, 0x060860ba, 0x000860bd, 0x000860bb, 	/* cnig.INT_STS, mode bb */
	0x00000003, 0x07086086, 0x00086089, 0x00086087, 	/* cnig.INT_STS, mode !bb */
	0x01460005, 0x020860d2, 0x000860d5, 0x000860d3, 	/* cnig.PRTY_STS, mode bb */
	0x01480003, 0x0208608b, 0x0008608e, 0x0008608c, 	/* cnig.PRTY_STS, mode !bb */
	0x00000000, 0x0100c0f8, 0x0000c0fb, 0x0000c0f9, 	/* cpmu.INT_STS_0 */
	0x00000000, 0x01010133, 0x00010136, 0x00010134, 	/* ncsi.INT_STS_0 */
	0x00000000, 0x03010000, 0x00010003, 0x00010001, 	/* ncsi.PRTY_STS_H_0 */
	0x00000083, 0x01014c82, 0x00014c85, 0x00014c83, 	/* opte.PRTY_STS, mode !e5 */
	0x00010083, 0x0b014c00, 0x00014c03, 0x00014c01, 	/* opte.PRTY_STS_H_0, mode !e5 */
	0x014a0000, 0x17150030, 0x00150033, 0x00150031, 	/* bmb.INT_STS_0 */
	0x01610000, 0x20150036, 0x00150039, 0x00150037, 	/* bmb.INT_STS_1 */
	0x01810000, 0x1c15003c, 0x0015003f, 0x0015003d, 	/* bmb.INT_STS_2 */
	0x019d0000, 0x20150042, 0x00150045, 0x00150043, 	/* bmb.INT_STS_3 */
	0x01bd0000, 0x1d150048, 0x0015004b, 0x00150049, 	/* bmb.INT_STS_4 */
	0x01da0000, 0x2015004e, 0x00150051, 0x0015004f, 	/* bmb.INT_STS_5 */
	0x01fa0000, 0x20150054, 0x00150057, 0x00150055, 	/* bmb.INT_STS_6 */
	0x00bb0000, 0x2015005a, 0x0015005d, 0x0015005b, 	/* bmb.INT_STS_7 */
	0x00db0000, 0x20150061, 0x00150064, 0x00150062, 	/* bmb.INT_STS_8 */
	0x021a0000, 0x25150067, 0x0015006a, 0x00150068, 	/* bmb.INT_STS_9 */
	0x023f0000, 0x0715006d, 0x00150070, 0x0015006e, 	/* bmb.INT_STS_10 */
	0x02460000, 0x08150073, 0x00150076, 0x00150074, 	/* bmb.INT_STS_11 */
	0x00000000, 0x05150077, 0x0015007a, 0x00150078, 	/* bmb.PRTY_STS */
	0x024e0000, 0x2e150100, 0x00150103, 0x00150101, 	/* bmb.PRTY_STS_H_0 */
	0x027c0005, 0x0f150104, 0x00150107, 0x00150105, 	/* bmb.PRTY_STS_H_1, mode bb */
	0x027c001b, 0x0f150104, 0x00150107, 0x00150105, 	/* bmb.PRTY_STS_H_1, mode k2 */
	0x028b0015, 0x17150104, 0x00150107, 0x00150105, 	/* bmb.PRTY_STS_H_1, mode !(bb|k2) */
	0x00000003, 0x110151e8, 0x000151eb, 0x000151e9, 	/* pcie.INT_STS, mode !bb */
	0x00000003, 0x030151ec, 0x000151ef, 0x000151ed, 	/* pcie.PRTY_STS, mode !bb */
	0x02a20005, 0x11015000, 0x00015003, 0x00015001, 	/* pcie.PRTY_STS_H_0, mode bb */
	0x00030003, 0x08015000, 0x00015003, 0x00015001, 	/* pcie.PRTY_STS_H_0, mode !bb */
	0x00000000, 0x01014810, 0x00014813, 0x00014811, 	/* mcp2.PRTY_STS */
	0x02b30005, 0x0c014881, 0x00014884, 0x00014882, 	/* mcp2.PRTY_STS_H_0, mode bb */
	0x02b3001b, 0x0c014881, 0x00014884, 0x00014882, 	/* mcp2.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x11014881, 0x00014884, 0x00014882, 	/* mcp2.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x120a8060, 0x000a8063, 0x000a8061, 	/* pswhst.INT_STS */
	0x00000000, 0x010a8064, 0x000a8067, 0x000a8065, 	/* pswhst.PRTY_STS */
	0x00010000, 0x110a8080, 0x000a8083, 0x000a8081, 	/* pswhst.PRTY_STS_H_0 */
	0x00000000, 0x050a7860, 0x000a7863, 0x000a7861, 	/* pswhst2.INT_STS */
	0x00000000, 0x010a7864, 0x000a7867, 0x000a7865, 	/* pswhst2.PRTY_STS */
	0x00000000, 0x030a7060, 0x000a7063, 0x000a7061, 	/* pswrd.INT_STS */
	0x00000000, 0x010a7064, 0x000a7067, 0x000a7065, 	/* pswrd.PRTY_STS */
	0x00000000, 0x050a7460, 0x000a7463, 0x000a7461, 	/* pswrd2.INT_STS */
	0x00000000, 0x010a7464, 0x000a7467, 0x000a7465, 	/* pswrd2.PRTY_STS */
	0x02bf0005, 0x1f0a7480, 0x000a7483, 0x000a7481, 	/* pswrd2.PRTY_STS_H_0, mode bb */
	0x02bf001b, 0x1f0a7480, 0x000a7483, 0x000a7481, 	/* pswrd2.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1f0a7480, 0x000a7483, 0x000a7481, 	/* pswrd2.PRTY_STS_H_0, mode !(bb|k2) */
	0x02de0005, 0x030a7484, 0x000a7487, 0x000a7485, 	/* pswrd2.PRTY_STS_H_1, mode bb */
	0x02de001b, 0x030a7484, 0x000a7487, 0x000a7485, 	/* pswrd2.PRTY_STS_H_1, mode k2 */
	0x02e10015, 0x050a7484, 0x000a7487, 0x000a7485, 	/* pswrd2.PRTY_STS_H_1, mode !(bb|k2) */
	0x00000000, 0x130a6860, 0x000a6863, 0x000a6861, 	/* pswwr.INT_STS */
	0x00000000, 0x010a6864, 0x000a6867, 0x000a6865, 	/* pswwr.PRTY_STS */
	0x00000000, 0x160a6c60, 0x000a6c63, 0x000a6c61, 	/* pswwr2.INT_STS */
	0x00000000, 0x010a6c64, 0x000a6c67, 0x000a6c65, 	/* pswwr2.PRTY_STS */
	0x02e60005, 0x1f0a6c80, 0x000a6c83, 0x000a6c81, 	/* pswwr2.PRTY_STS_H_0, mode bb */
	0x02e6001b, 0x1f0a6c80, 0x000a6c83, 0x000a6c81, 	/* pswwr2.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1f0a6c80, 0x000a6c83, 0x000a6c81, 	/* pswwr2.PRTY_STS_H_0, mode !(bb|k2) */
	0x03050005, 0x1f0a6c84, 0x000a6c87, 0x000a6c85, 	/* pswwr2.PRTY_STS_H_1, mode bb */
	0x0305001b, 0x1f0a6c84, 0x000a6c87, 0x000a6c85, 	/* pswwr2.PRTY_STS_H_1, mode k2 */
	0x03240015, 0x1f0a6c84, 0x000a6c87, 0x000a6c85, 	/* pswwr2.PRTY_STS_H_1, mode !(bb|k2) */
	0x03430005, 0x1f0a6c88, 0x000a6c8b, 0x000a6c89, 	/* pswwr2.PRTY_STS_H_2, mode bb */
	0x0343001b, 0x1f0a6c88, 0x000a6c8b, 0x000a6c89, 	/* pswwr2.PRTY_STS_H_2, mode k2 */
	0x03620015, 0x1f0a6c88, 0x000a6c8b, 0x000a6c89, 	/* pswwr2.PRTY_STS_H_2, mode !(bb|k2) */
	0x03810005, 0x140a6c8c, 0x000a6c8f, 0x000a6c8d, 	/* pswwr2.PRTY_STS_H_3, mode bb */
	0x0381001b, 0x140a6c8c, 0x000a6c8f, 0x000a6c8d, 	/* pswwr2.PRTY_STS_H_3, mode k2 */
	0x03950015, 0x1f0a6c8c, 0x000a6c8f, 0x000a6c8d, 	/* pswwr2.PRTY_STS_H_3, mode !(bb|k2) */
	0x03b40015, 0x0d0a6c90, 0x000a6c93, 0x000a6c91, 	/* pswwr2.PRTY_STS_H_4, mode !(bb|k2) */
	0x00000000, 0x170a0060, 0x000a0063, 0x000a0061, 	/* pswrq.INT_STS */
	0x00000000, 0x010a0064, 0x000a0067, 0x000a0065, 	/* pswrq.PRTY_STS */
	0x00000000, 0x12090060, 0x00090063, 0x00090061, 	/* pswrq2.INT_STS */
	0x03c10005, 0x09090080, 0x00090083, 0x00090081, 	/* pswrq2.PRTY_STS_H_0, mode bb */
	0x03ca001b, 0x0a090080, 0x00090083, 0x00090081, 	/* pswrq2.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x05090080, 0x00090083, 0x00090081, 	/* pswrq2.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x02000740, 0x00000743, 0x00000741, 	/* pglcs.INT_STS */
	0x00000000, 0x02003060, 0x00003063, 0x00003061, 	/* dmae.INT_STS */
	0x00000000, 0x03003080, 0x00003083, 0x00003081, 	/* dmae.PRTY_STS_H_0 */
	0x00000000, 0x08158060, 0x00158063, 0x00158061, 	/* ptu.INT_STS */
	0x03d40005, 0x12158080, 0x00158083, 0x00158081, 	/* ptu.PRTY_STS_H_0, mode bb */
	0x03d4001b, 0x12158080, 0x00158083, 0x00158081, 	/* ptu.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x11158080, 0x00158083, 0x00158081, 	/* ptu.PRTY_STS_H_0, mode !(bb|k2) */
	0x03e60005, 0x08460060, 0x00460063, 0x00460061, 	/* tcm.INT_STS_0, mode bb */
	0x03e6001b, 0x08460060, 0x00460063, 0x00460061, 	/* tcm.INT_STS_0, mode k2 */
	0x00000015, 0x0e460060, 0x00460063, 0x00460061, 	/* tcm.INT_STS_0, mode !(bb|k2) */
	0x03ee0000, 0x22460064, 0x00460067, 0x00460065, 	/* tcm.INT_STS_1 */
	0x00300000, 0x01460068, 0x0046006b, 0x00460069, 	/* tcm.INT_STS_2 */
	0x04100005, 0x1f460080, 0x00460083, 0x00460081, 	/* tcm.PRTY_STS_H_0, mode bb */
	0x042f001b, 0x1f460080, 0x00460083, 0x00460081, 	/* tcm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x1f460080, 0x00460083, 0x00460081, 	/* tcm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00380005, 0x02460084, 0x00460087, 0x00460085, 	/* tcm.PRTY_STS_H_1, mode bb */
	0x044e001b, 0x03460084, 0x00460087, 0x00460085, 	/* tcm.PRTY_STS_H_1, mode k2 */
	0x04510015, 0x09460084, 0x00460087, 0x00460085, 	/* tcm.PRTY_STS_H_1, mode !(bb|k2) */
	0x045a0005, 0x0e480060, 0x00480063, 0x00480061, 	/* mcm.INT_STS_0, mode bb */
	0x045a001b, 0x0e480060, 0x00480063, 0x00480061, 	/* mcm.INT_STS_0, mode k2 */
	0x00000015, 0x14480060, 0x00480063, 0x00480061, 	/* mcm.INT_STS_0, mode !(bb|k2) */
	0x00140000, 0x1a480064, 0x00480067, 0x00480065, 	/* mcm.INT_STS_1 */
	0x002e0000, 0x01480068, 0x0048006b, 0x00480069, 	/* mcm.INT_STS_2 */
	0x04680005, 0x1f480080, 0x00480083, 0x00480081, 	/* mcm.PRTY_STS_H_0, mode bb */
	0x0468001b, 0x1f480080, 0x00480083, 0x00480081, 	/* mcm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x1f480080, 0x00480083, 0x00480081, 	/* mcm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00300005, 0x04480084, 0x00480087, 0x00480085, 	/* mcm.PRTY_STS_H_1, mode bb */
	0x0030001b, 0x04480084, 0x00480087, 0x00480085, 	/* mcm.PRTY_STS_H_1, mode k2 */
	0x04870015, 0x09480084, 0x00480087, 0x00480085, 	/* mcm.PRTY_STS_H_1, mode !(bb|k2) */
	0x04900000, 0x144a0060, 0x004a0063, 0x004a0061, 	/* ucm.INT_STS_0 */
	0x00150005, 0x1d4a0064, 0x004a0067, 0x004a0065, 	/* ucm.INT_STS_1, mode bb */
	0x0015001b, 0x1d4a0064, 0x004a0067, 0x004a0065, 	/* ucm.INT_STS_1, mode k2 */
	0x00140015, 0x1e4a0064, 0x004a0067, 0x004a0065, 	/* ucm.INT_STS_1, mode !(bb|k2) */
	0x00320000, 0x014a0068, 0x004a006b, 0x004a0069, 	/* ucm.INT_STS_2 */
	0x04a40005, 0x1f4a0080, 0x004a0083, 0x004a0081, 	/* ucm.PRTY_STS_H_0, mode bb */
	0x04a4001b, 0x1f4a0080, 0x004a0083, 0x004a0081, 	/* ucm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x1f4a0080, 0x004a0083, 0x004a0081, 	/* ucm.PRTY_STS_H_0, mode !(bb|k2) */
	0x04c30005, 0x074a0084, 0x004a0087, 0x004a0085, 	/* ucm.PRTY_STS_H_1, mode bb */
	0x04c3001b, 0x074a0084, 0x004a0087, 0x004a0085, 	/* ucm.PRTY_STS_H_1, mode k2 */
	0x002a0015, 0x064a0084, 0x004a0087, 0x004a0085, 	/* ucm.PRTY_STS_H_1, mode !(bb|k2) */
	0x00000000, 0x12400060, 0x00400063, 0x00400061, 	/* xcm.INT_STS_0 */
	0x00120000, 0x19400064, 0x00400067, 0x00400065, 	/* xcm.INT_STS_1 */
	0x002b0000, 0x08400068, 0x0040006b, 0x00400069, 	/* xcm.INT_STS_2 */
	0x04ca0005, 0x1f400080, 0x00400083, 0x00400081, 	/* xcm.PRTY_STS_H_0, mode bb */
	0x04e9001b, 0x1f400080, 0x00400083, 0x00400081, 	/* xcm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x1f400080, 0x00400083, 0x00400081, 	/* xcm.PRTY_STS_H_0, mode !(bb|k2) */
	0x05080005, 0x0b400084, 0x00400087, 0x00400085, 	/* xcm.PRTY_STS_H_1, mode bb */
	0x00270003, 0x0c400084, 0x00400087, 0x00400085, 	/* xcm.PRTY_STS_H_1, mode !bb */
	0x00000000, 0x0f420060, 0x00420063, 0x00420061, 	/* ycm.INT_STS_0 */
	0x000f0000, 0x17420064, 0x00420067, 0x00420065, 	/* ycm.INT_STS_1 */
	0x00260000, 0x01420068, 0x0042006b, 0x00420069, 	/* ycm.INT_STS_2 */
	0x05130005, 0x1f420080, 0x00420083, 0x00420081, 	/* ycm.PRTY_STS_H_0, mode bb */
	0x0532001b, 0x1f420080, 0x00420083, 0x00420081, 	/* ycm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x1f420080, 0x00420083, 0x00420081, 	/* ycm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00360005, 0x03420084, 0x00420087, 0x00420085, 	/* ycm.PRTY_STS_H_1, mode bb */
	0x0551001b, 0x04420084, 0x00420087, 0x00420085, 	/* ycm.PRTY_STS_H_1, mode k2 */
	0x05550015, 0x08420084, 0x00420087, 0x00420085, 	/* ycm.PRTY_STS_H_1, mode !(bb|k2) */
	0x00000000, 0x09440060, 0x00440063, 0x00440061, 	/* pcm.INT_STS_0 */
	0x055d0005, 0x0e440064, 0x00440067, 0x00440065, 	/* pcm.INT_STS_1, mode bb */
	0x055d001b, 0x0e440064, 0x00440067, 0x00440065, 	/* pcm.INT_STS_1, mode k2 */
	0x00090015, 0x0c440064, 0x00440067, 0x00440065, 	/* pcm.INT_STS_1, mode !(bb|k2) */
	0x00170000, 0x01440068, 0x0044006b, 0x00440069, 	/* pcm.INT_STS_2 */
	0x056b0005, 0x0b440080, 0x00440083, 0x00440081, 	/* pcm.PRTY_STS_H_0, mode bb */
	0x0576001b, 0x0f440080, 0x00440083, 0x00440081, 	/* pcm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x10440080, 0x00440083, 0x00440081, 	/* pcm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x160bc060, 0x000bc063, 0x000bc061, 	/* qm.INT_STS */
	0x00000000, 0x0b0bc064, 0x000bc067, 0x000bc065, 	/* qm.PRTY_STS */
	0x05850005, 0x1f0bc080, 0x000bc083, 0x000bc081, 	/* qm.PRTY_STS_H_0, mode bb */
	0x0585001b, 0x1f0bc080, 0x000bc083, 0x000bc081, 	/* qm.PRTY_STS_H_0, mode k2 */
	0x000b0015, 0x1f0bc080, 0x000bc083, 0x000bc081, 	/* qm.PRTY_STS_H_0, mode !(bb|k2) */
	0x05a40005, 0x1f0bc084, 0x000bc087, 0x000bc085, 	/* qm.PRTY_STS_H_1, mode bb */
	0x05a4001b, 0x1f0bc084, 0x000bc087, 0x000bc085, 	/* qm.PRTY_STS_H_1, mode k2 */
	0x05c30015, 0x1f0bc084, 0x000bc087, 0x000bc085, 	/* qm.PRTY_STS_H_1, mode !(bb|k2) */
	0x05e20005, 0x0b0bc088, 0x000bc08b, 0x000bc089, 	/* qm.PRTY_STS_H_2, mode bb */
	0x00000003, 0x000bc088, 0x000bc08b, 0x000bc089, 	/* qm.PRTY_STS_H_2, mode !bb */
	0x00000000, 0x200b0060, 0x000b0063, 0x000b0061, 	/* tm.INT_STS_0 */
	0x00200000, 0x0b0b0064, 0x000b0067, 0x000b0065, 	/* tm.INT_STS_1 */
	0x05ed0005, 0x110b0080, 0x000b0083, 0x000b0081, 	/* tm.PRTY_STS_H_0, mode bb */
	0x05ed001b, 0x110b0080, 0x000b0083, 0x000b0081, 	/* tm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0f0b0080, 0x000b0083, 0x000b0081, 	/* tm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x0c040060, 0x00040063, 0x00040061, 	/* dorq.INT_STS */
	0x00000000, 0x01040064, 0x00040067, 0x00040065, 	/* dorq.PRTY_STS */
	0x05fe0005, 0x06040080, 0x00040083, 0x00040081, 	/* dorq.PRTY_STS_H_0, mode bb */
	0x05fe001b, 0x06040080, 0x00040083, 0x00040081, 	/* dorq.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x0c040080, 0x00040083, 0x00040081, 	/* dorq.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x200d0030, 0x000d0033, 0x000d0031, 	/* brb.INT_STS_0 */
	0x06040000, 0x200d0036, 0x000d0039, 0x000d0037, 	/* brb.INT_STS_1 */
	0x003e0000, 0x1c0d003c, 0x000d003f, 0x000d003d, 	/* brb.INT_STS_2 */
	0x06240000, 0x200d0042, 0x000d0045, 0x000d0043, 	/* brb.INT_STS_3 */
	0x06440000, 0x1c0d0048, 0x000d004b, 0x000d0049, 	/* brb.INT_STS_4 */
	0x00940000, 0x010d004e, 0x000d0051, 0x000d004f, 	/* brb.INT_STS_5 */
	0x06600000, 0x0a0d0054, 0x000d0057, 0x000d0055, 	/* brb.INT_STS_6 */
	0x009d0000, 0x200d005a, 0x000d005d, 0x000d005b, 	/* brb.INT_STS_7 */
	0x00bd0000, 0x110d0061, 0x000d0064, 0x000d0062, 	/* brb.INT_STS_8 */
	0x00ce0000, 0x010d0067, 0x000d006a, 0x000d0068, 	/* brb.INT_STS_9 */
	0x066a0000, 0x150d006d, 0x000d0070, 0x000d006e, 	/* brb.INT_STS_10 */
	0x067f0000, 0x090d0073, 0x000d0076, 0x000d0074, 	/* brb.INT_STS_11 */
	0x00000000, 0x050d0077, 0x000d007a, 0x000d0078, 	/* brb.PRTY_STS */
	0x06880005, 0x1f0d0100, 0x000d0103, 0x000d0101, 	/* brb.PRTY_STS_H_0, mode bb */
	0x06a7001b, 0x1f0d0100, 0x000d0103, 0x000d0101, 	/* brb.PRTY_STS_H_0, mode k2 */
	0x00050015, 0x1f0d0100, 0x000d0103, 0x000d0101, 	/* brb.PRTY_STS_H_0, mode !(bb|k2) */
	0x06c60005, 0x0e0d0104, 0x000d0107, 0x000d0105, 	/* brb.PRTY_STS_H_1, mode bb */
	0x06d4001b, 0x1e0d0104, 0x000d0107, 0x000d0105, 	/* brb.PRTY_STS_H_1, mode k2 */
	0x06f20015, 0x1f0d0104, 0x000d0107, 0x000d0105, 	/* brb.PRTY_STS_H_1, mode !(bb|k2) */
	0x00520015, 0x010d0108, 0x000d010b, 0x000d0109, 	/* brb.PRTY_STS_H_2, mode !(bb|k2) */
	0x00000000, 0x0108e076, 0x0008e077, 0x0008e079, 	/* src.INT_STS */
	0x00000000, 0x0207c010, 0x0007c013, 0x0007c011, 	/* prs.INT_STS_0 */
	0x00000000, 0x0207c014, 0x0007c017, 0x0007c015, 	/* prs.PRTY_STS */
	0x07110005, 0x1f07c081, 0x0007c084, 0x0007c082, 	/* prs.PRTY_STS_H_0, mode bb */
	0x0730001b, 0x1f07c081, 0x0007c084, 0x0007c082, 	/* prs.PRTY_STS_H_0, mode k2 */
	0x00020015, 0x1707c081, 0x0007c084, 0x0007c082, 	/* prs.PRTY_STS_H_0, mode !(bb|k2) */
	0x074f0005, 0x0507c085, 0x0007c088, 0x0007c086, 	/* prs.PRTY_STS_H_1, mode bb */
	0x07540027, 0x1f07c085, 0x0007c088, 0x0007c086, 	/* prs.PRTY_STS_H_1, mode !(bb|e5) */
	0x00000000, 0x1f3ec010, 0x003ec013, 0x003ec011, 	/* tsdm.INT_STS */
	0x07730005, 0x0a3ec080, 0x003ec083, 0x003ec081, 	/* tsdm.PRTY_STS_H_0, mode bb */
	0x0773001b, 0x0a3ec080, 0x003ec083, 0x003ec081, 	/* tsdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0b3ec080, 0x003ec083, 0x003ec081, 	/* tsdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x1f3f0010, 0x003f0013, 0x003f0011, 	/* msdm.INT_STS */
	0x077d0005, 0x0b3f0080, 0x003f0083, 0x003f0081, 	/* msdm.PRTY_STS_H_0, mode bb */
	0x077d001b, 0x0b3f0080, 0x003f0083, 0x003f0081, 	/* msdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0c3f0080, 0x003f0083, 0x003f0081, 	/* msdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x1f3f4010, 0x003f4013, 0x003f4011, 	/* usdm.INT_STS */
	0x07880005, 0x0a3f4080, 0x003f4083, 0x003f4081, 	/* usdm.PRTY_STS_H_0, mode bb */
	0x0788001b, 0x0a3f4080, 0x003f4083, 0x003f4081, 	/* usdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0b3f4080, 0x003f4083, 0x003f4081, 	/* usdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x1f3e0010, 0x003e0013, 0x003e0011, 	/* xsdm.INT_STS */
	0x07920005, 0x0a3e0080, 0x003e0083, 0x003e0081, 	/* xsdm.PRTY_STS_H_0, mode bb */
	0x0792001b, 0x0a3e0080, 0x003e0083, 0x003e0081, 	/* xsdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0b3e0080, 0x003e0083, 0x003e0081, 	/* xsdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x1f3e4010, 0x003e4013, 0x003e4011, 	/* ysdm.INT_STS */
	0x07920005, 0x093e4080, 0x003e4083, 0x003e4081, 	/* ysdm.PRTY_STS_H_0, mode bb */
	0x0792001b, 0x093e4080, 0x003e4083, 0x003e4081, 	/* ysdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0a3e4080, 0x003e4083, 0x003e4081, 	/* ysdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x1f3e8010, 0x003e8013, 0x003e8011, 	/* psdm.INT_STS */
	0x079c0005, 0x093e8080, 0x003e8083, 0x003e8081, 	/* psdm.PRTY_STS_H_0, mode bb */
	0x079c001b, 0x093e8080, 0x003e8083, 0x003e8081, 	/* psdm.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0a3e8080, 0x003e8083, 0x003e8081, 	/* psdm.PRTY_STS_H_0, mode !(bb|k2) */
	0x07a50005, 0x205c0010, 0x005c0013, 0x005c0011, 	/* tsem.INT_STS_0, mode bb */
	0x07a5001b, 0x205c0010, 0x005c0013, 0x005c0011, 	/* tsem.INT_STS_0, mode k2 */
	0x00000015, 0x1f5c0010, 0x005c0013, 0x005c0011, 	/* tsem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d5c0014, 0x005c0017, 0x005c0015, 	/* tsem.INT_STS_1 */
	0x07f20015, 0x1f5c0018, 0x005c001b, 0x005c0019, 	/* tsem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x015d0010, 0x005d0013, 0x005d0011, 	/* tsem.fast_memory.INT_STS */
	0x08110005, 0x035c0032, 0x005c0035, 0x005c0033, 	/* tsem.PRTY_STS, mode bb */
	0x0811001b, 0x035c0032, 0x005c0035, 0x005c0033, 	/* tsem.PRTY_STS, mode k2 */
	0x00000015, 0x055c0032, 0x005c0035, 0x005c0033, 	/* tsem.PRTY_STS, mode !(bb|k2) */
	0x00060083, 0x065c0080, 0x005c0083, 0x005c0081, 	/* tsem.PRTY_STS_H_0, mode !e5 */
	0x00000003, 0x005d0080, 0x005d0083, 0x005d0081, 	/* tsem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x08140005, 0x065d2880, 0x005d2883, 0x005d2881, 	/* tsem.fast_memory.vfc_config.PRTY_STS_H_0, mode bb */
	0x081a001b, 0x065d2880, 0x005d2883, 0x005d2881, 	/* tsem.fast_memory.vfc_config.PRTY_STS_H_0, mode k2 */
	0x08200015, 0x055d2880, 0x005d2883, 0x005d2881, 	/* tsem.fast_memory.vfc_config.PRTY_STS_H_0, mode !(bb|k2) */
	0x07a50005, 0x20600010, 0x00600013, 0x00600011, 	/* msem.INT_STS_0, mode bb */
	0x07a5001b, 0x20600010, 0x00600013, 0x00600011, 	/* msem.INT_STS_0, mode k2 */
	0x00000015, 0x1f600010, 0x00600013, 0x00600011, 	/* msem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d600014, 0x00600017, 0x00600015, 	/* msem.INT_STS_1 */
	0x07f20015, 0x1f600018, 0x0060001b, 0x00600019, 	/* msem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x01610010, 0x00610013, 0x00610011, 	/* msem.fast_memory.INT_STS */
	0x08110005, 0x03600032, 0x00600035, 0x00600033, 	/* msem.PRTY_STS, mode bb */
	0x0811001b, 0x03600032, 0x00600035, 0x00600033, 	/* msem.PRTY_STS, mode k2 */
	0x00000015, 0x05600032, 0x00600035, 0x00600033, 	/* msem.PRTY_STS, mode !(bb|k2) */
	0x08250005, 0x06600080, 0x00600083, 0x00600081, 	/* msem.PRTY_STS_H_0, mode bb */
	0x0825001b, 0x06600080, 0x00600083, 0x00600081, 	/* msem.PRTY_STS_H_0, mode k2 */
	0x00060015, 0x10600080, 0x00600083, 0x00600081, 	/* msem.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000003, 0x00610080, 0x00610083, 0x00610081, 	/* msem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x07a50005, 0x20640010, 0x00640013, 0x00640011, 	/* usem.INT_STS_0, mode bb */
	0x07a5001b, 0x20640010, 0x00640013, 0x00640011, 	/* usem.INT_STS_0, mode k2 */
	0x00000015, 0x1f640010, 0x00640013, 0x00640011, 	/* usem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d640014, 0x00640017, 0x00640015, 	/* usem.INT_STS_1 */
	0x07f20015, 0x1f640018, 0x0064001b, 0x00640019, 	/* usem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x01650010, 0x00650013, 0x00650011, 	/* usem.fast_memory.INT_STS */
	0x08110005, 0x03640032, 0x00640035, 0x00640033, 	/* usem.PRTY_STS, mode bb */
	0x0811001b, 0x03640032, 0x00640035, 0x00640033, 	/* usem.PRTY_STS, mode k2 */
	0x00000015, 0x05640032, 0x00640035, 0x00640033, 	/* usem.PRTY_STS, mode !(bb|k2) */
	0x00060083, 0x06640080, 0x00640083, 0x00640081, 	/* usem.PRTY_STS_H_0, mode !e5 */
	0x00000003, 0x00650080, 0x00650083, 0x00650081, 	/* usem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x07a50005, 0x20500010, 0x00500013, 0x00500011, 	/* xsem.INT_STS_0, mode bb */
	0x07a5001b, 0x20500010, 0x00500013, 0x00500011, 	/* xsem.INT_STS_0, mode k2 */
	0x00000015, 0x1f500010, 0x00500013, 0x00500011, 	/* xsem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d500014, 0x00500017, 0x00500015, 	/* xsem.INT_STS_1 */
	0x07f20015, 0x1f500018, 0x0050001b, 0x00500019, 	/* xsem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x01510010, 0x00510013, 0x00510011, 	/* xsem.fast_memory.INT_STS */
	0x08110005, 0x03500032, 0x00500035, 0x00500033, 	/* xsem.PRTY_STS, mode bb */
	0x0811001b, 0x03500032, 0x00500035, 0x00500033, 	/* xsem.PRTY_STS, mode k2 */
	0x00000015, 0x05500032, 0x00500035, 0x00500033, 	/* xsem.PRTY_STS, mode !(bb|k2) */
	0x00060083, 0x07500080, 0x00500083, 0x00500081, 	/* xsem.PRTY_STS_H_0, mode !e5 */
	0x00000003, 0x00510080, 0x00510083, 0x00510081, 	/* xsem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x07a50005, 0x20540010, 0x00540013, 0x00540011, 	/* ysem.INT_STS_0, mode bb */
	0x07a5001b, 0x20540010, 0x00540013, 0x00540011, 	/* ysem.INT_STS_0, mode k2 */
	0x00000015, 0x1f540010, 0x00540013, 0x00540011, 	/* ysem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d540014, 0x00540017, 0x00540015, 	/* ysem.INT_STS_1 */
	0x07f20015, 0x1f540018, 0x0054001b, 0x00540019, 	/* ysem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x01550010, 0x00550013, 0x00550011, 	/* ysem.fast_memory.INT_STS */
	0x08110005, 0x03540032, 0x00540035, 0x00540033, 	/* ysem.PRTY_STS, mode bb */
	0x0811001b, 0x03540032, 0x00540035, 0x00540033, 	/* ysem.PRTY_STS, mode k2 */
	0x00000015, 0x05540032, 0x00540035, 0x00540033, 	/* ysem.PRTY_STS, mode !(bb|k2) */
	0x00060083, 0x07540080, 0x00540083, 0x00540081, 	/* ysem.PRTY_STS_H_0, mode !e5 */
	0x00000003, 0x00550080, 0x00550083, 0x00550081, 	/* ysem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x07a50005, 0x20580010, 0x00580013, 0x00580011, 	/* psem.INT_STS_0, mode bb */
	0x07a5001b, 0x20580010, 0x00580013, 0x00580011, 	/* psem.INT_STS_0, mode k2 */
	0x00000015, 0x1f580010, 0x00580013, 0x00580011, 	/* psem.INT_STS_0, mode !(bb|k2) */
	0x07c50000, 0x2d580014, 0x00580017, 0x00580015, 	/* psem.INT_STS_1 */
	0x07f20015, 0x1f580018, 0x0058001b, 0x00580019, 	/* psem.INT_STS_2, mode !(bb|k2) */
	0x00000000, 0x01590010, 0x00590013, 0x00590011, 	/* psem.fast_memory.INT_STS */
	0x08110005, 0x03580032, 0x00580035, 0x00580033, 	/* psem.PRTY_STS, mode bb */
	0x0811001b, 0x03580032, 0x00580035, 0x00580033, 	/* psem.PRTY_STS, mode k2 */
	0x00000015, 0x05580032, 0x00580035, 0x00580033, 	/* psem.PRTY_STS, mode !(bb|k2) */
	0x00060083, 0x06580080, 0x00580083, 0x00580081, 	/* psem.PRTY_STS_H_0, mode !e5 */
	0x00000003, 0x00590080, 0x00590083, 0x00590081, 	/* psem.fast_memory.PRTY_STS_H_0, mode !bb */
	0x08140005, 0x06592880, 0x00592883, 0x00592881, 	/* psem.fast_memory.vfc_config.PRTY_STS_H_0, mode bb */
	0x081a001b, 0x06592880, 0x00592883, 0x00592881, 	/* psem.fast_memory.vfc_config.PRTY_STS_H_0, mode k2 */
	0x08200015, 0x05592880, 0x00592883, 0x00592881, 	/* psem.fast_memory.vfc_config.PRTY_STS_H_0, mode !(bb|k2) */
	0x082b0000, 0x1608e260, 0x0008e263, 0x0008e261, 	/* rss.INT_STS */
	0x08410000, 0x0a08e280, 0x0008e283, 0x0008e281, 	/* rss.PRTY_STS_H_0 */
	0x00000000, 0x06134060, 0x00134063, 0x00134061, 	/* tmld.INT_STS */
	0x084b0005, 0x08134080, 0x00134083, 0x00134081, 	/* tmld.PRTY_STS_H_0, mode bb */
	0x084b001b, 0x08134080, 0x00134083, 0x00134081, 	/* tmld.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0e134080, 0x00134083, 0x00134081, 	/* tmld.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x06138060, 0x00138063, 0x00138061, 	/* muld.INT_STS */
	0x08530005, 0x0a138080, 0x00138083, 0x00138081, 	/* muld.PRTY_STS_H_0, mode bb */
	0x0853001b, 0x0a138080, 0x00138083, 0x00138081, 	/* muld.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x10138080, 0x00138083, 0x00138081, 	/* muld.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000083, 0x06132060, 0x00132063, 0x00132061, 	/* yuld.INT_STS, mode !e5 */
	0x00000083, 0x06132080, 0x00132083, 0x00132081, 	/* yuld.PRTY_STS_H_0, mode !e5 */
	0x00000000, 0x06130060, 0x00130063, 0x00130061, 	/* xyld.INT_STS */
	0x085d0005, 0x09130080, 0x00130083, 0x00130081, 	/* xyld.PRTY_STS_H_0, mode bb */
	0x085d001b, 0x09130080, 0x00130083, 0x00130081, 	/* xyld.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0f130080, 0x00130083, 0x00130081, 	/* xyld.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x06168060, 0x00168063, 0x00168061, 	/* ptld.INT_STS, mode !(bb|k2) */
	0x00000015, 0x08168080, 0x00168083, 0x00168081, 	/* ptld.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x06170060, 0x00170063, 0x00170061, 	/* ypld.INT_STS, mode !(bb|k2) */
	0x00000015, 0x08170080, 0x00170083, 0x00170081, 	/* ypld.PRTY_STS_H_0, mode !(bb|k2) */
	0x08660000, 0x0b08c010, 0x0008c013, 0x0008c011, 	/* prm.INT_STS */
	0x00000000, 0x0108c014, 0x0008c017, 0x0008c015, 	/* prm.PRTY_STS */
	0x08710005, 0x1808c080, 0x0008c083, 0x0008c081, 	/* prm.PRTY_STS_H_0, mode bb */
	0x0889001b, 0x1708c080, 0x0008c083, 0x0008c081, 	/* prm.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1a08c080, 0x0008c083, 0x0008c081, 	/* prm.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x09368010, 0x00368013, 0x00368011, 	/* pbf_pb1.INT_STS */
	0x00000000, 0x01368014, 0x00368017, 0x00368015, 	/* pbf_pb1.PRTY_STS */
	0x00000000, 0x09369010, 0x00369013, 0x00369011, 	/* pbf_pb2.INT_STS */
	0x00000000, 0x01369014, 0x00369017, 0x00369015, 	/* pbf_pb2.PRTY_STS */
	0x00000000, 0x0908f010, 0x0008f013, 0x0008f011, 	/* rpb.INT_STS */
	0x00000000, 0x0108f014, 0x0008f017, 0x0008f015, 	/* rpb.PRTY_STS */
	0x08a00000, 0x1a36c030, 0x0036c033, 0x0036c031, 	/* btb.INT_STS_0 */
	0x08ba0000, 0x1236c036, 0x0036c039, 0x0036c037, 	/* btb.INT_STS_1 */
	0x08cc0000, 0x0536c03c, 0x0036c03f, 0x0036c03d, 	/* btb.INT_STS_2 */
	0x00240000, 0x2036c042, 0x0036c045, 0x0036c043, 	/* btb.INT_STS_3 */
	0x08d10000, 0x1c36c048, 0x0036c04b, 0x0036c049, 	/* btb.INT_STS_4 */
	0x005b0000, 0x2036c04e, 0x0036c051, 0x0036c04f, 	/* btb.INT_STS_5 */
	0x007b0000, 0x0136c054, 0x0036c057, 0x0036c055, 	/* btb.INT_STS_6 */
	0x007c0000, 0x0136c061, 0x0036c064, 0x0036c062, 	/* btb.INT_STS_8 */
	0x007d0000, 0x0136c067, 0x0036c06a, 0x0036c068, 	/* btb.INT_STS_9 */
	0x08ed0000, 0x0236c06d, 0x0036c070, 0x0036c06e, 	/* btb.INT_STS_10 */
	0x08ef0000, 0x0436c073, 0x0036c076, 0x0036c074, 	/* btb.INT_STS_11 */
	0x00000000, 0x0536c077, 0x0036c07a, 0x0036c078, 	/* btb.PRTY_STS */
	0x08f30005, 0x1736c100, 0x0036c103, 0x0036c101, 	/* btb.PRTY_STS_H_0, mode bb */
	0x090a001b, 0x1f36c100, 0x0036c103, 0x0036c101, 	/* btb.PRTY_STS_H_0, mode k2 */
	0x00050015, 0x1f36c100, 0x0036c103, 0x0036c101, 	/* btb.PRTY_STS_H_0, mode !(bb|k2) */
	0x00240015, 0x0836c104, 0x0036c107, 0x0036c105, 	/* btb.PRTY_STS_H_1, mode !(bb|k2) */
	0x00000000, 0x01360060, 0x00360063, 0x00360061, 	/* pbf.INT_STS */
	0x00000000, 0x01360064, 0x00360067, 0x00360065, 	/* pbf.PRTY_STS */
	0x09290005, 0x1f360080, 0x00360083, 0x00360081, 	/* pbf.PRTY_STS_H_0, mode bb */
	0x0929001b, 0x1f360080, 0x00360083, 0x00360081, 	/* pbf.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1f360080, 0x00360083, 0x00360081, 	/* pbf.PRTY_STS_H_0, mode !(bb|k2) */
	0x09480005, 0x1b360084, 0x00360087, 0x00360085, 	/* pbf.PRTY_STS_H_1, mode bb */
	0x0948001b, 0x1b360084, 0x00360087, 0x00360085, 	/* pbf.PRTY_STS_H_1, mode k2 */
	0x09630015, 0x1f360084, 0x00360087, 0x00360085, 	/* pbf.PRTY_STS_H_1, mode !(bb|k2) */
	0x09820015, 0x04360088, 0x0036008b, 0x00360089, 	/* pbf.PRTY_STS_H_2, mode !(bb|k2) */
	0x00000000, 0x090c0060, 0x000c0063, 0x000c0061, 	/* rdif.INT_STS */
	0x01480000, 0x020c0064, 0x000c0067, 0x000c0065, 	/* rdif.PRTY_STS */
	0x00000000, 0x090c4060, 0x000c4063, 0x000c4061, 	/* tdif.INT_STS */
	0x01480000, 0x020c4064, 0x000c4067, 0x000c4065, 	/* tdif.PRTY_STS */
	0x09860005, 0x0b0c4080, 0x000c4083, 0x000c4081, 	/* tdif.PRTY_STS_H_0, mode bb */
	0x0986001b, 0x0b0c4080, 0x000c4083, 0x000c4081, 	/* tdif.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x0c0c4080, 0x000c4083, 0x000c4081, 	/* tdif.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x08160070, 0x00160071, 0x00160073, 	/* cdu.INT_STS */
	0x09910005, 0x05160080, 0x00160083, 0x00160081, 	/* cdu.PRTY_STS_H_0, mode bb */
	0x0991001b, 0x05160080, 0x00160083, 0x00160081, 	/* cdu.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x04160080, 0x00160083, 0x00160081, 	/* cdu.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x020b8060, 0x000b8063, 0x000b8061, 	/* ccfc.INT_STS_0 */
	0x00000000, 0x060b8179, 0x000b817c, 0x000b817a, 	/* ccfc.PRTY_STS */
	0x09960005, 0x020b8080, 0x000b8083, 0x000b8081, 	/* ccfc.PRTY_STS_H_0, mode bb */
	0x0996001b, 0x020b8080, 0x000b8083, 0x000b8081, 	/* ccfc.PRTY_STS_H_0, mode k2 */
	0x00060015, 0x060b8080, 0x000b8083, 0x000b8081, 	/* ccfc.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x020b4060, 0x000b4063, 0x000b4061, 	/* tcfc.INT_STS_0 */
	0x00000000, 0x060b4179, 0x000b417c, 0x000b417a, 	/* tcfc.PRTY_STS */
	0x00080005, 0x020b4080, 0x000b4083, 0x000b4081, 	/* tcfc.PRTY_STS_H_0, mode bb */
	0x0008001b, 0x020b4080, 0x000b4083, 0x000b4081, 	/* tcfc.PRTY_STS_H_0, mode k2 */
	0x00060015, 0x040b4080, 0x000b4083, 0x000b4081, 	/* tcfc.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000000, 0x0b060060, 0x00060063, 0x00060061, 	/* igu.INT_STS */
	0x00000000, 0x01060064, 0x00060067, 0x00060065, 	/* igu.PRTY_STS */
	0x09980005, 0x1f060080, 0x00060083, 0x00060081, 	/* igu.PRTY_STS_H_0, mode bb */
	0x09b7001b, 0x1c060080, 0x00060083, 0x00060081, 	/* igu.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1f060080, 0x00060083, 0x00060081, 	/* igu.PRTY_STS_H_0, mode !(bb|k2) */
	0x000001e5, 0x00060084, 0x00060087, 0x00060085, 	/* igu.PRTY_STS_H_1, mode !k2 */
	0x09d30000, 0x0b070035, 0x00070036, 0x00070038, 	/* cau.INT_STS */
	0x09de0005, 0x0d070080, 0x00070083, 0x00070081, 	/* cau.PRTY_STS_H_0, mode bb */
	0x09eb001b, 0x0d070080, 0x00070083, 0x00070081, 	/* cau.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x0a070080, 0x00070083, 0x00070081, 	/* cau.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x013c0060, 0x003c0063, 0x003c0061, 	/* rgfs.INT_STS, mode !(bb|k2) */
	0x00000015, 0x013c0064, 0x003c0067, 0x003c0065, 	/* rgfs.PRTY_STS, mode !(bb|k2) */
	0x00010015, 0x0f3c048c, 0x003c048f, 0x003c048d, 	/* rgfs.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x010c8060, 0x000c8063, 0x000c8061, 	/* rgsrc.INT_STS, mode !(bb|k2) */
	0x00000015, 0x020c8081, 0x000c8084, 0x000c8082, 	/* rgsrc.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x01340060, 0x00340063, 0x00340061, 	/* tgfs.INT_STS, mode !(bb|k2) */
	0x00000015, 0x01340064, 0x00340067, 0x00340065, 	/* tgfs.PRTY_STS, mode !(bb|k2) */
	0x00010015, 0x0f34048c, 0x0034048f, 0x0034048d, 	/* tgfs.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000015, 0x010c8860, 0x000c8863, 0x000c8861, 	/* tgsrc.INT_STS, mode !(bb|k2) */
	0x00000015, 0x020c8881, 0x000c8884, 0x000c8882, 	/* tgsrc.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000003, 0x02014460, 0x00014463, 0x00014461, 	/* umac.INT_STS, mode !bb */
	0x00000000, 0x01004060, 0x00004063, 0x00004061, 	/* dbg.INT_STS */
	0x00000000, 0x01004080, 0x00004083, 0x00004081, 	/* dbg.PRTY_STS_H_0 */
	0x00000000, 0x0e140010, 0x00140013, 0x00140011, 	/* nig.INT_STS_0 */
	0x000e0000, 0x20140014, 0x00140017, 0x00140015, 	/* nig.INT_STS_1 */
	0x002e0000, 0x16140018, 0x0014001b, 0x00140019, 	/* nig.INT_STS_2 */
	0x00440000, 0x1214001c, 0x0014001f, 0x0014001d, 	/* nig.INT_STS_3 */
	0x00560000, 0x16140020, 0x00140023, 0x00140021, 	/* nig.INT_STS_4 */
	0x006c0000, 0x12140024, 0x00140027, 0x00140025, 	/* nig.INT_STS_5 */
	0x007e0003, 0x14140028, 0x0014002b, 0x00140029, 	/* nig.INT_STS_6, mode !bb */
	0x00940003, 0x1214002c, 0x0014002f, 0x0014002d, 	/* nig.INT_STS_7, mode !bb */
	0x00a60003, 0x14140030, 0x00140033, 0x00140031, 	/* nig.INT_STS_8, mode !bb */
	0x00bc0003, 0x12140034, 0x00140037, 0x00140035, 	/* nig.INT_STS_9, mode !bb */
	0x00ce0015, 0x1014003c, 0x0014003f, 0x0014003d, 	/* nig.INT_STS_10, mode !(bb|k2) */
	0x00000005, 0x01140028, 0x0014002b, 0x00140029, 	/* nig.PRTY_STS, mode bb */
	0x00000003, 0x01140038, 0x0014003b, 0x00140039, 	/* nig.PRTY_STS, mode !bb */
	0x09f80005, 0x1f140080, 0x00140083, 0x00140081, 	/* nig.PRTY_STS_H_0, mode bb */
	0x0a17001b, 0x1f140080, 0x00140083, 0x00140081, 	/* nig.PRTY_STS_H_0, mode k2 */
	0x00010015, 0x1f140080, 0x00140083, 0x00140081, 	/* nig.PRTY_STS_H_0, mode !(bb|k2) */
	0x0a360005, 0x1f140084, 0x00140087, 0x00140085, 	/* nig.PRTY_STS_H_1, mode bb */
	0x0a55001b, 0x1f140084, 0x00140087, 0x00140085, 	/* nig.PRTY_STS_H_1, mode k2 */
	0x0a740015, 0x1f140084, 0x00140087, 0x00140085, 	/* nig.PRTY_STS_H_1, mode !(bb|k2) */
	0x0a930005, 0x1f140088, 0x0014008b, 0x00140089, 	/* nig.PRTY_STS_H_2, mode bb */
	0x0ab2001b, 0x1f140088, 0x0014008b, 0x00140089, 	/* nig.PRTY_STS_H_2, mode k2 */
	0x0ad10015, 0x1f140088, 0x0014008b, 0x00140089, 	/* nig.PRTY_STS_H_2, mode !(bb|k2) */
	0x0af00005, 0x1114008c, 0x0014008f, 0x0014008d, 	/* nig.PRTY_STS_H_3, mode bb */
	0x0b01001b, 0x0e14008c, 0x0014008f, 0x0014008d, 	/* nig.PRTY_STS_H_3, mode k2 */
	0x0b0f0015, 0x1714008c, 0x0014008f, 0x0014008d, 	/* nig.PRTY_STS_H_3, mode !(bb|k2) */
	0x00000003, 0x01180010, 0x00180013, 0x00180011, 	/* wol.INT_STS_0, mode !bb */
	0x00000003, 0x18180080, 0x00180083, 0x00180081, 	/* wol.PRTY_STS_H_0, mode !bb */
	0x00000003, 0x01184010, 0x00184013, 0x00184011, 	/* bmbn.INT_STS_0, mode !bb */
	0x0b260005, 0x0e008143, 0x00008146, 0x00008144, 	/* ipc.INT_STS_0, mode bb */
	0x0b26001b, 0x060080b7, 0x000080ba, 0x000080b8, 	/* ipc.INT_STS_0, mode k2 */
	0x0b260015, 0x060080cf, 0x000080d2, 0x000080d0, 	/* ipc.INT_STS_0, mode !(bb|k2) */
	0x00000047, 0x01008147, 0x0000814a, 0x00008148, 	/* ipc.PRTY_STS, mode !(k2|e5) */
	0x0b340003, 0x12200001, 0x00200004, 0x00200002, 	/* nwm.INT_STS, mode !bb */
	0x00000003, 0x1f200080, 0x00200083, 0x00200081, 	/* nwm.PRTY_STS_H_0, mode !bb */
	0x001f0003, 0x1f200084, 0x00200087, 0x00200085, 	/* nwm.PRTY_STS_H_1, mode !bb */
	0x003e0003, 0x0a200088, 0x0020008b, 0x00200089, 	/* nwm.PRTY_STS_H_2, mode !bb */
	0x00000003, 0x011c0060, 0x001c0063, 0x001c0061, 	/* nws.INT_STS_0, mode !bb */
	0x0b460027, 0x0a1c0064, 0x001c0067, 0x001c0065, 	/* nws.INT_STS_1, mode !(bb|e5) */
	0x0b500027, 0x0a1c0068, 0x001c006b, 0x001c0069, 	/* nws.INT_STS_2, mode !(bb|e5) */
	0x0b5a0027, 0x0a1c006c, 0x001c006f, 0x001c006d, 	/* nws.INT_STS_3, mode !(bb|e5) */
	0x0b64001b, 0x041c0080, 0x001c0083, 0x001c0081, 	/* nws.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x011c0080, 0x001c0083, 0x001c0081, 	/* nws.PRTY_STS_H_0, mode !(bb|k2) */
	0x00000003, 0x011a8060, 0x001a8063, 0x001a8061, 	/* ms.INT_STS, mode !bb */
	0x00000003, 0x011ae060, 0x001ae063, 0x001ae061, 	/* led.INT_STS_0, mode !bb */
	0x0b680027, 0x031ad002, 0x001ad005, 0x001ad003, 	/* avs_wrap.INT_STS, mode !(bb|e5) */
	0x00000027, 0x031ad006, 0x001ad009, 0x001ad007, 	/* avs_wrap.PRTY_STS, mode !(bb|e5) */
	0x0b6b001b, 0x14015800, 0x00015803, 0x00015801, 	/* pxpreqbus.PRTY_STS_H_0, mode k2 */
	0x00000015, 0x16015800, 0x00015803, 0x00015801, 	/* pxpreqbus.PRTY_STS_H_0, mode !(bb|k2) */
};
/* Data size: 7072 bytes */

/* Array of attentions data per block */
static const u32 attn_block[] = {
	0x00000000, 0x00000001, 0x00000005, 0x00010001, 	/* block grc, 1 interrupt regs (5 attentions), 1 parity regs (2 attentions) */
	0x00000007, 0x00020002, 0x00000015, 0x00040001, 	/* block miscs, 2 interrupt regs (14 attentions), 1 parity regs (1 attentions) */
	0x00000000, 0x00050001, 0x00000000, 0x00060000, 	/* block misc, 1 interrupt regs (1 attentions) */
	0x00000000, 0x00060000, 0x00000000, 0x00060000, 	/* block dbu */
	0x00000016, 0x00060001, 0x0000002e, 0x00070004, 	/* block pglue_b, 1 interrupt regs (24 attentions), 4 parity regs (35 attentions) */
	0x00000051, 0x000b0002, 0x0000005b, 0x000d0002, 	/* block cnig, 2 interrupt regs (10 attentions), 2 parity regs (2 attentions) */
	0x00000000, 0x000f0001, 0x00000000, 0x00100000, 	/* block cpmu, 1 interrupt regs (1 attentions) */
	0x00000000, 0x00100001, 0x0000005d, 0x00110001, 	/* block ncsi, 1 interrupt regs (1 attentions), 1 parity regs (3 attentions) */
	0x00000000, 0x00120000, 0x00000060, 0x00120002, 	/* block opte, 2 parity regs (12 attentions) */
	0x0000006c, 0x0014000c, 0x00000196, 0x00200005, 	/* block bmb, 12 interrupt regs (298 attentions), 5 parity regs (66 attentions) */
	0x000001d8, 0x00250001, 0x000001e9, 0x00260003, 	/* block pcie, 1 interrupt regs (17 attentions), 3 parity regs (24 attentions) */
	0x00000000, 0x00290000, 0x00000000, 0x00290000, 	/* block mcp */
	0x00000000, 0x00290000, 0x00000201, 0x00290004, 	/* block mcp2, 4 parity regs (28 attentions) */
	0x0000021d, 0x002d0001, 0x0000022f, 0x002e0002, 	/* block pswhst, 1 interrupt regs (18 attentions), 2 parity regs (18 attentions) */
	0x00000241, 0x00300001, 0x0000002e, 0x00310001, 	/* block pswhst2, 1 interrupt regs (5 attentions), 1 parity regs (1 attentions) */
	0x00000246, 0x00320001, 0x0000002e, 0x00330001, 	/* block pswrd, 1 interrupt regs (3 attentions), 1 parity regs (1 attentions) */
	0x00000249, 0x00340001, 0x0000024e, 0x00350007, 	/* block pswrd2, 1 interrupt regs (5 attentions), 7 parity regs (44 attentions) */
	0x0000027a, 0x003c0001, 0x0000002e, 0x003d0001, 	/* block pswwr, 1 interrupt regs (19 attentions), 1 parity regs (1 attentions) */
	0x0000028d, 0x003e0001, 0x000002a3, 0x003f000e, 	/* block pswwr2, 1 interrupt regs (22 attentions), 14 parity regs (161 attentions) */
	0x00000344, 0x004d0001, 0x0000035b, 0x004e0001, 	/* block pswrq, 1 interrupt regs (23 attentions), 1 parity regs (1 attentions) */
	0x0000035c, 0x004f0001, 0x0000036e, 0x00500003, 	/* block pswrq2, 1 interrupt regs (18 attentions), 3 parity regs (14 attentions) */
	0x0000037c, 0x00530001, 0x00000000, 0x00540000, 	/* block pglcs, 1 interrupt regs (2 attentions) */
	0x0000037e, 0x00540001, 0x0000005d, 0x00550001, 	/* block dmae, 1 interrupt regs (2 attentions), 1 parity regs (3 attentions) */
	0x00000380, 0x00560001, 0x00000388, 0x00570003, 	/* block ptu, 1 interrupt regs (8 attentions), 3 parity regs (20 attentions) */
	0x0000039c, 0x005a0005, 0x000003cd, 0x005f0006, 	/* block tcm, 5 interrupt regs (49 attentions), 6 parity regs (58 attentions) */
	0x00000407, 0x00650005, 0x00000436, 0x006a0006, 	/* block mcm, 5 interrupt regs (47 attentions), 6 parity regs (52 attentions) */
	0x0000046a, 0x00700005, 0x0000049d, 0x00750006, 	/* block ucm, 5 interrupt regs (51 attentions), 6 parity regs (48 attentions) */
	0x000004cd, 0x007b0003, 0x00000500, 0x007e0005, 	/* block xcm, 3 interrupt regs (51 attentions), 5 parity regs (51 attentions) */
	0x00000533, 0x00830003, 0x0000055a, 0x00860006, 	/* block ycm, 3 interrupt regs (39 attentions), 6 parity regs (57 attentions) */
	0x00000593, 0x008c0005, 0x000005ab, 0x00910003, 	/* block pcm, 5 interrupt regs (24 attentions), 3 parity regs (24 attentions) */
	0x000005c3, 0x00940001, 0x000005d9, 0x00950009, 	/* block qm, 1 interrupt regs (22 attentions), 9 parity regs (116 attentions) */
	0x0000064d, 0x009e0002, 0x00000678, 0x00a00003, 	/* block tm, 2 interrupt regs (43 attentions), 3 parity regs (20 attentions) */
	0x0000068c, 0x00a30001, 0x00000698, 0x00a40004, 	/* block dorq, 1 interrupt regs (12 attentions), 4 parity regs (13 attentions) */
	0x000006a5, 0x00a8000c, 0x0000078e, 0x00b40008, 	/* block brb, 12 interrupt regs (233 attentions), 8 parity regs (83 attentions) */
	0x00000000, 0x00bc0001, 0x00000000, 0x00bd0000, 	/* block src, 1 interrupt regs (1 attentions) */
	0x000007e1, 0x00bd0001, 0x000007e3, 0x00be0006, 	/* block prs, 1 interrupt regs (2 attentions), 6 parity regs (78 attentions) */
	0x00000831, 0x00c40001, 0x00000850, 0x00c50003, 	/* block tsdm, 1 interrupt regs (31 attentions), 3 parity regs (11 attentions) */
	0x00000831, 0x00c80001, 0x0000085b, 0x00c90003, 	/* block msdm, 1 interrupt regs (31 attentions), 3 parity regs (14 attentions) */
	0x00000831, 0x00cc0001, 0x00000869, 0x00cd0003, 	/* block usdm, 1 interrupt regs (31 attentions), 3 parity regs (11 attentions) */
	0x00000831, 0x00d00001, 0x00000874, 0x00d10003, 	/* block xsdm, 1 interrupt regs (31 attentions), 3 parity regs (11 attentions) */
	0x00000831, 0x00d40001, 0x0000087f, 0x00d50003, 	/* block ysdm, 1 interrupt regs (31 attentions), 3 parity regs (10 attentions) */
	0x00000831, 0x00d80001, 0x00000889, 0x00d90003, 	/* block psdm, 1 interrupt regs (31 attentions), 3 parity regs (12 attentions) */
	0x00000895, 0x00dc0006, 0x00000917, 0x00e20008, 	/* block tsem, 6 interrupt regs (130 attentions), 8 parity regs (32 attentions) */
	0x00000895, 0x00ea0006, 0x00000937, 0x00f00007, 	/* block msem, 6 interrupt regs (130 attentions), 7 parity regs (33 attentions) */
	0x00000895, 0x00f70006, 0x00000917, 0x00fd0005, 	/* block usem, 6 interrupt regs (130 attentions), 5 parity regs (27 attentions) */
	0x00000895, 0x01020006, 0x00000958, 0x01080005, 	/* block xsem, 6 interrupt regs (130 attentions), 5 parity regs (28 attentions) */
	0x00000895, 0x010d0006, 0x00000958, 0x01130005, 	/* block ysem, 6 interrupt regs (130 attentions), 5 parity regs (28 attentions) */
	0x00000895, 0x01180006, 0x00000917, 0x011e0008, 	/* block psem, 6 interrupt regs (130 attentions), 8 parity regs (32 attentions) */
	0x00000974, 0x01260001, 0x0000098a, 0x01270001, 	/* block rss, 1 interrupt regs (22 attentions), 1 parity regs (10 attentions) */
	0x00000994, 0x01280001, 0x0000099a, 0x01290003, 	/* block tmld, 1 interrupt regs (6 attentions), 3 parity regs (15 attentions) */
	0x00000994, 0x012c0001, 0x000009a9, 0x012d0003, 	/* block muld, 1 interrupt regs (6 attentions), 3 parity regs (19 attentions) */
	0x00000994, 0x01300001, 0x000009bc, 0x01310001, 	/* block yuld, 1 interrupt regs (6 attentions), 1 parity regs (6 attentions) */
	0x00000994, 0x01320001, 0x000009c2, 0x01330003, 	/* block xyld, 1 interrupt regs (6 attentions), 3 parity regs (17 attentions) */
	0x00000994, 0x01360001, 0x000009d3, 0x01370001, 	/* block ptld, 1 interrupt regs (6 attentions), 1 parity regs (8 attentions) */
	0x00000994, 0x01380001, 0x000009d3, 0x01390001, 	/* block ypld, 1 interrupt regs (6 attentions), 1 parity regs (8 attentions) */
	0x000009db, 0x013a0001, 0x000009e6, 0x013b0004, 	/* block prm, 1 interrupt regs (11 attentions), 4 parity regs (33 attentions) */
	0x00000a07, 0x013f0001, 0x0000002e, 0x01400001, 	/* block pbf_pb1, 1 interrupt regs (9 attentions), 1 parity regs (1 attentions) */
	0x00000a07, 0x01410001, 0x0000002e, 0x01420001, 	/* block pbf_pb2, 1 interrupt regs (9 attentions), 1 parity regs (1 attentions) */
	0x00000a07, 0x01430001, 0x0000002e, 0x01440001, 	/* block rpb, 1 interrupt regs (9 attentions), 1 parity regs (1 attentions) */
	0x00000a10, 0x0145000b, 0x00000a91, 0x01500005, 	/* block btb, 11 interrupt regs (129 attentions), 5 parity regs (44 attentions) */
	0x00000000, 0x01550001, 0x00000abd, 0x01560008, 	/* block pbf, 1 interrupt regs (1 attentions), 8 parity regs (93 attentions) */
	0x00000b1a, 0x015e0001, 0x0000002e, 0x015f0001, 	/* block rdif, 1 interrupt regs (9 attentions), 1 parity regs (1 attentions) */
	0x00000b1a, 0x01600001, 0x00000b23, 0x01610004, 	/* block tdif, 1 interrupt regs (9 attentions), 4 parity regs (13 attentions) */
	0x00000b30, 0x01650001, 0x00000b38, 0x01660003, 	/* block cdu, 1 interrupt regs (8 attentions), 3 parity regs (5 attentions) */
	0x00000b3d, 0x01690001, 0x00000b3f, 0x016a0004, 	/* block ccfc, 1 interrupt regs (2 attentions), 4 parity regs (13 attentions) */
	0x00000b3d, 0x016e0001, 0x00000b4c, 0x016f0004, 	/* block tcfc, 1 interrupt regs (2 attentions), 4 parity regs (10 attentions) */
	0x00000b56, 0x01730001, 0x00000b61, 0x01740005, 	/* block igu, 1 interrupt regs (11 attentions), 5 parity regs (58 attentions) */
	0x00000b9b, 0x01790001, 0x00000ba6, 0x017a0003, 	/* block cau, 1 interrupt regs (11 attentions), 3 parity regs (16 attentions) */
	0x00000000, 0x017d0001, 0x00000bb6, 0x017e0002, 	/* block rgfs, 1 interrupt regs (1 attentions), 2 parity regs (16 attentions) */
	0x00000000, 0x01800001, 0x00000990, 0x01810001, 	/* block rgsrc, 1 interrupt regs (1 attentions), 1 parity regs (2 attentions) */
	0x00000000, 0x01820001, 0x00000bc6, 0x01830002, 	/* block tgfs, 1 interrupt regs (1 attentions), 2 parity regs (16 attentions) */
	0x00000000, 0x01850001, 0x00000990, 0x01860001, 	/* block tgsrc, 1 interrupt regs (1 attentions), 1 parity regs (2 attentions) */
	0x00000bd6, 0x01870001, 0x00000000, 0x01880000, 	/* block umac, 1 interrupt regs (2 attentions) */
	0x00000000, 0x01880000, 0x00000000, 0x01880000, 	/* block xmac */
	0x00000000, 0x01880001, 0x00000050, 0x01890001, 	/* block dbg, 1 interrupt regs (1 attentions), 1 parity regs (1 attentions) */
	0x00000bd8, 0x018a000b, 0x00000cb6, 0x0195000e, 	/* block nig, 11 interrupt regs (222 attentions), 14 parity regs (119 attentions) */
	0x00000000, 0x01a30001, 0x00000d2d, 0x01a40001, 	/* block wol, 1 interrupt regs (1 attentions), 1 parity regs (24 attentions) */
	0x00000000, 0x01a50001, 0x00000000, 0x01a60000, 	/* block bmbn, 1 interrupt regs (1 attentions) */
	0x00000d45, 0x01a60003, 0x00000d52, 0x01a90001, 	/* block ipc, 3 interrupt regs (13 attentions), 1 parity regs (1 attentions) */
	0x00000d53, 0x01aa0001, 0x00000d64, 0x01ab0003, 	/* block nwm, 1 interrupt regs (17 attentions), 3 parity regs (72 attentions) */
	0x00000dac, 0x01ae0004, 0x00000dd1, 0x01b20002, 	/* block nws, 4 interrupt regs (37 attentions), 2 parity regs (4 attentions) */
	0x00000000, 0x01b40001, 0x00000000, 0x01b50000, 	/* block ms, 1 interrupt regs (1 attentions) */
	0x00000000, 0x01b50000, 0x00000000, 0x01b50000, 	/* block phy_pcie */
	0x00000000, 0x01b50001, 0x00000000, 0x01b60000, 	/* block led, 1 interrupt regs (1 attentions) */
	0x00000dd5, 0x01b60001, 0x00000dd7, 0x01b70001, 	/* block avs_wrap, 1 interrupt regs (2 attentions), 1 parity regs (3 attentions) */
	0x00000000, 0x01b80000, 0x00000dda, 0x01b80002, 	/* block pxpreqbus, 2 parity regs (22 attentions) */
	0x00000000, 0x01ba0000, 0x00000000, 0x01ba0000, 	/* block misc_aeu */
	0x00000000, 0x01ba0000, 0x00000000, 0x01ba0000, 	/* block bar0_map */
};
/* Data size: 1408 bytes */

/* Debug Bus lines */
static const u32 dbg_bus_lines[] = {
	0x0301, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 0x0202, 
	0x0202, 0x0202, 0x0301, 0x0103, 0x0004, 0x0004, 0x0301, 0x0502, 0x1003, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0002, 0x0301, 0x0301, 
	0x0301, 0x0311, 0x0311, 0x0311, 0x0311, 0x0502, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0502, 0x0301, 0x0301, 0x0003, 0x0301, 0x0101, 0x0102, 
	0x0301, 0x0502, 0x1003, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0002, 0x0301, 0x0301, 0x0301, 0x0311, 0x0311, 0x0311, 0x0311, 0x0502, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0502, 0x0301, 0x0301, 0x0003, 
	0x0301, 0x0101, 0x0102, 0x0502, 0x0502, 0x0502, 0x0502, 0x0502, 0x0101, 
	0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x1003, 0x1003, 0x1003, 
	0x1003, 0x1003, 0x1003, 0x1003, 0x1003, 0x1003, 0x1003, 0x0101, 0x0101, 
	0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 
	0x0101, 0x0101, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 0x0101, 
	0x0101, 0x0101, 0x0101, 0x0101, 0x0301, 0x0101, 0x0101, 0x0001, 0x0001, 
	0x0301, 0x0101, 0x0101, 0x0101, 0x0101, 0x0001, 0x0001, 0x0001, 0x0001, 
	0x0001, 0x0001, 0x0001, 0x0001, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0001, 0x0301, 0x0301, 0x0502, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0001, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 0x0004, 
	0x0004, 0x0502, 0x0301, 0x0301, 0x0502, 0x0301, 0x0301, 0x0502, 0x0502, 
	0x0301, 0x0014, 0x0502, 0x0512, 0x0301, 0x0301, 0x0512, 0x0502, 0x0402, 
	0x0502, 0x0001, 0x0311, 0x0311, 0x0502, 0x0301, 0x0002, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0202, 0x0502, 0x0502, 0x0502, 
	0x0502, 0x0502, 0x0502, 0x0301, 0x0311, 0x0311, 0x0001, 0x0301, 0x0001, 
	0x0101, 0x0301, 0x0103, 0x0102, 0x0301, 0x0301, 0x0301, 0x0301, 0x0101, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0512, 0x0512, 0x0502, 0x0301, 0x0311, 
	0x0512, 0x0512, 0x0502, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0101, 
	0x0301, 0x1003, 0x0502, 0x0502, 0x0202, 0x0202, 0x1003, 0x0004, 0x1003, 
	0x0004, 0x0001, 0x0301, 0x0001, 0x0002, 0x0002, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0101, 0x0301, 0x0301, 0x0101, 0x0001, 0x0301, 0x0502, 
	0x0502, 0x0301, 0x0301, 0x0301, 0x0301, 0x0502, 0x0301, 0x0301, 0x0402, 
	0x0502, 0x0502, 0x0402, 0x0502, 0x0502, 0x0502, 0x0502, 0x0002, 0x0102, 
	0x0502, 0x0002, 0x0102, 0x0502, 0x0002, 0x0102, 0x0502, 0x1003, 0x0301, 
	0x0301, 0x0301, 0x0101, 0x0301, 0x0002, 0x0502, 0x0502, 0x0101, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0101, 0x0301, 0x0301, 0x0301, 0x0101, 0x0301, 
	0x0301, 0x0301, 0x0101, 0x0301, 0x0301, 0x0301, 0x0101, 0x0301, 0x0301, 
	0x0301, 0x0101, 0x0502, 0x0002, 0x0001, 0x0301, 0x0502, 0x0301, 0x0301, 
	0x0301, 0x0001, 0x1003, 0x0003, 0x0301, 0x0502, 0x0402, 0x0001, 0x0101, 
	0x0402, 0x0102, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0002, 
	0x0201, 0x0201, 0x0201, 0x0201, 0x0201, 0x0004, 0x0201, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0502, 0x0502, 0x0201, 0x0301, 0x0502, 0x0201, 0x0201, 0x0201, 0x0301, 
	0x0301, 0x0201, 0x0301, 0x0201, 0x0301, 0x0502, 0x0202, 0x0103, 0x0103, 
	0x0301, 0x0103, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0502, 0x0014, 0x0311, 0x0014, 0x0011, 0x0101, 0x0301, 
	0x0201, 0x0301, 0x0101, 0x0301, 0x0101, 0x0001, 0x0001, 0x0301, 0x0101, 
	0x0001, 0x0001, 0x0301, 0x0101, 0x0301, 0x0101, 0x0001, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0101, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0201, 0x0101, 0x0301, 0x0201, 0x0201, 
	0x0101, 0x0301, 0x0101, 0x0301, 0x0201, 0x0101, 0x0301, 0x0201, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0001, 0x0001, 0x0001, 0x0111, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0003, 0x0301, 0x0301, 
	0x0301, 0x0001, 0x0301, 0x0001, 0x0301, 0x0001, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0502, 0x0004, 0x0512, 0x0512, 0x0301, 0x0301, 
	0x0502, 0x0004, 0x0301, 0x0512, 0x0301, 0x0301, 0x0502, 0x0004, 0x0301, 
	0x0512, 0x0301, 0x0301, 0x0301, 0x0301, 0x0111, 0x0311, 0x0301, 0x0101, 
	0x0301, 0x0201, 0x0001, 0x0311, 0x0311, 0x0311, 0x0311, 0x0301, 0x0301, 
	0x0301, 0x0111, 0x0311, 0x0301, 0x0101, 0x0301, 0x0201, 0x0001, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0301, 0x0301, 0x0311, 0x0311, 0x0301, 0x0301, 
	0x0001, 0x0001, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0101, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0311, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0311, 
	0x0311, 0x0301, 0x0301, 0x0311, 0x0301, 0x0301, 0x0502, 0x0311, 0x0502, 
	0x0301, 0x0301, 0x0301, 0x0502, 0x0311, 0x0301, 0x0502, 0x0301, 0x0014, 
	0x0502, 0x0311, 0x0502, 0x0502, 0x0502, 0x0502, 0x0502, 0x0502, 0x0502, 
	0x0502, 0x0502, 0x0502, 0x0502, 0x0014, 0x0502, 0x0311, 0x0004, 0x0004, 
	0x0004, 0x0001, 0x0014, 0x0014, 0x0014, 0x0012, 0x0101, 0x0512, 0x0512, 
	0x0502, 0x0301, 0x0004, 0x0004, 0x0004, 0x0001, 0x0014, 0x0014, 0x0014, 
	0x0012, 0x0101, 0x0512, 0x0512, 0x0502, 0x0301, 0x0004, 0x0004, 0x0004, 
	0x0002, 0x0301, 0x0102, 0x0502, 0x0301, 0x0311, 0x0402, 0x0201, 0x0502, 
	0x0502, 0x0402, 0x0301, 0x0201, 0x0101, 0x0802, 0x0301, 0x0201, 0x0301, 
	0x0502, 0x0502, 0x0301, 0x0301, 0x0101, 0x0101, 0x0201, 0x0201, 0x0301, 
	0x0802, 0x0301, 0x0202, 0x0301, 0x0202, 0x0301, 0x0301, 0x0802, 0x0301, 
	0x0802, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 0x0301, 
	0x0301, 0x0301, 0x0402, 0x0102, 0x0502, 0x0502, 0x0502, 0x0201, 0x0301, 
	0x0301, 0x0301, 0x0301, 0x0301, 0x0502, 0x0502, 0x0502, 0x0502, 0x0502, 
	0x0111, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0004, 0x0004, 
	0x0301, 0x0301, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 0x0311, 
	0x0311, 
};
/* Data size: 1802 bytes */

/* Debug Bus blocks */
static const u32 dbg_bus_blocks[] = {
	0x0000000f, 	/* grc, bb, 15 lines */
	0x0000000f, 	/* grc, k2, 15 lines */
	0x00000000, 	/* grc, e5, 0 lines */
	0x00000000, 	/* miscs, bb, 0 lines */
	0x00000000, 	/* miscs, k2, 0 lines */
	0x00000000, 	/* miscs, e5, 0 lines */
	0x00000000, 	/* misc, bb, 0 lines */
	0x00000000, 	/* misc, k2, 0 lines */
	0x00000000, 	/* misc, e5, 0 lines */
	0x00000000, 	/* dbu, bb, 0 lines */
	0x00000000, 	/* dbu, k2, 0 lines */
	0x00000000, 	/* dbu, e5, 0 lines */
	0x000f0127, 	/* pglue_b, bb, 39 lines */
	0x0036012a, 	/* pglue_b, k2, 42 lines */
	0x00000000, 	/* pglue_b, e5, 0 lines */
	0x00000000, 	/* cnig, bb, 0 lines */
	0x00120102, 	/* cnig, k2, 2 lines */
	0x00000000, 	/* cnig, e5, 0 lines */
	0x00000000, 	/* cpmu, bb, 0 lines */
	0x00000000, 	/* cpmu, k2, 0 lines */
	0x00000000, 	/* cpmu, e5, 0 lines */
	0x00000001, 	/* ncsi, bb, 1 lines */
	0x00000001, 	/* ncsi, k2, 1 lines */
	0x00000000, 	/* ncsi, e5, 0 lines */
	0x00000000, 	/* opte, bb, 0 lines */
	0x00000000, 	/* opte, k2, 0 lines */
	0x00000000, 	/* opte, e5, 0 lines */
	0x00600085, 	/* bmb, bb, 133 lines */
	0x00600085, 	/* bmb, k2, 133 lines */
	0x00000000, 	/* bmb, e5, 0 lines */
	0x00000000, 	/* pcie, bb, 0 lines */
	0x00e50033, 	/* pcie, k2, 51 lines */
	0x00000000, 	/* pcie, e5, 0 lines */
	0x00000000, 	/* mcp, bb, 0 lines */
	0x00000000, 	/* mcp, k2, 0 lines */
	0x00000000, 	/* mcp, e5, 0 lines */
	0x01180009, 	/* mcp2, bb, 9 lines */
	0x01180009, 	/* mcp2, k2, 9 lines */
	0x00000000, 	/* mcp2, e5, 0 lines */
	0x01210104, 	/* pswhst, bb, 4 lines */
	0x01210104, 	/* pswhst, k2, 4 lines */
	0x00000000, 	/* pswhst, e5, 0 lines */
	0x01250103, 	/* pswhst2, bb, 3 lines */
	0x01250103, 	/* pswhst2, k2, 3 lines */
	0x00000000, 	/* pswhst2, e5, 0 lines */
	0x00340101, 	/* pswrd, bb, 1 lines */
	0x00340101, 	/* pswrd, k2, 1 lines */
	0x00000000, 	/* pswrd, e5, 0 lines */
	0x01280119, 	/* pswrd2, bb, 25 lines */
	0x01280119, 	/* pswrd2, k2, 25 lines */
	0x00000000, 	/* pswrd2, e5, 0 lines */
	0x01410109, 	/* pswwr, bb, 9 lines */
	0x01410109, 	/* pswwr, k2, 9 lines */
	0x00000000, 	/* pswwr, e5, 0 lines */
	0x00000000, 	/* pswwr2, bb, 0 lines */
	0x00000000, 	/* pswwr2, k2, 0 lines */
	0x00000000, 	/* pswwr2, e5, 0 lines */
	0x001c0001, 	/* pswrq, bb, 1 lines */
	0x001c0001, 	/* pswrq, k2, 1 lines */
	0x00000000, 	/* pswrq, e5, 0 lines */
	0x014a0015, 	/* pswrq2, bb, 21 lines */
	0x014a0015, 	/* pswrq2, k2, 21 lines */
	0x00000000, 	/* pswrq2, e5, 0 lines */
	0x00000000, 	/* pglcs, bb, 0 lines */
	0x00120006, 	/* pglcs, k2, 6 lines */
	0x00000000, 	/* pglcs, e5, 0 lines */
	0x00100001, 	/* dmae, bb, 1 lines */
	0x00100001, 	/* dmae, k2, 1 lines */
	0x00000000, 	/* dmae, e5, 0 lines */
	0x015f0105, 	/* ptu, bb, 5 lines */
	0x015f0105, 	/* ptu, k2, 5 lines */
	0x00000000, 	/* ptu, e5, 0 lines */
	0x01640120, 	/* tcm, bb, 32 lines */
	0x01640120, 	/* tcm, k2, 32 lines */
	0x00000000, 	/* tcm, e5, 0 lines */
	0x01640120, 	/* mcm, bb, 32 lines */
	0x01640120, 	/* mcm, k2, 32 lines */
	0x00000000, 	/* mcm, e5, 0 lines */
	0x01640120, 	/* ucm, bb, 32 lines */
	0x01640120, 	/* ucm, k2, 32 lines */
	0x00000000, 	/* ucm, e5, 0 lines */
	0x01640120, 	/* xcm, bb, 32 lines */
	0x01640120, 	/* xcm, k2, 32 lines */
	0x00000000, 	/* xcm, e5, 0 lines */
	0x01640120, 	/* ycm, bb, 32 lines */
	0x01640120, 	/* ycm, k2, 32 lines */
	0x00000000, 	/* ycm, e5, 0 lines */
	0x01640120, 	/* pcm, bb, 32 lines */
	0x01640120, 	/* pcm, k2, 32 lines */
	0x00000000, 	/* pcm, e5, 0 lines */
	0x01840062, 	/* qm, bb, 98 lines */
	0x01840062, 	/* qm, k2, 98 lines */
	0x00000000, 	/* qm, e5, 0 lines */
	0x01e60021, 	/* tm, bb, 33 lines */
	0x01e60021, 	/* tm, k2, 33 lines */
	0x00000000, 	/* tm, e5, 0 lines */
	0x02070107, 	/* dorq, bb, 7 lines */
	0x02070107, 	/* dorq, k2, 7 lines */
	0x00000000, 	/* dorq, e5, 0 lines */
	0x00600185, 	/* brb, bb, 133 lines */
	0x00600185, 	/* brb, k2, 133 lines */
	0x00000000, 	/* brb, e5, 0 lines */
	0x020e0019, 	/* src, bb, 25 lines */
	0x020c001a, 	/* src, k2, 26 lines */
	0x00000000, 	/* src, e5, 0 lines */
	0x02270104, 	/* prs, bb, 4 lines */
	0x02270104, 	/* prs, k2, 4 lines */
	0x00000000, 	/* prs, e5, 0 lines */
	0x022b0133, 	/* tsdm, bb, 51 lines */
	0x022b0133, 	/* tsdm, k2, 51 lines */
	0x00000000, 	/* tsdm, e5, 0 lines */
	0x022b0133, 	/* msdm, bb, 51 lines */
	0x022b0133, 	/* msdm, k2, 51 lines */
	0x00000000, 	/* msdm, e5, 0 lines */
	0x022b0133, 	/* usdm, bb, 51 lines */
	0x022b0133, 	/* usdm, k2, 51 lines */
	0x00000000, 	/* usdm, e5, 0 lines */
	0x022b0133, 	/* xsdm, bb, 51 lines */
	0x022b0133, 	/* xsdm, k2, 51 lines */
	0x00000000, 	/* xsdm, e5, 0 lines */
	0x022b0133, 	/* ysdm, bb, 51 lines */
	0x022b0133, 	/* ysdm, k2, 51 lines */
	0x00000000, 	/* ysdm, e5, 0 lines */
	0x022b0133, 	/* psdm, bb, 51 lines */
	0x022b0133, 	/* psdm, k2, 51 lines */
	0x00000000, 	/* psdm, e5, 0 lines */
	0x025e010c, 	/* tsem, bb, 12 lines */
	0x025e010c, 	/* tsem, k2, 12 lines */
	0x00000000, 	/* tsem, e5, 0 lines */
	0x025e010c, 	/* msem, bb, 12 lines */
	0x025e010c, 	/* msem, k2, 12 lines */
	0x00000000, 	/* msem, e5, 0 lines */
	0x025e010c, 	/* usem, bb, 12 lines */
	0x025e010c, 	/* usem, k2, 12 lines */
	0x00000000, 	/* usem, e5, 0 lines */
	0x025e010c, 	/* xsem, bb, 12 lines */
	0x025e010c, 	/* xsem, k2, 12 lines */
	0x00000000, 	/* xsem, e5, 0 lines */
	0x025e010c, 	/* ysem, bb, 12 lines */
	0x025e010c, 	/* ysem, k2, 12 lines */
	0x00000000, 	/* ysem, e5, 0 lines */
	0x025e010c, 	/* psem, bb, 12 lines */
	0x025e010c, 	/* psem, k2, 12 lines */
	0x00000000, 	/* psem, e5, 0 lines */
	0x026a000d, 	/* rss, bb, 13 lines */
	0x026a000d, 	/* rss, k2, 13 lines */
	0x00000000, 	/* rss, e5, 0 lines */
	0x02770106, 	/* tmld, bb, 6 lines */
	0x02770106, 	/* tmld, k2, 6 lines */
	0x00000000, 	/* tmld, e5, 0 lines */
	0x027d0106, 	/* muld, bb, 6 lines */
	0x027d0106, 	/* muld, k2, 6 lines */
	0x00000000, 	/* muld, e5, 0 lines */
	0x02770005, 	/* yuld, bb, 5 lines */
	0x02770005, 	/* yuld, k2, 5 lines */
	0x00000000, 	/* yuld, e5, 0 lines */
	0x02830107, 	/* xyld, bb, 7 lines */
	0x027d0107, 	/* xyld, k2, 7 lines */
	0x00000000, 	/* xyld, e5, 0 lines */
	0x00000000, 	/* ptld, bb, 0 lines */
	0x00000000, 	/* ptld, k2, 0 lines */
	0x00000000, 	/* ptld, e5, 0 lines */
	0x00000000, 	/* ypld, bb, 0 lines */
	0x00000000, 	/* ypld, k2, 0 lines */
	0x00000000, 	/* ypld, e5, 0 lines */
	0x028a010e, 	/* prm, bb, 14 lines */
	0x02980110, 	/* prm, k2, 16 lines */
	0x00000000, 	/* prm, e5, 0 lines */
	0x02a8000d, 	/* pbf_pb1, bb, 13 lines */
	0x02a8000d, 	/* pbf_pb1, k2, 13 lines */
	0x00000000, 	/* pbf_pb1, e5, 0 lines */
	0x02a8000d, 	/* pbf_pb2, bb, 13 lines */
	0x02a8000d, 	/* pbf_pb2, k2, 13 lines */
	0x00000000, 	/* pbf_pb2, e5, 0 lines */
	0x02a8000d, 	/* rpb, bb, 13 lines */
	0x02a8000d, 	/* rpb, k2, 13 lines */
	0x00000000, 	/* rpb, e5, 0 lines */
	0x00600185, 	/* btb, bb, 133 lines */
	0x00600185, 	/* btb, k2, 133 lines */
	0x00000000, 	/* btb, e5, 0 lines */
	0x02b50117, 	/* pbf, bb, 23 lines */
	0x02b50117, 	/* pbf, k2, 23 lines */
	0x00000000, 	/* pbf, e5, 0 lines */
	0x02cc0006, 	/* rdif, bb, 6 lines */
	0x02cc0006, 	/* rdif, k2, 6 lines */
	0x00000000, 	/* rdif, e5, 0 lines */
	0x02d20006, 	/* tdif, bb, 6 lines */
	0x02d20006, 	/* tdif, k2, 6 lines */
	0x00000000, 	/* tdif, e5, 0 lines */
	0x02d80003, 	/* cdu, bb, 3 lines */
	0x02db000e, 	/* cdu, k2, 14 lines */
	0x00000000, 	/* cdu, e5, 0 lines */
	0x02e9010d, 	/* ccfc, bb, 13 lines */
	0x02f60117, 	/* ccfc, k2, 23 lines */
	0x00000000, 	/* ccfc, e5, 0 lines */
	0x02e9010d, 	/* tcfc, bb, 13 lines */
	0x02f60117, 	/* tcfc, k2, 23 lines */
	0x00000000, 	/* tcfc, e5, 0 lines */
	0x030d0133, 	/* igu, bb, 51 lines */
	0x030d0133, 	/* igu, k2, 51 lines */
	0x00000000, 	/* igu, e5, 0 lines */
	0x03400106, 	/* cau, bb, 6 lines */
	0x03400106, 	/* cau, k2, 6 lines */
	0x00000000, 	/* cau, e5, 0 lines */
	0x00000000, 	/* rgfs, bb, 0 lines */
	0x00000000, 	/* rgfs, k2, 0 lines */
	0x00000000, 	/* rgfs, e5, 0 lines */
	0x00000000, 	/* rgsrc, bb, 0 lines */
	0x00000000, 	/* rgsrc, k2, 0 lines */
	0x00000000, 	/* rgsrc, e5, 0 lines */
	0x00000000, 	/* tgfs, bb, 0 lines */
	0x00000000, 	/* tgfs, k2, 0 lines */
	0x00000000, 	/* tgfs, e5, 0 lines */
	0x00000000, 	/* tgsrc, bb, 0 lines */
	0x00000000, 	/* tgsrc, k2, 0 lines */
	0x00000000, 	/* tgsrc, e5, 0 lines */
	0x00000000, 	/* umac, bb, 0 lines */
	0x00120006, 	/* umac, k2, 6 lines */
	0x00000000, 	/* umac, e5, 0 lines */
	0x00000000, 	/* xmac, bb, 0 lines */
	0x00000000, 	/* xmac, k2, 0 lines */
	0x00000000, 	/* xmac, e5, 0 lines */
	0x00000000, 	/* dbg, bb, 0 lines */
	0x00000000, 	/* dbg, k2, 0 lines */
	0x00000000, 	/* dbg, e5, 0 lines */
	0x0346012b, 	/* nig, bb, 43 lines */
	0x0346011d, 	/* nig, k2, 29 lines */
	0x00000000, 	/* nig, e5, 0 lines */
	0x00000000, 	/* wol, bb, 0 lines */
	0x001c0002, 	/* wol, k2, 2 lines */
	0x00000000, 	/* wol, e5, 0 lines */
	0x00000000, 	/* bmbn, bb, 0 lines */
	0x00210008, 	/* bmbn, k2, 8 lines */
	0x00000000, 	/* bmbn, e5, 0 lines */
	0x00000000, 	/* ipc, bb, 0 lines */
	0x00000000, 	/* ipc, k2, 0 lines */
	0x00000000, 	/* ipc, e5, 0 lines */
	0x00000000, 	/* nwm, bb, 0 lines */
	0x0371000b, 	/* nwm, k2, 11 lines */
	0x00000000, 	/* nwm, e5, 0 lines */
	0x00000000, 	/* nws, bb, 0 lines */
	0x037c0009, 	/* nws, k2, 9 lines */
	0x00000000, 	/* nws, e5, 0 lines */
	0x00000000, 	/* ms, bb, 0 lines */
	0x00120004, 	/* ms, k2, 4 lines */
	0x00000000, 	/* ms, e5, 0 lines */
	0x00000000, 	/* phy_pcie, bb, 0 lines */
	0x00e5001a, 	/* phy_pcie, k2, 26 lines */
	0x00000000, 	/* phy_pcie, e5, 0 lines */
	0x00000000, 	/* led, bb, 0 lines */
	0x00000000, 	/* led, k2, 0 lines */
	0x00000000, 	/* led, e5, 0 lines */
	0x00000000, 	/* avs_wrap, bb, 0 lines */
	0x00000000, 	/* avs_wrap, k2, 0 lines */
	0x00000000, 	/* avs_wrap, e5, 0 lines */
	0x00000000, 	/* pxpreqbus, bb, 0 lines */
	0x00000000, 	/* pxpreqbus, k2, 0 lines */
	0x00000000, 	/* pxpreqbus, e5, 0 lines */
	0x00000000, 	/* bar0_map, bb, 0 lines */
	0x00000000, 	/* bar0_map, k2, 0 lines */
	0x00000000, 	/* bar0_map, e5, 0 lines */
};
/* Data size: 1044 bytes */

#endif /* __DBG_VALUES_H__ */
