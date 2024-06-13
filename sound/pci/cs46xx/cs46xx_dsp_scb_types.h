/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 * NOTE: comments are copy/paste from cwcemb80.lst 
 * provided by Tom Woller at Cirrus (my only
 * documentation about the SP OS running inside
 * the DSP) 
 */

#ifndef __CS46XX_DSP_SCB_TYPES_H__
#define __CS46XX_DSP_SCB_TYPES_H__

#include <asm/byteorder.h>

#ifndef ___DSP_DUAL_16BIT_ALLOC
#if   defined(__LITTLE_ENDIAN)
#define ___DSP_DUAL_16BIT_ALLOC(a,b) u16 a; u16 b;
#elif defined(__BIG_ENDIAN)
#define ___DSP_DUAL_16BIT_ALLOC(a,b) u16 b; u16 a;
#else
#error Not __LITTLE_ENDIAN and not __BIG_ENDIAN, then what ???
#endif
#endif

/* This structs are used internally by the SP */

struct dsp_basic_dma_req {
	/* DMA Requestor Word 0 (DCW)  fields:

	   31 [30-28]27  [26:24] 23 22 21 20 [19:18] [17:16] 15 14 13  12  11 10 9 8 7 6  [5:0]
	   _______________________________________________________________________________________	
	   |S| SBT  |D|  DBT    |wb|wb|  |  |  LS  |  SS   |Opt|Do|SSG|DSG|  |  | | | | | Dword   |
	   |H|_____ |H|_________|S_|D |__|__|______|_______|___|ne|__ |__ |__|__|_|_|_|_|_Count -1|
	*/
	u32 dcw;                 /* DMA Control Word */
	u32 dmw;                 /* DMA Mode Word */
	u32 saw;                 /* Source Address Word */
	u32 daw;                 /* Destination Address Word  */
};

struct dsp_scatter_gather_ext {
	u32 npaw;                /* Next-Page Address Word */

	/* DMA Requestor Word 5 (NPCW)  fields:
     
	   31-30 29 28          [27:16]              [15:12]             [11:3]                [2:0] 				
	   _________________________________________________________________________________________	
	   |SV  |LE|SE|   Sample-end byte offset   |         | Page-map entry offset for next  |    | 
	   |page|__|__| ___________________________|_________|__page, if !sample-end___________|____|
	*/
	u32 npcw;                /* Next-Page Control Word */
	u32 lbaw;                /* Loop-Begin Address Word */
	u32 nplbaw;              /* Next-Page after Loop-Begin Address Word */
	u32 sgaw;                /* Scatter/Gather Address Word */
};

struct dsp_volume_control {
	___DSP_DUAL_16BIT_ALLOC(
	   rightTarg,  /* Target volume for left & right channels */
	   leftTarg
	)
	___DSP_DUAL_16BIT_ALLOC(
	   rightVol,   /* Current left & right channel volumes */
	   leftVol
	)
};

/* Generic stream control block (SCB) structure definition */
struct dsp_generic_scb {
	/* For streaming I/O, the DSP should never alter any words in the DMA
	   requestor or the scatter/gather extension.  Only ad hoc DMA request
	   streams are free to alter the requestor (currently only occur in the
	   DOS-based MIDI controller and in debugger-inserted code).
    
	   If an SCB does not have any associated DMA requestor, these 9 ints
	   may be freed for use by other tasks, but the pointer to the SCB must
	   still be such that the insOrd:nextSCB appear at offset 9 from the
	   SCB pointer.
     
	   Basic (non scatter/gather) DMA requestor (4 ints)
	*/
  
	/* Initialized by the host, only modified by DMA 
	   R/O for the DSP task */
	struct dsp_basic_dma_req  basic_req;  /* Optional */

	/* Scatter/gather DMA requestor extension   (5 ints) 
	   Initialized by the host, only modified by DMA
	   DSP task never needs to even read these.
	*/
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */

