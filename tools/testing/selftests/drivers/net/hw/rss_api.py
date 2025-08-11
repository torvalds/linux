#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
API level tests for RSS (mostly Netlink vs IOCTL).
"""

import errno
import glob
import random
from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_is, ksft_ne, ksft_raises
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import defer, ethtool, CmdExitFailure
from lib.py import EthtoolFamily, NlError
from lib.py import NetDrvEnv


def _require_2qs(cfg):
    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*"))
    if qcnt < 2:
        raise KsftSkipEx(f"Local has only {qcnt} queues")
    return qcnt


def _ethtool_create(cfg, act, opts):
    output = ethtool(f"{act} {cfg.ifname} {opts}").stdout
    # Output will be something like: "New RSS context is 1" or
    # "Added rule with ID 7", we want the integer from the end
    return int(output.split()[-1])


def _ethtool_get_cfg(cfg, fl_type, to_nl=False):
    descr = ethtool(f"-n {cfg.ifname} rx-flow-hash {fl_type}").stdout

    if to_nl:
        converter = {
            "IP SA": "ip-src",
            "IP DA": "ip-dst",
            "L4 bytes 0 & 1 [TCP/UDP src port]": "l4-b-0-1",
            "L4 bytes 2 & 3 [TCP/UDP dst port]": "l4-b-2-3",
        }

        ret = set()
    else:
        converter = {
            "IP SA": "s",
            "IP DA": "d",
            "L3 proto": "t",
            "L4 bytes 0 & 1 [TCP/UDP src port]": "f",
            "L4 bytes 2 & 3 [TCP/UDP dst port]": "n",
        }

        ret = ""

    for line in descr.split("\n")[1:-2]:
        # if this raises we probably need to add more keys to converter above
        if to_nl:
            ret.add(converter[line])
        else:
            ret += converter[line]
    return ret


def test_rxfh_nl_set_fail(cfg):
    """
    Test error path of Netlink SET.
    """
    _require_2qs(cfg)

    ethnl = EthtoolFamily()
    ethnl.ntf_subscribe("monitor")

    with ksft_raises(NlError):
        ethnl.rss_set({"header": {"dev-name": "lo"},
                       "indir": None})

    with ksft_raises(NlError):
        ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "indir": [100000]})
    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    ksft_is(ntf, None)


def test_rxfh_nl_set_indir(cfg):
    """
    Test setting indirection table via Netlink.
    """
    qcnt = _require_2qs(cfg)

    # Test some SETs with a value
    reset = defer(cfg.ethnl.rss_set,
                  {"header": {"dev-index": cfg.ifindex}, "indir": None})
    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "indir": [1]})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(set(rss.get("indir", [-1])), {1})

    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "indir": [0, 1]})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(set(rss.get("indir", [-1])), {0, 1})

    # Make sure we can't set the queue count below max queue used
    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 0 rx 1")
    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 1 rx 0")

    # Test reset back to default
    reset.exec()
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(set(rss.get("indir", [-1])), set(range(qcnt)))


def test_rxfh_nl_set_indir_ctx(cfg):
    """
    Test setting indirection table for a custom context via Netlink.
    """
    _require_2qs(cfg)

    # Get setting for ctx 0, we'll make sure they don't get clobbered
    dflt = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})

    # Create context
    ctx_id = _ethtool_create(cfg, "-X", "context new")
    defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")

    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "context": ctx_id, "indir": [1]})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex},
                             "context": ctx_id})
    ksft_eq(set(rss.get("indir", [-1])), {1})

    ctx0 = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(ctx0, dflt)

    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "context": ctx_id, "indir": [0, 1]})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex},
                             "context": ctx_id})
    ksft_eq(set(rss.get("indir", [-1])), {0, 1})

    ctx0 = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(ctx0, dflt)

    # Make sure we can't set the queue count below max queue used
    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 0 rx 1")
    with ksft_raises(CmdExitFailure):
        ethtool(f"-L {cfg.ifname} combined 1 rx 0")


def test_rxfh_indir_ntf(cfg):
    """
    Check that Netlink notifications are generated when RSS indirection
    table was modified.
    """
    _require_2qs(cfg)

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
    _require_2qs(cfg)

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


def test_rxfh_nl_set_key(cfg):
    """
    Test setting hashing key via Netlink.
    """

    dflt = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    defer(cfg.ethnl.rss_set,
          {"header": {"dev-index": cfg.ifindex},
           "hkey": dflt["hkey"], "indir": None})

    # Empty key should error out
    with ksft_raises(NlError) as cm:
        cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                           "hkey": None})
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.hkey')

    # Set key to random
    mod = random.randbytes(len(dflt["hkey"]))
    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "hkey": mod})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(rss.get("hkey", [-1]), mod)

    # Set key to random and indir tbl to something at once
    mod = random.randbytes(len(dflt["hkey"]))
    cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                       "indir": [0, 1], "hkey": mod})
    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    ksft_eq(rss.get("hkey", [-1]), mod)
    ksft_eq(set(rss.get("indir", [-1])), {0, 1})


def test_rxfh_fields(cfg):
    """
    Test reading Rx Flow Hash over Netlink.
    """

    flow_types = ["tcp4", "tcp6", "udp4", "udp6"]
    ethnl = EthtoolFamily()

    cfg_nl = ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    for fl_type in flow_types:
        one = _ethtool_get_cfg(cfg, fl_type, to_nl=True)
        ksft_eq(one, cfg_nl["flow-hash"][fl_type],
                comment="Config for " + fl_type)


def test_rxfh_fields_set(cfg):
    """ Test configuring Rx Flow Hash over Netlink. """

    flow_types = ["tcp4", "tcp6", "udp4", "udp6"]

    # Collect current settings
    cfg_old = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    # symmetric hashing is config-order-sensitive make sure we leave
    # symmetric mode, or make the flow-hash sym-compatible first
    changes = [{"flow-hash": cfg_old["flow-hash"],},
               {"input-xfrm": cfg_old.get("input-xfrm", {}),}]
    if cfg_old.get("input-xfrm"):
        changes = list(reversed(changes))
    for old in changes:
        defer(cfg.ethnl.rss_set, {"header": {"dev-index": cfg.ifindex},} | old)

    # symmetric hashing prevents some of the configs below
    if cfg_old.get("input-xfrm"):
        cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                           "input-xfrm": {}})

    for fl_type in flow_types:
        cur = _ethtool_get_cfg(cfg, fl_type)
        if cur == "sdfn":
            change_nl = {"ip-src", "ip-dst"}
            change_ic = "sd"
        else:
            change_nl = {"l4-b-0-1", "l4-b-2-3", "ip-src", "ip-dst"}
            change_ic = "sdfn"

        cfg.ethnl.rss_set({
            "header": {"dev-index": cfg.ifindex},
            "flow-hash": {fl_type: change_nl}
        })
        reset = defer(ethtool, f"--disable-netlink -N {cfg.ifname} "
                      f"rx-flow-hash {fl_type} {cur}")

        cfg_nl = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
        ksft_eq(change_nl, cfg_nl["flow-hash"][fl_type],
                comment=f"Config for {fl_type} over Netlink")
        cfg_ic = _ethtool_get_cfg(cfg, fl_type)
        ksft_eq(change_ic, cfg_ic,
                comment=f"Config for {fl_type} over IOCTL")

        reset.exec()
        cfg_nl = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
        ksft_eq(cfg_old["flow-hash"][fl_type], cfg_nl["flow-hash"][fl_type],
                comment=f"Un-config for {fl_type} over Netlink")
        cfg_ic = _ethtool_get_cfg(cfg, fl_type)
        ksft_eq(cur, cfg_ic, comment=f"Un-config for {fl_type} over IOCTL")

    # Try to set multiple at once, the defer was already installed at the start
    change = {"ip-src"}
    if change == cfg_old["flow-hash"]["tcp4"]:
        change = {"ip-dst"}
    cfg.ethnl.rss_set({
        "header": {"dev-index": cfg.ifindex},
        "flow-hash": {x: change for x in flow_types}
    })

    cfg_nl = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    for fl_type in flow_types:
        ksft_eq(change, cfg_nl["flow-hash"][fl_type],
                comment=f"multi-config for {fl_type} over Netlink")


def test_rxfh_fields_set_xfrm(cfg):
    """ Test changing Rx Flow Hash vs xfrm_input at once.  """

    def set_rss(cfg, xfrm, fh):
        cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                           "input-xfrm": xfrm, "flow-hash": fh})

    # Install the reset handler
    cfg_old = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    # symmetric hashing is config-order-sensitive make sure we leave
    # symmetric mode, or make the flow-hash sym-compatible first
    changes = [{"flow-hash": cfg_old["flow-hash"],},
               {"input-xfrm": cfg_old.get("input-xfrm", {}),}]
    if cfg_old.get("input-xfrm"):
        changes = list(reversed(changes))
    for old in changes:
        defer(cfg.ethnl.rss_set, {"header": {"dev-index": cfg.ifindex},} | old)

    # Make sure we start with input-xfrm off, and tcp4 config non-sym
    set_rss(cfg, {}, {})
    set_rss(cfg, {}, {"tcp4": {"ip-src"}})

    # Setting sym and fixing tcp4 config not expected to pass right now
    with ksft_raises(NlError):
        set_rss(cfg, {"sym-xor"}, {"tcp4": {"ip-src", "ip-dst"}})
    # One at a time should work, hopefully
    set_rss(cfg, 0, {"tcp4": {"ip-src", "ip-dst"}})
    no_support = False
    try:
        set_rss(cfg, {"sym-xor"}, {})
    except NlError:
        try:
            set_rss(cfg, {"sym-or-xor"}, {})
        except NlError:
            no_support = True
    if no_support:
        raise KsftSkipEx("no input-xfrm supported")
    # Disabling two at once should not work either without kernel changes
    with ksft_raises(NlError):
        set_rss(cfg, {}, {"tcp4": {"ip-src"}})


def test_rxfh_fields_ntf(cfg):
    """ Test Rx Flow Hash notifications. """

    cur = _ethtool_get_cfg(cfg, "tcp4")
    if cur == "sdfn":
        change = {"ip-src", "ip-dst"}
    else:
        change = {"l4-b-0-1", "l4-b-2-3", "ip-src", "ip-dst"}

    ethnl = EthtoolFamily()
    ethnl.ntf_subscribe("monitor")

    ethnl.rss_set({
        "header": {"dev-index": cfg.ifindex},
        "flow-hash": {"tcp4": change}
    })
    reset = defer(ethtool,
                  f"--disable-netlink -N {cfg.ifname} rx-flow-hash tcp4 {cur}")

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("No notification received after IOCTL change")
    ksft_eq(ntf["name"], "rss-ntf")
    ksft_eq(ntf["msg"]["flow-hash"]["tcp4"], change)
    ksft_eq(next(ethnl.poll_ntf(duration=0.01), None), None)

    reset.exec()
    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("No notification received after Netlink change")
    ksft_eq(ntf["name"], "rss-ntf")
    ksft_ne(ntf["msg"]["flow-hash"]["tcp4"], change)
    ksft_eq(next(ethnl.poll_ntf(duration=0.01), None), None)


def test_rss_ctx_add(cfg):
    """ Test creating an additional RSS context via Netlink """

    _require_2qs(cfg)

    # Test basic creation
    ctx = cfg.ethnl.rss_create_act({"header": {"dev-index": cfg.ifindex}})
    d = defer(ethtool, f"-X {cfg.ifname} context {ctx.get('context')} delete")
    ksft_ne(ctx.get("context", 0), 0)
    ksft_ne(set(ctx.get("indir", [0])), {0},
            comment="Driver should init the indirection table")

    # Try requesting the ID we just got allocated
    with ksft_raises(NlError) as cm:
        ctx = cfg.ethnl.rss_create_act({
            "header": {"dev-index": cfg.ifindex},
            "context": ctx.get("context"),
        })
        ethtool(f"-X {cfg.ifname} context {ctx.get('context')} delete")
    d.exec()
    ksft_eq(cm.exception.nl_msg.error, -errno.EBUSY)

    # Test creating with a specified RSS table, and context ID
    ctx_id = ctx.get("context")
    ctx = cfg.ethnl.rss_create_act({
        "header": {"dev-index": cfg.ifindex},
        "context": ctx_id,
        "indir": [1],
    })
    ethtool(f"-X {cfg.ifname} context {ctx.get('context')} delete")
    ksft_eq(ctx.get("context"), ctx_id)
    ksft_eq(set(ctx.get("indir", [0])), {1})


def test_rss_ctx_ntf(cfg):
    """ Test notifications for creating additional RSS contexts """

    ethnl = EthtoolFamily()
    ethnl.ntf_subscribe("monitor")

    # Create / delete via Netlink
    ctx = cfg.ethnl.rss_create_act({"header": {"dev-index": cfg.ifindex}})
    cfg.ethnl.rss_delete_act({
        "header": {"dev-index": cfg.ifindex},
        "context": ctx["context"],
    })

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("[NL] No notification after context creation")
    ksft_eq(ntf["name"], "rss-create-ntf")
    ksft_eq(ctx, ntf["msg"])

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("[NL] No notification after context deletion")
    ksft_eq(ntf["name"], "rss-delete-ntf")

    # Create / deleve via IOCTL
    ctx_id = _ethtool_create(cfg, "--disable-netlink -X", "context new")
    ethtool(f"--disable-netlink -X {cfg.ifname} context {ctx_id} delete")
    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("[IOCTL] No notification after context creation")
    ksft_eq(ntf["name"], "rss-create-ntf")

    ntf = next(ethnl.poll_ntf(duration=0.2), None)
    if ntf is None:
        raise KsftFailEx("[IOCTL] No notification after context deletion")
    ksft_eq(ntf["name"], "rss-delete-ntf")


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
