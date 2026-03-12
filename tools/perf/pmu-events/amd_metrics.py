#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import argparse
import math
import os
from typing import Optional
from common_metrics import Cycles
from metric import (d_ratio, has_event, max, Event, JsonEncodeMetric,
                    JsonEncodeMetricGroupDescriptions, Literal, LoadEvents,
                    Metric, MetricGroup, Select)

# Global command line arguments.
_args = None
_zen_model: int = 1
interval_sec = Event("duration_time")
ins = Event("instructions")
cycles = Event("cycles")
# Number of CPU cycles scaled for SMT.
smt_cycles = Select(cycles / 2, Literal("#smt_on"), cycles)


def AmdBr():
    def Total() -> MetricGroup:
        br = Event("ex_ret_brn")
        br_m_all = Event("ex_ret_brn_misp")
        br_clr = Event("ex_ret_brn_cond_misp",
                       "ex_ret_msprd_brnch_instr_dir_msmtch",
                       "ex_ret_brn_resync")

        br_r = d_ratio(br, interval_sec)
        ins_r = d_ratio(ins, br)
        misp_r = d_ratio(br_m_all, br)
        clr_r = d_ratio(br_clr, interval_sec)

        return MetricGroup("lpm_br_total", [
            Metric("lpm_br_total_retired",
                   "The number of branch instructions retired per second.", br_r,
                   "insn/s"),
            Metric(
                "lpm_br_total_mispred",
                "The number of branch instructions retired, of any type, that were "
                "not correctly predicted as a percentage of all branch instrucions.",
                misp_r, "100%"),
            Metric("lpm_br_total_insn_between_branches",
                   "The number of instructions divided by the number of branches.",
                   ins_r, "insn"),
            Metric("lpm_br_total_insn_fe_resteers",
                   "The number of resync branches per second.", clr_r, "req/s")
        ])

    def Taken() -> MetricGroup:
        br = Event("ex_ret_brn_tkn")
        br_m_tk = Event("ex_ret_brn_tkn_misp")
        br_r = d_ratio(br, interval_sec)
        ins_r = d_ratio(ins, br)
        misp_r = d_ratio(br_m_tk, br)
        return MetricGroup("lpm_br_taken", [
            Metric("lpm_br_taken_retired",
                   "The number of taken branches that were retired per second.",
                   br_r, "insn/s"),
            Metric(
                "lpm_br_taken_mispred",
                "The number of retired taken branch instructions that were "
                "mispredicted as a percentage of all taken branches.", misp_r,
                "100%"),
            Metric(
                "lpm_br_taken_insn_between_branches",
                "The number of instructions divided by the number of taken branches.",
                ins_r, "insn"),
        ])

    def Conditional() -> Optional[MetricGroup]:
        global _zen_model
        br = Event("ex_ret_brn_cond", "ex_ret_cond")
        br_r = d_ratio(br, interval_sec)
        ins_r = d_ratio(ins, br)

        metrics = [
            Metric("lpm_br_cond_retired", "Retired conditional branch instructions.",
                   br_r, "insn/s"),
            Metric("lpm_br_cond_insn_between_branches",
                   "The number of instructions divided by the number of conditional "
                   "branches.", ins_r, "insn"),
        ]
        if _zen_model == 2:
            br_m_cond = Event("ex_ret_cond_misp")
            misp_r = d_ratio(br_m_cond, br)
            metrics += [
                Metric("lpm_br_cond_mispred",
                       "Retired conditional branch instructions mispredicted as a "
                       "percentage of all conditional branches.", misp_r, "100%"),
            ]

        return MetricGroup("lpm_br_cond", metrics)

    def Fused() -> MetricGroup:
        br = Event("ex_ret_fused_instr", "ex_ret_fus_brnch_inst")
        br_r = d_ratio(br, interval_sec)
        ins_r = d_ratio(ins, br)
        return MetricGroup("lpm_br_cond", [
            Metric("lpm_br_fused_retired",
                   "Retired fused branch instructions per second.", br_r, "insn/s"),
            Metric(
                "lpm_br_fused_insn_between_branches",
                "The number of instructions divided by the number of fused "
                "branches.", ins_r, "insn"),
        ])

    def Far() -> MetricGroup:
        br = Event("ex_ret_brn_far")
        br_r = d_ratio(br, interval_sec)
        ins_r = d_ratio(ins, br)
        return MetricGroup("lpm_br_far", [
            Metric("lpm_br_far_retired", "Retired far control transfers per second.",
                   br_r, "insn/s"),
            Metric(
                "lpm_br_far_insn_between_branches",
                "The number of instructions divided by the number of far branches.",
                ins_r, "insn"),
        ])

    return MetricGroup("lpm_br", [Total(), Taken(), Conditional(), Fused(), Far()],
                       description="breakdown of retired branch instructions")


