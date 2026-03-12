#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import argparse
import os
from metric import (JsonEncodeMetric, JsonEncodeMetricGroupDescriptions, LoadEvents,
                    MetricGroup)
from common_metrics import Cycles

# Global command line arguments.
_args = None


def main() -> None:
    global _args

    def dir_path(path: str) -> str:
        """Validate path is a directory for argparse."""
        if os.path.isdir(path):
            return path
        raise argparse.ArgumentTypeError(
            f'\'{path}\' is not a valid directory')

    parser = argparse.ArgumentParser(description="ARM perf json generator")
    parser.add_argument(
        "-metricgroups", help="Generate metricgroups data", action='store_true')
    parser.add_argument("vendor", help="e.g. arm")
    parser.add_argument("model", help="e.g. neoverse-n1")
    parser.add_argument(
        'events_path',
        type=dir_path,
        help='Root of tree containing architecture directories containing json files'
    )
    _args = parser.parse_args()

    directory = f"{_args.events_path}/arm64/{_args.vendor}/{_args.model}/"
    LoadEvents(directory)

    all_metrics = MetricGroup("", [
        Cycles(),
    ])

    if _args.metricgroups:
        print(JsonEncodeMetricGroupDescriptions(all_metrics))
    else:
        print(JsonEncodeMetric(all_metrics))


if __name__ == '__main__':
    main()