	/* Sublist pointer & next stream control block (SCB) link.
	   Initialized & modified by the host R/O for the DSP task
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,     /* REQUIRED */
	    sub_list_ptr  /* REQUIRED */
	)
  
	/* Pointer to this tasks parameter block & stream function pointer 
	   Initialized by the host  R/O for the DSP task */
	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,  /* REQUIRED */
	    this_spb      /* REQUIRED */
	)

	/* rsConfig register for stream buffer (rsDMA reg. 
	   is loaded from basicReq.daw for incoming streams, or 
	   basicReq.saw, for outgoing streams) 

	   31 30 29  [28:24]     [23:16] 15 14 13 12 11 10 9 8 7 6  5      4      [3:0]
	   ______________________________________________________________________________
	   |DMA  |D|maxDMAsize| streamNum|dir|p|  |  |  |  | | |ds |shr 1|rev Cy | mod   |
	   |prio |_|__________|__________|___|_|__|__|__|__|_|_|___|_____|_______|_______|
	   31 30 29  [28:24]     [23:16] 15 14 13 12 11 10 9 8 7 6  5      4      [3:0]


	   Initialized by the host R/O for the DSP task
	*/
	u32  strm_rs_config; /* REQUIRED */
               // 
	/* On mixer input streams: indicates mixer input stream configuration
	   On Tees, this is copied from the stream being snooped

	   Stream sample pointer & MAC-unit mode for this stream 
     
	   Initialized by the host Updated by the DSP task
	*/
	u32  strm_buf_ptr; /* REQUIRED  */

	/* On mixer input streams: points to next mixer input and is updated by the
                                   mixer subroutine in the "parent" DSP task
				   (least-significant 16 bits are preserved, unused)
    
           On Tees, the pointer is copied from the stream being snooped on
	   initialization, and, subsequently, it is copied into the
	   stream being snooped.

	   On wavetable/3D voices: the strmBufPtr will use all 32 bits to allow for
                                   fractional phase accumulation

	   Fractional increment per output sample in the input sample buffer

	   (Not used on mixer input streams & redefined on Tees)
	   On wavetable/3D voices: this 32-bit word specifies the integer.fractional 
	   increment per output sample.
	*/
	u32  strmPhiIncr;


	/* Standard stereo volume control
	   Initialized by the host (host updates target volumes) 

	   Current volumes update by the DSP task
	   On mixer input streams: required & updated by the mixer subroutine in the
                                   "parent" DSP task

	   On Tees, both current & target volumes are copied up on initialization,
	   and, subsequently, the target volume is copied up while the current
	   volume is copied down.
     
	   These two 32-bit words are redefined for wavetable & 3-D voices.    
	*/
	struct dsp_volume_control vol_ctrl_t;   /* Optional */
};


struct dsp_spos_control_block {
	/* WARNING: Certain items in this structure are modified by the host
	            Any dword that can be modified by the host, must not be
		    modified by the SP as the host can only do atomic dword
		    writes, and to do otherwise, even a read modify write, 
		    may lead to corrupted data on the SP.
  
		    This rule does not apply to one off boot time initialisation prior to starting the SP
	*/


	___DSP_DUAL_16BIT_ALLOC( 
	/* First element on the Hyper forground task tree */
	    hfg_tree_root_ptr,  /* HOST */			    
	/* First 3 dwords are written by the host and read-only on the DSP */
	    hfg_stack_base      /* HOST */
	)

	___DSP_DUAL_16BIT_ALLOC(
	/* Point to this data structure to enable easy access */
	    spos_cb_ptr,	 /* SP */
	    prev_task_tree_ptr   /* SP && HOST */
	)

	___DSP_DUAL_16BIT_ALLOC(
	/* Currently Unused */
	    xxinterval_timer_period,
	/* Enable extension of SPOS data structure */
	    HFGSPB_ptr
	)


	___DSP_DUAL_16BIT_ALLOC(
	    xxnum_HFG_ticks_thisInterval,
	/* Modified by the DSP */
	    xxnum_tntervals
	)


	/* Set by DSP upon encountering a trap (breakpoint) or a spurious
	   interrupt.  The host must clear this dword after reading it
	   upon receiving spInt1. */
	___DSP_DUAL_16BIT_ALLOC(
	    spurious_int_flag,	 /* (Host & SP) Nature of the spurious interrupt */
	    trap_flag            /* (Host & SP) Nature of detected Trap */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    unused2,					
	    invalid_IP_flag        /* (Host & SP ) Indicate detection of invalid instruction pointer */
	)

	___DSP_DUAL_16BIT_ALLOC(
	/* pointer to forground task tree header for use in next task search */
	    fg_task_tree_hdr_ptr,	  /* HOST */		
	/* Data structure for controlling synchronous link update */
	    hfg_sync_update_ptr           /* HOST */
	)
  
