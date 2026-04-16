#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import argparse
import json
import math
import os
import re
from typing import Optional
from common_metrics import Cycles
from metric import (d_ratio, has_event, max, source_count, CheckPmu, Event,
                    JsonEncodeMetric, JsonEncodeMetricGroupDescriptions,
                    Literal, LoadEvents, Metric, MetricConstraint, MetricGroup,
                    MetricRef, Select)

# Global command line arguments.
_args = None
interval_sec = Event("duration_time")


def Idle() -> Metric:
    cyc = Event("msr/mperf/")
    tsc = Event("msr/tsc/")
    low = max(tsc - cyc, 0)
    return Metric(
        "lpm_idle",
        "Percentage of total wallclock cycles where CPUs are in low power state (C1 or deeper sleep state)",
        d_ratio(low, tsc), "100%")


def Rapl() -> MetricGroup:
    """Processor power consumption estimate.

    Use events from the running average power limit (RAPL) driver.
    """
    # Watts = joules/second
    pkg = Event("power/energy\\-pkg/")
    cond_pkg = Select(pkg, has_event(pkg), math.nan)
    cores = Event("power/energy\\-cores/")
    cond_cores = Select(cores, has_event(cores), math.nan)
    ram = Event("power/energy\\-ram/")
    cond_ram = Select(ram, has_event(ram), math.nan)
    gpu = Event("power/energy\\-gpu/")
    cond_gpu = Select(gpu, has_event(gpu), math.nan)
    psys = Event("power/energy\\-psys/")
    cond_psys = Select(psys, has_event(psys), math.nan)
    scale = 2.3283064365386962890625e-10
    metrics = [
        Metric("lpm_cpu_power_pkg", "",
               d_ratio(cond_pkg * scale, interval_sec), "Watts"),
        Metric("lpm_cpu_power_cores", "",
               d_ratio(cond_cores * scale, interval_sec), "Watts"),
        Metric("lpm_cpu_power_ram", "",
               d_ratio(cond_ram * scale, interval_sec), "Watts"),
        Metric("lpm_cpu_power_gpu", "",
               d_ratio(cond_gpu * scale, interval_sec), "Watts"),
        Metric("lpm_cpu_power_psys", "",
               d_ratio(cond_psys * scale, interval_sec), "Watts"),
    ]

    return MetricGroup("lpm_cpu_power", metrics,
                       description="Running Average Power Limit (RAPL) power consumption estimates")


def Smi() -> MetricGroup:
    pmu = "<cpu_core or cpu_atom>" if CheckPmu("cpu_core") else "cpu"
    aperf = Event('msr/aperf/')
    cycles = Event('cycles')
    smi_num = Event('msr/smi/')
    smi_cycles = Select(Select((aperf - cycles) / aperf, smi_num > 0, 0),
                        has_event(aperf),
                        0)
    return MetricGroup('smi', [
        Metric('smi_num', 'Number of SMI interrupts.',
               Select(smi_num, has_event(smi_num), 0), 'SMI#'),
        # Note, the smi_cycles "Event" is really a reference to the metric.
        Metric('smi_cycles',
               'Percentage of cycles spent in System Management Interrupts. '
               f'Requires /sys/bus/event_source/devices/{pmu}/freeze_on_smi to be 1.',
               smi_cycles, '100%', threshold=(MetricRef('smi_cycles') > 0.10))
    ], description='System Management Interrupt metrics')


def Tsx() -> Optional[MetricGroup]:
    pmu = "cpu_core" if CheckPmu("cpu_core") else "cpu"
    cycles = Event('cycles')
    cycles_in_tx = Event(f'{pmu}/cycles\\-t/')
    cycles_in_tx_cp = Event(f'{pmu}/cycles\\-ct/')
    try:
        # Test if the tsx event is present in the json, prefer the
        # sysfs version so that we can detect its presence at runtime.
        transaction_start = Event("RTM_RETIRED.START")
        transaction_start = Event(f'{pmu}/tx\\-start/')
    except:
        return None

    elision_start = None
    try:
        # Elision start isn't supported by all models, but we'll not
        # generate the tsx_cycles_per_elision metric in that
        # case. Again, prefer the sysfs encoding of the event.
        elision_start = Event("HLE_RETIRED.START")
        elision_start = Event(f'{pmu}/el\\-start/')
    except:
        pass

    return MetricGroup('transaction', [
        Metric('tsx_transactional_cycles',
               'Percentage of cycles within a transaction region.',
               Select(cycles_in_tx / cycles, has_event(cycles_in_tx), 0),
               '100%'),
        Metric('tsx_aborted_cycles', 'Percentage of cycles in aborted transactions.',
               Select(max(cycles_in_tx - cycles_in_tx_cp, 0) / cycles,
                      has_event(cycles_in_tx),
                      0),
               '100%'),
        Metric('tsx_cycles_per_transaction',
               'Number of cycles within a transaction divided by the number of transactions.',
               Select(cycles_in_tx / transaction_start,
                      has_event(cycles_in_tx),
                      0),
               "cycles / transaction"),
        Metric('tsx_cycles_per_elision',
               'Number of cycles within a transaction divided by the number of elisions.',
               Select(cycles_in_tx / elision_start,
                      has_event(elision_start),
                      0),
               "cycles / elision") if elision_start else None,
    ], description="Breakdown of transactional memory statistics")


