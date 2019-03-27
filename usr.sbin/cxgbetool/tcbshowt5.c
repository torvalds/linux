/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Chelsio Communications, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Auto-generated file.  Avoid direct editing.     */
/* Edits will be lost when file regenerated.       */
#include <stdio.h>
#include "tcb_common.h"

void t5_display_tcb_aux_0 (_TCBVAR *tvp, int aux)
{






  
  PR("STATE:\n");
  PR("  %-12s (%-2u), %s, lock_tid %u, rss_fw %u\n",
	      spr_tcp_state(val("t_state")), 
	      val("t_state"),
	      spr_ip_version(val("ip_version")),
	      val("lock_tid"),
	      val("rss_fw")
	     );
  PR("  l2t_ix 0x%x, smac sel 0x%x, tos 0x%x\n",
	      val("l2t_ix"),
	      val("smac_sel"),
	      val("tos")
	      );
  PR("  maxseg %u, recv_scaleflag %u, recv_tstmp %u, recv_sack %u\n",
	      val("t_maxseg"),   val("recv_scale"),
	      val("recv_tstmp"), val("recv_sack"));


  PR("TIMERS:\n"); /* **************************************** */
  PR("  timer    %u,  dack_timer      %u\n", 
	   val("timer"), val("dack_timer"));
  PR("  mod_schd: tx: %u, rx: %u, reason 0x%1x\n", 
	      val("mod_schd_tx"), 
	      val("mod_schd_rx"),
	      ((val("mod_schd_reason2")<<2) | (val("mod_schd_reason1")<<1) |
	       val("mod_schd_reason0"))
	      );


  PR("  max_rt   %-2u, rxtshift        %u, keepalive   %u\n", 
	   val("max_rt"),  val("t_rxtshift"), 
	   val("keepalive"));
  PR("  timestamp_offset 0x%x,  timestamp 0x%x\n",
	   val("timestamp_offset"),val("timestamp"));


  PR("  t_rtt_ts_recent_age %u  t_rttseq_recent %u\n", 
	   val("t_rtt_ts_recent_age"), val("t_rtseq_recent"));
  PR("  t_srtt %u, t_rttvar %u\n",
	   val("t_srtt"),val("t_rttvar"));






  PR("TRANSMIT BUFFER:\n");   /* *************************** */
  PR("  snd_una %u, snd_nxt %u, snd_max %u, tx_max %u\n",
	      val("snd_una"),val("snd_nxt"),
	      val("snd_max"),val("tx_max"));
  PR("  core_fin %u, tx_hdr_offset %u\n",  
	      val("core_fin"), SEQ_SUB(val("tx_max"),val("snd_una"))
	     );
  if (val("recv_scale") && !val("active_open")) {
    PR("  rcv_adv    %-5u << %-2u == %u (recv_scaleflag %u rcv_scale %u active open %u)\n",
		val("rcv_adv"), val("rcv_scale"), 
		val("rcv_adv") << val("rcv_scale"), 
		val("recv_scale"), val("rcv_scale"), val("active_open"));
  } else {
    PR("  rcv_adv    %-5u (rcv_scale %-2u recv_scaleflag %u active_open %u)\n",
		val("rcv_adv"), val("rcv_scale"), 
		val("recv_scale"), val("active_open"));
  }
  
  PR("  snd_cwnd   %-5u  snd_ssthresh %u  snd_rec %u\n",
	      val("snd_cwnd")    , val("snd_ssthresh"), val("snd_rec")
	     );




  PR("  cctrl: sel %s, ecn %u, ece %u, cwr %u, rfr %u\n",
	      spr_cctrl_sel(val("cctrl_sel0"),val("cctrl_sel1")),
	      val("cctrl_ecn"), val("cctrl_ece"), val("cctrl_cwr"),
	      val("cctrl_rfr"));
  PR("  t_dupacks %u, dupack_count_odd %u, fast_recovery %u\n",
	      val("t_dupacks"), val("dupack_count_odd"),val("fast_recovery"));
  PR("  core_more    %u, core_urg,       %u  core_push   %u,",
	      val("core_more"),val("core_urg"),val("core_push"));
  PR("  core_flush %u\n",val("core_flush"));
  PR("  nagle        %u, ssws_disable    %u, turbo       %u,",
	      val("nagle"), val("ssws_disabled"), val("turbo"));
  PR("  tx_pdu_out %u\n",val("tx_pdu_out"));
  PR("  tx_pace_auto %u, tx_pace_fixed   %u, tx_queue    %u",
	      val("tx_pace_auto"),val("tx_pace_fixed"),val("tx_queue"));


  PR("   tx_quiesce %u\n",val("tx_quiesce"));
  PR("  tx_channel   %u, tx_channel1     %u, tx_channel0 %u\n",
	      val("tx_channel"),
	      (val("tx_channel")>>1)&1,
	      val("tx_channel")&1
	      );




  PR("  tx_hdr_ptr   0x%-6x   tx_last_ptr 0x%-6x  tx_compact %u\n", 
	      val("tx_hdr_ptr"),val("tx_last_ptr"),val("tx_compact"));




  PR("RECEIVE BUFFER:\n");  /* *************************** */
  PR("  last_ack_sent %-10u                      rx_compact %u\n", 
	      val("ts_last_ack_sent"),val("rx_compact"));
  PR("  rcv_nxt       %-10u  hdr_off %-10u\n",
	      val("rcv_nxt"), val("rx_hdr_offset"));
  PR("  frag0_idx     %-10u  length  %-10u  rx_ptr  0x%-8x\n", 
	      val("rx_frag0_start_idx"),
	      val("rx_frag0_len"),
	      val("rx_ptr"));
  PR("  frag1_idx     %-10u  length  %-10u  ", 
	      val("rx_frag1_start_idx_offset"),
	      val("rx_frag1_len"));




  if (val("ulp_type")!=4) { /* RDMA has FRAG1 idx && len, but no ptr?  Should I not display frag1 at all? */
    PR("frag1_ptr  0x%-8x\n",val("rx_frag1_ptr"));
  } else {
    PR("\n");
  }
	      
  
  if (val("ulp_type") !=6 && val("ulp_type") != 5 && val("ulp_type") !=4) {
    PR("  frag2_idx     %-10u  length  %-10u  frag2_ptr  0x%-8x\n", 
		val("rx_frag2_start_idx_offset"),
		val("rx_frag2_len"),
		val("rx_frag2_ptr"));
    PR("  frag3_idx     %-10u  length  %-10u  frag3_ptr  0x%-8x\n", 
		val("rx_frag3_start_idx_offset"),
		val("rx_frag3_len"),
		val("rx_frag3_ptr"));
  }






  PR("  peer_fin %u,   rx_pdu_out %u, pdu_len %u\n",
	      val("peer_fin"),val("rx_pdu_out"), val("pdu_len"));




  if (val("recv_scale")) {
    PR("  rcv_wnd %u >> snd_scale %u == %u, recv_scaleflag = %u\n",
		val("rcv_wnd"), val("snd_scale"), 
		val("rcv_wnd") >> val("snd_scale"), 
		val("recv_scale"));
  } else {
    PR("  rcv_wnd %u.  (snd_scale %u, recv_scaleflag = %u)\n",
		val("rcv_wnd"), val("snd_scale"), 
		val("recv_scale"));
  }




 PR("  dack_mss   %u dack       %u,  dack_not_acked: %u\n", 
	      val("dack_mss"),val("dack"),val("dack_not_acked"));
  PR("  rcv_coal   %u rcv_co_psh %u rcv_co_last_psh  %u heart %u\n",
	      val("rcv_coalesce_enable"),
	      val("rcv_coalesce_push"),
	      val("rcv_coalesce_last_psh"),
	      val("rcv_coalesce_heartbeat"));
  
  PR("  rx_channel %u rx_quiesce %u rx_flow_ctrl_dis %u,",
	      val("rx_channel"), val("rx_quiesce"),
	      val("rx_flow_control_disable"));
  PR("  rx_flow_ctrl_ddp %u\n",
	      val("rx_flow_control_ddp"));


  PR("MISCELANEOUS:\n");  /* *************************** */
  PR("  pend_ctl: 0x%1x, unused_flags: 0x%x,  main_slush: 0x%x\n",
	      ((val("pend_ctl2")<<2) | (val("pend_ctl1")<<1) | 
	       val("pend_ctl0")),
	      val("unused"),val("main_slush"));
  PR("  Migrating %u, ask_mode %u, non_offload %u, rss_info %u\n",
	      val("migrating"), 
	      val("ask_mode"), val("non_offload"), val("rss_info"));
  PR("  ULP: ulp_type %u (%s), ulp_raw %u",
	      val("ulp_type"), spr_ulp_type(val("ulp_type")),
	      val("ulp_raw"));


  if (aux==1) {
    PR(",  ulp_ext %u",val("ulp_ext"));
  }
  PR("\n");




  PR("  RDMA: error   %u, flm_err %u\n", 
	      val("rdma_error"), val("rdma_flm_error"));


}
void t5_display_tcb_aux_1 (_TCBVAR *tvp, int aux)
{


  
  PR("    aux1_slush0: 0x%x aux1_slush1 0x%x\n",
	      val("aux1_slush0"), val("aux1_slush1"));
  PR("    pdu_hdr_len %u\n",val("pdu_hdr_len"));
  


}
void t5_display_tcb_aux_2 (_TCBVAR *tvp, int aux)
{




  PR("    qp_id %u, pd_id %u, stag %u\n",
	      val("qp_id"), val("pd_id"),val("stag"));
  PR("    irs_ulp %u, iss_ulp %u\n",
	      val("irs_ulp"),val("iss_ulp"));
  PR("    tx_pdu_len %u\n",
	      val("tx_pdu_len"));
  PR("    cq_idx_sq %u, cq_idx_rq %u\n",
	      val("cq_idx_sq"),val("cq_idx_rq"));
  PR("    rq_start %u, rq_MSN %u, rq_max_off %u, rq_write_ptr %u\n",
	      val("rq_start"),val("rq_msn"),val("rq_max_offset"),
	      val("rq_write_ptr"));
  PR("    L_valid %u, rdmap opcode %u\n",
	      val("ord_l_bit_vld"),val("rdmap_opcode"));
  PR("    tx_flush: %u, tx_oos_rxmt %u, tx_oos_txmt %u\n",
	      val("tx_flush"),val("tx_oos_rxmt"),val("tx_oos_txmt"));




}
void t5_display_tcb_aux_3 (_TCBVAR *tvp, int aux)
{




  PR("  aux3_slush: 0x%x, unused: buf0 0x%x, buf1: 0x%x, main: 0x%x\n",
	      val("aux3_slush"),val("ddp_buf0_unused"),val("ddp_buf1_unused"),
	      val("ddp_main_unused"));
	      




  PR("  DDP: DDPOFF  ActBuf  IndOut  WaitFrag  Rx2Tx  BufInf\n");
  PR("         %u       %u       %u        %u        %u      %u\n",
	      val("ddp_off"),val("ddp_active_buf"),val("ddp_indicate_out"),
	      val("ddp_wait_frag"),val("ddp_rx2tx"),val("ddp_buf_inf")
	     );


	      


  PR("        Ind  PshfEn PushDis Flush NoInvalidate\n");
  PR("   Buf0: %u      %u       %u    %u       %u\n",
	      val("ddp_buf0_indicate"),
	      val("ddp_pshf_enable_0"), val("ddp_push_disable_0"),
	      val("ddp_buf0_flush"),  val("ddp_psh_no_invalidate0")
	       );
  PR("   Buf1: %u      %u       %u    %u       %u\n",
	      val("ddp_buf1_indicate"),
	      val("ddp_pshf_enable_1"), val("ddp_push_disable_1"),
	      val("ddp_buf1_flush"),  val("ddp_psh_no_invalidate1")
	       );










  PR("        Valid  Offset   Length    Tag\n");
  PR("   Buf0:  %u    0x%6.6x 0x%6.6x  0x%8.8x",
	      val("ddp_buf0_valid"),val("rx_ddp_buf0_offset"), 
	      val("rx_ddp_buf0_len"),val("rx_ddp_buf0_tag") 


	       );
  if      (0==val("ddp_off") && 1==val("ddp_buf0_valid") && 0==val("ddp_active_buf")) {
    PR("   (Active)\n");
  } else {
    PR(" (Inactive)\n");
  }


  PR("   Buf1:  %u    0x%6.6x 0x%6.6x  0x%8.8x",
	      val("ddp_buf1_valid"),val("rx_ddp_buf1_offset"), 
	      val("rx_ddp_buf1_len"),val("rx_ddp_buf1_tag") 


	       );


  if      (0==val("ddp_off") && 1==val("ddp_buf1_valid") && 1==val("ddp_active_buf")) {
    PR("   (Active)\n");
  } else {
    PR(" (Inactive)\n");
  }






  if    (1==val("ddp_off")) {
    PR("   DDP is off (which also disables indicate)\n");
  } else if (1==val("ddp_buf0_valid") && 0==val("ddp_active_buf")) {
    PR("   Data being DDP'ed to buf 0, ");
    PR("which has %u - %u = %u bytes of space left\n",
		val("rx_ddp_buf0_len"),val("rx_ddp_buf0_offset"),
		val("rx_ddp_buf0_len")-val("rx_ddp_buf0_offset")
	       );
    if (1==val("ddp_buf1_valid")) {
      PR("   And buf1, which is also valid, has %u - %u = %u bytes of space left\n",
		  val("rx_ddp_buf1_len"),val("rx_ddp_buf1_offset"),
		  val("rx_ddp_buf1_len")-val("rx_ddp_buf1_offset")
		 );
    }
  } else if (1==val("ddp_buf1_valid") && 1==val("ddp_active_buf")) {
    PR("   Data being DDP'ed to buf 1, ");
    PR("which has %u - %u = %u bytes of space left\n",
		val("rx_ddp_buf1_len"),val("rx_ddp_buf1_offset"),
		val("rx_ddp_buf1_len")-val("rx_ddp_buf1_offset")
	       );
    if (1==val("ddp_buf0_valid")) {
      PR("   And buf0, which is also valid, has %u - %u = %u bytes of space left\n",
		  val("rx_ddp_buf0_len"),val("rx_ddp_buf0_offset"),
		  val("rx_ddp_buf0_len")-val("rx_ddp_buf0_offset")
		 );
    }
  } else if (0==val("ddp_buf0_valid") && 1==val("ddp_buf1_valid") && 0==val("ddp_active_buf")) {
    PR("   !!! Invalid DDP buf 1 valid, but buf 0 active.\n");
  } else if (1==val("ddp_buf0_valid") && 0==val("ddp_buf1_valid") && 1==val("ddp_active_buf")) {
    PR("   !!! Invalid DDP buf 0 valid, but buf 1 active.\n");
  } else {
    PR("   DDP is enabled, but no buffers are active && valid.\n");




    if (0==val("ddp_indicate_out")) {
      if (0==val("ddp_buf0_indicate") && 0==val("ddp_buf1_indicate")) {
	PR("   0 length Indicate buffers ");
	if (0==val("rx_hdr_offset")) {
	  PR("will cause new data to be held in PMRX.\n");	
	} else {
	  PR("is causing %u bytes to be held in PMRX\n",
		      val("rx_hdr_offset"));
	}
      } else {
	PR("   Data being indicated to host\n");	  
      }
    } else if (1==val("ddp_indicate_out")) {
      PR("   Indicate is off, which ");
      if (0==val("rx_hdr_offset")) {
	PR("will cause new data to be held in PMRX.\n");	
      } else {
	PR("is causing %u bytes to be held in PMRX\n",
		    val("rx_hdr_offset"));
      }	
    }
  }




}