	___DSP_DUAL_16BIT_ALLOC(
	     begin_foreground_FCNT,  /* SP */
	/* Place holder for holding sleep timing */
	     last_FCNT_before_sleep  /* SP */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    unused7,           /* SP */
	    next_task_treePtr  /* SP */
	)

	u32 unused5;        

	___DSP_DUAL_16BIT_ALLOC(
	    active_flags,   /* SP */
	/* State flags, used to assist control of execution of Hyper Forground */
	    HFG_flags       /* SP */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    unused9,
	    unused8
	)
                              
	/* Space for saving enough context so that we can set up enough 
	   to save some more context.
	*/
	u32 rFE_save_for_invalid_IP;
	u32 r32_save_for_spurious_int;
	u32 r32_save_for_trap;
	u32 r32_save_for_HFG;
};

/* SPB for MIX_TO_OSTREAM algorithm family */
struct dsp_mix2_ostream_spb
{
	/* 16b.16b integer.frac approximation to the
	   number of 3 sample triplets to output each
	   frame. (approximation must be floor, to
	   insure that the fractional error is always
	   positive)
	*/
	u32 outTripletsPerFrame;

	/* 16b.16b integer.frac accumulated number of
	   output triplets since the start of group 
	*/
	u32 accumOutTriplets;  
};

/* SCB for Timing master algorithm */
struct dsp_timing_master_scb {
	/* First 12 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,     /* REQUIRED */
	    sub_list_ptr  /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,  /* REQUIRED */
	    this_spb      /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	/* Initial values are 0000:xxxx */
 	    reserved,
	    extra_sample_accum
	)

  
	/* Initial values are xxxx:0000
	   hi: Current CODEC output FIFO pointer
	       (0 to 0x0f)
           lo: Flag indicating that the CODEC
	       FIFO is sync'd (host clears to
	       resynchronize the FIFO pointer
	       upon start/restart) 
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    codec_FIFO_syncd, 
	    codec_FIFO_ptr
	)
  
	/* Init. 8000:0005 for 44.1k
                 8000:0001 for 48k
	   hi: Fractional sample accumulator 0.16b
	   lo: Number of frames remaining to be
	       processed in the current group of
	       frames
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    frac_samp_accum_qm1,
	    TM_frms_left_in_group
	) 

	/* Init. 0001:0005 for 44.1k
                 0000:0001 for 48k
	   hi: Fractional sample correction factor 0.16b
	       to be added every frameGroupLength frames
	       to correct for truncation error in
	       nsamp_per_frm_q15
	   lo: Number of frames in the group
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    frac_samp_correction_qm1,
	    TM_frm_group_length  
	)

	/* Init. 44.1k*65536/8k = 0x00058333 for 44.1k
                 48k*65536/8k = 0x00060000 for 48k
	   16b.16b integer.frac approximation to the
	   number of samples to output each frame.
	   (approximation must be floor, to insure */
	u32 nsamp_per_frm_q15;
};

/* SCB for CODEC output algorithm */
struct dsp_codec_output_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_rs_config; /* REQUIRED */

	u32 strm_buf_ptr;   /* REQUIRED */

	/* NOTE: The CODEC output task reads samples from the first task on its
                 sublist at the stream buffer pointer (init. to lag DMA destination
		 address word).  After the required number of samples is transferred,
		 the CODEC output task advances sub_list_ptr->strm_buf_ptr past the samples
		 consumed.
	*/

	/* Init. 0000:0010 for SDout
                 0060:0010 for SDout2
		 0080:0010 for SDout3
	   hi: Base IO address of FIFO to which
	       the left-channel samples are to
	       be written.
	   lo: Displacement for the base IO
	       address for left-channel to obtain
	       the base IO address for the FIFO
	       to which the right-channel samples
	       are to be written.
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    left_chan_base_IO_addr,
	    right_chan_IO_disp
	)


	/* Init: 0x0080:0004 for non-AC-97
	   Init: 0x0080:0000 for AC-97
	   hi: Exponential volume change rate
	       for input stream
	   lo: Positive shift count to shift the
	       16-bit input sample to obtain the
	       32-bit output word
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    CO_scale_shift_count, 
	    CO_exp_vol_change_rate
	)

	/* Pointer to SCB at end of input chain */
	___DSP_DUAL_16BIT_ALLOC(
	    reserved,
	    last_sub_ptr
	)
};

