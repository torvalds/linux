#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import json
import os
import subprocess

import _damon_sysfs

def dump_damon_status_dict(pid):
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

def main():
    kdamonds = _damon_sysfs.Kdamonds(
            [_damon_sysfs.Kdamond(
                contexts=[_damon_sysfs.DamonCtx(
                    targets=[_damon_sysfs.DamonTarget(pid=-1)])])])
    err = kdamonds.start()
    if err is not None:
        print('kdamond start failed: %s' % err)
        exit(1)

    status, err = dump_damon_status_dict(kdamonds.kdamonds[0].pid)
    if err is not None:
        print(err)
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

    if ctx['schemes'] != []:
        fail('schemes')

    kdamonds.stop()

if __name__ == '__main__':
    main()
