#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import argparse
import math
import os
from typing import Optional
from metric import (d_ratio, has_event, max, CheckPmu, Event, JsonEncodeMetric,
                    JsonEncodeMetricGroupDescriptions, LoadEvents, Metric,
                    MetricGroup, MetricRef, Select)

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
        Idle(),
        Rapl(),
        Smi(),
        Tsx(),
        IntelBr(),
        IntelSwpf(),
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