def IntelBr():
    ins = Event("instructions")

    def Total() -> MetricGroup:
        br_all = Event("BR_INST_RETIRED.ALL_BRANCHES", "BR_INST_RETIRED.ANY")
        br_m_all = Event("BR_MISP_RETIRED.ALL_BRANCHES",
                         "BR_INST_RETIRED.MISPRED",
                         "BR_MISP_EXEC.ANY")
        br_clr = None
        try:
            br_clr = Event("BACLEARS.ANY", "BACLEARS.ALL")
        except:
            pass

        br_r = d_ratio(br_all, interval_sec)
        ins_r = d_ratio(ins, br_all)
        misp_r = d_ratio(br_m_all, br_all)
        clr_r = d_ratio(br_clr, interval_sec) if br_clr else None

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
                   "The number of resync branches per second.", clr_r, "req/s"
                   ) if clr_r else None
        ])

    def Taken() -> MetricGroup:
        br_all = Event("BR_INST_RETIRED.ALL_BRANCHES", "BR_INST_RETIRED.ANY")
        br_m_tk = None
        try:
            br_m_tk = Event("BR_MISP_RETIRED.NEAR_TAKEN",
                            "BR_MISP_RETIRED.TAKEN_JCC",
                            "BR_INST_RETIRED.MISPRED_TAKEN")
        except:
            pass
        br_r = d_ratio(br_all, interval_sec)
        ins_r = d_ratio(ins, br_all)
        misp_r = d_ratio(br_m_tk, br_all) if br_m_tk else None
        return MetricGroup("lpm_br_taken", [
            Metric("lpm_br_taken_retired",
                   "The number of taken branches that were retired per second.",
                   br_r, "insn/s"),
            Metric(
                "lpm_br_taken_mispred",
                "The number of retired taken branch instructions that were "
                "mispredicted as a percentage of all taken branches.", misp_r,
                "100%") if misp_r else None,
            Metric(
                "lpm_br_taken_insn_between_branches",
                "The number of instructions divided by the number of taken branches.",
                ins_r, "insn"),
        ])

    def Conditional() -> Optional[MetricGroup]:
        try:
            br_cond = Event("BR_INST_RETIRED.COND",
                            "BR_INST_RETIRED.CONDITIONAL",
                            "BR_INST_RETIRED.TAKEN_JCC")
            br_m_cond = Event("BR_MISP_RETIRED.COND",
                              "BR_MISP_RETIRED.CONDITIONAL",
                              "BR_MISP_RETIRED.TAKEN_JCC")
        except:
            return None

        br_cond_nt = None
        br_m_cond_nt = None
        try:
            br_cond_nt = Event("BR_INST_RETIRED.COND_NTAKEN")
            br_m_cond_nt = Event("BR_MISP_RETIRED.COND_NTAKEN")
        except:
            pass
        br_r = d_ratio(br_cond, interval_sec)
        ins_r = d_ratio(ins, br_cond)
        misp_r = d_ratio(br_m_cond, br_cond)
        taken_metrics = [
            Metric("lpm_br_cond_retired", "Retired conditional branch instructions.",
                   br_r, "insn/s"),
            Metric("lpm_br_cond_insn_between_branches",
                   "The number of instructions divided by the number of conditional "
                   "branches.", ins_r, "insn"),
            Metric("lpm_br_cond_mispred",
                   "Retired conditional branch instructions mispredicted as a "
                   "percentage of all conditional branches.", misp_r, "100%"),
        ]
        if not br_m_cond_nt:
            return MetricGroup("lpm_br_cond", taken_metrics)

        br_r = d_ratio(br_cond_nt, interval_sec)
        ins_r = d_ratio(ins, br_cond_nt)
        misp_r = d_ratio(br_m_cond_nt, br_cond_nt)

        not_taken_metrics = [
            Metric("lpm_br_cond_retired", "Retired conditional not taken branch instructions.",
                   br_r, "insn/s"),
            Metric("lpm_br_cond_insn_between_branches",
                   "The number of instructions divided by the number of not taken conditional "
                   "branches.", ins_r, "insn"),
            Metric("lpm_br_cond_mispred",
                   "Retired not taken conditional branch instructions mispredicted as a "
                   "percentage of all not taken conditional branches.", misp_r, "100%"),
        ]
        return MetricGroup("lpm_br_cond", [
            MetricGroup("lpm_br_cond_nt", not_taken_metrics),
            MetricGroup("lpm_br_cond_tkn", taken_metrics),
        ])

    def Far() -> Optional[MetricGroup]:
        try:
            br_far = Event("BR_INST_RETIRED.FAR_BRANCH")
        except:
            return None

        br_r = d_ratio(br_far, interval_sec)
        ins_r = d_ratio(ins, br_far)
        return MetricGroup("lpm_br_far", [
            Metric("lpm_br_far_retired", "Retired far control transfers per second.",
                   br_r, "insn/s"),
            Metric(
                "lpm_br_far_insn_between_branches",
                "The number of instructions divided by the number of far branches.",
                ins_r, "insn"),
        ])

    return MetricGroup("lpm_br", [Total(), Taken(), Conditional(), Far()],
                       description="breakdown of retired branch instructions")


def IntelCtxSw() -> MetricGroup:
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

    try:
        ev = Event("MEM_INST_RETIRED.ALL_LOADS", "MEM_UOPS_RETIRED.ALL_LOADS")
        metrics.append(Metric("lpm_cs_loads", "Loads per context switch",
                              d_ratio(ev, cs), "loads/cs"))
    except:
        pass

    try:
        ev = Event("MEM_INST_RETIRED.ALL_STORES",
                   "MEM_UOPS_RETIRED.ALL_STORES")
        metrics.append(Metric("lpm_cs_stores", "Stores per context switch",
                              d_ratio(ev, cs), "stores/cs"))
    except:
        pass

    try:
        ev = Event("BR_INST_RETIRED.NEAR_TAKEN", "BR_INST_RETIRED.TAKEN_JCC")
        metrics.append(Metric("lpm_cs_br_taken", "Branches taken per context switch",
                              d_ratio(ev, cs), "br_taken/cs"))
    except:
        pass

    try:
        l2_misses = (Event("L2_RQSTS.DEMAND_DATA_RD_MISS") +
                     Event("L2_RQSTS.RFO_MISS") +
                     Event("L2_RQSTS.CODE_RD_MISS"))
        try:
            l2_misses += Event("L2_RQSTS.HWPF_MISS",
                               "L2_RQSTS.L2_PF_MISS", "L2_RQSTS.PF_MISS")
        except:
            pass

        metrics.append(Metric("lpm_cs_l2_misses", "L2 misses per context switch",
                              d_ratio(l2_misses, cs), "l2_misses/cs"))
    except:
        pass

    return MetricGroup("lpm_cs", metrics,
                       description=("Number of context switches per second, instructions "
                                    "retired & core cycles between context switches"))


