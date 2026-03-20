#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Driver-related behavior tests for RSS.
"""

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ge
from lib.py import ksft_variants, KsftNamedVariant, KsftSkipEx, ksft_raises
from lib.py import defer, ethtool, CmdExitFailure
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


def _require_dynamic_indir_size(cfg, ch_max):
    """Skip if the device does not dynamically size its indirection table."""
    ethtool(f"-X {cfg.ifname} default")
    ethtool(f"-L {cfg.ifname} combined 2")
    small = len(_get_rss(cfg)['rss-indirection-table'])
    ethtool(f"-L {cfg.ifname} combined {ch_max}")
    large = len(_get_rss(cfg)['rss-indirection-table'])

    if small == large:
        raise KsftSkipEx("Device does not dynamically size indirection table")


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


@ksft_variants([
    KsftNamedVariant("main", False),
    KsftNamedVariant("ctx", True),
])
def resize_periodic(cfg, create_context):
    """Test that a periodic indirection table survives channel changes.

    Set a non-default periodic table ([3, 2, 1, 0] x N) via netlink,
    reduce channels to trigger a fold, then increase to trigger an
    unfold. Using a reversed pattern (instead of [0, 1, 2, 3]) ensures
    the test can distinguish a correct fold from a driver that silently
    resets the table to defaults. Verify the exact pattern is preserved
    and the size tracks the channel count.
    """
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < 4:
        raise KsftSkipEx(f"Not enough queues for the test: max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    _require_dynamic_indir_size(cfg, ch_max)

    ctx_id = _maybe_create_context(cfg, create_context)

    # Set a non-default periodic pattern via netlink.
    # Send only 4 entries (user_size=4) so the kernel replicates it
    # to fill the device table. This allows folding down to 4 entries.
    rss = _get_rss(cfg, context=ctx_id)
    orig_size = len(rss['rss-indirection-table'])
    pattern = [3, 2, 1, 0]
    req = {'header': {'dev-index': cfg.ifindex}, 'indir': pattern}
    if ctx_id:
        req['context'] = ctx_id
    else:
        defer(ethtool, f"-X {cfg.ifname} default")
    cfg.ethnl.rss_set(req)

    # Shrink — should fold
    ethtool(f"-L {cfg.ifname} combined 4")
    rss = _get_rss(cfg, context=ctx_id)
    indir = rss['rss-indirection-table']

    ksft_ge(orig_size, len(indir), "Table did not shrink")
    ksft_eq(indir, [3, 2, 1, 0] * (len(indir) // 4),
            "Folded table has wrong pattern")

    # Grow back — should unfold
    ethtool(f"-L {cfg.ifname} combined {ch_max}")
    rss = _get_rss(cfg, context=ctx_id)
    indir = rss['rss-indirection-table']

    ksft_eq(len(indir), orig_size, "Table size not restored")
    ksft_eq(indir, [3, 2, 1, 0] * (len(indir) // 4),
            "Unfolded table has wrong pattern")


@ksft_variants([
    KsftNamedVariant("main", False),
    KsftNamedVariant("ctx", True),
])
def resize_below_user_size_reject(cfg, create_context):
    """Test that shrinking below user_size is rejected.

    Send a table via netlink whose size (user_size) sits between
    the small and large device table sizes. The table is periodic,
    so folding would normally succeed, but the user_size floor must
    prevent it.
    """
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < 4:
        raise KsftSkipEx(f"Not enough queues for the test: max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    _require_dynamic_indir_size(cfg, ch_max)

    ctx_id = _maybe_create_context(cfg, create_context)

    # Measure the table size at max channels
    rss = _get_rss(cfg, context=ctx_id)
    big_size = len(rss['rss-indirection-table'])

    # Measure the table size at reduced channels
    ethtool(f"-L {cfg.ifname} combined 4")
    rss = _get_rss(cfg, context=ctx_id)
    small_size = len(rss['rss-indirection-table'])
    ethtool(f"-L {cfg.ifname} combined {ch_max}")

    if small_size >= big_size:
        raise KsftSkipEx("Table did not shrink at reduced channels")

    # Find a user_size
    user_size = None
    for div in [2, 4]:
        candidate = big_size // div
        if candidate > small_size and big_size % candidate == 0:
            user_size = candidate
            break
    if user_size is None:
        raise KsftSkipEx("No suitable user_size between small and big table")

    # Send a periodic sub-table of exactly user_size entries.
    # Pattern safe for 4 channels.
    pattern = [0, 1, 2, 3] * (user_size // 4)
    if len(pattern) != user_size:
        raise KsftSkipEx(f"user_size ({user_size}) not divisible by 4")
    req = {'header': {'dev-index': cfg.ifindex}, 'indir': pattern}
    if ctx_id:
        req['context'] = ctx_id
    else:
        defer(ethtool, f"-X {cfg.ifname} default")
    cfg.ethnl.rss_set(req)

    # Shrink channels — table would go to small_size < user_size.
    # The table is periodic so folding would work, but user_size
    # floor must reject it.
    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 4")


@ksft_variants([
    KsftNamedVariant("main", False),
    KsftNamedVariant("ctx", True),
])
def resize_nonperiodic_reject(cfg, create_context):
    """Test that a non-periodic table blocks channel reduction.

    Set equal weight across all queues so the table is not periodic
    at any smaller size, then verify channel reduction is rejected.
    An additional context with a periodic table is created to verify
    that validation catches the non-periodic one even when others
    are fine.
    """
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < 4:
        raise KsftSkipEx(f"Not enough queues for the test: max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    _require_dynamic_indir_size(cfg, ch_max)

    ctx_id = _maybe_create_context(cfg, create_context)
    ctx_ref = f"context {ctx_id}" if ctx_id else ""

    # Create an extra context with a periodic (foldable) table so that
    # the validation must iterate all contexts to find the bad one.
    extra_ctx = _maybe_create_context(cfg, True)
    ethtool(f"-X {cfg.ifname} context {extra_ctx} equal 2")

    ethtool(f"-X {cfg.ifname} {ctx_ref} equal {ch_max}")
    if not create_context:
        defer(ethtool, f"-X {cfg.ifname} default")

    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 2")


@ksft_variants([
    KsftNamedVariant("main", False),
    KsftNamedVariant("ctx", True),
])
def resize_nonperiodic_no_corruption(cfg, create_context):
    """Test that a failed resize does not corrupt table or channel count.

    Set a non-periodic table, attempt a channel reduction (which must
    fail), then verify both the indirection table contents and the
    channel count are unchanged.
    """
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < 4:
        raise KsftSkipEx(f"Not enough queues for the test: max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    _require_dynamic_indir_size(cfg, ch_max)

    ctx_id = _maybe_create_context(cfg, create_context)
    ctx_ref = f"context {ctx_id}" if ctx_id else ""

    ethtool(f"-X {cfg.ifname} {ctx_ref} equal {ch_max}")
    if not create_context:
        defer(ethtool, f"-X {cfg.ifname} default")

    rss_before = _get_rss(cfg, context=ctx_id)

    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 2")

    rss_after = _get_rss(cfg, context=ctx_id)
    ksft_eq(rss_after['rss-indirection-table'],
            rss_before['rss-indirection-table'],
            "Indirection table corrupted after failed resize")

    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ksft_eq(channels['combined-count'], ch_max,
            "Channel count changed after failed resize")


def main() -> None:
    """ Ksft boiler plate main """
    with NetDrvEnv(__file__) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run([indir_size_4x, resize_periodic,
                  resize_below_user_size_reject,
                  resize_nonperiodic_reject,
                  resize_nonperiodic_no_corruption], args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
