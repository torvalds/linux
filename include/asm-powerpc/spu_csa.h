/*
 * spu_csa.h: Definitions for SPU context save area (CSA).
 *
 * (C) Copyright IBM 2005
 *
 * Author: Mark Nutter <mnutter@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _SPU_CSA_H_
#define _SPU_CSA_H_
#ifdef __KERNEL__

/*
 * Total number of 128-bit registers.
 */
#define NR_SPU_GPRS         	128
#define NR_SPU_SPRS         	9
#define NR_SPU_REGS_PAD	    	7
#define NR_SPU_SPILL_REGS   	144	/* GPRS + SPRS + PAD */
#define SIZEOF_SPU_SPILL_REGS	NR_SPU_SPILL_REGS * 16

#define SPU_SAVE_COMPLETE      	0x3FFB
#define SPU_RESTORE_COMPLETE   	0x3FFC

/*
 * Definitions for various 'stopped' status conditions,
 * to be recreated during context restore.
 */
#define SPU_STOPPED_STATUS_P    1
#define SPU_STOPPED_STATUS_I    2
#define SPU_STOPPED_STATUS_H    3
#define SPU_STOPPED_STATUS_S    4
#define SPU_STOPPED_STATUS_S_I  5
#define SPU_STOPPED_STATUS_S_P  6
#define SPU_STOPPED_STATUS_P_H  7
#define SPU_STOPPED_STATUS_P_I  8
#define SPU_STOPPED_STATUS_R    9

#ifndef  __ASSEMBLY__
/**
 * spu_reg128 - generic 128-bit register definition.
 */
struct spu_reg128 {
	u32 slot[4];
};

/**
 * struct spu_lscsa - Local Store Context Save Area.
 * @gprs: Array of saved registers.
 * @fpcr: Saved floating point status control register.
 * @decr: Saved decrementer value.
 * @decr_status: Indicates decrementer run status.
 * @ppu_mb: Saved PPU mailbox data.
 * @ppuint_mb: Saved PPU interrupting mailbox data.
 * @tag_mask: Saved tag group mask.
 * @event_mask: Saved event mask.
 * @srr0: Saved SRR0.
 * @stopped_status: Conditions to be recreated by restore.
 * @ls: Saved contents of Local Storage Area.
 *
 * The LSCSA represents state that is primarily saved and
 * restored by SPU-side code.
 */
struct spu_lscsa {
	struct spu_reg128 gprs[128];
	struct spu_reg128 fpcr;
	struct spu_reg128 decr;
	struct spu_reg128 decr_status;
	struct spu_reg128 ppu_mb;
	struct spu_reg128 ppuint_mb;
	struct spu_reg128 tag_mask;
	struct spu_reg128 event_mask;
	struct spu_reg128 srr0;
	struct spu_reg128 stopped_status;

	/*
	 * 'ls' must be page-aligned on all configurations.
	 * Since we don't want to rely on having the spu-gcc
	 * installed to build the kernel and this structure
	 * is used in the SPU-side code, make it 64k-page
	 * aligned for now.
	 */
	unsigned char ls[LS_SIZE] __attribute__((aligned(65536)));
};

#ifndef __SPU__
/*
 * struct spu_problem_collapsed - condensed problem state area, w/o pads.
 */
struct spu_problem_collapsed {
	u64 spc_mssync_RW;
	u32 mfc_lsa_W;
	u32 unused_pad0;
	u64 mfc_ea_W;
	union mfc_tag_size_class_cmd mfc_union_W;
	u32 dma_qstatus_R;
	u32 dma_querytype_RW;
	u32 dma_querymask_RW;
	u32 dma_tagstatus_R;
	u32 pu_mb_R;
	u32 spu_mb_W;
	u32 mb_stat_R;
	u32 spu_runcntl_RW;
	u32 spu_status_R;
	u32 spu_spc_R;
	u32 spu_npc_RW;
	u32 signal_notify1;
	u32 signal_notify2;
	u32 unused_pad1;
};

/*
 * struct spu_priv1_collapsed - condensed privileged 1 area, w/o pads.
 */