def IntelFpu() -> Optional[MetricGroup]:
    cyc = Event("cycles")
    try:
        s_64 = Event("FP_ARITH_INST_RETIRED.SCALAR_SINGLE",
                     "SIMD_INST_RETIRED.SCALAR_SINGLE")
    except:
        return None
    d_64 = Event("FP_ARITH_INST_RETIRED.SCALAR_DOUBLE",
                 "SIMD_INST_RETIRED.SCALAR_DOUBLE")
    s_128 = Event("FP_ARITH_INST_RETIRED.128B_PACKED_SINGLE",
                  "SIMD_INST_RETIRED.PACKED_SINGLE")

    flop = s_64 + d_64 + 4 * s_128

    d_128 = None
    s_256 = None
    d_256 = None
    s_512 = None
    d_512 = None
    try:
        d_128 = Event("FP_ARITH_INST_RETIRED.128B_PACKED_DOUBLE")
        flop += 2 * d_128
        s_256 = Event("FP_ARITH_INST_RETIRED.256B_PACKED_SINGLE")
        flop += 8 * s_256
        d_256 = Event("FP_ARITH_INST_RETIRED.256B_PACKED_DOUBLE")
        flop += 4 * d_256
        s_512 = Event("FP_ARITH_INST_RETIRED.512B_PACKED_SINGLE")
        flop += 16 * s_512
        d_512 = Event("FP_ARITH_INST_RETIRED.512B_PACKED_DOUBLE")
        flop += 8 * d_512
    except:
        pass

    f_assist = Event("ASSISTS.FP", "FP_ASSIST.ANY", "FP_ASSIST.S")
    if f_assist in [
        "ASSISTS.FP",
        "FP_ASSIST.S",
    ]:
        f_assist += "/cmask=1/"

    flop_r = d_ratio(flop, interval_sec)
    flop_c = d_ratio(flop, cyc)
    nmi_constraint = MetricConstraint.GROUPED_EVENTS
    if f_assist.name == "ASSISTS.FP":  # Icelake+
        nmi_constraint = MetricConstraint.NO_GROUP_EVENTS_NMI

    def FpuMetrics(group: str, fl: Optional[Event], mult: int, desc: str) -> Optional[MetricGroup]:
        if not fl:
            return None

        f = fl * mult
        fl_r = d_ratio(f, interval_sec)
        r_s = d_ratio(fl, interval_sec)
        return MetricGroup(group, [
            Metric(f"{group}_of_total", desc + " floating point operations per second",
                   d_ratio(f, flop), "100%"),
            Metric(f"{group}_flops", desc + " floating point operations per second",
                   fl_r, "flops/s"),
            Metric(f"{group}_ops", desc + " operations per second",
                   r_s, "ops/s"),
        ])

    return MetricGroup("lpm_fpu", [
        MetricGroup("lpm_fpu_total", [
            Metric("lpm_fpu_total_flops", "Floating point operations per second",
                   flop_r, "flops/s"),
            Metric("lpm_fpu_total_flopc", "Floating point operations per cycle",
                   flop_c, "flops/cycle", constraint=nmi_constraint),
        ]),
        MetricGroup("lpm_fpu_64", [
            FpuMetrics("lpm_fpu_64_single", s_64, 1, "64-bit single"),
            FpuMetrics("lpm_fpu_64_double", d_64, 1, "64-bit double"),
        ]),
        MetricGroup("lpm_fpu_128", [
            FpuMetrics("lpm_fpu_128_single", s_128,
                       4, "128-bit packed single"),
            FpuMetrics("lpm_fpu_128_double", d_128,
                       2, "128-bit packed double"),
        ]),
        MetricGroup("lpm_fpu_256", [
            FpuMetrics("lpm_fpu_256_single", s_256,
                       8, "128-bit packed single"),
            FpuMetrics("lpm_fpu_256_double", d_256,
                       4, "128-bit packed double"),
        ]),
        MetricGroup("lpm_fpu_512", [
            FpuMetrics("lpm_fpu_512_single", s_512,
                       16, "128-bit packed single"),
            FpuMetrics("lpm_fpu_512_double", d_512,
                       8, "128-bit packed double"),
        ]),
        Metric("lpm_fpu_assists", "FP assists as a percentage of cycles",
               d_ratio(f_assist, cyc), "100%"),
    ])


def IntelIlp() -> MetricGroup:
    tsc = Event("msr/tsc/")
    c0 = Event("msr/mperf/")
    low = tsc - c0
    inst_ret = Event("INST_RETIRED.ANY_P")
    inst_ret_c = [Event(f"{inst_ret.name}/cmask={x}/") for x in range(1, 6)]
    core_cycles = Event("CPU_CLK_UNHALTED.THREAD_P_ANY",
                        "CPU_CLK_UNHALTED.DISTRIBUTED",
                        "cycles")
    ilp = [d_ratio(max(inst_ret_c[x] - inst_ret_c[x + 1], 0), core_cycles)
           for x in range(0, 4)]
    ilp.append(d_ratio(inst_ret_c[4], core_cycles))
    ilp0 = 1
    for x in ilp:
        ilp0 -= x
    return MetricGroup("lpm_ilp", [
        Metric("lpm_ilp_idle", "Lower power cycles as a percentage of all cycles",
               d_ratio(low, tsc), "100%"),
        Metric("lpm_ilp_inst_ret_0",
               "Instructions retired in 0 cycles as a percentage of all cycles",
               ilp0, "100%"),
        Metric("lpm_ilp_inst_ret_1",
               "Instructions retired in 1 cycles as a percentage of all cycles",
               ilp[0], "100%"),
        Metric("lpm_ilp_inst_ret_2",
               "Instructions retired in 2 cycles as a percentage of all cycles",
               ilp[1], "100%"),
        Metric("lpm_ilp_inst_ret_3",
               "Instructions retired in 3 cycles as a percentage of all cycles",
               ilp[2], "100%"),
        Metric("lpm_ilp_inst_ret_4",
               "Instructions retired in 4 cycles as a percentage of all cycles",
               ilp[3], "100%"),
        Metric("lpm_ilp_inst_ret_5",
               "Instructions retired in 5 or more cycles as a percentage of all cycles",
               ilp[4], "100%"),
    ])


