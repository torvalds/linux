#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def test_nr_regions(real_nr_regions, min_nr_regions, max_nr_regions):
    '''
    Create process of the given 'real_nr_regions' regions, monitor it using
    DAMON with given '{min,max}_nr_regions' monitoring parameter.

    Exit with non-zero return code if the given {min,max}_nr_regions is not
    kept.
    '''
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory_even', '%d' % real_nr_regions,
                             '%d' % sz_region])

    # stat every monitored regions
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                monitoring_attrs=_damon_sysfs.DamonAttrs(
                    min_nr_regions=min_nr_regions,
                    max_nr_regions=max_nr_regions),
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(action='stat',
                    )] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err is not None:
        proc.terminate()
        print('kdamond start failed: %s' % err)
        exit(1)

    collected_nr_regions = []
    while proc.poll() is None:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_tried_regions()
        if err is not None:
            proc.terminate()
            print('tried regions update failed: %s' % err)
            exit(1)

        scheme = kdamonds.kdamonds[0].contexts[0].schemes[0]
        if scheme.tried_regions is None:
            proc.terminate()
            print('tried regions is not collected')
            exit(1)

        nr_tried_regions = len(scheme.tried_regions)
        if nr_tried_regions <= 0:
            proc.terminate()
            print('tried regions is not created')
            exit(1)
        collected_nr_regions.append(nr_tried_regions)
        if len(collected_nr_regions) > 10:
            break
    proc.terminate()
    kdamonds.stop()

    test_name = 'nr_regions test with %d/%d/%d real/min/max nr_regions' % (
            real_nr_regions, min_nr_regions, max_nr_regions)
    collected_nr_regions.sort()
    if (collected_nr_regions[0] < min_nr_regions or
        collected_nr_regions[-1] > max_nr_regions):
        print('fail %s' % test_name)
        print('number of regions that collected are:')
        for nr in collected_nr_regions:
            print(nr)
        exit(1)
    print('pass %s ' % test_name)

def main():
    # test min_nr_regions larger than real nr regions
    test_nr_regions(10, 20, 100)

    # test max_nr_regions smaller than real nr regions
    test_nr_regions(15, 3, 10)

    # test online-tuned max_nr_regions that smaller than real nr regions
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory_even', '14', '%d' % sz_region])

    # stat every monitored regions
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                monitoring_attrs=_damon_sysfs.DamonAttrs(
                    min_nr_regions=10, max_nr_regions=1000),
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(action='stat',
                    )] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err is not None:
        proc.terminate()
        print('kdamond start failed: %s' % err)
        exit(1)

    # wait until the real regions are found
    time.sleep(3)

    attrs = kdamonds.kdamonds[0].contexts[0].monitoring_attrs
    attrs.min_nr_regions = 3
    attrs.max_nr_regions = 7
    attrs.update_us = 100000
    err = kdamonds.kdamonds[0].commit()
    if err is not None:
        proc.terminate()
        print('commit failed: %s' % err)
        exit(1)
    # wait for next merge operation is executed
    time.sleep(0.3)

    err = kdamonds.kdamonds[0].update_schemes_tried_regions()
    if err is not None:
        proc.terminate()
        print('tried regions update failed: %s' % err)
        exit(1)

    scheme = kdamonds.kdamonds[0].contexts[0].schemes[0]
    if scheme.tried_regions is None:
        proc.terminate()
        print('tried regions is not collected')
        exit(1)

    nr_tried_regions = len(scheme.tried_regions)
    if nr_tried_regions <= 0:
        proc.terminate()
        print('tried regions is not created')
        exit(1)
    proc.terminate()

    if nr_tried_regions > 7:
        print('fail online-tuned max_nr_regions: %d > 7' % nr_tried_regions)
        exit(1)
    print('pass online-tuned max_nr_regions')

if __name__ == '__main__':
    main()