/* SCB for CODEC input algorithm */
struct dsp_codec_input_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_rs_config; /* REQUIRED */
	u32 strm_buf_ptr;   /* REQUIRED */

	/* NOTE: The CODEC input task reads samples from the hardware FIFO 
                 sublist at the DMA source address word (sub_list_ptr->basic_req.saw).
                 After the required number of samples is transferred, the CODEC
                 output task advances sub_list_ptr->basic_req.saw past the samples
                 consumed.  SPuD must initialize the sub_list_ptr->basic_req.saw
                 to point half-way around from the initial sub_list_ptr->strm_nuf_ptr
                 to allow for lag/lead.
	*/

	/* Init. 0000:0010 for SDout
                 0060:0010 for SDout2
		 0080:0010 for SDout3
	   hi: Base IO address of FIFO to which
	       the left-channel samples are to
	       be written.
	   lo: Displacement for the base IO
	       address for left-channel to obtain
	       the base IO address for the FIFO
	       to which the right-channel samples
	       are to be written.
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    rightChanINdisp, 
	    left_chan_base_IN_addr
	)
	/* Init. ?:fffc
	   lo: Negative shift count to shift the
	       32-bit input dword to obtain the
	       16-bit sample msb-aligned (count
	       is negative to shift left)
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    scaleShiftCount, 
	    reserver1
	)

	u32  reserved2;
};


struct dsp_pcm_serial_input_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_buf_ptr;   /* REQUIRED */
	u32 strm_rs_config; /* REQUIRED */
  
	/* Init. Ptr to CODEC input SCB
	   hi: Pointer to the SCB containing the
	       input buffer to which CODEC input
	       samples are written
	   lo: Flag indicating the link to the CODEC
	       input task is to be initialized
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    init_codec_input_link,
	    codec_input_buf_scb
	)

	/* Initialized by the host (host updates target volumes) */
	struct dsp_volume_control psi_vol_ctrl;   
  
};

struct dsp_src_task_scb {
	___DSP_DUAL_16BIT_ALLOC(
	    frames_left_in_gof,
	    gofs_left_in_sec
	)

	___DSP_DUAL_16BIT_ALLOC(
	    const2_thirds,
	    num_extra_tnput_samples
	)

	___DSP_DUAL_16BIT_ALLOC(
	    cor_per_gof,
	    correction_per_sec 
	)

	___DSP_DUAL_16BIT_ALLOC(
	    output_buf_producer_ptr,  
	    junk_DMA_MID
	)

	___DSP_DUAL_16BIT_ALLOC(
	    gof_length,  
	    gofs_per_sec
	)

	u32 input_buf_strm_config;

	___DSP_DUAL_16BIT_ALLOC(
	    reserved_for_SRC_use,
	    input_buf_consumer_ptr
	)

	u32 accum_phi;

	___DSP_DUAL_16BIT_ALLOC(
	    exp_src_vol_change_rate,
	    input_buf_producer_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    src_next_scb,
	    src_sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    src_entry_point,
	    src_this_sbp
	)

	u32  src_strm_rs_config;
	u32  src_strm_buf_ptr;
  
	u32   phiIncr6int_26frac;
  
	struct dsp_volume_control src_vol_ctrl;
};

struct dsp_decimate_by_pow2_scb {
	/* decimationFactor = 2, 4, or 8 (larger factors waste too much memory
	                                  when compared to cascading decimators)
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    dec2_coef_base_ptr,
	    dec2_coef_increment
	)

	/* coefIncrement = 128 / decimationFactor (for our ROM filter)
	   coefBasePtr = 0x8000 (for our ROM filter)
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    dec2_in_samples_per_out_triplet,
	    dec2_extra_in_samples
	)
	/* extraInSamples: # of accumulated, unused input samples (init. to 0)
	   inSamplesPerOutTriplet = 3 * decimationFactor
	*/

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_const2_thirds,
	    dec2_half_num_taps_mp5
	)
	/* halfNumTapsM5: (1/2 number of taps in decimation filter) minus 5
	   const2thirds: constant 2/3 in 16Q0 format (sign.15)
	*/

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_output_buf_producer_ptr,
	    dec2_junkdma_mid
	)

	u32  dec2_reserved2;

	u32  dec2_input_nuf_strm_config;
	/* inputBufStrmConfig: rsConfig for the input buffer to the decimator
	   (buffer size = decimationFactor * 32 dwords)
	*/

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_phi_incr,
	    dec2_input_buf_consumer_ptr
	)
	/* inputBufConsumerPtr: Input buffer read pointer (into SRC filter)
	   phiIncr = decimationFactor * 4
	*/

	u32 dec2_reserved3;

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_exp_vol_change_rate,
	    dec2_input_buf_producer_ptr
	)
	/* inputBufProducerPtr: Input buffer write pointer
	   expVolChangeRate: Exponential volume change rate for possible
	                     future mixer on input streams
	*/

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_next_scb,
	    dec2_sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    dec2_entry_point,
	    dec2_this_spb
	)

	u32  dec2_strm_rs_config;
	u32  dec2_strm_buf_ptr;

	u32  dec2_reserved4;

	struct dsp_volume_control dec2_vol_ctrl; /* Not used! */
};

