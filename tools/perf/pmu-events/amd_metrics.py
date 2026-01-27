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
