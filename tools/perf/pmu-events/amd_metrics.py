#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import argparse
import math
import os
from typing import Optional
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
        AmdItlb(),
        AmdUpc(),
        Idle(),
        Rapl(),
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
