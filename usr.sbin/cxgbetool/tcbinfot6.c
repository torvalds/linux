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
_TCBVAR g_tcb_info6[]={
  {"ulp_type"                     , 0,    0,    3, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "ulp_type"                     , /* aka */
   COMP_NONE                      , /* comp */
   "ULP mode: 0 =toe, 2=iscsi, 4=rdma, 5=ddp, 6=fcoe, 7=user, 8=tls, 9=dtls, remaining values reserved", /*desc*/
    NULL, /*akadesc */
  },
  {"ulp_raw"                      , 0,    4,   11, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "ulp"                          , /* aka */
   COMP_ULP                       , /* comp */
   "ULP subtype", /*desc*/
    NULL, /*akadesc */
  },
  {"l2t_ix"                       , 0,   12,   23, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "l2t_ix"                       , /* aka */
   COMP_NONE                      , /* comp */
   "Destination MAC address index", /*desc*/
    NULL, /*akadesc */
  },
  {"smac_sel"                     , 0,   24,   31, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "smac_sel"                     , /* aka */
   COMP_NONE                      , /* comp */
   "Source MAC address index", /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MIGRATING"                 , 0,   32,   32, /* name,aux,lo,hi */
   "t_flags"                      ,        0,   0, /* faka,flo,fhi */
   "migrating"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_NON_OFFLOAD"               , 0,   33,   33, /* name,aux,lo,hi */
   "t_flags"                      ,        1,   1, /* faka,flo,fhi */
   "non_offload"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_LOCK_TID"                  , 0,   34,   34, /* name,aux,lo,hi */
   "t_flags"                      ,        2,   2, /* faka,flo,fhi */
   "lock_tid"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_KEEPALIVE"                 , 0,   35,   35, /* name,aux,lo,hi */
   "t_flags"                      ,        3,   3, /* faka,flo,fhi */
   "keepalive"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DACK"                      , 0,   36,   36, /* name,aux,lo,hi */
   "t_flags"                      ,        4,   4, /* faka,flo,fhi */
   "dack"                         , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DACK_MSS"                  , 0,   37,   37, /* name,aux,lo,hi */
   "t_flags"                      ,        5,   5, /* faka,flo,fhi */
   "dack_mss"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DACK_NOT_ACKED"            , 0,   38,   38, /* name,aux,lo,hi */
   "t_flags"                      ,        6,   6, /* faka,flo,fhi */
   "dack_not_acked"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_NAGLE"                     , 0,   39,   39, /* name,aux,lo,hi */
   "t_flags"                      ,        7,   7, /* faka,flo,fhi */
   "nagle"                        , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_SSWS_DISABLED"             , 0,   40,   40, /* name,aux,lo,hi */
   "t_flags"                      ,        8,   8, /* faka,flo,fhi */
   "ssws_disabled"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_FLOW_CONTROL_DDP"       , 0,   41,   41, /* name,aux,lo,hi */
   "t_flags"                      ,        9,   9, /* faka,flo,fhi */
   "rx_flow_control_ddp"          , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_FLOW_CONTROL_DISABLE"   , 0,   42,   42, /* name,aux,lo,hi */
   "t_flags"                      ,       10,  10, /* faka,flo,fhi */
   "rx_flow_control_disable"      , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_CHANNEL"                , 0,   43,   43, /* name,aux,lo,hi */
   "t_flags"                      ,       11,  11, /* faka,flo,fhi */
   "rx_channel"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_CHANNEL"                , 0,   44,   45, /* name,aux,lo,hi */
   "t_flags"                      ,       12,  13, /* faka,flo,fhi */
   "tx_channel"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_QUIESCE"                , 0,   46,   46, /* name,aux,lo,hi */
   "t_flags"                      ,       14,  14, /* faka,flo,fhi */
   "tx_quiesce"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_QUIESCE"                , 0,   47,   47, /* name,aux,lo,hi */
   "t_flags"                      ,       15,  15, /* faka,flo,fhi */
   "rx_quiesce"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_PACE_AUTO"              , 0,   48,   48, /* name,aux,lo,hi */
   "t_flags"                      ,       16,  16, /* faka,flo,fhi */
   "tx_pace_auto"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_PACE_FIXED"             , 0,   49,   49, /* name,aux,lo,hi */
   "t_flags"                      ,       17,  17, /* faka,flo,fhi */
   "tx_pace_fixed"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_QUEUE"                  , 0,   50,   52, /* name,aux,lo,hi */
   "t_flags"                      ,       18,  20, /* faka,flo,fhi */
   "tx_queue"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TURBO"                     , 0,   53,   53, /* name,aux,lo,hi */
   "t_flags"                      ,       21,  21, /* faka,flo,fhi */
   "turbo"                        , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_SEL0"                , 0,   54,   54, /* name,aux,lo,hi */
   "t_flags"                      ,       22,  22, /* faka,flo,fhi */
   "cctrl_sel0"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_SEL1"                , 0,   55,   55, /* name,aux,lo,hi */
   "t_flags"                      ,       23,  23, /* faka,flo,fhi */
   "cctrl_sel1"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_FIN"                  , 0,   56,   56, /* name,aux,lo,hi */
   "t_flags"                      ,       24,  24, /* faka,flo,fhi */
   "core_fin"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_URG"                  , 0,   57,   57, /* name,aux,lo,hi */
   "t_flags"                      ,       25,  25, /* faka,flo,fhi */
   "core_urg"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_MORE"                 , 0,   58,   58, /* name,aux,lo,hi */
   "t_flags"                      ,       26,  26, /* faka,flo,fhi */
   "core_more"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_PUSH"                 , 0,   59,   59, /* name,aux,lo,hi */
   "t_flags"                      ,       27,  27, /* faka,flo,fhi */
   "core_push"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_FLUSH"                , 0,   60,   60, /* name,aux,lo,hi */
   "t_flags"                      ,       28,  28, /* faka,flo,fhi */
   "core_flush"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RCV_COALESCE_ENABLE"       , 0,   61,   61, /* name,aux,lo,hi */
   "t_flags"                      ,       29,  29, /* faka,flo,fhi */
   "rcv_coalesce_enable"          , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RCV_COALESCE_PUSH"         , 0,   62,   62, /* name,aux,lo,hi */
   "t_flags"                      ,       30,  30, /* faka,flo,fhi */
   "rcv_coalesce_push"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RCV_COALESCE_LAST_PSH"     , 0,   63,   63, /* name,aux,lo,hi */
   "t_flags"                      ,       31,  31, /* faka,flo,fhi */
   "rcv_coalesce_last_psh"        , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RCV_COALESCE_HEARTBEAT"    , 0,   64,   64, /* name,aux,lo,hi */
   "t_flags"                      ,       32,  32, /* faka,flo,fhi */
   "rcv_coalesce_heartbeat"       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RSS_FW"                    , 0,   65,   65, /* name,aux,lo,hi */
   "t_flags"                      ,       33,  33, /* faka,flo,fhi */
   "rss_fw"                       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_ACTIVE_OPEN"               , 0,   66,   66, /* name,aux,lo,hi */
   "t_flags"                      ,       34,  34, /* faka,flo,fhi */
   "active_open"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_ASK_MODE"                  , 0,   67,   67, /* name,aux,lo,hi */
   "t_flags"                      ,       35,  35, /* faka,flo,fhi */
   "ask_mode"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MOD_SCHD_REASON0"          , 0,   68,   68, /* name,aux,lo,hi */
   "t_flags"                      ,       36,  36, /* faka,flo,fhi */
   "mod_schd_reason0"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MOD_SCHD_REASON1"          , 0,   69,   69, /* name,aux,lo,hi */
   "t_flags"                      ,       37,  37, /* faka,flo,fhi */
   "mod_schd_reason1"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MOD_SCHD_REASON2"          , 0,   70,   70, /* name,aux,lo,hi */
   "t_flags"                      ,       38,  38, /* faka,flo,fhi */
   "mod_schd_reason2"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MOD_SCHD_TX"               , 0,   71,   71, /* name,aux,lo,hi */
   "t_flags"                      ,       39,  39, /* faka,flo,fhi */
   "mod_schd_tx"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_MOD_SCHD_RX"               , 0,   72,   72, /* name,aux,lo,hi */
   "t_flags"                      ,       40,  40, /* faka,flo,fhi */
   "mod_schd_rx"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TIMER"                     , 0,   73,   73, /* name,aux,lo,hi */
   "t_flags"                      ,       41,  41, /* faka,flo,fhi */
   "timer"                        , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DACK_TIMER"                , 0,   74,   74, /* name,aux,lo,hi */
   "t_flags"                      ,       42,  42, /* faka,flo,fhi */
   "dack_timer"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_PEER_FIN"                  , 0,   75,   75, /* name,aux,lo,hi */
   "t_flags"                      ,       43,  43, /* faka,flo,fhi */
   "peer_fin"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_COMPACT"                , 0,   76,   76, /* name,aux,lo,hi */
   "t_flags"                      ,       44,  44, /* faka,flo,fhi */
   "tx_compact"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_COMPACT"                , 0,   77,   77, /* name,aux,lo,hi */
   "t_flags"                      ,       45,  45, /* faka,flo,fhi */
   "rx_compact"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RDMA_ERROR"                , 0,   78,   78, /* name,aux,lo,hi */
   "t_flags"                      ,       46,  46, /* faka,flo,fhi */
   "rdma_error"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RDMA_FLM_ERROR"            , 0,   79,   79, /* name,aux,lo,hi */
   "t_flags"                      ,       47,  47, /* faka,flo,fhi */
   "rdma_flm_error"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TX_PDU_OUT"                , 0,   80,   80, /* name,aux,lo,hi */
   "t_flags"                      ,       48,  48, /* faka,flo,fhi */
   "tx_pdu_out"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RX_PDU_OUT"                , 0,   81,   81, /* name,aux,lo,hi */
   "t_flags"                      ,       49,  49, /* faka,flo,fhi */
   "rx_pdu_out"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DUPACK_COUNT_ODD"          , 0,   82,   82, /* name,aux,lo,hi */
   "t_flags"                      ,       50,  50, /* faka,flo,fhi */
   "dupack_count_odd"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_FAST_RECOVERY"             , 0,   83,   83, /* name,aux,lo,hi */
   "t_flags"                      ,       51,  51, /* faka,flo,fhi */
   "fast_recovery"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RECV_SCALE"                , 0,   84,   84, /* name,aux,lo,hi */
   "t_flags"                      ,       52,  52, /* faka,flo,fhi */
   "recv_scale"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RECV_TSTMP"                , 0,   85,   85, /* name,aux,lo,hi */
   "t_flags"                      ,       53,  53, /* faka,flo,fhi */
   "recv_tstmp"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_RECV_SACK"                 , 0,   86,   86, /* name,aux,lo,hi */
   "t_flags"                      ,       54,  54, /* faka,flo,fhi */
   "recv_sack"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_PEND_CTL0"                 , 0,   87,   87, /* name,aux,lo,hi */
   "t_flags"                      ,       55,  55, /* faka,flo,fhi */
   "pend_ctl0"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_PEND_CTL1"                 , 0,   88,   88, /* name,aux,lo,hi */
   "t_flags"                      ,       56,  56, /* faka,flo,fhi */
   "pend_ctl1"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_PEND_CTL2"                 , 0,   89,   89, /* name,aux,lo,hi */
   "t_flags"                      ,       57,  57, /* faka,flo,fhi */
   "pend_ctl2"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_IP_VERSION"                , 0,   90,   90, /* name,aux,lo,hi */
   "t_flags"                      ,       58,  58, /* faka,flo,fhi */
   "ip_version"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_ECN"                 , 0,   91,   91, /* name,aux,lo,hi */
   "t_flags"                      ,       59,  59, /* faka,flo,fhi */
   "cctrl_ecn"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_ECE"                 , 0,   92,   92, /* name,aux,lo,hi */
   "t_flags"                      ,       60,  60, /* faka,flo,fhi */
   "cctrl_ece"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_CWR"                 , 0,   93,   93, /* name,aux,lo,hi */
   "t_flags"                      ,       61,  61, /* faka,flo,fhi */
   "cctrl_cwr"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CCTRL_RFR"                 , 0,   94,   94, /* name,aux,lo,hi */
   "t_flags"                      ,       62,  62, /* faka,flo,fhi */
   "cctrl_rfr"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_CORE_BYPASS"               , 0,   95,   95, /* name,aux,lo,hi */
   "t_flags"                      ,       63,  63, /* faka,flo,fhi */
   "core_bypass"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"rss_info"                     , 0,   96,  105, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rss_info"                     , /* aka */
   COMP_NONE                      , /* comp */
   "RSS field", /*desc*/
    NULL, /*akadesc */
  },
  {"tos"                          , 0,  106,  111, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tos"                          , /* aka */
   COMP_NONE                      , /* comp */
   "TOS field for IP header", /*desc*/
    NULL, /*akadesc */
  },
  {"t_state"                      , 0,  112,  115, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_state"                      , /* aka */
   COMP_NONE                      , /* comp */
   "Connection TCP state (see TCP state table)", /*desc*/
    NULL, /*akadesc */
  },
  {"max_rt"                       , 0,  116,  119, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "max_rt"                       , /* aka */
   COMP_NONE                      , /* comp */
   "Maximum re-transmissions", /*desc*/
    NULL, /*akadesc */
  },
  {"t_maxseg"                     , 0,  120,  123, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_maxseg"                     , /* aka */
   COMP_NONE                      , /* comp */
   "MTU table index", /*desc*/
    NULL, /*akadesc */
  },
  {"snd_scale"                    , 0,  124,  127, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_scale"                    , /* aka */
   COMP_NONE                      , /* comp */
   "Scaling for receive window (0-14). Note: this is reverse of common definition.", /*desc*/
    NULL, /*akadesc */
  },
  {"rcv_scale"                    , 0,  128,  131, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rcv_scale"                    , /* aka */
   COMP_NONE                      , /* comp */
   "Scaling for send window (0-14). Note: this is reverse of common definition.", /*desc*/
    NULL, /*akadesc */
  },
  {"t_rxtshift"                   , 0,  132,  135, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_rxtshift"                   , /* aka */
   COMP_NONE                      , /* comp */
   "Retransmit exponential backoff", /*desc*/
    NULL, /*akadesc */
  },
  {"t_dupacks"                    , 0,  136,  139, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_dupacks"                    , /* aka */
   COMP_NONE                      , /* comp */
   "Number of duplicate ACKs received", /*desc*/
    NULL, /*akadesc */
  },
  {"timestamp_offset"             , 0,  140,  143, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "timestamp_offset"             , /* aka */
   COMP_NONE                      , /* comp */
   "Timestamp offset from running clock", /*desc*/
    NULL, /*akadesc */
  },
  {"rcv_adv"                      , 0,  144,  159, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rcv_adv"                      , /* aka */
   COMP_NONE                      , /* comp */
   "Peer advertised window", /*desc*/
    NULL, /*akadesc */
  },
  {"timestamp"                    , 0,  160,  191, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "timestamp"                    , /* aka */
   COMP_NONE                      , /* comp */
   "Timer accounting field", /*desc*/
    NULL, /*akadesc */
  },
  {"t_rtt_ts_recent_age"          , 0,  192,  223, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_rtt_ts_recent_age"          , /* aka */
   COMP_NONE                      , /* comp */
   "Round-trip time; timestamps: ts_recent_age", /*desc*/
    NULL, /*akadesc */
  },
  {"t_rtseq_recent"               , 0,  224,  255, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_rtseq_recent"               , /* aka */
   COMP_NONE                      , /* comp */
   "Sequence number being timed t_rtseq; timestamps t_recent", /*desc*/
    NULL, /*akadesc */
  },
  {"t_srtt"                       , 0,  256,  271, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_srtt"                       , /* aka */
   COMP_NONE                      , /* comp */
   "Smoothed round-trip time", /*desc*/
    NULL, /*akadesc */
  },
  {"t_rttvar"                     , 0,  272,  287, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "t_rttvar"                     , /* aka */
   COMP_NONE                      , /* comp */
   "Variance in round-trip time", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_max"                       , 0,  288,  319, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_max"                       , /* aka */
   COMP_NONE                      , /* comp */
   "Highest sequence number in transmit buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"snd_una_raw"                  , 0,  320,  347, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_una"                      , /* aka */
   COMP_TX_MAX                    , /* comp */
   "Offset of snd_una from tx_max", /*desc*/
    "Send unacknowledged", /*akadesc */
  },
  {"snd_nxt_raw"                  , 0,  348,  375, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_nxt"                      , /* aka */
   COMP_TX_MAX                    , /* comp */
   "Offset of snd_nxt from tx_max", /*desc*/
    "Send next", /*akadesc */
  },
  {"snd_max_raw"                  , 0,  376,  403, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_max"                      , /* aka */
   COMP_TX_MAX                    , /* comp */
   "Offset of snd_max from tx_max", /*desc*/
    "Highest sequence number sent", /*akadesc */
  },
  {"snd_rec_raw"                  , 0,  404,  431, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_rec"                      , /* aka */
   COMP_TX_MAX                    , /* comp */
   "Offset of NewReno fast recovery end sequence from tx_max", /*desc*/
    "NewReno fast recovery end sequence number", /*akadesc */
  },
  {"snd_cwnd"                     , 0,  432,  459, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_cwnd"                     , /* aka */
   COMP_NONE                      , /* comp */
   "Congestion-control window", /*desc*/
    NULL, /*akadesc */
  },
  {"snd_ssthresh"                 , 0,  460,  487, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "snd_ssthresh"                 , /* aka */
   COMP_NONE                      , /* comp */
   "Slow Start threshold", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_hdr_ptr_raw"               , 0,  488,  504, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_hdr_ptr"                   , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for first byte in send buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_last_ptr_raw"              , 0,  505,  521, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_last_ptr"                  , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for last byte in send buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rcv_nxt"                      , 0,  522,  553, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rcv_nxt"                      , /* aka */
   COMP_NONE                      , /* comp */
   "TCP receive next", /*desc*/
    NULL, /*akadesc */
  },
  {"rcv_wnd"                      , 0,  554,  581, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rcv_wnd"                      , /* aka */
   COMP_NONE                      , /* comp */
   "Receive credits (advertised to peer in receive window)", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_hdr_offset"                , 0,  582,  609, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_hdr_offset"                , /* aka */
   COMP_NONE                      , /* comp */
   "Receive in-order buffered data", /*desc*/
    NULL, /*akadesc */
  },
  {"ts_last_ack_sent_raw"         , 0,  610,  637, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "ts_last_ack_sent"             , /* aka */
   COMP_RCV_NXT                   , /* comp */
   "Offset of highest sequence acked from rcv_nxt", /*desc*/
    "Highest sequence number acked", /*akadesc */
  },
  {"rx_frag0_start_idx_raw"       , 0,  638,  665, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag0_start_idx"           , /* aka */
   COMP_RCV_NXT                   , /* comp */
   "Offset of receive fragment 0 start sequence from rcv_nxt", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag1_start_idx_offset"    , 0,  666,  693, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag1_start_idx_offset"    , /* aka */
   COMP_RCV_NXT                   , /* comp */
   "Offset of receive fragment 1 start sequence from rcv_nxt", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag0_len"                 , 0,  694,  721, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag0_len"                 , /* aka */
   COMP_NONE                      , /* comp */
   "Receive re-order fragment 0 length", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag1_len"                 , 0,  722,  749, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag1_len"                 , /* aka */
   COMP_NONE                      , /* comp */
   "Receive re-order fragment 1 length", /*desc*/
    NULL, /*akadesc */
  },
  {"pdu_len"                      , 0,  750,  765, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "pdu_len"                      , /* aka */
   COMP_NONE                      , /* comp */
   "Receive recovered PDU length", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ptr_raw"                   , 0,  766,  782, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ptr"                       , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for in-order receive buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag1_ptr_raw"             , 0,  783,  799, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag1_ptr"                 , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for out-of-order receive buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"main_slush"                   , 0,  800,  831, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "main_slush"                   , /* aka */
   COMP_NONE                      , /* comp */
   "Reserved", /*desc*/
    NULL, /*akadesc */
  },
  {"aux1_slush0"                  , 1,  832,  846, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "aux1_slush0"                  , /* aka */
   COMP_NONE                      , /* comp */
   "Reserved", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag2_start_idx_offset_raw", 1,  847,  874, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag2_start_idx_offset"    , /* aka */
   COMP_RCV_NXT                   , /* comp */
   "Offset of receive fragment 2 start sequence from rcv_nxt", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag2_ptr_raw"             , 1,  875,  891, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag2_ptr"                 , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for out-of-order receive buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag2_len_raw"             , 1,  892,  919, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag2_len"                 , /* aka */
   COMP_LEN                       , /* comp */
   "Receive re-order fragment 2 length", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag3_ptr_raw"             , 1,  920,  936, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag3_ptr"                 , /* aka */
   COMP_PTR                       , /* comp */
   "Page pointer for out-of-order receive buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag3_len_raw"             , 1,  937,  964, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag3_len"                 , /* aka */
   COMP_LEN                       , /* comp */
   "Receive re-order fragment 3 length", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_frag3_start_idx_offset_raw", 1,  965,  992, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_frag3_start_idx_offset"    , /* aka */
   COMP_RCV_NXT                   , /* comp */
   "Offset of receive fragment 3 start sequence from rcv_nxt", /*desc*/
    NULL, /*akadesc */
  },
  {"pdu_hdr_len"                  , 1,  993, 1000, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "pdu_hdr_len"                  , /* aka */
   COMP_NONE                      , /* comp */
   "Receive recovered PDU header length", /*desc*/
    NULL, /*akadesc */
  },
  {"aux1_slush1"                  , 1, 1001, 1019, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "aux1_slush1"                  , /* aka */
   COMP_NONE                      , /* comp */
   "Reserved", /*desc*/
    NULL, /*akadesc */
  },
  {"ulp_ext"                      , 1, 1020, 1023, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "ulp_ext"                      , /* aka */
   COMP_NONE                      , /* comp */
   "Extension of ulp_raw for PI configuration", /*desc*/
    NULL, /*akadesc */
  },

  {"irs_ulp"                      , 2,  832,  840, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "irs_ulp"                      , /* aka */
   COMP_NONE                      , /* comp */
   "IRS modulo marker_interval when enterring iWARP mode", /*desc*/
    NULL, /*akadesc */
  },
  {"iss_ulp"                      , 2,  841,  849, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "iss_ulp"                      , /* aka */
   COMP_NONE                      , /* comp */
   "ISS modulo marker_interval when entering iWARP mode", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_pdu_len"                   , 2,  850,  863, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_pdu_len"                   , /* aka */
   COMP_NONE                      , /* comp */
   "Length of Tx FPDU", /*desc*/
    NULL, /*akadesc */
  },
  {"cq_idx_sq"                    , 2,  864,  879, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "cq_idx_sq"                    , /* aka */
   COMP_NONE                      , /* comp */
   "CQ index of CQ for SQ", /*desc*/
    NULL, /*akadesc */
  },
  {"cq_idx_rq"                    , 2,  880,  895, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "cq_idx_rq"                    , /* aka */
   COMP_NONE                      , /* comp */
   "CQ index of CQ for RQ", /*desc*/
    NULL, /*akadesc */
  },
  {"qp_id"                        , 2,  896,  911, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "qp_id"                        , /* aka */
   COMP_NONE                      , /* comp */
   "QP index", /*desc*/
    NULL, /*akadesc */
  },
  {"pd_id"                        , 2,  912,  927, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "pd_id"                        , /* aka */
   COMP_NONE                      , /* comp */
   "PD index", /*desc*/
    NULL, /*akadesc */
  },
  {"STAG"                         , 2,  928,  959, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "stag"                         , /* aka */
   COMP_NONE                      , /* comp */
   "PDU response STAG", /*desc*/
    NULL, /*akadesc */
  },
  {"rq_start"                     , 2,  960,  985, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rq_start"                     , /* aka */
   COMP_NONE                      , /* comp */
   "DW aligned starting addres of RQ", /*desc*/
    NULL, /*akadesc */
  },
  {"rq_MSN"                       , 2,  986,  998, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rq_msn"                       , /* aka */
   COMP_NONE                      , /* comp */
   "Current MSN (modulo 8K, further check in ULP_RX)", /*desc*/
    NULL, /*akadesc */
  },
  {"rq_max_offset"                , 2,  999, 1002, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rq_max_offset"                , /* aka */
   COMP_NONE                      , /* comp */
   "Log size RQ (the size in hardware is rounded up to a power of 2)", /*desc*/
    NULL, /*akadesc */
  },
  {"rq_write_ptr"                 , 2, 1003, 1015, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rq_write_ptr"                 , /* aka */
   COMP_NONE                      , /* comp */
   "Host RQ write pointer", /*desc*/
    NULL, /*akadesc */
  },
  {"RDMAP_opcode"                 , 2, 1016, 1019, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rdmap_opcode"                 , /* aka */
   COMP_NONE                      , /* comp */
   "Current FPDU command", /*desc*/
    NULL, /*akadesc */
  },
  {"ord_L_bit_vld"                , 2, 1020, 1020, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "ord_l_bit_vld"                , /* aka */
   COMP_NONE                      , /* comp */
   "Current FPDU has L-bit set", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_flush"                     , 2, 1021, 1021, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_flush"                     , /* aka */
   COMP_NONE                      , /* comp */
   "1 = flush CPL_TX_DATA", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_oos_rxmt"                  , 2, 1022, 1022, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_oos_rxmt"                  , /* aka */
   COMP_NONE                      , /* comp */
   "Retransmit is out of FPDU sync", /*desc*/
    NULL, /*akadesc */
  },
  {"tx_oos_txmt"                  , 2, 1023, 1023, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "tx_oos_txmt"                  , /* aka */
   COMP_NONE                      , /* comp */
   "Transmit is out of FPDU sync, or disable aligned transmission", /*desc*/
    NULL, /*akadesc */
  },

  {"rx_ddp_buf0_offset"           , 3,  832,  855, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf0_offset"           , /* aka */
   COMP_NONE                      , /* comp */
   "Current offset into DDP buffer 0", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ddp_buf0_len"              , 3,  856,  879, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf0_len"              , /* aka */
   COMP_NONE                      , /* comp */
   "Length of DDP buffer 0", /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_INDICATE_OUT"          , 3,  880,  880, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        0,   0, /* faka,flo,fhi */
   "ddp_indicate_out"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_ACTIVE_BUF"            , 3,  881,  881, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        1,   1, /* faka,flo,fhi */
   "ddp_active_buf"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_OFF"                   , 3,  882,  882, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        2,   2, /* faka,flo,fhi */
   "ddp_off"                      , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_WAIT_FRAG"             , 3,  883,  883, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        3,   3, /* faka,flo,fhi */
   "ddp_wait_frag"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF_INF"               , 3,  884,  884, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        4,   4, /* faka,flo,fhi */
   "ddp_buf_inf"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_RX2TX"                 , 3,  885,  885, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        5,   5, /* faka,flo,fhi */
   "ddp_rx2tx"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_INDICATE_FLL"          , 3,  886,  886, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        6,   6, /* faka,flo,fhi */
   "ddp_indicate_fll"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_MAIN_UNUSED"           , 3,  887,  887, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        7,   7, /* faka,flo,fhi */
   "ddp_main_unused"              , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_TLS_KEY_MODE"              , 3,  887,  887, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        7,   7, /* faka,flo,fhi */
   "tls_key_mode"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF0_VALID"            , 3,  888,  888, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        8,   8, /* faka,flo,fhi */
   "ddp_buf0_valid"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF0_INDICATE"         , 3,  889,  889, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,        9,   9, /* faka,flo,fhi */
   "ddp_buf0_indicate"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF0_FLUSH"            , 3,  890,  890, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       10,  10, /* faka,flo,fhi */
   "ddp_buf0_flush"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PSHF_ENABLE_0"         , 3,  891,  891, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       11,  11, /* faka,flo,fhi */
   "ddp_pshf_enable_0"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PUSH_DISABLE_0"        , 3,  892,  892, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       12,  12, /* faka,flo,fhi */
   "ddp_push_disable_0"           , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PSH_NO_INVALIDATE0"    , 3,  893,  893, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       13,  13, /* faka,flo,fhi */
   "ddp_psh_no_invalidate0"       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF0_UNUSED"           , 3,  894,  895, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       14,  15, /* faka,flo,fhi */
   "ddp_buf0_unused"              , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF1_VALID"            , 3,  896,  896, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       16,  16, /* faka,flo,fhi */
   "ddp_buf1_valid"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF1_INDICATE"         , 3,  897,  897, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       17,  17, /* faka,flo,fhi */
   "ddp_buf1_indicate"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF1_FLUSH"            , 3,  898,  898, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       18,  18, /* faka,flo,fhi */
   "ddp_buf1_flush"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PSHF_ENABLE_1"         , 3,  899,  899, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       19,  19, /* faka,flo,fhi */
   "ddp_pshf_enable_1"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PUSH_DISABLE_1"        , 3,  900,  900, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       20,  20, /* faka,flo,fhi */
   "ddp_push_disable_1"           , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_PSH_NO_INVALIDATE1"    , 3,  901,  901, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       21,  21, /* faka,flo,fhi */
   "ddp_psh_no_invalidate1"       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"TF_DDP_BUF1_UNUSED"           , 3,  902,  903, /* name,aux,lo,hi */
   "rx_ddp_flags"                 ,       22,  23, /* faka,flo,fhi */
   "ddp_buf1_unused"              , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ddp_buf1_offset"           , 3,  904,  927, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf1_offset"           , /* aka */
   COMP_NONE                      , /* comp */
   "Current offset into DDP buffer 1", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ddp_buf1_len"              , 3,  928,  951, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf1_len"              , /* aka */
   COMP_NONE                      , /* comp */
   "Length of DDP buffer 1", /*desc*/
    NULL, /*akadesc */
  },
  {"aux3_slush"                   , 3,  952,  959, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "aux3_slush"                   , /* aka */
   COMP_NONE                      , /* comp */
   "Reserved", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ddp_buf0_tag"              , 3,  960,  991, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf0_tag"              , /* aka */
   COMP_NONE                      , /* comp */
   "Tag for DDP buffer 0", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_ddp_buf1_tag"              , 3,  992, 1023, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_ddp_buf1_tag"              , /* aka */
   COMP_NONE                      , /* comp */
   "Tag for DDP buffer 1", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_buf_offset"            , 4,  832,  855, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_buf_offset"            , /* aka */
   COMP_NONE                      , /* comp */
   "Current offset into DDP buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_buf_len"               , 4,  856,  879, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_buf_len"               , /* aka */
   COMP_NONE                      , /* comp */
   "Length of DDP buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_flags"                 , 4,  880,  895, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_flags"                 , /* aka */
   COMP_NONE                      , /* comp */
   "DDP control flags", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_seq"                   , 4,  896,  959, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_seq"                   , /* aka */
   COMP_NONE                      , /* comp */
   "TLS/SSL sequence number", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_buf_tag"               , 4,  960,  991, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_buf_tag"               , /* aka */
   COMP_NONE                      , /* comp */
   "Tag for DDP buffer", /*desc*/
    NULL, /*akadesc */
  },
  {"rx_tls_key_tag"               , 4,  992, 1023, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "rx_tls_key_tag"               , /* aka */
   COMP_NONE                      , /* comp */
   "Tag for TLS crypto state", /*desc*/
    NULL, /*akadesc */
  },
  {NULL,0,0,0,  NULL,0,0, NULL, 0, NULL, NULL}, /*terminator*/
};