def IntelL2() -> Optional[MetricGroup]:
    try:
        DC_HIT = Event("L2_RQSTS.DEMAND_DATA_RD_HIT")
    except:
        return None
    try:
        DC_MISS = Event("L2_RQSTS.DEMAND_DATA_RD_MISS")
        l2_dmnd_miss = DC_MISS
        l2_dmnd_rd_all = DC_MISS + DC_HIT
    except:
        DC_ALL = Event("L2_RQSTS.ALL_DEMAND_DATA_RD")
        l2_dmnd_miss = DC_ALL - DC_HIT
        l2_dmnd_rd_all = DC_ALL
    l2_dmnd_mrate = d_ratio(l2_dmnd_miss, interval_sec)
    l2_dmnd_rrate = d_ratio(l2_dmnd_rd_all, interval_sec)

    DC_PFH = None
    DC_PFM = None
    l2_pf_all = None
    l2_pf_mrate = None
    l2_pf_rrate = None
    try:
        DC_PFH = Event("L2_RQSTS.PF_HIT")
        DC_PFM = Event("L2_RQSTS.PF_MISS")
        l2_pf_all = DC_PFH + DC_PFM
        l2_pf_mrate = d_ratio(DC_PFM, interval_sec)
        l2_pf_rrate = d_ratio(l2_pf_all, interval_sec)
    except:
        pass

    DC_RFOH = None
    DC_RFOM = None
    l2_rfo_all = None
    l2_rfo_mrate = None
    l2_rfo_rrate = None
    try:
        DC_RFOH = Event("L2_RQSTS.RFO_HIT")
        DC_RFOM = Event("L2_RQSTS.RFO_MISS")
        l2_rfo_all = DC_RFOH + DC_RFOM
        l2_rfo_mrate = d_ratio(DC_RFOM, interval_sec)
        l2_rfo_rrate = d_ratio(l2_rfo_all, interval_sec)
    except:
        pass

    DC_CH = None
    try:
        DC_CH = Event("L2_RQSTS.CODE_RD_HIT")
    except:
        pass
    DC_CM = Event("L2_RQSTS.CODE_RD_MISS")
    DC_IN = Event("L2_LINES_IN.ALL")
    DC_OUT_NS = None
    DC_OUT_S = None
    l2_lines_out = None
    l2_out_rate = None
    wbn = None
    isd = None
    try:
        DC_OUT_NS = Event("L2_LINES_OUT.NON_SILENT",
                          "L2_LINES_OUT.DEMAND_DIRTY",
                          "L2_LINES_IN.S")
        DC_OUT_S = Event("L2_LINES_OUT.SILENT",
                         "L2_LINES_OUT.DEMAND_CLEAN",
                         "L2_LINES_IN.I")
        if DC_OUT_S.name == "L2_LINES_OUT.SILENT" and (
                args.model.startswith("skylake") or
                args.model == "cascadelakex"):
            DC_OUT_S.name = "L2_LINES_OUT.SILENT/any/"
        # bring is back to per-CPU
        l2_s = Select(DC_OUT_S / 2, Literal("#smt_on"), DC_OUT_S)
        l2_ns = DC_OUT_NS
        l2_lines_out = l2_s + l2_ns
        l2_out_rate = d_ratio(l2_lines_out, interval_sec)
        nlr = max(l2_ns - DC_WB_U - DC_WB_D, 0)
        wbn = d_ratio(nlr, interval_sec)
        isd = d_ratio(l2_s, interval_sec)
    except:
        pass
    DC_OUT_U = None
    l2_pf_useless = None
    l2_useless_rate = None
    try:
        DC_OUT_U = Event("L2_LINES_OUT.USELESS_HWPF")
        l2_pf_useless = DC_OUT_U
        l2_useless_rate = d_ratio(l2_pf_useless, interval_sec)
    except:
        pass
    DC_WB_U = None
    DC_WB_D = None
    wbu = None
    wbd = None
    try:
        DC_WB_U = Event("IDI_MISC.WB_UPGRADE")
        DC_WB_D = Event("IDI_MISC.WB_DOWNGRADE")
        wbu = d_ratio(DC_WB_U, interval_sec)
        wbd = d_ratio(DC_WB_D, interval_sec)
    except:
        pass

    l2_lines_in = DC_IN
    l2_code_all = (DC_CH + DC_CM) if DC_CH else None
    l2_code_rate = d_ratio(l2_code_all, interval_sec) if DC_CH else None
    l2_code_miss_rate = d_ratio(DC_CM, interval_sec)
    l2_in_rate = d_ratio(l2_lines_in, interval_sec)

    return MetricGroup("lpm_l2", [
        MetricGroup("lpm_l2_totals", [
            Metric("lpm_l2_totals_in", "L2 cache total in per second",
                   l2_in_rate, "In/s"),
            Metric("lpm_l2_totals_out", "L2 cache total out per second",
                   l2_out_rate, "Out/s") if l2_out_rate else None,
        ]),
        MetricGroup("lpm_l2_rd", [
            Metric("lpm_l2_rd_hits", "L2 cache data read hits",
                   d_ratio(DC_HIT, l2_dmnd_rd_all), "100%"),
            Metric("lpm_l2_rd_hits", "L2 cache data read hits",
                   d_ratio(l2_dmnd_miss, l2_dmnd_rd_all), "100%"),
            Metric("lpm_l2_rd_requests", "L2 cache data read requests per second",
                   l2_dmnd_rrate, "requests/s"),
            Metric("lpm_l2_rd_misses", "L2 cache data read misses per second",
                   l2_dmnd_mrate, "misses/s"),
        ]),
        MetricGroup("lpm_l2_hwpf", [
            Metric("lpm_l2_hwpf_hits", "L2 cache hardware prefetcher hits",
                   d_ratio(DC_PFH, l2_pf_all), "100%"),
            Metric("lpm_l2_hwpf_misses", "L2 cache hardware prefetcher misses",
                   d_ratio(DC_PFM, l2_pf_all), "100%"),
            Metric("lpm_l2_hwpf_useless", "L2 cache hardware prefetcher useless prefetches per second",
                   l2_useless_rate, "100%") if l2_useless_rate else None,
            Metric("lpm_l2_hwpf_requests", "L2 cache hardware prefetcher requests per second",
                   l2_pf_rrate, "100%"),
            Metric("lpm_l2_hwpf_misses", "L2 cache hardware prefetcher misses per second",
                   l2_pf_mrate, "100%"),
        ]) if DC_PFH else None,
        MetricGroup("lpm_l2_rfo", [
            Metric("lpm_l2_rfo_hits", "L2 cache request for ownership (RFO) hits",
                   d_ratio(DC_RFOH, l2_rfo_all), "100%"),
            Metric("lpm_l2_rfo_misses", "L2 cache request for ownership (RFO) misses",
                   d_ratio(DC_RFOM, l2_rfo_all), "100%"),
            Metric("lpm_l2_rfo_requests", "L2 cache request for ownership (RFO) requests per second",
                   l2_rfo_rrate, "requests/s"),
            Metric("lpm_l2_rfo_misses", "L2 cache request for ownership (RFO) misses per second",
                   l2_rfo_mrate, "misses/s"),
        ]) if DC_RFOH else None,
        MetricGroup("lpm_l2_code", [
            Metric("lpm_l2_code_hits", "L2 cache code hits",
                   d_ratio(DC_CH, l2_code_all), "100%") if DC_CH else None,
            Metric("lpm_l2_code_misses", "L2 cache code misses",
                   d_ratio(DC_CM, l2_code_all), "100%") if DC_CH else None,
            Metric("lpm_l2_code_requests", "L2 cache code requests per second",
                   l2_code_rate, "requests/s") if DC_CH else None,
            Metric("lpm_l2_code_misses", "L2 cache code misses per second",
                   l2_code_miss_rate, "misses/s"),
        ]),
        MetricGroup("lpm_l2_evict", [
            MetricGroup("lpm_l2_evict_mef_lines", [
                Metric("lpm_l2_evict_mef_lines_l3_hot_lru", "L2 evictions M/E/F lines L3 hot LRU per second",
                       wbu, "HotLRU/s") if wbu else None,
                Metric("lpm_l2_evict_mef_lines_l3_norm_lru", "L2 evictions M/E/F lines L3 normal LRU per second",
                       wbn, "NormLRU/s") if wbn else None,
                Metric("lpm_l2_evict_mef_lines_dropped", "L2 evictions M/E/F lines dropped per second",
                       wbd, "dropped/s") if wbd else None,
                Metric("lpm_l2_evict_is_lines_dropped", "L2 evictions I/S lines dropped per second",
                       isd, "dropped/s") if isd else None,
            ]),
        ]),
    ], description="L2 data cache analysis")


