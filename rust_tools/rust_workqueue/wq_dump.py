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
from drgn.helpers.linux.nodemask import for_each_node
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

wq_type_len = 9

def wq_type_str(wq):
    if wq.flags & WQ_BH:
        return f'{"bh":{wq_type_len}}'
    elif wq.flags & WQ_UNBOUND:
        if wq.flags & WQ_ORDERED:
            return f'{"ordered":{wq_type_len}}'
        else:
            if wq.unbound_attrs.affn_strict:
                return f'{"unbound,S":{wq_type_len}}'
            else:
                return f'{"unbound":{wq_type_len}}'
    else:
        return f'{"percpu":{wq_type_len}}'

worker_pool_idr         = prog['worker_pool_idr']
workqueues              = prog['workqueues']
wq_unbound_cpumask      = prog['wq_unbound_cpumask']
wq_pod_types            = prog['wq_pod_types']
wq_affn_dfl             = prog['wq_affn_dfl']
wq_affn_names           = prog['wq_affn_names']

WQ_BH                   = prog['WQ_BH']
WQ_UNBOUND              = prog['WQ_UNBOUND']
WQ_ORDERED              = prog['__WQ_ORDERED']
WQ_MEM_RECLAIM          = prog['WQ_MEM_RECLAIM']

WQ_AFFN_CPU             = prog['WQ_AFFN_CPU']
WQ_AFFN_SMT             = prog['WQ_AFFN_SMT']
WQ_AFFN_CACHE           = prog['WQ_AFFN_CACHE']
WQ_AFFN_NUMA            = prog['WQ_AFFN_NUMA']
WQ_AFFN_SYSTEM          = prog['WQ_AFFN_SYSTEM']

POOL_BH                 = prog['POOL_BH']

WQ_NAME_LEN             = prog['WQ_NAME_LEN'].value_()
cpumask_str_len         = len(cpumask_str(wq_unbound_cpumask))

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
    print(f'pool[{pi:0{max_pool_id_len}}] flags=0x{pool.flags.value_():02x} ref={pool.refcnt.value_():{max_ref_len}} nice={pool.attrs.nice.value_():3} ', end='')
    print(f'idle/workers={pool.nr_idle.value_():3}/{pool.nr_workers.value_():3} ', end='')
    if pool.cpu >= 0:
        print(f'cpu={pool.cpu.value_():3}', end='')
        if pool.flags & POOL_BH:
            print(' bh', end='')
    else:
        print(f'cpus={cpumask_str(pool.attrs.cpumask)}', end='')
        print(f' pod_cpus={cpumask_str(pool.attrs.__pod_cpumask)}', end='')
        if pool.attrs.affn_strict:
            print(' strict', end='')
    print('')

print('')
print('Workqueue CPU -> pool')
print('=====================')

print(f'[{"workqueue":^{WQ_NAME_LEN-2}}\\ {"type   CPU":{wq_type_len}}', end='')
for cpu in for_each_possible_cpu(prog):
    print(f' {cpu:{max_pool_id_len}}', end='')
print(' dfl]')

for wq in list_for_each_entry('struct workqueue_struct', workqueues.address_of_(), 'list'):
    print(f'{wq.name.string_().decode():{WQ_NAME_LEN}} {wq_type_str(wq):10}', end='')

    for cpu in for_each_possible_cpu(prog):
        pool_id = per_cpu_ptr(wq.cpu_pwq, cpu)[0].pool.id.value_()
        field_len = max(len(str(cpu)), max_pool_id_len)
        print(f' {pool_id:{field_len}}', end='')

    if wq.flags & WQ_UNBOUND:
        print(f' {wq.dfl_pwq.pool.id.value_():{max_pool_id_len}}', end='')
    print('')

print('')
print('Workqueue -> rescuer')
print('====================')

ucpus_len = max(cpumask_str_len, len("unbound_cpus"))
rcpus_len = max(cpumask_str_len, len("rescuer_cpus"))

print(f'[{"workqueue":^{WQ_NAME_LEN-2}}\\ {"unbound_cpus":{ucpus_len}}    pid {"rescuer_cpus":{rcpus_len}} ]')

for wq in list_for_each_entry('struct workqueue_struct', workqueues.address_of_(), 'list'):
    if not (wq.flags & WQ_MEM_RECLAIM):
        continue

    print(f'{wq.name.string_().decode():{WQ_NAME_LEN}}', end='')
    if wq.unbound_attrs.value_() != 0:
        print(f' {cpumask_str(wq.unbound_attrs.cpumask):{ucpus_len}}', end='')
    else:
        print(f' {"":{ucpus_len}}', end='')

    print(f' {wq.rescuer.task.pid.value_():6}', end='')
    print(f' {cpumask_str(wq.rescuer.task.cpus_ptr):{rcpus_len}}', end='')
    print('')

print('')
print('Unbound workqueue -> node_nr/max_active')
print('=======================================')

if 'node_to_cpumask_map' in prog:
    __cpu_online_mask = prog['__cpu_online_mask']
    node_to_cpumask_map = prog['node_to_cpumask_map']
    nr_node_ids = prog['nr_node_ids'].value_()

    print(f'online_cpus={cpumask_str(__cpu_online_mask.address_of_())}')
    for node in for_each_node():
        print(f'NODE[{node:02}]={cpumask_str(node_to_cpumask_map[node])}')
    print('')

    print(f'[{"workqueue":^{WQ_NAME_LEN-2}}\\ min max', end='')
    first = True
    for node in for_each_node():
        if first:
            print(f'  NODE {node}', end='')
            first = False
        else:
            print(f' {node:7}', end='')
    print(f' {"dfl":>7} ]')
    print('')

    for wq in list_for_each_entry('struct workqueue_struct', workqueues.address_of_(), 'list'):
        if not (wq.flags & WQ_UNBOUND):
            continue

        print(f'{wq.name.string_().decode():{WQ_NAME_LEN}} ', end='')
        print(f'{wq.min_active.value_():3} {wq.max_active.value_():3}', end='')
        for node in for_each_node():
            nna = wq.node_nr_active[node]
            print(f' {nna.nr.counter.value_():3}/{nna.max.value_():3}', end='')
        nna = wq.node_nr_active[nr_node_ids]
        print(f' {nna.nr.counter.value_():3}/{nna.max.value_():3}')
else:
    printf(f'node_to_cpumask_map not present, is NUMA enabled?')