/* ====================================================== */
_TCBVAR g_scb_info6[]={
  {"OPT_1_RSS_INFO"               , 0,    0,   11, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_RSS_INFO"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_1_LISTEN_INTERFACE"       , 0,   12,   19, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_LISTEN_INTERFACE"       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_1_LISTEN_FILTER"          , 0,   20,   20, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_LISTEN_FILTER"          , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_1_SYN_DEFENSE"            , 0,   21,   21, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_SYN_DEFENSE"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_1_CONNECTION_POLICY"      , 0,   22,   23, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_CONNECTION_POLICY"      , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_1_FLT_INFO"               , 0,   24,   63, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_1_FLT_INFO"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_ACCEPT_MODE"            , 0,   64,   65, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_ACCEPT_MODE"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_TX_CHANNEL"             , 0,   66,   67, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_TX_CHANNEL"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_NO_CONGESTION_CONTROL"  , 0,   68,   68, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_NO_CONGESTION_CONTROL"  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_DELAYED_ACK"            , 0,   69,   69, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_DELAYED_ACK"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_INJECT_TIMER"           , 0,   70,   70, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_INJECT_TIMER"           , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_NON_OFFLOAD"            , 0,   71,   71, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_NON_OFFLOAD"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_ULP_MODE"               , 0,   72,   75, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_ULP_MODE"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_MAX_RCV_BUFFER"         , 0,   76,   85, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_MAX_RCV_BUFFER"         , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_TOS"                    , 0,   86,   91, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_TOS"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_SM_SEL"                 , 0,   92,   99, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_SM_SEL"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_L2T_IX"                 , 0,  100,  111, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_L2T_IX"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_TCAM_BYPASS"            , 0,  112,  112, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_TCAM_BYPASS"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_NAGLE"                  , 0,  113,  113, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_NAGLE"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_WSF"                    , 0,  114,  117, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_WSF"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_KEEPALIVE"              , 0,  118,  118, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_KEEPALIVE"              , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_CONN_MAXRT"             , 0,  119,  122, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_CONN_MAXRT"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_MAXRT_OVERRIDE"         , 0,  123,  123, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_MAXRT_OVERRIDE"         , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"OPT_0_MAX_SEG"                , 0,  124,  127, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "OPT_0_MAX_SEG"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"scb_slush"                    , 0,  128, 1023, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "scb_slush"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {NULL,0,0,0,  NULL,0,0, NULL, 0, NULL, NULL}, /*terminator*/
};

/* ====================================================== */
_TCBVAR g_fcb_info6[]={
  {"filter"                       , 0,   33,   33, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "filter"                       , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Drop_Encapsulation_Headers"   , 0,   35,   35, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Drop_Encapsulation_Headers"   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Report_TID"                   , 0,   53,   53, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Report_TID"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Drop"                         , 0,   54,   54, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Drop"                         , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Direct_Steer"                 , 0,   55,   55, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Direct_Steer"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Mask_Hash"                    , 0,   48,   48, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Mask_Hash"                    , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Direct_Steer_Hash"            , 0,   49,   49, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Direct_Steer_Hash"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Loopback"                     , 0,   91,   91, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Loopback"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Loopback_TX_Channel"          , 0,   44,   45, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Loopback_TX_Channel"          , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Loopback_TX_Loopback"         , 0,   85,   85, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Loopback_TX_Loopback"         , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Swap_MAC_addresses"           , 0,   86,   86, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Swap_MAC_addresses"           , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Rewrite_DMAC"                 , 0,   92,   92, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Rewrite_DMAC"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Rewrite_SMAC"                 , 0,   93,   93, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Rewrite_SMAC"                 , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Insert_VLAN"                  , 0,   94,   94, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Insert_VLAN"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Remove_VLAN"                  , 0,   39,   39, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Remove_VLAN"                  , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"NAT_Mode"                     , 0,   50,   52, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "NAT_Mode"                     , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"NAT_seq_check"                , 0,   42,   42, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "NAT_seq_check"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"NAT_flag_check"               , 0,   84,   84, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "NAT_flag_check"               , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Count_Hits"                   , 0,   36,   36, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Count_Hits"                   , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Hit_frame_cnt"                , 0,  160,  191, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Hit_frame_cnt"                , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Hit_byte_cnt_high"            , 0,  224,  255, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Hit_byte_cnt_high"            , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {"Hit_byte_cnt_low"             , 0,  192,  223, /* name,aux,lo,hi */
   NULL                           ,        0,   0, /* faka,flo,fhi */
   "Hit_byte_cnt_low"             , /* aka */
   COMP_NONE                      , /* comp */
   NULL, /*desc*/
    NULL, /*akadesc */
  },
  {NULL,0,0,0,  NULL,0,0, NULL, 0, NULL, NULL}, /*terminator*/
};