def IntelMissLat() -> Optional[MetricGroup]:
    try:
        ticks = Event("UNC_CHA_CLOCKTICKS", "UNC_C_CLOCKTICKS")
        data_rd_loc_occ = Event("UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_LOCAL",
                                "UNC_CHA_TOR_OCCUPANCY.IA_MISS",
                                "UNC_C_TOR_OCCUPANCY.MISS_LOCAL_OPCODE",
                                "UNC_C_TOR_OCCUPANCY.MISS_OPCODE")
        data_rd_loc_ins = Event("UNC_CHA_TOR_INSERTS.IA_MISS_DRD_LOCAL",
                                "UNC_CHA_TOR_INSERTS.IA_MISS",
                                "UNC_C_TOR_INSERTS.MISS_LOCAL_OPCODE",
                                "UNC_C_TOR_INSERTS.MISS_OPCODE")
        data_rd_rem_occ = Event("UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_REMOTE",
                                "UNC_CHA_TOR_OCCUPANCY.IA_MISS",
                                "UNC_C_TOR_OCCUPANCY.MISS_REMOTE_OPCODE",
                                "UNC_C_TOR_OCCUPANCY.NID_MISS_OPCODE")
        data_rd_rem_ins = Event("UNC_CHA_TOR_INSERTS.IA_MISS_DRD_REMOTE",
                                "UNC_CHA_TOR_INSERTS.IA_MISS",
                                "UNC_C_TOR_INSERTS.MISS_REMOTE_OPCODE",
                                "UNC_C_TOR_INSERTS.NID_MISS_OPCODE")
    except:
        return None

    if (data_rd_loc_occ.name == "UNC_C_TOR_OCCUPANCY.MISS_LOCAL_OPCODE" or
            data_rd_loc_occ.name == "UNC_C_TOR_OCCUPANCY.MISS_OPCODE"):
        data_rd = 0x182
        for e in [data_rd_loc_occ, data_rd_loc_ins, data_rd_rem_occ, data_rd_rem_ins]:
            e.name += f"/filter_opc={hex(data_rd)}/"
    elif data_rd_loc_occ.name == "UNC_CHA_TOR_OCCUPANCY.IA_MISS":
        # Demand Data Read - Full cache-line read requests from core for
        # lines to be cached in S or E, typically for data
        demand_data_rd = 0x202
        #  LLC Prefetch Data - Uncore will first look up the line in the
        #  LLC; for a cache hit, the LRU will be updated, on a miss, the
        #  DRd will be initiated
        llc_prefetch_data = 0x25a
        local_filter = (f"/filter_opc0={hex(demand_data_rd)},"
                        f"filter_opc1={hex(llc_prefetch_data)},"
                        "filter_loc,filter_nm,filter_not_nm/")
        remote_filter = (f"/filter_opc0={hex(demand_data_rd)},"
                         f"filter_opc1={hex(llc_prefetch_data)},"
                         "filter_rem,filter_nm,filter_not_nm/")
        for e in [data_rd_loc_occ, data_rd_loc_ins]:
            e.name += local_filter
        for e in [data_rd_rem_occ, data_rd_rem_ins]:
            e.name += remote_filter
    else:
        assert data_rd_loc_occ.name == "UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_LOCAL", data_rd_loc_occ

    ticks_per_cha = ticks / source_count(data_rd_loc_ins)
    loc_lat = interval_sec * 1e9 * data_rd_loc_occ / \
        (ticks_per_cha * data_rd_loc_ins)
    ticks_per_cha = ticks / source_count(data_rd_rem_ins)
    rem_lat = interval_sec * 1e9 * data_rd_rem_occ / \
        (ticks_per_cha * data_rd_rem_ins)
    return MetricGroup("lpm_miss_lat", [
        Metric("lpm_miss_lat_loc", "Local to a socket miss latency in nanoseconds",
               loc_lat, "ns"),
        Metric("lpm_miss_lat_rem", "Remote to a socket miss latency in nanoseconds",
               rem_lat, "ns"),
    ])


def IntelMlp() -> Optional[Metric]:
    try:
        l1d = Event("L1D_PEND_MISS.PENDING")
        l1dc = Event("L1D_PEND_MISS.PENDING_CYCLES")
    except:
        return None

    l1dc = Select(l1dc / 2, Literal("#smt_on"), l1dc)
    ml = d_ratio(l1d, l1dc)
    return Metric("lpm_mlp",
                  "Miss level parallelism - number of outstanding load misses per cycle (higher is better)",
                  ml, "load_miss_pending/cycle")


