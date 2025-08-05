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

def assert_monitoring_attrs_committed(attrs, dump):
    assert_true(dump['sample_interval'] == attrs.sample_us, 'sample_interval',
                dump)
    assert_true(dump['aggr_interval'] == attrs.aggr_us, 'aggr_interval', dump)
    assert_true(dump['intervals_goal']['access_bp'] ==
                attrs.intervals_goal.access_bp, 'access_bp',
                dump['intervals_goal'])
    assert_true(dump['intervals_goal']['aggrs'] == attrs.intervals_goal.aggrs,
                'aggrs', dump['intervals_goal'])
    assert_true(dump['intervals_goal']['min_sample_us'] ==
                attrs.intervals_goal.min_sample_us, 'min_sample_us',
                dump['intervals_goal'])
    assert_true(dump['intervals_goal']['max_sample_us'] ==
                attrs.intervals_goal.max_sample_us, 'max_sample_us',
                dump['intervals_goal'])

    assert_true(dump['ops_update_interval'] == attrs.update_us,
                'ops_update_interval', dump)
    assert_true(dump['min_nr_regions'] == attrs.min_nr_regions,
                'min_nr_regions', dump)
    assert_true(dump['max_nr_regions'] == attrs.max_nr_regions,
                'max_nr_regions', dump)

def assert_ctx_committed(ctx, dump):
    ops_val = {
            'vaddr': 0,
            'fvaddr': 1,
            'paddr': 2,
            }
    assert_true(dump['ops']['id'] == ops_val[ctx.ops], 'ops_id', dump)
    assert_monitoring_attrs_committed(ctx.monitoring_attrs, dump['attrs'])
    assert_schemes_committed(ctx.schemes, dump['schemes'])

def assert_ctxs_committed(ctxs, dump):
    assert_true(len(ctxs) == len(dump), 'ctxs length', dump)
    for idx, ctx in enumerate(ctxs):
        assert_ctx_committed(ctx, dump[idx])

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

    assert_ctxs_committed(kdamonds.kdamonds[0].contexts, status['contexts'])

    context = _damon_sysfs.DamonCtx(
            monitoring_attrs=_damon_sysfs.DamonAttrs(
                sample_us=100000, aggr_us=2000000,
                intervals_goal=_damon_sysfs.IntervalsGoal(
                    access_bp=400, aggrs=3, min_sample_us=5000,
                    max_sample_us=10000000),
                update_us=2000000),
            schemes=[_damon_sysfs.Damos(
                action='pageout',
                access_pattern=_damon_sysfs.DamosAccessPattern(
                    size=[4096, 2**10],
                    nr_accesses=[3, 317],
                    age=[5,71]),
                quota=_damon_sysfs.DamosQuota(
                    sz=100*1024*1024, ms=100,
                    goals=[_damon_sysfs.DamosQuotaGoal(
                        metric='node_mem_used_bp',
                        target_value=9950,
                        nid=1)],
                    reset_interval_ms=1500,
                    weight_sz_permil=20,
                    weight_nr_accesses_permil=200,
                    weight_age_permil=1000),
                watermarks=_damon_sysfs.DamosWatermarks(
                    metric = 'free_mem_rate', interval = 500000, # 500 ms
                    high = 500, mid = 400, low = 50),
                target_nid=1,
                apply_interval_us=1000000,
                dests=_damon_sysfs.DamosDests(
                    dests=[_damon_sysfs.DamosDest(id=1, weight=30),
                           _damon_sysfs.DamosDest(id=0, weight=70)]),
                core_filters=[
                    _damon_sysfs.DamosFilter(type_='addr', matching=True,
                                             allow=False, addr_start=42,
                                             addr_end=4242),
                    ],
                ops_filters=[
                    _damon_sysfs.DamosFilter(type_='anon', matching=True,
                                             allow=True),
                    ],
                )])
    context.idx = 0
    context.kdamond = kdamonds.kdamonds[0]
    kdamonds.kdamonds[0].contexts = [context]
    kdamonds.kdamonds[0].commit()

    status, err = dump_damon_status_dict(kdamonds.kdamonds[0].pid)
    if err is not None:
        print(err)
        exit(1)

    assert_ctxs_committed(kdamonds.kdamonds[0].contexts, status['contexts'])

    # test online commitment of minimum context.
    context = _damon_sysfs.DamonCtx()
    context.idx = 0
    context.kdamond = kdamonds.kdamonds[0]
    kdamonds.kdamonds[0].contexts = [context]
    kdamonds.kdamonds[0].commit()

    status, err = dump_damon_status_dict(kdamonds.kdamonds[0].pid)
    if err is not None:
        print(err)
        exit(1)

    assert_ctxs_committed(kdamonds.kdamonds[0].contexts, status['contexts'])

    kdamonds.stop()

if __name__ == '__main__':
    main()
