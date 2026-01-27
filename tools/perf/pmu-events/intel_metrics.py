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
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