def IntelPorts() -> Optional[MetricGroup]:
    pipeline_events = json.load(
        open(f"{_args.events_path}/x86/{_args.model}/pipeline.json"))

    core_cycles = Event("CPU_CLK_UNHALTED.THREAD_P_ANY",
                        "CPU_CLK_UNHALTED.DISTRIBUTED",
                        "cycles")
    # Number of CPU cycles scaled for SMT.
    smt_cycles = Select(core_cycles / 2, Literal("#smt_on"), core_cycles)

    metrics = []
    for x in pipeline_events:
        if "EventName" in x and re.search("^UOPS_DISPATCHED.PORT", x["EventName"]):
            name = x["EventName"]
            port = re.search(r"(PORT_[0-9].*)", name).group(0).lower()
            if name.endswith("_CORE"):
                cyc = core_cycles
            else:
                cyc = smt_cycles
            metrics.append(Metric(f"lpm_{port}", f"{port} utilization (higher is better)",
                                  d_ratio(Event(name), cyc), "100%"))
    if len(metrics) == 0:
        return None

    return MetricGroup("lpm_ports", metrics, "functional unit (port) utilization -- "
                       "fraction of cycles each port is utilized (higher is better)")


def IntelSwpf() -> Optional[MetricGroup]:
    ins = Event("instructions")
    try:
        s_ld = Event("MEM_INST_RETIRED.ALL_LOADS",
                     "MEM_UOPS_RETIRED.ALL_LOADS")
        s_nta = Event("SW_PREFETCH_ACCESS.NTA")
        s_t0 = Event("SW_PREFETCH_ACCESS.T0")
        s_t1 = Event("SW_PREFETCH_ACCESS.T1_T2")
        s_w = Event("SW_PREFETCH_ACCESS.PREFETCHW")
    except:
        return None

    all_sw = s_nta + s_t0 + s_t1 + s_w
    swp_r = d_ratio(all_sw, interval_sec)
    ins_r = d_ratio(ins, all_sw)
    ld_r = d_ratio(s_ld, all_sw)

    return MetricGroup("lpm_swpf", [
        MetricGroup("lpm_swpf_totals", [
            Metric("lpm_swpf_totals_exec", "Software prefetch instructions per second",
                   swp_r, "swpf/s"),
            Metric("lpm_swpf_totals_insn_per_pf",
                   "Average number of instructions between software prefetches",
                   ins_r, "insn/swpf"),
            Metric("lpm_swpf_totals_loads_per_pf",
                   "Average number of loads between software prefetches",
                   ld_r, "loads/swpf"),
        ]),
        MetricGroup("lpm_swpf_bkdwn", [
            MetricGroup("lpm_swpf_bkdwn_nta", [
                Metric("lpm_swpf_bkdwn_nta_per_swpf",
                       "Software prefetch NTA instructions as a percent of all prefetch instructions",
                       d_ratio(s_nta, all_sw), "100%"),
                Metric("lpm_swpf_bkdwn_nta_rate",
                       "Software prefetch NTA instructions per second",
                       d_ratio(s_nta, interval_sec), "insn/s"),
            ]),
            MetricGroup("lpm_swpf_bkdwn_t0", [
                Metric("lpm_swpf_bkdwn_t0_per_swpf",
                       "Software prefetch T0 instructions as a percent of all prefetch instructions",
                       d_ratio(s_t0, all_sw), "100%"),
                Metric("lpm_swpf_bkdwn_t0_rate",
                       "Software prefetch T0 instructions per second",
                       d_ratio(s_t0, interval_sec), "insn/s"),
            ]),
            MetricGroup("lpm_swpf_bkdwn_t1_t2", [
                Metric("lpm_swpf_bkdwn_t1_t2_per_swpf",
                       "Software prefetch T1 or T2 instructions as a percent of all prefetch instructions",
                       d_ratio(s_t1, all_sw), "100%"),
                Metric("lpm_swpf_bkdwn_t1_t2_rate",
                       "Software prefetch T1 or T2 instructions per second",
                       d_ratio(s_t1, interval_sec), "insn/s"),
            ]),
            MetricGroup("lpm_swpf_bkdwn_w", [
                Metric("lpm_swpf_bkdwn_w_per_swpf",
                       "Software prefetch W instructions as a percent of all prefetch instructions",
                       d_ratio(s_w, all_sw), "100%"),
                Metric("lpm_swpf_bkdwn_w_rate",
                       "Software prefetch W instructions per second",
                       d_ratio(s_w, interval_sec), "insn/s"),
            ]),
        ]),
    ], description="Software prefetch instruction breakdown")


