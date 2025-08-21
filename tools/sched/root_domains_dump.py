#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 Juri Lelli <juri.lelli@redhat.com>
# Copyright (C) 2025 Red Hat, Inc.

desc = """
This is a drgn script to show the current root domains configuration. For more
info on drgn, visit https://github.com/osandov/drgn.

Root domains are only printed once, as multiple CPUs might be attached to the
same root domain.
"""

import os
import argparse

import drgn
from drgn import FaultError
from drgn.helpers.common import *
from drgn.helpers.linux import *

def print_root_domains_info():

    # To store unique root domains found
    seen_root_domains = set()

    print("Retrieving (unique) Root Domain Information:")

    runqueues = prog['runqueues']
    def_root_domain = prog['def_root_domain']

    for cpu_id in for_each_possible_cpu(prog):
        try:
            rq = per_cpu(runqueues, cpu_id)

            root_domain = rq.rd

            # Check if we've already processed this root domain to avoid duplicates
            # Use the memory address of the root_domain as a unique identifier
            root_domain_cast = int(root_domain)
            if root_domain_cast in seen_root_domains:
                continue
            seen_root_domains.add(root_domain_cast)

            if root_domain_cast == int(def_root_domain.address_):
                print(f"\n--- Root Domain @ def_root_domain ---")
            else:
                print(f"\n--- Root Domain @ 0x{root_domain_cast:x} ---")

            print(f"  From CPU: {cpu_id}") # This CPU belongs to this root domain

            # Access and print relevant fields from struct root_domain
            print(f"  Span       : {cpumask_to_cpulist(root_domain.span[0])}")
            print(f"  Online     : {cpumask_to_cpulist(root_domain.span[0])}")

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

    print_root_domains_info()