def AmdCtxSw() -> MetricGroup:
    cs = Event("context\\-switches")
    metrics = [
        Metric("lpm_cs_rate", "Context switches per second",
               d_ratio(cs, interval_sec), "ctxsw/s")
    ]

    ev = Event("instructions")
    metrics.append(Metric("lpm_cs_instr", "Instructions per context switch",
                          d_ratio(ev, cs), "instr/cs"))

    ev = Event("cycles")
    metrics.append(Metric("lpm_cs_cycles", "Cycles per context switch",
                          d_ratio(ev, cs), "cycles/cs"))

    ev = Event("ls_dispatch.pure_ld", "ls_dispatch.ld_dispatch")
    metrics.append(Metric("lpm_cs_loads", "Loads per context switch",
                          d_ratio(ev, cs), "loads/cs"))

    ev = Event("ls_dispatch.pure_st", "ls_dispatch.store_dispatch")
    metrics.append(Metric("lpm_cs_stores", "Stores per context switch",
                          d_ratio(ev, cs), "stores/cs"))

    ev = Event("ex_ret_brn_tkn")
    metrics.append(Metric("lpm_cs_br_taken", "Branches taken per context switch",
                          d_ratio(ev, cs), "br_taken/cs"))

    return MetricGroup("lpm_cs", metrics,
                       description=("Number of context switches per second, instructions "
                                    "retired & core cycles between context switches"))


