#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import json
import os
import subprocess

import _damon_sysfs

def dump_damon_status_dict(pid):
    try:
        subprocess.check_output(['which', 'drgn'], stderr=subprocess.DEVNULL)
    except:
        return None, 'drgn not found'
    file_dir = os.path.dirname(os.path.abspath(__file__))
    dump_script = os.path.join(file_dir, 'drgn_dump_damon_status.py')
    rc = subprocess.call(['drgn', dump_script, pid, 'damon_dump_output'],
                         stderr=subprocess.DEVNULL)
    if rc != 0:
        return None, 'drgn fail'
    try:
        with open('damon_dump_output', 'r') as f:
            return json.load(f), None
    except Exception as e:
        return None, 'json.load fail (%s)' % e

def fail(expectation, status):
    print('unexpected %s' % expectation)
    print(json.dumps(status, indent=4))
    exit(1)

def assert_true(condition, expectation, status):
    if condition is not True:
        fail(expectation, status)

def assert_watermarks_committed(watermarks, dump):
    wmark_metric_val = {
            'none': 0,
            'free_mem_rate': 1,
            }
    assert_true(dump['metric'] == wmark_metric_val[watermarks.metric],
                'metric', dump)
    assert_true(dump['interval'] == watermarks.interval, 'interval', dump)
    assert_true(dump['high'] == watermarks.high, 'high', dump)
    assert_true(dump['mid'] == watermarks.mid, 'mid', dump)
    assert_true(dump['low'] == watermarks.low, 'low', dump)

def assert_quota_goal_committed(qgoal, dump):
    metric_val = {
            'user_input': 0,
            'some_mem_psi_us': 1,
            'node_mem_used_bp': 2,
            'node_mem_free_bp': 3,
            }
    assert_true(dump['metric'] == metric_val[qgoal.metric], 'metric', dump)
    assert_true(dump['target_value'] == qgoal.target_value, 'target_value',
                dump)
    if qgoal.metric == 'user_input':
        assert_true(dump['current_value'] == qgoal.current_value,
                    'current_value', dump)
    assert_true(dump['nid'] == qgoal.nid, 'nid', dump)

def assert_quota_committed(quota, dump):
    assert_true(dump['reset_interval'] == quota.reset_interval_ms,
                'reset_interval', dump)
    assert_true(dump['ms'] == quota.ms, 'ms', dump)
    assert_true(dump['sz'] == quota.sz, 'sz', dump)
    for idx, qgoal in enumerate(quota.goals):
        assert_quota_goal_committed(qgoal, dump['goals'][idx])
    assert_true(dump['weight_sz'] == quota.weight_sz_permil, 'weight_sz', dump)
    assert_true(dump['weight_nr_accesses'] == quota.weight_nr_accesses_permil,
                'weight_nr_accesses', dump)
    assert_true(
            dump['weight_age'] == quota.weight_age_permil, 'weight_age', dump)


def assert_migrate_dests_committed(dests, dump):
    assert_true(dump['nr_dests'] == len(dests.dests), 'nr_dests', dump)
    for idx, dest in enumerate(dests.dests):
        assert_true(dump['node_id_arr'][idx] == dest.id, 'node_id', dump)
        assert_true(dump['weight_arr'][idx] == dest.weight, 'weight', dump)

def assert_filter_committed(filter_, dump):
    assert_true(filter_.type_ == dump['type'], 'type', dump)
    assert_true(filter_.matching == dump['matching'], 'matching', dump)
    assert_true(filter_.allow == dump['allow'], 'allow', dump)
    # TODO: check memcg_path and memcg_id if type is memcg
    if filter_.type_ == 'addr':
        assert_true([filter_.addr_start, filter_.addr_end] ==
                    dump['addr_range'], 'addr_range', dump)
    elif filter_.type_ == 'target':
        assert_true(filter_.target_idx == dump['target_idx'], 'target_idx',
                    dump)
    elif filter_.type_ == 'hugepage_size':
        assert_true([filter_.min_, filter_.max_] == dump['sz_range'],
                    'sz_range', dump)

