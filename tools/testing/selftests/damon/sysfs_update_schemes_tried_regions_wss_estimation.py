#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def pass_wss_estimation(sz_region):
    # access two regions of given size, 2 seocnds per each region
    proc = subprocess.Popen(
            ['./access_memory', '2', '%d' % sz_region, '2000', 'repeat'])
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(
                    access_pattern=_damon_sysfs.DamosAccessPattern(
                        # >= 25% access rate, >= 200ms age
                        nr_accesses=[5, 20], age=[2, 2**64 - 1]))] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdamond start failed: %s' % err)
        exit(1)

    wss_collected = []
    while proc.poll() is None and len(wss_collected) < 40:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_tried_bytes()
        if err != None:
            print('tried bytes update failed: %s' % err)
            exit(1)

        wss_collected.append(
                kdamonds.kdamonds[0].contexts[0].schemes[0].tried_bytes)
    proc.terminate()
    err = kdamonds.stop()
    if err is not None:
        print('kdamond stop failed: %s' % err)
        exit(1)

    wss_collected.sort()
    acceptable_error_rate = 0.2
    for percentile in [50, 75]:
        sample = wss_collected[int(len(wss_collected) * percentile / 100)]
        error_rate = abs(sample - sz_region) / sz_region
        print('%d-th percentile error %f (expect %d, result %d)' %
                (percentile, error_rate, sz_region, sample))
        if error_rate > acceptable_error_rate:
            print('the error rate is not acceptable (> %f)' %
                    acceptable_error_rate)
            print('samples are as below')
            for idx, wss in enumerate(wss_collected):
                if idx < len(wss_collected) - 1 and \
                        wss_collected[idx + 1] == wss:
                    continue
                print('%d/%d: %d' % (idx, len(wss_collected), wss))
            return False
    return True

def main():
    # DAMON doesn't flush TLB.  If the system has large TLB that can cover
    # whole test working set, DAMON cannot see the access.  Test up to 160 MiB
    # test working set.
    sz_region_mb = 10
    max_sz_region_mb = 160
    while sz_region_mb <= max_sz_region_mb:
        test_pass = pass_wss_estimation(sz_region_mb * 1024 * 1024)
        if test_pass is True:
            exit(0)
        sz_region_mb *= 2
    exit(1)

if __name__ == '__main__':
    main()