def AmdDtlb() -> Optional[MetricGroup]:
    global _zen_model
    if _zen_model >= 4:
        return None

    d_dat = Event("ls_dc_accesses") if _zen_model <= 3 else None
    d_h4k = Event("ls_l1_d_tlb_miss.tlb_reload_4k_l2_hit")
    d_hcoal = Event(
        "ls_l1_d_tlb_miss.tlb_reload_coalesced_page_hit") if _zen_model >= 2 else 0
    d_h2m = Event("ls_l1_d_tlb_miss.tlb_reload_2m_l2_hit")
    d_h1g = Event("ls_l1_d_tlb_miss.tlb_reload_1g_l2_hit")

    d_m4k = Event("ls_l1_d_tlb_miss.tlb_reload_4k_l2_miss")
    d_mcoal = Event(
        "ls_l1_d_tlb_miss.tlb_reload_coalesced_page_miss") if _zen_model >= 2 else 0
    d_m2m = Event("ls_l1_d_tlb_miss.tlb_reload_2m_l2_miss")
    d_m1g = Event("ls_l1_d_tlb_miss.tlb_reload_1g_l2_miss")

    d_w0 = Event("ls_tablewalker.dc_type0") if _zen_model <= 3 else None
    d_w1 = Event("ls_tablewalker.dc_type1") if _zen_model <= 3 else None
    walks = d_w0 + d_w1
    walks_r = d_ratio(walks, interval_sec)
    ins_w = d_ratio(ins, walks)
    l1 = d_dat
    l1_r = d_ratio(l1, interval_sec)
    l2_hits = d_h4k + d_hcoal + d_h2m + d_h1g
    l2_miss = d_m4k + d_mcoal + d_m2m + d_m1g
    l2_r = d_ratio(l2_hits + l2_miss, interval_sec)
    l1_miss = l2_hits + l2_miss + walks
    l1_hits = max(l1 - l1_miss, 0)
    ins_l = d_ratio(ins, l1_miss)

    return MetricGroup("lpm_dtlb", [
        MetricGroup("lpm_dtlb_ov", [
            Metric("lpm_dtlb_ov_insn_bt_l1_miss",
                   "DTLB overview: instructions between l1 misses.", ins_l,
                   "insns"),
            Metric("lpm_dtlb_ov_insn_bt_walks",
                   "DTLB overview: instructions between dtlb page table walks.",
                   ins_w, "insns"),
        ]),
        MetricGroup("lpm_dtlb_l1", [
            Metric("lpm_dtlb_l1_hits",
                   "DTLB L1 hits as percentage of all DTLB L1 accesses.",
                   d_ratio(l1_hits, l1), "100%"),
            Metric("lpm_dtlb_l1_miss",
                   "DTLB L1 misses as percentage of all DTLB L1 accesses.",
                   d_ratio(l1_miss, l1), "100%"),
            Metric("lpm_dtlb_l1_reqs", "DTLB L1 accesses per second.", l1_r,
                   "insns/s"),
        ]),
        MetricGroup("lpm_dtlb_l2", [
            Metric("lpm_dtlb_l2_hits",
                   "DTLB L2 hits as percentage of all DTLB L2 accesses.",
                   d_ratio(l2_hits, l2_hits + l2_miss), "100%"),
            Metric("lpm_dtlb_l2_miss",
                   "DTLB L2 misses as percentage of all DTLB L2 accesses.",
                   d_ratio(l2_miss, l2_hits + l2_miss), "100%"),
            Metric("lpm_dtlb_l2_reqs", "DTLB L2 accesses per second.", l2_r,
                   "insns/s"),
            MetricGroup("lpm_dtlb_l2_4kb", [
                Metric(
                    "lpm_dtlb_l2_4kb_hits",
                    "DTLB L2 4kb page size hits as percentage of all DTLB L2 4kb "
                    "accesses.", d_ratio(d_h4k, d_h4k + d_m4k), "100%"),
                Metric(
                    "lpm_dtlb_l2_4kb_miss",
                    "DTLB L2 4kb page size misses as percentage of all DTLB L2 4kb"
                    "accesses.", d_ratio(d_m4k, d_h4k + d_m4k), "100%")
            ]),
            MetricGroup("lpm_dtlb_l2_coalesced", [
                Metric(
                    "lpm_dtlb_l2_coal_hits",
                    "DTLB L2 coalesced page (16kb) hits as percentage of all DTLB "
                    "L2 coalesced accesses.", d_ratio(d_hcoal,
                                                      d_hcoal + d_mcoal), "100%"),
                Metric(
                    "lpm_dtlb_l2_coal_miss",
                    "DTLB L2 coalesced page (16kb) misses as percentage of all "
                    "DTLB L2 coalesced accesses.",
                    d_ratio(d_mcoal, d_hcoal + d_mcoal), "100%")
            ]),
            MetricGroup("lpm_dtlb_l2_2mb", [
                Metric(
                    "lpm_dtlb_l2_2mb_hits",
                    "DTLB L2 2mb page size hits as percentage of all DTLB L2 2mb "
                    "accesses.", d_ratio(d_h2m, d_h2m + d_m2m), "100%"),
                Metric(
                    "lpm_dtlb_l2_2mb_miss",
                    "DTLB L2 2mb page size misses as percentage of all DTLB L2 "
                    "accesses.", d_ratio(d_m2m, d_h2m + d_m2m), "100%")
            ]),
            MetricGroup("lpm_dtlb_l2_1g", [
                Metric(
                    "lpm_dtlb_l2_1g_hits",
                    "DTLB L2 1gb page size hits as percentage of all DTLB L2 1gb "
                    "accesses.", d_ratio(d_h1g, d_h1g + d_m1g), "100%"),
                Metric(
                    "lpm_dtlb_l2_1g_miss",
                    "DTLB L2 1gb page size misses as percentage of all DTLB L2 "
                    "1gb accesses.", d_ratio(d_m1g, d_h1g + d_m1g), "100%")
            ]),
        ]),
        MetricGroup("lpm_dtlb_walks", [
            Metric("lpm_dtlb_walks_reqs", "DTLB page table walks per second.",
                   walks_r, "walks/s"),
        ]),
    ], description="Data TLB metrics")


