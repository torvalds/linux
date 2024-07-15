#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def main():
    # access two 10 MiB memory regions, 2 second per each
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory', '2', '%d' % sz_region, '2000'])

    # Set quota up to 1 MiB per 100 ms
    sz_quota = 1024 * 1024 # 1 MiB
    quota_reset_interval = 100 # 100 ms
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(
                    access_pattern=_damon_sysfs.DamosAccessPattern(
                        # >= 25% access rate, >= 200ms age
                        nr_accesses=[5, 20], age=[2, 2**64 - 1]),
                    quota=_damon_sysfs.DamosQuota(
                        sz=sz_quota, reset_interval_ms=quota_reset_interval)
                    )] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdamond start failed: %s' % err)
        exit(1)

    wss_collected = []
    nr_quota_exceeds = 0
    while proc.poll() == None:
        time.sleep(0.1)
        err = kdamonds.kdamonds[0].update_schemes_tried_bytes()
        if err != None:
            print('tried bytes update failed: %s' % err)
            exit(1)
        err = kdamonds.kdamonds[0].update_schemes_stats()
        if err != None:
            print('stats update failed: %s' % err)
            exit(1)

        scheme = kdamonds.kdamonds[0].contexts[0].schemes[0]
        wss_collected.append(scheme.tried_bytes)
        nr_quota_exceeds = scheme.stats.qt_exceeds

    wss_collected.sort()
    for wss in wss_collected:
        if wss > sz_quota:
            print('quota is not kept: %s > %s' % (wss, sz_quota))
            print('collected samples are as below')
            print('\n'.join(['%d' % wss for wss in wss_collected]))
            exit(1)

    if nr_quota_exceeds < len(wss_collected):
        print('quota is not always exceeded: %d > %d' %
              (len(wss_collected), nr_quota_exceeds))
        exit(1)

if __name__ == '__main__':
    main()
