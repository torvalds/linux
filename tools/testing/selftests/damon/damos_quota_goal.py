#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import subprocess
import time

import _damon_sysfs

def main():
    # access two 10 MiB memory regions, 2 second per each
    sz_region = 10 * 1024 * 1024
    proc = subprocess.Popen(['./access_memory', '2', '%d' % sz_region, '2000'])

    goal = _damon_sysfs.DamosQuotaGoal(
            metric=_damon_sysfs.qgoal_metric_user_input, target_value=10000)
    kdamonds = _damon_sysfs.Kdamonds([_damon_sysfs.Kdamond(
            contexts=[_damon_sysfs.DamonCtx(
                ops='vaddr',
                targets=[_damon_sysfs.DamonTarget(pid=proc.pid)],
                schemes=[_damon_sysfs.Damos(
                    action='stat',
                    quota=_damon_sysfs.DamosQuota(
                        goals=[goal], reset_interval_ms=100),
                    )] # schemes
                )] # contexts
            )]) # kdamonds

    err = kdamonds.start()
    if err != None:
        print('kdamond start failed: %s' % err)
        exit(1)

    score_values_to_test = [0, 15000, 5000, 18000]
    while proc.poll() == None:
        if len(score_values_to_test) == 0:
            time.sleep(0.1)
            continue

        goal.current_value = score_values_to_test.pop(0)
        expect_increase = goal.current_value < goal.target_value

        err = kdamonds.kdamonds[0].commit_schemes_quota_goals()
        if err is not None:
            print('commit_schemes_quota_goals failed: %s' % err)
            exit(1)

        err = kdamonds.kdamonds[0].update_schemes_effective_quotas()
        if err is not None:
            print('before-update_schemes_effective_quotas failed: %s' % err)
            exit(1)
        last_effective_bytes = goal.effective_bytes

        time.sleep(0.5)

        err = kdamonds.kdamonds[0].update_schemes_effective_quotas()
        if err is not None:
            print('after-update_schemes_effective_quotas failed: %s' % err)
            exit(1)

        print('score: %s, effective quota: %d -> %d (%.3fx)' % (
            goal.current_value, last_effective_bytes, goal.effective_bytes,
            goal.effective_bytes / last_effective_bytes
            if last_effective_bytes != 0 else -1.0))

        if last_effective_bytes == goal.effective_bytes:
            print('efective bytes not changed: %d' % goal.effective_bytes)
            exit(1)

        increased = last_effective_bytes < goal.effective_bytes
        if expect_increase != increased:
            print('expectation of increase (%s) != increased (%s)' %
                  (expect_increase, increased))
            exit(1)
        last_effective_bytes = goal.effective_bytes

if __name__ == '__main__':
    main()