def assert_access_pattern_committed(pattern, dump):
    assert_true(dump['min_sz_region'] == pattern.size[0], 'min_sz_region',
                dump)
    assert_true(dump['max_sz_region'] == pattern.size[1], 'max_sz_region',
                dump)
    assert_true(dump['min_nr_accesses'] == pattern.nr_accesses[0],
                'min_nr_accesses', dump)
    assert_true(dump['max_nr_accesses'] == pattern.nr_accesses[1],
                'max_nr_accesses', dump)
    assert_true(dump['min_age_region'] == pattern.age[0], 'min_age_region',
                dump)
    assert_true(dump['max_age_region'] == pattern.age[1], 'miaxage_region',
                dump)

def assert_scheme_committed(scheme, dump):
    assert_access_pattern_committed(scheme.access_pattern, dump['pattern'])
    action_val = {
            'willneed': 0,
            'cold': 1,
            'pageout': 2,
            'hugepage': 3,
            'nohugeapge': 4,
            'lru_prio': 5,
            'lru_deprio': 6,
            'migrate_hot': 7,
            'migrate_cold': 8,
            'stat': 9,
            }
    assert_true(dump['action'] == action_val[scheme.action], 'action', dump)
    assert_true(dump['apply_interval_us'] == scheme. apply_interval_us,
                'apply_interval_us', dump)
    assert_true(dump['target_nid'] == scheme.target_nid, 'target_nid', dump)
    assert_migrate_dests_committed(scheme.dests, dump['migrate_dests'])
    assert_quota_committed(scheme.quota, dump['quota'])
    assert_watermarks_committed(scheme.watermarks, dump['wmarks'])
    # TODO: test filters directory
    for idx, f in enumerate(scheme.core_filters.filters):
        assert_filter_committed(f, dump['filters'][idx])
    for idx, f in enumerate(scheme.ops_filters.filters):
        assert_filter_committed(f, dump['ops_filters'][idx])

def assert_schemes_committed(schemes, dump):
    assert_true(len(schemes) == len(dump), 'len_schemes', dump)
    for idx, scheme in enumerate(schemes):
        assert_scheme_committed(scheme, dump[idx])

def main():
    kdamonds = _damon_sysfs.Kdamonds(
            [_damon_sysfs.Kdamond(
                contexts=[_damon_sysfs.DamonCtx(
                    targets=[_damon_sysfs.DamonTarget(pid=-1)],
                    schemes=[_damon_sysfs.Damos()],
                    )])])
    err = kdamonds.start()
    if err is not None:
        print('kdamond start failed: %s' % err)
        exit(1)

    status, err = dump_damon_status_dict(kdamonds.kdamonds[0].pid)
    if err is not None:
        print(err)
        kdamonds.stop()
        exit(1)

    if len(status['contexts']) != 1:
        fail('number of contexts', status)

    ctx = status['contexts'][0]
    attrs = ctx['attrs']
    if attrs['sample_interval'] != 5000:
        fail('sample interval', status)
    if attrs['aggr_interval'] != 100000:
        fail('aggr interval', status)
    if attrs['ops_update_interval'] != 1000000:
        fail('ops updte interval', status)

    if attrs['intervals_goal'] != {
            'access_bp': 0, 'aggrs': 0,
            'min_sample_us': 0, 'max_sample_us': 0}:
        fail('intervals goal')

    if attrs['min_nr_regions'] != 10:
        fail('min_nr_regions')
    if attrs['max_nr_regions'] != 1000:
        fail('max_nr_regions')

    if ctx['adaptive_targets'] != [
            { 'pid': 0, 'nr_regions': 0, 'regions_list': []}]:
        fail('adaptive targets', status)

    assert_schemes_committed([_damon_sysfs.Damos()], ctx['schemes'])

    kdamonds.stop()

if __name__ == '__main__':
    main()
