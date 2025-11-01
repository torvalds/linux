#!/usr/bin/env drgn
# SPDX-License-Identifier: GPL-2.0

'''
Read DAMON context data and dump as a json string.
'''
import drgn
from drgn import FaultError, NULL, Object, cast, container_of, execscript, offsetof, reinterpret, sizeof
from drgn.helpers.common import *
from drgn.helpers.linux import *

import json
import sys

if "prog" not in globals():
    try:
        prog = drgn.get_default_prog()
    except drgn.NoDefaultProgramError:
        prog = drgn.program_from_kernel()
        drgn.set_default_prog(prog)

def to_dict(object, attr_name_converter):
    d = {}
    for attr_name, converter in attr_name_converter:
        d[attr_name] = converter(getattr(object, attr_name))
    return d

def ops_to_dict(ops):
    return to_dict(ops, [
        ['id', int],
        ])

def intervals_goal_to_dict(goal):
    return to_dict(goal, [
        ['access_bp', int],
        ['aggrs', int],
        ['min_sample_us', int],
        ['max_sample_us', int],
        ])

def attrs_to_dict(attrs):
    return to_dict(attrs, [
        ['sample_interval', int],
        ['aggr_interval', int],
        ['ops_update_interval', int],
        ['intervals_goal', intervals_goal_to_dict],
        ['min_nr_regions', int],
        ['max_nr_regions', int],
        ])

def addr_range_to_dict(addr_range):
    return to_dict(addr_range, [
        ['start', int],
        ['end', int],
        ])

def region_to_dict(region):
    return to_dict(region, [
        ['ar', addr_range_to_dict],
        ['sampling_addr', int],
        ['nr_accesses', int],
        ['nr_accesses_bp', int],
        ['age', int],
        ])

def regions_to_list(regions):
    return [region_to_dict(r)
            for r in list_for_each_entry(
                'struct damon_region', regions.address_of_(), 'list')]

def target_to_dict(target):
    return to_dict(target, [
        ['pid', int],
        ['nr_regions', int],
        ['regions_list', regions_to_list],
        ])

def targets_to_list(targets):
    return [target_to_dict(t)
            for t in list_for_each_entry(
                'struct damon_target', targets.address_of_(), 'list')]

def damos_access_pattern_to_dict(pattern):
    return to_dict(pattern, [
        ['min_sz_region', int],
        ['max_sz_region', int],
        ['min_nr_accesses', int],
        ['max_nr_accesses', int],
        ['min_age_region', int],
        ['max_age_region', int],
        ])

def damos_quota_goal_to_dict(goal):
    return to_dict(goal, [
        ['metric', int],
        ['target_value', int],
        ['current_value', int],
        ['last_psi_total', int],
        ['nid', int],
        ])

def damos_quota_goals_to_list(goals):
    return [damos_quota_goal_to_dict(g)
            for g in list_for_each_entry(
                'struct damos_quota_goal', goals.address_of_(), 'list')]

def damos_quota_to_dict(quota):
    return to_dict(quota, [
        ['reset_interval', int],
        ['ms', int], ['sz', int],
        ['goals', damos_quota_goals_to_list],
        ['esz', int],
        ['weight_sz', int],
        ['weight_nr_accesses', int],
        ['weight_age', int],
        ])

def damos_watermarks_to_dict(watermarks):
    return to_dict(watermarks, [
        ['metric', int],
        ['interval', int],
        ['high', int], ['mid', int], ['low', int],
        ])

def damos_migrate_dests_to_dict(dests):
    nr_dests = int(dests.nr_dests)
    node_id_arr = []
    weight_arr = []
    for i in range(nr_dests):
        node_id_arr.append(int(dests.node_id_arr[i]))
        weight_arr.append(int(dests.weight_arr[i]))
    return {
            'node_id_arr': node_id_arr,
            'weight_arr': weight_arr,
            'nr_dests': nr_dests,
            }

def damos_filter_to_dict(damos_filter):
    filter_type_keyword = {
            0: 'anon',
            1: 'active',
            2: 'memcg',
            3: 'young',
            4: 'hugepage_size',
            5: 'unmapped',
            6: 'addr',
            7: 'target'
            }
    dict_ = {
            'type': filter_type_keyword[int(damos_filter.type)],
            'matching': bool(damos_filter.matching),
            'allow': bool(damos_filter.allow),
            }
    type_ = dict_['type']
    if type_ == 'memcg':
        dict_['memcg_id'] = int(damos_filter.memcg_id)
    elif type_ == 'addr':
        dict_['addr_range'] = [int(damos_filter.addr_range.start),
                               int(damos_filter.addr_range.end)]
    elif type_ == 'target':
        dict_['target_idx'] = int(damos_filter.target_idx)
    elif type_ == 'hugeapge_size':
        dict_['sz_range'] = [int(damos_filter.sz_range.min),
                             int(damos_filter.sz_range.max)]
    return dict_

def scheme_to_dict(scheme):
    dict_ = to_dict(scheme, [
        ['pattern', damos_access_pattern_to_dict],
        ['action', int],
        ['apply_interval_us', int],
        ['quota', damos_quota_to_dict],
        ['wmarks', damos_watermarks_to_dict],
        ['target_nid', int],
        ['migrate_dests', damos_migrate_dests_to_dict],
        ])
    filters = []
    for f in list_for_each_entry(
            'struct damos_filter', scheme.filters.address_of_(), 'list'):
        filters.append(damos_filter_to_dict(f))
    dict_['filters'] = filters
    ops_filters = []
    for f in list_for_each_entry(
            'struct damos_filter', scheme.ops_filters.address_of_(), 'list'):
        ops_filters.append(damos_filter_to_dict(f))
    dict_['ops_filters'] = ops_filters

    return dict_

def schemes_to_list(schemes):
    return [scheme_to_dict(s)
            for s in list_for_each_entry(
                'struct damos', schemes.address_of_(), 'list')]

def damon_ctx_to_dict(ctx):
    return to_dict(ctx, [
        ['ops', ops_to_dict],
        ['attrs', attrs_to_dict],
        ['adaptive_targets', targets_to_list],
        ['schemes', schemes_to_list],
        ])

def main():
    if len(sys.argv) < 3:
        print('Usage: %s <kdamond pid> <file>' % sys.argv[0])
        exit(1)

    pid = int(sys.argv[1])
    file_to_store = sys.argv[2]

    kthread_data = cast('struct kthread *',
                        find_task(prog, pid).worker_private).data
    ctx = cast('struct damon_ctx *', kthread_data)
    status = {'contexts': [damon_ctx_to_dict(ctx)]}
    if file_to_store == 'stdout':
        print(json.dumps(status, indent=4))
    else:
        with open(file_to_store, 'w') as f:
            json.dump(status, f, indent=4)

if __name__ == '__main__':
    main()
