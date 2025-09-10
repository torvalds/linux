#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 Juri Lelli <juri.lelli@redhat.com>
# Copyright (C) 2025 Red Hat, Inc.

desc = """
This is a drgn script to show dl_rq bandwidth accounting information. For more
info on drgn, visit https://github.com/osandov/drgn.

Only online CPUs are reported.
"""

import os
import argparse

import drgn
from drgn import FaultError
from drgn.helpers.common import *
from drgn.helpers.linux import *

def print_dl_bws_info():

    print("Retrieving dl_rq bandwidth accounting information:")

    runqueues = prog['runqueues']

    for cpu_id in for_each_possible_cpu(prog):
        try:
            rq = per_cpu(runqueues, cpu_id)

            if rq.online == 0:
                continue

            dl_rq = rq.dl

            print(f"  From CPU: {cpu_id}")

            # Access and print relevant fields from struct dl_rq
            print(f"  running_bw : {dl_rq.running_bw}")
            print(f"  this_bw    : {dl_rq.this_bw}")
            print(f"  extra_bw   : {dl_rq.extra_bw}")
            print(f"  max_bw     : {dl_rq.max_bw}")
            print(f"  bw_ratio   : {dl_rq.bw_ratio}")

        except drgn.FaultError as fe:
            print(f"  (CPU {cpu_id}: Fault accessing kernel memory: {fe})")
        except AttributeError as ae:
            print(f"  (CPU {cpu_id}: Missing attribute for root_domain (kernel struct change?): {ae})")
        except Exception as e:
            print(f"  (CPU {cpu_id}: An unexpected error occurred: {e})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=desc,
                                     formatter_class=argparse.RawTextHelpFormatter)
    args = parser.parse_args()

    print_dl_bws_info()