struct spu_priv1_collapsed {
	u64 mfc_sr1_RW;
	u64 mfc_lpid_RW;
	u64 spu_idr_RW;
	u64 mfc_vr_RO;
	u64 spu_vr_RO;
	u64 int_mask_class0_RW;
	u64 int_mask_class1_RW;
	u64 int_mask_class2_RW;
	u64 int_stat_class0_RW;
	u64 int_stat_class1_RW;
	u64 int_stat_class2_RW;
	u64 int_route_RW;
	u64 mfc_atomic_flush_RW;
	u64 resource_allocation_groupID_RW;
	u64 resource_allocation_enable_RW;
	u64 mfc_fir_R;
	u64 mfc_fir_status_or_W;
	u64 mfc_fir_status_and_W;
	u64 mfc_fir_mask_R;
	u64 mfc_fir_mask_or_W;
	u64 mfc_fir_mask_and_W;
	u64 mfc_fir_chkstp_enable_RW;
	u64 smf_sbi_signal_sel;
	u64 smf_ato_signal_sel;
	u64 mfc_sdr_RW;
	u64 tlb_index_hint_RO;
	u64 tlb_index_W;
	u64 tlb_vpn_RW;
	u64 tlb_rpn_RW;
	u64 tlb_invalidate_entry_W;
	u64 tlb_invalidate_all_W;
	u64 smm_hid;
	u64 mfc_accr_RW;
	u64 mfc_dsisr_RW;
	u64 mfc_dar_RW;
	u64 rmt_index_RW;
	u64 rmt_data1_RW;
	u64 mfc_dsir_R;
	u64 mfc_lsacr_RW;
	u64 mfc_lscrr_R;
	u64 mfc_tclass_id_RW;
	u64 mfc_rm_boundary;
	u64 smf_dma_signal_sel;
	u64 smm_signal_sel;
	u64 mfc_cer_R;
	u64 pu_ecc_cntl_RW;
	u64 pu_ecc_stat_RW;
	u64 spu_ecc_addr_RW;
	u64 spu_err_mask_RW;
	u64 spu_trig0_sel;
	u64 spu_trig1_sel;
	u64 spu_trig2_sel;
	u64 spu_trig3_sel;
	u64 spu_trace_sel;
	u64 spu_event0_sel;
	u64 spu_event1_sel;
	u64 spu_event2_sel;
	u64 spu_event3_sel;
	u64 spu_trace_cntl;
};

/*
 * struct spu_priv2_collapsed - condensed priviliged 2 area, w/o pads.
 */
struct spu_priv2_collapsed {
	u64 slb_index_W;
	u64 slb_esid_RW;
	u64 slb_vsid_RW;
	u64 slb_invalidate_entry_W;
	u64 slb_invalidate_all_W;
	struct mfc_cq_sr spuq[16];
	struct mfc_cq_sr puq[8];
	u64 mfc_control_RW;
	u64 puint_mb_R;
	u64 spu_privcntl_RW;
	u64 spu_lslr_RW;
	u64 spu_chnlcntptr_RW;
	u64 spu_chnlcnt_RW;
	u64 spu_chnldata_RW;
	u64 spu_cfg_RW;
	u64 spu_tag_status_query_RW;
	u64 spu_cmd_buf1_RW;
	u64 spu_cmd_buf2_RW;
	u64 spu_atomic_status_RW;
};

/**
 * struct spu_state
 * @lscsa: Local Store Context Save Area.
 * @prob: Collapsed Problem State Area, w/o pads.
 * @priv1: Collapsed Privileged 1 Area, w/o pads.
 * @priv2: Collapsed Privileged 2 Area, w/o pads.
 * @spu_chnlcnt_RW: Array of saved channel counts.
 * @spu_chnldata_RW: Array of saved channel data.
 * @suspend_time: Time stamp when decrementer disabled.
 * @slb_esid_RW: Array of saved SLB esid entries.
 * @slb_vsid_RW: Array of saved SLB vsid entries.
 *
 * Structure representing the whole of the SPU
 * context save area (CSA).  This struct contains
 * all of the state necessary to suspend and then
 * later optionally resume execution of an SPU
 * context.
 *
 * The @lscsa region is by far the largest, and is
 * allocated separately so that it may either be
 * pinned or mapped to/from application memory, as
 * appropriate for the OS environment.
 */
struct spu_state {
	struct spu_lscsa *lscsa;
	struct spu_problem_collapsed prob;
	struct spu_priv1_collapsed priv1;
	struct spu_priv2_collapsed priv2;
	u64 spu_chnlcnt_RW[32];
	u64 spu_chnldata_RW[32];
	u32 spu_mailbox_data[4];
	u32 pu_mailbox_data[1];
	unsigned long suspend_time;
	u64 slb_esid_RW[8];
	u64 slb_vsid_RW[8];
	spinlock_t register_lock;
};

extern void spu_init_csa(struct spu_state *csa);
extern void spu_fini_csa(struct spu_state *csa);
extern int spu_save(struct spu_state *prev, struct spu *spu);
extern int spu_restore(struct spu_state *new, struct spu *spu);
extern int spu_switch(struct spu_state *prev, struct spu_state *new,
		      struct spu *spu);

#endif /* !__SPU__ */
#endif /* __KERNEL__ */
#endif /* !__ASSEMBLY__ */
#endif /* _SPU_CSA_H_ */
