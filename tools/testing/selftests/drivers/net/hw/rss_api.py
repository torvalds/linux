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


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
