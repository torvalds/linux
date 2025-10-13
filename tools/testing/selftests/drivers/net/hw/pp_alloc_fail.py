#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Test driver resilience vs page pool allocation failures.
"""

import errno
import time
import math
import os
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import NetdevFamily, NlError
from lib.py import NetDrvEpEnv
from lib.py import cmd, tool, GenerateTraffic


def _write_fail_config(config):
    for key, value in config.items():
        path = "/sys/kernel/debug/fail_function/"
        with open(path + key, "w", encoding='ascii') as fp:
            fp.write(str(value) + "\n")


def _enable_pp_allocation_fail():
    if not os.path.exists("/sys/kernel/debug/fail_function"):
        raise KsftSkipEx("Kernel built without function error injection (or DebugFS)")

    if not os.path.exists("/sys/kernel/debug/fail_function/page_pool_alloc_netmems"):
        _write_fail_config({"inject": "page_pool_alloc_netmems"})

    _write_fail_config({
        "verbose": 0,
        "interval": 511,
        "probability": 100,
        "times": -1,
    })


def _disable_pp_allocation_fail():
    if not os.path.exists("/sys/kernel/debug/fail_function"):
        return

    if os.path.exists("/sys/kernel/debug/fail_function/page_pool_alloc_netmems"):
        _write_fail_config({"inject": ""})

    _write_fail_config({
        "probability": 0,
        "times": 0,
    })


def test_pp_alloc(cfg, netdevnl):
    """
    Configure page pool allocation fail injection while traffic is running.
    """

    def get_stats():
        return netdevnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]

    def check_traffic_flowing():
        stat1 = get_stats()
        time.sleep(1)
        stat2 = get_stats()
        if stat2['rx-packets'] - stat1['rx-packets'] < 4000:
            raise KsftFailEx("Traffic seems low:", stat2['rx-packets'] - stat1['rx-packets'])


    try:
        stats = get_stats()
    except NlError as e:
        if e.nl_msg.error == -errno.EOPNOTSUPP:
            stats = {}
        else:
            raise
    if 'rx-alloc-fail' not in stats:
        raise KsftSkipEx("Driver does not report 'rx-alloc-fail' via qstats")

    set_g = False
    traffic = None
    try:
        traffic = GenerateTraffic(cfg)

        check_traffic_flowing()

        _enable_pp_allocation_fail()

        s1 = get_stats()
        time.sleep(3)
        s2 = get_stats()

        seen_fails = s2['rx-alloc-fail'] - s1['rx-alloc-fail']
        if seen_fails < 1:
            raise KsftSkipEx("Allocation failures not increasing")
        pkts = s2['rx-packets'] - s1['rx-packets']
        # Expecting one failure per 512 buffers, 3.1x safety margin
        want_fails = math.floor(pkts / 512 / 3.1)
        if seen_fails < want_fails:
            raise KsftSkipEx("Allocation increasing too slowly", seen_fails,
                             "packets:", pkts)
        ksft_pr(f"Seen: pkts:{pkts} fails:{seen_fails} (pass thrs:{want_fails})")

        # Basic failures are fine, try to wobble some settings to catch extra failures
        check_traffic_flowing()
        g = tool("ethtool", "-g " + cfg.ifname, json=True)[0]
        if 'rx' in g and g["rx"] * 2 <= g["rx-max"]:
            new_g = g['rx'] * 2
        elif 'rx' in g:
            new_g = g['rx'] // 2
        else:
            new_g = None

        if new_g:
            set_g = cmd(f"ethtool -G {cfg.ifname} rx {new_g}", fail=False).ret == 0
            if set_g:
                ksft_pr("ethtool -G change retval: success")
            else:
                ksft_pr("ethtool -G change retval: did not succeed", new_g)
        else:
            ksft_pr("ethtool -G change retval: did not try")

        time.sleep(0.1)
        check_traffic_flowing()
    finally:
        _disable_pp_allocation_fail()
        if traffic:
            traffic.stop()
        time.sleep(0.1)
        if set_g:
            cmd(f"ethtool -G {cfg.ifname} rx {g['rx']}")


def main() -> None:
    """ Ksft boiler plate main """
    netdevnl = NetdevFamily()
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:

        ksft_run([test_pp_alloc], args=(cfg, netdevnl, ))
    ksft_exit()


if __name__ == "__main__":
    main()
