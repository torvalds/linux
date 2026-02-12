#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Driver-related behavior tests for RSS.
"""

from lib.py import ksft_run, ksft_exit, ksft_ge
from lib.py import ksft_variants, KsftNamedVariant, KsftSkipEx
from lib.py import defer, ethtool
from lib.py import EthtoolFamily, NlError
from lib.py import NetDrvEnv


def _is_power_of_two(n):
    return n > 0 and (n & (n - 1)) == 0


def _get_rss(cfg, context=0):
    return ethtool(f"-x {cfg.ifname} context {context}", json=True)[0]


def _test_rss_indir_size(cfg, qcnt, context=0):
    """Test that indirection table size is at least 4x queue count."""
    ethtool(f"-L {cfg.ifname} combined {qcnt}")

    rss = _get_rss(cfg, context=context)
    indir = rss['rss-indirection-table']
    ksft_ge(len(indir), 4 * qcnt, "Table smaller than 4x")
    return len(indir)


def _maybe_create_context(cfg, create_context):
    """ Either create a context and return its ID or return 0 for main ctx """
    if not create_context:
        return 0
    try:
        ctx = cfg.ethnl.rss_create_act({'header': {'dev-index': cfg.ifindex}})
        ctx_id = ctx['context']
        defer(cfg.ethnl.rss_delete_act,
              {'header': {'dev-index': cfg.ifindex}, 'context': ctx_id})
    except NlError:
        raise KsftSkipEx("Device does not support additional RSS contexts")

    return ctx_id


@ksft_variants([
    KsftNamedVariant("main", False),
    KsftNamedVariant("ctx", True),
])
def indir_size_4x(cfg, create_context):
    """
    Test that the indirection table has at least 4 entries per queue.
    Empirically network-heavy workloads like memcache suffer with the 33%
    imbalance of a 2x indirection table size.
    4x table translates to a 16% imbalance.
    """
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < 3:
        raise KsftSkipEx(f"Not enough queues for the test: max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")
    ethtool(f"-L {cfg.ifname} combined 3")

    ctx_id = _maybe_create_context(cfg, create_context)

    indir_sz = _test_rss_indir_size(cfg, 3, context=ctx_id)

    # Test with max queue count (max - 1 if max is a power of two)
    test_max = ch_max - 1 if _is_power_of_two(ch_max) else ch_max
    if test_max > 3 and indir_sz < test_max * 4:
        _test_rss_indir_size(cfg, test_max, context=ctx_id)


def main() -> None:
    """ Ksft boiler plate main """
    with NetDrvEnv(__file__) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run([indir_size_4x], args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
