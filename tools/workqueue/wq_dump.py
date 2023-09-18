#!/usr/bin/env drgn
#
# Copyright (C) 2023 Tejun Heo <tj@kernel.org>
# Copyright (C) 2023 Meta Platforms, Inc. and affiliates.

desc = """
This is a drgn script to show the current workqueue configuration. For more
info on drgn, visit https://github.com/osandov/drgn.

Affinity Scopes
===============

Shows the CPUs that can be used for unbound workqueues and how they will be
grouped by each available affinity type. For each type:

  nr_pods   number of CPU pods in the affinity type
  pod_cpus  CPUs in each pod
  pod_node  NUMA node for memory allocation for each pod
  cpu_pod   pod that each CPU is associated to

Worker Pools
============

Lists all worker pools indexed by their ID. For each pool:

  ref       number of pool_workqueue's associated with this pool
  nice      nice value of the worker threads in the pool
  idle      number of idle workers
  workers   number of all workers
  cpu       CPU the pool is associated with (per-cpu pool)
  cpus      CPUs the workers in the pool can run on (unbound pool)

Workqueue CPU -> pool
=====================

Lists all workqueues along with their type and worker pool association. For
each workqueue:

  NAME TYPE[,FLAGS] POOL_ID...

  NAME      name of the workqueue
  TYPE      percpu, unbound or ordered
  FLAGS     S: strict affinity scope
  POOL_ID   worker pool ID associated with each possible CPU
"""

import sys

import drgn
from drgn.helpers.linux.list import list_for_each_entry,list_empty
from drgn.helpers.linux.percpu import per_cpu_ptr
from drgn.helpers.linux.cpumask import for_each_cpu,for_each_possible_cpu
from drgn.helpers.linux.idr import idr_for_each

import argparse
parser = argparse.ArgumentParser(description=desc,
                                 formatter_class=argparse.RawTextHelpFormatter)
args = parser.parse_args()

def err(s):
    print(s, file=sys.stderr, flush=True)
    sys.exit(1)

def cpumask_str(cpumask):
    output = ""
    base = 0
    v = 0
    for cpu in for_each_cpu(cpumask[0]):
        while cpu - base >= 32:
            output += f'{hex(v)} '
            base += 32
            v = 0
        v |= 1 << (cpu - base)
    if v > 0:
        output += f'{v:08x}'
    return output.strip()

worker_pool_idr         = prog['worker_pool_idr']
workqueues              = prog['workqueues']
wq_unbound_cpumask      = prog['wq_unbound_cpumask']
wq_pod_types            = prog['wq_pod_types']
wq_affn_dfl             = prog['wq_affn_dfl']
wq_affn_names           = prog['wq_affn_names']

WQ_UNBOUND              = prog['WQ_UNBOUND']
WQ_ORDERED              = prog['__WQ_ORDERED']
WQ_MEM_RECLAIM          = prog['WQ_MEM_RECLAIM']

WQ_AFFN_CPU             = prog['WQ_AFFN_CPU']
WQ_AFFN_SMT             = prog['WQ_AFFN_SMT']
WQ_AFFN_CACHE           = prog['WQ_AFFN_CACHE']
WQ_AFFN_NUMA            = prog['WQ_AFFN_NUMA']
WQ_AFFN_SYSTEM          = prog['WQ_AFFN_SYSTEM']

print('Affinity Scopes')
print('===============')

print(f'wq_unbound_cpumask={cpumask_str(wq_unbound_cpumask)}')

def print_pod_type(pt):
    print(f'  nr_pods  {pt.nr_pods.value_()}')

    print('  pod_cpus', end='')
    for pod in range(pt.nr_pods):
        print(f' [{pod}]={cpumask_str(pt.pod_cpus[pod])}', end='')
    print('')

    print('  pod_node', end='')
    for pod in range(pt.nr_pods):
        print(f' [{pod}]={pt.pod_node[pod].value_()}', end='')
    print('')

    print(f'  cpu_pod ', end='')
    for cpu in for_each_possible_cpu(prog):
        print(f' [{cpu}]={pt.cpu_pod[cpu].value_()}', end='')
    print('')

for affn in [WQ_AFFN_CPU, WQ_AFFN_SMT, WQ_AFFN_CACHE, WQ_AFFN_NUMA, WQ_AFFN_SYSTEM]:
    print('')
    print(f'{wq_affn_names[affn].string_().decode().upper()}{" (default)" if affn == wq_affn_dfl else ""}')
    print_pod_type(wq_pod_types[affn])

print('')
print('Worker Pools')
print('============')

max_pool_id_len = 0
max_ref_len = 0
for pi, pool in idr_for_each(worker_pool_idr):
    pool = drgn.Object(prog, 'struct worker_pool', address=pool)
    max_pool_id_len = max(max_pool_id_len, len(f'{pi}'))
    max_ref_len = max(max_ref_len, len(f'{pool.refcnt.value_()}'))

for pi, pool in idr_for_each(worker_pool_idr):
    pool = drgn.Object(prog, 'struct worker_pool', address=pool)
    print(f'pool[{pi:0{max_pool_id_len}}] ref={pool.refcnt.value_():{max_ref_len}} nice={pool.attrs.nice.value_():3} ', end='')
    print(f'idle/workers={pool.nr_idle.value_():3}/{pool.nr_workers.value_():3} ', end='')
    if pool.cpu >= 0:
        print(f'cpu={pool.cpu.value_():3}', end='')
    else:
        print(f'cpus={cpumask_str(pool.attrs.cpumask)}', end='')
        print(f' pod_cpus={cpumask_str(pool.attrs.__pod_cpumask)}', end='')
        if pool.attrs.affn_strict:
            print(' strict', end='')
    print('')

print('')
print('Workqueue CPU -> pool')
print('=====================')

print('[    workqueue     \     type   CPU', end='')
for cpu in for_each_possible_cpu(prog):
    print(f' {cpu:{max_pool_id_len}}', end='')
print(' dfl]')

for wq in list_for_each_entry('struct workqueue_struct', workqueues.address_of_(), 'list'):
    print(f'{wq.name.string_().decode()[-24:]:24}', end='')
    if wq.flags & WQ_UNBOUND:
        if wq.flags & WQ_ORDERED:
            print(' ordered   ', end='')
        else:
            print(' unbound', end='')
            if wq.unbound_attrs.affn_strict:
                print(',S ', end='')
            else:
                print('   ', end='')
    else:
        print(' percpu    ', end='')

    for cpu in for_each_possible_cpu(prog):
        pool_id = per_cpu_ptr(wq.cpu_pwq, cpu)[0].pool.id.value_()
        field_len = max(len(str(cpu)), max_pool_id_len)
        print(f' {pool_id:{field_len}}', end='')

    if wq.flags & WQ_UNBOUND:
        print(f' {wq.dfl_pwq.pool.id.value_():{max_pool_id_len}}', end='')
    print('')