struct dsp_vari_decimate_scb {
	___DSP_DUAL_16BIT_ALLOC(
	    vdec_frames_left_in_gof,
	    vdec_gofs_left_in_sec
	)

	___DSP_DUAL_16BIT_ALLOC(
	    vdec_const2_thirds,
	    vdec_extra_in_samples
	)
	/* extraInSamples: # of accumulated, unused input samples (init. to 0)
	   const2thirds: constant 2/3 in 16Q0 format (sign.15) */

	___DSP_DUAL_16BIT_ALLOC(
	    vdec_cor_per_gof,
	    vdec_correction_per_sec
	)

	___DSP_DUAL_16BIT_ALLOC(
	    vdec_output_buf_producer_ptr,
	    vdec_input_buf_consumer_ptr
	)
	/* inputBufConsumerPtr: Input buffer read pointer (into SRC filter) */
	___DSP_DUAL_16BIT_ALLOC(
	    vdec_gof_length,
	    vdec_gofs_per_sec
	)

	u32  vdec_input_buf_strm_config;
	/* inputBufStrmConfig: rsConfig for the input buffer to the decimator
	   (buffer size = 64 dwords) */
	u32  vdec_coef_increment;
	/* coefIncrement = - 128.0 / decimationFactor (as a 32Q15 number) */

	u32  vdec_accumphi;
	/* accumPhi: accumulated fractional phase increment (6.26) */

	___DSP_DUAL_16BIT_ALLOC(
 	    vdec_exp_vol_change_rate,
	    vdec_input_buf_producer_ptr
	)
	/* inputBufProducerPtr: Input buffer write pointer
	   expVolChangeRate: Exponential volume change rate for possible
	   future mixer on input streams */

	___DSP_DUAL_16BIT_ALLOC(
	    vdec_next_scb,
	    vdec_sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    vdec_entry_point,
	    vdec_this_spb
	)

	u32 vdec_strm_rs_config;
	u32 vdec_strm_buf_ptr;

	u32 vdec_phi_incr_6int_26frac;

	struct dsp_volume_control vdec_vol_ctrl;
};


/* SCB for MIX_TO_OSTREAM algorithm family */
struct dsp_mix2_ostream_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_rs_config; /* REQUIRED */
	u32 strm_buf_ptr;   /* REQUIRED */


	/* hi: Number of mixed-down input triplets
	       computed since start of group
	   lo: Number of frames remaining to be
	       processed in the current group of
	       frames
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    frames_left_in_group,
	    accum_input_triplets
	)

	/* hi: Exponential volume change rate
	       for mixer on input streams
	   lo: Number of frames in the group
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    frame_group_length,
	    exp_vol_change_rate
	)
  
	___DSP_DUAL_16BIT_ALLOC(
	    const_FFFF,
	    const_zero
	)
};


/* SCB for S16_MIX algorithm */
struct dsp_mix_only_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_rs_config; /* REQUIRED */
	u32 strm_buf_ptr;   /* REQUIRED */

	u32 reserved;
	struct dsp_volume_control vol_ctrl;
};

/* SCB for the async. CODEC input algorithm */
struct dsp_async_codec_input_scb {
	u32 io_free2;     
  
	u32 io_current_total;
	u32 io_previous_total;
  
	u16 io_count;
	u16 io_count_limit;
  
