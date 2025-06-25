#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
API level tests for RSS (mostly Netlink vs IOCTL).
"""

import glob
from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_is, ksft_ne
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import defer, ethtool
from lib.py import EthtoolFamily
from lib.py import NetDrvEnv


def _ethtool_create(cfg, act, opts):
    output = ethtool(f"{act} {cfg.ifname} {opts}").stdout
    # Output will be something like: "New RSS context is 1" or
    # "Added rule with ID 7", we want the integer from the end
    return int(output.split()[-1])


def test_rxfh_indir_ntf(cfg):
    """
    Check that Netlink notifications are generated when RSS indirection
    table was modified.
    """

    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*"))
    if qcnt < 2:
        raise KsftSkipEx(f"Local has only {qcnt} queues")

    ethnl = EthtoolFamily()
    ethnl.ntf_subscribe("monitor")

    ethtool(f"--disable-netlink -X {cfg.ifname} weight 0 1")
    reset = defer(ethtool, f"-X {cfg.ifname} default")

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("No notification received")
    ksft_eq(ntf["name"], "rss-ntf")
    ksft_eq(set(ntf["msg"]["indir"]), {1})

    reset.exec()
    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("No notification received after reset")
    ksft_eq(ntf["name"], "rss-ntf")
    ksft_is(ntf["msg"].get("context"), None)
    ksft_ne(set(ntf["msg"]["indir"]), {1})


def test_rxfh_indir_ctx_ntf(cfg):
    """
    Check that Netlink notifications are generated when RSS indirection
    table was modified on an additional RSS context.
    """

    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*"))
    if qcnt < 2:
        raise KsftSkipEx(f"Local has only {qcnt} queues")

    ctx_id = _ethtool_create(cfg, "-X", "context new")
    defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")

    ethnl = EthtoolFamily()
    ethnl.ntf_subscribe("monitor")

    ethtool(f"--disable-netlink -X {cfg.ifname} context {ctx_id} weight 0 1")

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("No notification received")
    ksft_eq(ntf["name"], "rss-ntf")
    ksft_eq(ntf["msg"].get("context"), ctx_id)
    ksft_eq(set(ntf["msg"]["indir"]), {1})


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
