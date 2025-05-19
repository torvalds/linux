#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- python -*-
# -*- coding: utf-8 -*-

import argparse
import perf

def main(event: str):
    evlist = perf.parse_events(event)

    for evsel in evlist:
        evsel.read_format = perf.FORMAT_TOTAL_TIME_ENABLED | perf.FORMAT_TOTAL_TIME_RUNNING

    evlist.open()
    evlist.enable()

    count = 100000
    while count > 0:
        count -= 1

    evlist.disable()

    for evsel in evlist:
        for cpu in evsel.cpus():
            for thread in evsel.threads():
                counts = evsel.read(cpu, thread)
                print(f"For {evsel} val: {counts.val} enable: {counts.ena} run: {counts.run}")

    evlist.close()

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('-e', '--event', help="Events to open", default="cpu-clock,task-clock")
    args = ap.parse_args()
    main(args.event)