def AmdItlb():
    global _zen_model
    l2h = Event("bp_l1_tlb_miss_l2_tlb_hit", "bp_l1_tlb_miss_l2_hit")
    l2m = Event("l2_itlb_misses")
    l2r = l2h + l2m

    itlb_l1_mg = None
    l1m = l2r
    if _zen_model <= 3:
        l1r = Event("ic_fw32")
        l1h = max(l1r - l1m, 0)
        itlb_l1_mg = MetricGroup("lpm_itlb_l1", [
            Metric("lpm_itlb_l1_hits",
                   "L1 ITLB hits as a perecentage of L1 ITLB accesses.",
                   d_ratio(l1h, l1h + l1m), "100%"),
            Metric("lpm_itlb_l1_miss",
                   "L1 ITLB misses as a perecentage of L1 ITLB accesses.",
                   d_ratio(l1m, l1h + l1m), "100%"),
            Metric("lpm_itlb_l1_reqs",
                   "The number of 32B fetch windows transferred from IC pipe to DE "
                   "instruction decoder per second.", d_ratio(
                       l1r, interval_sec),
                   "windows/sec"),
        ])

    return MetricGroup("lpm_itlb", [
        MetricGroup("lpm_itlb_ov", [
            Metric("lpm_itlb_ov_insn_bt_l1_miss",
                   "Number of instructions between l1 misses", d_ratio(
                       ins, l1m), "insns"),
            Metric("lpm_itlb_ov_insn_bt_l2_miss",
                   "Number of instructions between l2 misses", d_ratio(
                       ins, l2m), "insns"),
        ]),
        itlb_l1_mg,
        MetricGroup("lpm_itlb_l2", [
            Metric("lpm_itlb_l2_hits",
                   "L2 ITLB hits as a percentage of all L2 ITLB accesses.",
                   d_ratio(l2h, l2r), "100%"),
            Metric("lpm_itlb_l2_miss",
                   "L2 ITLB misses as a percentage of all L2 ITLB accesses.",
                   d_ratio(l2m, l2r), "100%"),
            Metric("lpm_itlb_l2_reqs", "ITLB accesses per second.",
                   d_ratio(l2r, interval_sec), "accesses/sec"),
        ]),
    ], description="Instruction TLB breakdown")


def AmdLdSt() -> MetricGroup:
    ldst_ld = Event("ls_dispatch.pure_ld", "ls_dispatch.ld_dispatch")
    ldst_st = Event("ls_dispatch.pure_st", "ls_dispatch.store_dispatch")
    ldst_ldc1 = Event(f"{ldst_ld}/cmask=1/")
    ldst_stc1 = Event(f"{ldst_st}/cmask=1/")
    ldst_ldc2 = Event(f"{ldst_ld}/cmask=2/")
    ldst_stc2 = Event(f"{ldst_st}/cmask=2/")
    ldst_ldc3 = Event(f"{ldst_ld}/cmask=3/")
    ldst_stc3 = Event(f"{ldst_st}/cmask=3/")
    ldst_cyc = Event("ls_not_halted_cyc")

    ld_rate = d_ratio(ldst_ld, interval_sec)
    st_rate = d_ratio(ldst_st, interval_sec)

    ld_v1 = max(ldst_ldc1 - ldst_ldc2, 0)
    ld_v2 = max(ldst_ldc2 - ldst_ldc3, 0)
    ld_v3 = ldst_ldc3

    st_v1 = max(ldst_stc1 - ldst_stc2, 0)
    st_v2 = max(ldst_stc2 - ldst_stc3, 0)
    st_v3 = ldst_stc3

    return MetricGroup("lpm_ldst", [
        MetricGroup("lpm_ldst_total", [
            Metric("lpm_ldst_total_ld", "Number of loads dispatched per second.",
                   ld_rate, "insns/sec"),
            Metric("lpm_ldst_total_st", "Number of stores dispatched per second.",
                   st_rate, "insns/sec"),
        ]),
        MetricGroup("lpm_ldst_percent_insn", [
            Metric("lpm_ldst_percent_insn_ld",
                   "Load instructions as a percentage of all instructions.",
                   d_ratio(ldst_ld, ins), "100%"),
            Metric("lpm_ldst_percent_insn_st",
                   "Store instructions as a percentage of all instructions.",
                   d_ratio(ldst_st, ins), "100%"),
        ]),
        MetricGroup("lpm_ldst_ret_loads_per_cycle", [
            Metric(
                "lpm_ldst_ret_loads_per_cycle_1",
                "Load instructions retiring in 1 cycle as a percentage of all "
                "unhalted cycles.", d_ratio(ld_v1, ldst_cyc), "100%"),
            Metric(
                "lpm_ldst_ret_loads_per_cycle_2",
                "Load instructions retiring in 2 cycles as a percentage of all "
                "unhalted cycles.", d_ratio(ld_v2, ldst_cyc), "100%"),
            Metric(
                "lpm_ldst_ret_loads_per_cycle_3",
                "Load instructions retiring in 3 or more cycles as a percentage"
                "of all unhalted cycles.", d_ratio(ld_v3, ldst_cyc), "100%"),
        ]),
        MetricGroup("lpm_ldst_ret_stores_per_cycle", [
            Metric(
                "lpm_ldst_ret_stores_per_cycle_1",
                "Store instructions retiring in 1 cycle as a percentage of all "
                "unhalted cycles.", d_ratio(st_v1, ldst_cyc), "100%"),
            Metric(
                "lpm_ldst_ret_stores_per_cycle_2",
                "Store instructions retiring in 2 cycles as a percentage of all "
                "unhalted cycles.", d_ratio(st_v2, ldst_cyc), "100%"),
            Metric(
                "lpm_ldst_ret_stores_per_cycle_3",
                "Store instructions retiring in 3 or more cycles as a percentage"
                "of all unhalted cycles.", d_ratio(st_v3, ldst_cyc), "100%"),
        ]),
        MetricGroup("lpm_ldst_insn_bt", [
            Metric("lpm_ldst_insn_bt_ld", "Number of instructions between loads.",
                   d_ratio(ins, ldst_ld), "insns"),
            Metric("lpm_ldst_insn_bt_st", "Number of instructions between stores.",
                   d_ratio(ins, ldst_st), "insns"),
        ])
    ], description="Breakdown of load/store instructions")