	u16 o_fifo_base_addr;            
	u16 ost_mo_format;
	/* 1 = stereo; 0 = mono 
	   xxx for ASER 1 (not allowed); 118 for ASER2 */

	u32  ostrm_rs_config;
	u32  ostrm_buf_ptr;
  
	___DSP_DUAL_16BIT_ALLOC(
	    io_sclks_per_lr_clk,
	    io_io_enable
	)

	u32  io_free4;

	___DSP_DUAL_16BIT_ALLOC(  
	    io_next_scb,
	    io_sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    io_entry_point,
	    io_this_spb
	)

	u32 istrm_rs_config;
	u32 istrm_buf_ptr;

	/* Init. 0000:8042: for ASER1
                 0000:8044: for ASER2  */
	___DSP_DUAL_16BIT_ALLOC(
	    io_stat_reg_addr,
	    iofifo_pointer
	)

	/* Init 1 stero:100 ASER1
	   Init 0 mono:110 ASER2 
	*/
	___DSP_DUAL_16BIT_ALLOC(
	    ififo_base_addr,            
	    ist_mo_format
	)

	u32 i_free;
};


/* SCB for the SP/DIF CODEC input and output */
struct dsp_spdifiscb {
	___DSP_DUAL_16BIT_ALLOC(
	    status_ptr,     
	    status_start_ptr
	)

	u32 current_total;
	u32 previous_total;

	___DSP_DUAL_16BIT_ALLOC(
	    count,
	    count_limit
	)

	u32 status_data;

	___DSP_DUAL_16BIT_ALLOC(  
	    status,
	    free4
	)

	u32 free3;

	___DSP_DUAL_16BIT_ALLOC(  
	    free2,
	    bit_count
	)

	u32  temp_status;
  
	___DSP_DUAL_16BIT_ALLOC(
	    next_SCB,
	    sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,
	    this_spb
	)

	u32  strm_rs_config;
	u32  strm_buf_ptr;
  
	___DSP_DUAL_16BIT_ALLOC(
	    stat_reg_addr, 
	    fifo_pointer
	)

	___DSP_DUAL_16BIT_ALLOC(
	    fifo_base_addr, 
	    st_mo_format
	)

	u32  free1;
};


/* SCB for the SP/DIF CODEC input and output  */
struct dsp_spdifoscb {		 

	u32 free2;     

	u32 free3[4];             

	/* Need to be here for compatibility with AsynchFGTxCode */
	u32 strm_rs_config;
                               
	u32 strm_buf_ptr;

	___DSP_DUAL_16BIT_ALLOC(  
	    status,
	    free5
	)

	u32 free4;

	___DSP_DUAL_16BIT_ALLOC(  
	    next_scb,
	    sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,
	    this_spb
	)

	u32 free6[2];
  
	___DSP_DUAL_16BIT_ALLOC(
	    stat_reg_addr, 
	    fifo_pointer
	)

	___DSP_DUAL_16BIT_ALLOC(
	    fifo_base_addr,
	    st_mo_format
	)

	u32  free1;                                         
};


struct dsp_asynch_fg_rx_scb {
	___DSP_DUAL_16BIT_ALLOC(
	    bot_buf_mask,
	    buf_Mask
	)

	___DSP_DUAL_16BIT_ALLOC(
	    max,
	    min
	)

	___DSP_DUAL_16BIT_ALLOC(
	    old_producer_pointer,
	    hfg_scb_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    delta,
	    adjust_count
	)

	u32 unused2[5];  

	___DSP_DUAL_16BIT_ALLOC(  
	    sibling_ptr,  
	    child_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    code_ptr,
	    this_ptr
	)

	u32 strm_rs_config; 

	u32 strm_buf_ptr;
  
	u32 unused_phi_incr;
  
	___DSP_DUAL_16BIT_ALLOC(
	    right_targ,   
	    left_targ
	)

	___DSP_DUAL_16BIT_ALLOC(
	    right_vol,
	    left_vol
	)
};


struct dsp_asynch_fg_tx_scb {
	___DSP_DUAL_16BIT_ALLOC(
	    not_buf_mask,
	    buf_mask
	)

	___DSP_DUAL_16BIT_ALLOC(
	    max,
	    min
	)

	___DSP_DUAL_16BIT_ALLOC(
	    unused1,
	    hfg_scb_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    delta,
	    adjust_count
	)

