#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0+
#
# Dump out the number of RCU callbacks outstanding.
#
# On older kernels having multiple flavors of RCU, this dumps out the
# number of callbacks for the most heavily used flavor.
#
# Usage: sudo drgn rcu-cbs.py
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

import sys
import drgn
from drgn import NULL, Object
from drgn.helpers.linux import *

def get_rdp0(prog):
	try:
		rdp0 = prog.variable('rcu_preempt_data', 'kernel/rcu/tree.c');
	except LookupError:
		rdp0 = NULL;

	if rdp0 == NULL:
		try:
			rdp0 = prog.variable('rcu_sched_data',
					     'kernel/rcu/tree.c');
		except LookupError:
			rdp0 = NULL;

	if rdp0 == NULL:
		rdp0 = prog.variable('rcu_data', 'kernel/rcu/tree.c');
	return rdp0.address_of_();

rdp0 = get_rdp0(prog);

# Sum up RCU callbacks.
sum = 0;
for cpu in for_each_possible_cpu(prog):
	rdp = per_cpu_ptr(rdp0, cpu);
	len = rdp.cblist.len.value_();
	# print("CPU " + str(cpu) + " RCU callbacks: " + str(len));
	sum += len;
print("Number of RCU callbacks in flight: " + str(sum));
