#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def main():
    # access two 10 MiB memory regions, 2 second per each
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory', '2', '%d' % sz_region, '2000'])
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
    while proc.poll() == None:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_tried_bytes()
        if err != None:
            print('tried bytes update failed: %s' % err)
            exit(1)

        wss_collected.append(
                kdamonds.kdamonds[0].contexts[0].schemes[0].tried_bytes)

    wss_collected.sort()
    acceptable_error_rate = 0.2
    for percentile in [50, 75]:
        sample = wss_collected[int(len(wss_collected) * percentile / 100)]
        error_rate = abs(sample - sz_region) / sz_region
        print('%d-th percentile (%d) error %f' %
                (percentile, sample, error_rate))
        if error_rate > acceptable_error_rate:
            print('the error rate is not acceptable (> %f)' %
                    acceptable_error_rate)
            print('samples are as below')
            print('\n'.join(['%d' % wss for wss in wss_collected]))
            exit(1)

if __name__ == '__main__':
    main()