def AmdUpc() -> Metric:
    ops = Event("ex_ret_ops", "ex_ret_cops")
    upc = d_ratio(ops, smt_cycles)
    return Metric("lpm_upc", "Micro-ops retired per core cycle (higher is better)",
                  upc, "uops/cycle")


def Idle() -> Metric:
    cyc = Event("msr/mperf/")
    tsc = Event("msr/tsc/")
    low = max(tsc - cyc, 0)
    return Metric(
        "lpm_idle",
        "Percentage of total wallclock cycles where CPUs are in low power state (C1 or deeper sleep state)",
        d_ratio(low, tsc), "100%")


def Rapl() -> MetricGroup:
    """Processor socket power consumption estimate.

    Use events from the running average power limit (RAPL) driver.
    """
    # Watts = joules/second
    # Currently only energy-pkg is supported by AMD:
    # https://lore.kernel.org/lkml/20220105185659.643355-1-eranian@google.com/
    pkg = Event("power/energy\\-pkg/")
    cond_pkg = Select(pkg, has_event(pkg), math.nan)
    scale = 2.3283064365386962890625e-10
    metrics = [
        Metric("lpm_cpu_power_pkg", "",
               d_ratio(cond_pkg * scale, interval_sec), "Watts"),
    ]

    return MetricGroup("lpm_cpu_power", metrics,
                       description="Processor socket power consumption estimates")


def UncoreL3():
    acc = Event("l3_lookup_state.all_coherent_accesses_to_l3",
                "l3_lookup_state.all_l3_req_typs")
    miss = Event("l3_lookup_state.l3_miss",
                 "l3_comb_clstr_state.request_miss")
    acc = max(acc, miss)
    hits = acc - miss

    return MetricGroup("lpm_l3", [
        Metric("lpm_l3_accesses", "L3 victim cache accesses",
               d_ratio(acc, interval_sec), "accesses/sec"),
        Metric("lpm_l3_hits", "L3 victim cache hit rate",
               d_ratio(hits, acc), "100%"),
        Metric("lpm_l3_miss", "L3 victim cache miss rate", d_ratio(miss, acc),
               "100%"),
    ], description="L3 cache breakdown per CCX")


def main() -> None:
    global _args
    global _zen_model

    def dir_path(path: str) -> str:
        """Validate path is a directory for argparse."""
        if os.path.isdir(path):
            return path
        raise argparse.ArgumentTypeError(
            f'\'{path}\' is not a valid directory')

    parser = argparse.ArgumentParser(description="AMD perf json generator")
    parser.add_argument(
        "-metricgroups", help="Generate metricgroups data", action='store_true')
    parser.add_argument("model", help="e.g. amdzen[123]")
    parser.add_argument(
        'events_path',
        type=dir_path,
        help='Root of tree containing architecture directories containing json files'
    )
    _args = parser.parse_args()

    directory = f"{_args.events_path}/x86/{_args.model}/"
    LoadEvents(directory)

    _zen_model = int(_args.model[6:])

    all_metrics = MetricGroup("", [
        AmdBr(),
        AmdCtxSw(),
        AmdDtlb(),
        AmdItlb(),
        AmdLdSt(),
        AmdUpc(),
        Cycles(),
        Idle(),
        Rapl(),
        UncoreL3(),
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
