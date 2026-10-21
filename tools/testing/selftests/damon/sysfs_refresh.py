#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import os
import subprocess
import time

import _damon_sysfs

def main():
    # Continuously access a memory region for far longer than the test needs,
    # so the kdamond always has a live target to monitor while we poll.
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(
            ['./access_memory', '1', '%d' % sz_region, '60000', 'repeat'])

    # A 'stat' scheme with the default (maximally wide) access pattern matches
    # every monitored region, so its 'nr_tried' stat increases as the kdamond
    # runs.  refresh_ms should make DAMON update the schemes' stats files under
    # sysfs on its own, without a manual 'update_schemes_stats' request.
    kdamond = _damon_sysfs.Kdamond(
            refresh_ms=100,
            contexts=[_damon_sysfs.DamonCtx(
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(action='stat')],
                )])
    kdamonds = _damon_sysfs.Kdamonds([kdamond])

    err = kdamonds.start()
    if err is not None:
        # Kernels older than the refresh_ms feature have no such file; treat
        # that as unsupported rather than a failure.
        if not os.path.exists(os.path.join(kdamond.sysfs_dir(), 'refresh_ms')):
            proc.terminate()
            proc.wait()
            print('kdamond has no refresh_ms file; skipping')
            exit(_damon_sysfs.ksft_skip)
        proc.terminate()
        proc.wait()
        print('kdamond start failed: %s' % err)
        exit(1)

    scheme = kdamond.contexts[0].schemes[0]
    nr_tried_path = os.path.join(scheme.sysfs_dir(), 'stats', 'nr_tried')

    try:
        # Poll the stat file directly.  We never request an update (e.g.
        # 'update_schemes_stats'), so 'nr_tried' can become non-zero only
        # through the periodic refresh that refresh_ms enables.
        nr_tried = 0
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                print('the access_memory target exited unexpectedly')
                exit(1)
            content, err = _damon_sysfs.read_file(nr_tried_path)
            if err is not None:
                print('reading %s failed: %s' % (nr_tried_path, err))
                exit(1)
            nr_tried = int(content)
            if nr_tried > 0:
                break
            time.sleep(0.1)
    finally:
        kdamonds.stop()
        proc.terminate()
        proc.wait()

    if nr_tried == 0:
        print('refresh_ms did not auto-update the schemes stats')
        exit(1)

if __name__ == '__main__':
    main()
