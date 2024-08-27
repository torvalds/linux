#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def main():
    # repeatedly access even-numbered ones in 14 regions of 10 MiB size
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory_even', '14', '%d' % sz_region])

    # stat every monitored regions
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
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

    collected_nr_regions.sort()
    sample = collected_nr_regions[4]
    print('50-th percentile nr_regions: %d' % sample)
    print('expectation (>= 14) is %s' % 'met' if sample >= 14 else 'not met')
    if collected_nr_regions[4] < 14:
        print('full nr_regions:')
        print('\n'.join(collected_nr_regions))
        exit(1)

if __name__ == '__main__':
    main()