	u32 accum_phi;

	___DSP_DUAL_16BIT_ALLOC(
	    unused2,
	    const_one_third
	)

	u32 unused3[3];

	___DSP_DUAL_16BIT_ALLOC(
	    sibling_ptr,
	    child_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    codePtr,
	    this_ptr
	)

	u32 strm_rs_config;

	u32 strm_buf_ptr;

	u32 phi_incr;

	___DSP_DUAL_16BIT_ALLOC(
	    unused_right_targ,
	    unused_left_targ
	)

	___DSP_DUAL_16BIT_ALLOC(
	    unused_right_vol,
	    unused_left_vol
	)
};


struct dsp_output_snoop_scb {
	/* First 13 dwords from generic_scb_t */
	struct dsp_basic_dma_req  basic_req;  /* Optional */
	struct dsp_scatter_gather_ext sg_ext;  /* Optional */
	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,       /* REQUIRED */
	    sub_list_ptr    /* REQUIRED */
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,    /* REQUIRED */
	    this_spb        /* REQUIRED */
	)

	u32 strm_rs_config; /* REQUIRED */
	u32 strm_buf_ptr;   /* REQUIRED */

	___DSP_DUAL_16BIT_ALLOC(
	    init_snoop_input_link,
	    snoop_child_input_scb
	)

	u32 snoop_input_buf_ptr;

	___DSP_DUAL_16BIT_ALLOC(
	    reserved,
	    input_scb
	)
};

struct dsp_spio_write_scb {
	___DSP_DUAL_16BIT_ALLOC(
	    address1,
	    address2
	)

	u32 data1;

	u32 data2;

	___DSP_DUAL_16BIT_ALLOC(
	    address3,
	    address4
	)

	u32 data3;

	u32 data4;

	___DSP_DUAL_16BIT_ALLOC(
	    unused1,
	    data_ptr
	)

	u32 unused2[2];

	___DSP_DUAL_16BIT_ALLOC(
	    sibling_ptr,
	    child_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,
	    this_ptr
	)

	u32 unused3[5];
};

struct dsp_magic_snoop_task {
	u32 i0;
	u32 i1;

	u32 strm_buf_ptr1;
  
	u16 i2;
	u16 snoop_scb;

	u32 i3;
	u32 i4;
	u32 i5;
	u32 i6;

	u32 i7;

	___DSP_DUAL_16BIT_ALLOC(
	    next_scb,
	    sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	    entry_point,
	    this_ptr
	)

	u32 strm_buf_config;
	u32 strm_buf_ptr2;

	u32 i8;

	struct dsp_volume_control vdec_vol_ctrl;
};


struct dsp_filter_scb {
	___DSP_DUAL_16BIT_ALLOC(
	      a0_right,          /* 0x00 */
	      a0_left
	)
	___DSP_DUAL_16BIT_ALLOC(
	      a1_right,          /* 0x01 */
	      a1_left
	)
	___DSP_DUAL_16BIT_ALLOC(
	      a2_right,          /* 0x02 */
	      a2_left
	)
	___DSP_DUAL_16BIT_ALLOC(
	      output_buf_ptr,    /* 0x03 */
	      init
	)

	___DSP_DUAL_16BIT_ALLOC(
	      filter_unused3,    /* 0x04 */
	      filter_unused2
	)

	u32 prev_sample_output1; /* 0x05 */
	u32 prev_sample_output2; /* 0x06 */
	u32 prev_sample_input1;  /* 0x07 */
	u32 prev_sample_input2;  /* 0x08 */

	___DSP_DUAL_16BIT_ALLOC(
	      next_scb_ptr,      /* 0x09 */
	      sub_list_ptr
	)

	___DSP_DUAL_16BIT_ALLOC(
	      entry_point,       /* 0x0A */
	      spb_ptr
	)

	u32  strm_rs_config;     /* 0x0B */
	u32  strm_buf_ptr;       /* 0x0C */

	___DSP_DUAL_16BIT_ALLOC(
              b0_right,          /* 0x0D */
	      b0_left
	)
	___DSP_DUAL_16BIT_ALLOC(
              b1_right,          /* 0x0E */
	      b1_left
	)
	___DSP_DUAL_16BIT_ALLOC(
              b2_right,          /* 0x0F */
	      b2_left
	)
};
#endif /* __DSP_SCB_TYPES_H__ */