def IntelLdSt() -> Optional[MetricGroup]:
    if _args.model in [
        "bonnell",
        "nehalemep",
        "nehalemex",
        "westmereep-dp",
        "westmereep-sp",
        "westmereex",
    ]:
        return None
    LDST_LD = Event("MEM_INST_RETIRED.ALL_LOADS", "MEM_UOPS_RETIRED.ALL_LOADS")
    LDST_ST = Event("MEM_INST_RETIRED.ALL_STORES",
                    "MEM_UOPS_RETIRED.ALL_STORES")
    LDST_LDC1 = Event(f"{LDST_LD.name}/cmask=1/")
    LDST_STC1 = Event(f"{LDST_ST.name}/cmask=1/")
    LDST_LDC2 = Event(f"{LDST_LD.name}/cmask=2/")
    LDST_STC2 = Event(f"{LDST_ST.name}/cmask=2/")
    LDST_LDC3 = Event(f"{LDST_LD.name}/cmask=3/")
    LDST_STC3 = Event(f"{LDST_ST.name}/cmask=3/")
    ins = Event("instructions")
    LDST_CYC = Event("CPU_CLK_UNHALTED.THREAD",
                     "CPU_CLK_UNHALTED.CORE_P",
                     "CPU_CLK_UNHALTED.THREAD_P")
    LDST_PRE = None
    try:
        LDST_PRE = Event("LOAD_HIT_PREFETCH.SWPF", "LOAD_HIT_PRE.SW_PF")
    except:
        pass
    LDST_AT = None
    try:
        LDST_AT = Event("MEM_INST_RETIRED.LOCK_LOADS")
    except:
        pass
    cyc = LDST_CYC

    ld_rate = d_ratio(LDST_LD, interval_sec)
    st_rate = d_ratio(LDST_ST, interval_sec)
    pf_rate = d_ratio(LDST_PRE, interval_sec) if LDST_PRE else None
    at_rate = d_ratio(LDST_AT, interval_sec) if LDST_AT else None

    ldst_ret_constraint = MetricConstraint.GROUPED_EVENTS
    if LDST_LD.name == "MEM_UOPS_RETIRED.ALL_LOADS":
        ldst_ret_constraint = MetricConstraint.NO_GROUP_EVENTS_NMI

    return MetricGroup("lpm_ldst", [
        MetricGroup("lpm_ldst_total", [
            Metric("lpm_ldst_total_loads", "Load/store instructions total loads",
                   ld_rate, "loads"),
            Metric("lpm_ldst_total_stores", "Load/store instructions total stores",
                   st_rate, "stores"),
        ]),
        MetricGroup("lpm_ldst_prcnt", [
            Metric("lpm_ldst_prcnt_loads", "Percent of all instructions that are loads",
                   d_ratio(LDST_LD, ins), "100%"),
            Metric("lpm_ldst_prcnt_stores", "Percent of all instructions that are stores",
                   d_ratio(LDST_ST, ins), "100%"),
        ]),
        MetricGroup("lpm_ldst_ret_lds", [
            Metric("lpm_ldst_ret_lds_1", "Retired loads in 1 cycle",
                   d_ratio(max(LDST_LDC1 - LDST_LDC2, 0), cyc), "100%",
                   constraint=ldst_ret_constraint),
            Metric("lpm_ldst_ret_lds_2", "Retired loads in 2 cycles",
                   d_ratio(max(LDST_LDC2 - LDST_LDC3, 0), cyc), "100%",
                   constraint=ldst_ret_constraint),
            Metric("lpm_ldst_ret_lds_3", "Retired loads in 3 or more cycles",
                   d_ratio(LDST_LDC3, cyc), "100%"),
        ]),
        MetricGroup("lpm_ldst_ret_sts", [
            Metric("lpm_ldst_ret_sts_1", "Retired stores in 1 cycle",
                   d_ratio(max(LDST_STC1 - LDST_STC2, 0), cyc), "100%",
                   constraint=ldst_ret_constraint),
            Metric("lpm_ldst_ret_sts_2", "Retired stores in 2 cycles",
                   d_ratio(max(LDST_STC2 - LDST_STC3, 0), cyc), "100%",
                   constraint=ldst_ret_constraint),
            Metric("lpm_ldst_ret_sts_3", "Retired stores in 3 more cycles",
                   d_ratio(LDST_STC3, cyc), "100%"),
        ]),
        Metric("lpm_ldst_ld_hit_swpf", "Load hit software prefetches per second",
               pf_rate, "swpf/s") if pf_rate else None,
        Metric("lpm_ldst_atomic_lds", "Atomic loads per second",
               at_rate, "loads/s") if at_rate else None,
    ], description="Breakdown of load/store instructions")


def UncoreCState() -> Optional[MetricGroup]:
    try:
        pcu_ticks = Event("UNC_P_CLOCKTICKS")
        c0 = Event("UNC_P_POWER_STATE_OCCUPANCY.CORES_C0")
        c3 = Event("UNC_P_POWER_STATE_OCCUPANCY.CORES_C3")
        c6 = Event("UNC_P_POWER_STATE_OCCUPANCY.CORES_C6")
    except:
        return None

    num_cores = Literal("#num_cores") / Literal("#num_packages")

    max_cycles = pcu_ticks * num_cores
    total_cycles = c0 + c3 + c6

    # remove fused-off cores which show up in C6/C7.
    c6 = Select(max(c6 - (total_cycles - max_cycles), 0),
                total_cycles > max_cycles,
                c6)

    return MetricGroup("lpm_cstate", [
        Metric("lpm_cstate_c0", "C-State cores in C0/C1",
               d_ratio(c0, pcu_ticks), "cores"),
        Metric("lpm_cstate_c3", "C-State cores in C3",
               d_ratio(c3, pcu_ticks), "cores"),
        Metric("lpm_cstate_c6", "C-State cores in C6/C7",
               d_ratio(c6, pcu_ticks), "cores"),
    ])


def UncoreDir() -> Optional[MetricGroup]:
    try:
        m2m_upd = Event("UNC_M2M_DIRECTORY_UPDATE.ANY")
        m2m_hits = Event("UNC_M2M_DIRECTORY_HIT.DIRTY_I")
        # Turn the umask into a ANY rather than DIRTY_I filter.
        m2m_hits.name += "/umask=0xFF,name=UNC_M2M_DIRECTORY_HIT.ANY/"
        m2m_miss = Event("UNC_M2M_DIRECTORY_MISS.DIRTY_I")
        # Turn the umask into a ANY rather than DIRTY_I filter.
        m2m_miss.name += "/umask=0xFF,name=UNC_M2M_DIRECTORY_MISS.ANY/"
        cha_upd = Event("UNC_CHA_DIR_UPDATE.HA")
        # Turn the umask into a ANY rather than HA filter.
        cha_upd.name += "/umask=3,name=UNC_CHA_DIR_UPDATE.ANY/"
    except:
        return None

    m2m_total = m2m_hits + m2m_miss
    upd = m2m_upd + cha_upd  # in cache lines
    upd_r = upd / interval_sec
    look_r = m2m_total / interval_sec

    scale = 64 / 1_000_000  # Cache lines to MB
    return MetricGroup("lpm_dir", [
        Metric("lpm_dir_lookup_rate", "",
               d_ratio(m2m_total, interval_sec), "requests/s"),
        Metric("lpm_dir_lookup_hits", "",
               d_ratio(m2m_hits, m2m_total), "100%"),
        Metric("lpm_dir_lookup_misses", "",
               d_ratio(m2m_miss, m2m_total), "100%"),
        Metric("lpm_dir_update_requests", "",
               d_ratio(m2m_upd + cha_upd, interval_sec), "requests/s"),
        Metric("lpm_dir_update_bw", "",
               d_ratio(m2m_upd + cha_upd, interval_sec), f"{scale}MB/s"),
    ])


