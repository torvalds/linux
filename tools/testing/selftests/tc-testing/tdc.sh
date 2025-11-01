#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# If a module is required and was not compiled
# the test that requires it will fail anyways
try_modprobe() {
   if ! modprobe -q -R "$1"; then
      echo "Module $1 not found... skipping."
   else
      modprobe "$1"
   fi
}

try_modprobe netdevsim
try_modprobe act_bpf
try_modprobe act_connmark
try_modprobe act_csum
try_modprobe act_ct
try_modprobe act_ctinfo
try_modprobe act_gact
try_modprobe act_gate
try_modprobe act_mirred
try_modprobe act_mpls
try_modprobe act_nat
try_modprobe act_pedit
try_modprobe act_police
try_modprobe act_sample
try_modprobe act_simple
try_modprobe act_skbedit
try_modprobe act_skbmod
try_modprobe act_tunnel_key
try_modprobe act_vlan
try_modprobe act_ife
try_modprobe act_meta_mark
try_modprobe act_meta_skbtcindex
try_modprobe act_meta_skbprio
try_modprobe cls_basic
try_modprobe cls_bpf
try_modprobe cls_cgroup
try_modprobe cls_flow
try_modprobe cls_flower
try_modprobe cls_fw
try_modprobe cls_matchall
try_modprobe cls_route
try_modprobe cls_u32
try_modprobe em_canid
try_modprobe em_cmp
try_modprobe em_ipset
try_modprobe em_ipt
try_modprobe em_meta
try_modprobe em_nbyte
try_modprobe em_text
try_modprobe em_u32
try_modprobe sch_cake
try_modprobe sch_cbs
try_modprobe sch_choke
try_modprobe sch_codel
try_modprobe sch_drr
try_modprobe sch_etf
try_modprobe sch_ets
try_modprobe sch_fq
try_modprobe sch_fq_codel
try_modprobe sch_fq_pie
try_modprobe sch_gred
try_modprobe sch_hfsc
try_modprobe sch_hhf
try_modprobe sch_htb
try_modprobe sch_teql
try_modprobe sch_dualpi2
./tdc.py -J"$(nproc)"
