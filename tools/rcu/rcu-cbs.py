#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0+
#
# Enhanced version of the RCU callback counter script.
# Originally by Paul E. McKenney <paulmck@kernel.org>
# Enhanced by Muhannad - github.com/muhannad-iz-a-tech-nerd
#
# Description:
#   This script sums up the number of outstanding RCU callbacks in
#   the system. On kernels with multiple RCU flavors, it targets
#   the most actively used one.
#
# Usage:
#   sudo drgn rcu-cbs.py
#
# Dependencies:
#   drgn - https://github.com/osandov/drgn

import sys
import drgn
from drgn import NULL
from drgn.helpers.linux import *

def get_rdp0(prog):
    """Get the base rcu_data pointer."""
    for symbol in ("rcu_preempt_data", "rcu_sched_data", "rcu_data"):
        try:
            return prog.variable(symbol, "kernel/rcu/tree.c").address_of_()
        except LookupError:
            continue
    print("Error: Could not find any RCU data symbol.")
    sys.exit(1)

def main():
    rdp0 = get_rdp0(prog)

    total_callbacks = 0
    print_per_cpu = False  # Change to True to enable per-CPU output

    for cpu in for_each_possible_cpu(prog):
        rdp = per_cpu_ptr(rdp0, cpu)
        cb_count = rdp.cblist.len.value_()
        if print_per_cpu:
            print(f"CPU {cpu:2d}: {cb_count} RCU callbacks")
        total_callbacks += cb_count

    print(f"\nTotal RCU callbacks in flight: {total_callbacks}")

if __name__ == "__main__":
    main()