def UncoreMem() -> Optional[MetricGroup]:
    try:
        loc_rds = Event("UNC_CHA_REQUESTS.READS_LOCAL",
                        "UNC_H_REQUESTS.READS_LOCAL")
        rem_rds = Event("UNC_CHA_REQUESTS.READS_REMOTE",
                        "UNC_H_REQUESTS.READS_REMOTE")
        loc_wrs = Event("UNC_CHA_REQUESTS.WRITES_LOCAL",
                        "UNC_H_REQUESTS.WRITES_LOCAL")
        rem_wrs = Event("UNC_CHA_REQUESTS.WRITES_REMOTE",
                        "UNC_H_REQUESTS.WRITES_REMOTE")
    except:
        return None

    scale = 64 / 1_000_000
    return MetricGroup("lpm_mem", [
        MetricGroup("lpm_mem_local", [
            Metric("lpm_mem_local_read", "Local memory read bandwidth not including directory updates",
                   d_ratio(loc_rds, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_local_write", "Local memory write bandwidth not including directory updates",
                   d_ratio(loc_wrs, interval_sec), f"{scale}MB/s"),
        ]),
        MetricGroup("lpm_mem_remote", [
            Metric("lpm_mem_remote_read", "Remote memory read bandwidth not including directory updates",
                   d_ratio(rem_rds, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_remote_write", "Remote memory write bandwidth not including directory updates",
                   d_ratio(rem_wrs, interval_sec), f"{scale}MB/s"),
        ]),
    ], description="Memory Bandwidth breakdown local vs. remote (remote requests in). directory updates not included")


def UncoreMemBw() -> Optional[MetricGroup]:
    mem_events = []
    try:
        mem_events = json.load(open(f"{os.path.dirname(os.path.realpath(__file__))}"
                                    f"/arch/x86/{args.model}/uncore-memory.json"))
    except:
        pass

    ddr_rds = 0
    ddr_wrs = 0
    ddr_total = 0
    for x in mem_events:
        if "EventName" in x:
            name = x["EventName"]
            if re.search("^UNC_MC[0-9]+_RDCAS_COUNT_FREERUN", name):
                ddr_rds += Event(name)
            elif re.search("^UNC_MC[0-9]+_WRCAS_COUNT_FREERUN", name):
                ddr_wrs += Event(name)
            # elif re.search("^UNC_MC[0-9]+_TOTAL_REQCOUNT_FREERUN", name):
            #  ddr_total += Event(name)

    if ddr_rds == 0:
        try:
            ddr_rds = Event("UNC_M_CAS_COUNT.RD")
            ddr_wrs = Event("UNC_M_CAS_COUNT.WR")
        except:
            return None

    ddr_total = ddr_rds + ddr_wrs

    pmm_rds = 0
    pmm_wrs = 0
    try:
        pmm_rds = Event("UNC_M_PMM_RPQ_INSERTS")
        pmm_wrs = Event("UNC_M_PMM_WPQ_INSERTS")
    except:
        pass

    pmm_total = pmm_rds + pmm_wrs

    scale = 64 / 1_000_000
    return MetricGroup("lpm_mem_bw", [
        MetricGroup("lpm_mem_bw_ddr", [
            Metric("lpm_mem_bw_ddr_read", "DDR memory read bandwidth",
                   d_ratio(ddr_rds, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_bw_ddr_write", "DDR memory write bandwidth",
                   d_ratio(ddr_wrs, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_bw_ddr_total", "DDR memory write bandwidth",
                   d_ratio(ddr_total, interval_sec), f"{scale}MB/s"),
        ], description="DDR Memory Bandwidth"),
        MetricGroup("lpm_mem_bw_pmm", [
            Metric("lpm_mem_bw_pmm_read", "PMM memory read bandwidth",
                   d_ratio(pmm_rds, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_bw_pmm_write", "PMM memory write bandwidth",
                   d_ratio(pmm_wrs, interval_sec), f"{scale}MB/s"),
            Metric("lpm_mem_bw_pmm_total", "PMM memory write bandwidth",
                   d_ratio(pmm_total, interval_sec), f"{scale}MB/s"),
        ], description="PMM Memory Bandwidth") if pmm_rds != 0 else None,
    ], description="Memory Bandwidth")


def UncoreMemSat() -> Optional[Metric]:
    try:
        clocks = Event("UNC_CHA_CLOCKTICKS", "UNC_C_CLOCKTICKS")
        sat = Event("UNC_CHA_DISTRESS_ASSERTED.VERT", "UNC_CHA_FAST_ASSERTED.VERT",
                    "UNC_C_FAST_ASSERTED")
    except:
        return None

    desc = ("Mesh Bandwidth saturation (% CBOX cycles with FAST signal asserted, "
            "include QPI bandwidth saturation), lower is better")
    if "UNC_CHA_" in sat.name:
        desc = ("Mesh Bandwidth saturation (% CHA cycles with FAST signal asserted, "
                "include UPI bandwidth saturation), lower is better")
    return Metric("lpm_mem_sat", desc, d_ratio(sat, clocks), "100%")


def UncoreUpiBw() -> Optional[MetricGroup]:
    try:
        upi_rds = Event("UNC_UPI_RxL_FLITS.ALL_DATA")
        upi_wrs = Event("UNC_UPI_TxL_FLITS.ALL_DATA")
    except:
        return None

    upi_total = upi_rds + upi_wrs

    # From "Uncore Performance Monitoring": When measuring the amount of
    # bandwidth consumed by transmission of the data (i.e. NOT including
    # the header), it should be .ALL_DATA / 9 * 64B.
    scale = (64 / 9) / 1_000_000
    return MetricGroup("lpm_upi_bw", [
        Metric("lpm_upi_bw_read", "UPI read bandwidth",
               d_ratio(upi_rds, interval_sec), f"{scale}MB/s"),
        Metric("lpm_upi_bw_write", "DDR memory write bandwidth",
               d_ratio(upi_wrs, interval_sec), f"{scale}MB/s"),
    ], description="UPI Bandwidth")


def main() -> None:
    global _args

    def dir_path(path: str) -> str:
        """Validate path is a directory for argparse."""
        if os.path.isdir(path):
            return path
        raise argparse.ArgumentTypeError(
            f'\'{path}\' is not a valid directory')

    parser = argparse.ArgumentParser(description="Intel perf json generator")
    parser.add_argument(
        "-metricgroups", help="Generate metricgroups data", action='store_true')
    parser.add_argument("model", help="e.g. skylakex")
    parser.add_argument(
        'events_path',
        type=dir_path,
        help='Root of tree containing architecture directories containing json files'
    )
    _args = parser.parse_args()

    directory = f"{_args.events_path}/x86/{_args.model}/"
    LoadEvents(directory)

    all_metrics = MetricGroup("", [
        Cycles(),
        Idle(),
        Rapl(),
        Smi(),
        Tsx(),
        IntelBr(),
        IntelCtxSw(),
        IntelFpu(),
        IntelIlp(),
        IntelL2(),
        IntelLdSt(),
        IntelMissLat(),
        IntelMlp(),
        IntelPorts(),
        IntelSwpf(),
        UncoreCState(),
        UncoreDir(),
        UncoreMem(),
        UncoreMemBw(),
        UncoreMemSat(),
        UncoreUpiBw(),
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
