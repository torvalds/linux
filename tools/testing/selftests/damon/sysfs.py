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

    if len(ctx['schemes']) != 1:
        fail('number of schemes', status)

    scheme = ctx['schemes'][0]
    if scheme['pattern'] != {
            'min_sz_region': 0,
            'max_sz_region': 2**64 - 1,
            'min_nr_accesses': 0,
            'max_nr_accesses': 2**32 - 1,
            'min_age_region': 0,
            'max_age_region': 2**32 - 1,
            }:
        fail('damos pattern', status)
    if scheme['action'] != 9:   # stat
        fail('damos action', status)
    if scheme['apply_interval_us'] != 0:
        fail('damos apply interval', status)
    if scheme['target_nid'] != -1:
        fail('damos target nid', status)

    assert_migrate_dests_committed(_damon_sysfs.DamosDests(),
                                   scheme['migrate_dests'])
    assert_quota_committed(_damon_sysfs.DamosQuota(), scheme['quota'])
    assert_watermarks_committed(_damon_sysfs.DamosWatermarks(),
                                scheme['wmarks'])

    kdamonds.stop()

if __name__ == '__main__':
    main()
