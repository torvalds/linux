#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests for RSS hashing on IPv6 Flow Label.
"""

import glob
import os
import socket
from lib.py import CmdExitFailure
from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ge, ksft_in, \
    ksft_not_in, ksft_raises, KsftSkipEx
from lib.py import bkg, cmd, defer, fd_read_timeout, rand_port
from lib.py import NetDrvEpEnv


def _check_system(cfg):
    if not hasattr(socket, "SO_INCOMING_CPU"):
        raise KsftSkipEx("socket.SO_INCOMING_CPU was added in Python 3.11")

    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*"))
    if qcnt < 2:
        raise KsftSkipEx(f"Local has only {qcnt} queues")

    for f in [f"/sys/class/net/{cfg.ifname}/queues/rx-0/rps_flow_cnt",
              f"/sys/class/net/{cfg.ifname}/queues/rx-0/rps_cpus"]:
        try:
            with open(f, 'r') as fp:
                setting = fp.read().strip()
                # CPU mask will be zeros and commas
                if setting.replace("0", "").replace(",", ""):
                    raise KsftSkipEx(f"RPS/RFS is configured: {f}: {setting}")
        except FileNotFoundError:
            pass

    # 1 is the default, if someone changed it we probably shouldn"t mess with it
    af = cmd("cat /proc/sys/net/ipv6/auto_flowlabels", host=cfg.remote).stdout
    if af.strip() != "1":
        raise KsftSkipEx("Remote does not have auto_flowlabels enabled")


def _ethtool_get_cfg(cfg, fl_type):
    descr = cmd(f"ethtool -n {cfg.ifname} rx-flow-hash {fl_type}").stdout

    converter = {
        "IP SA": "s",
        "IP DA": "d",
        "L3 proto": "t",
        "L4 bytes 0 & 1 [TCP/UDP src port]": "f",
        "L4 bytes 2 & 3 [TCP/UDP dst port]": "n",
        "IPv6 Flow Label": "l",
    }

    ret = ""
    for line in descr.split("\n")[1:-2]:
        # if this raises we probably need to add more keys to converter above
        ret += converter[line]
    return ret


def _traffic(cfg, one_sock, one_cpu):
    local_port  = rand_port(socket.SOCK_DGRAM)
    remote_port = rand_port(socket.SOCK_DGRAM)

    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    sock.bind(("", local_port))
    sock.connect((cfg.remote_addr_v["6"], 0))
    if one_sock:
        send = f"exec 5<>/dev/udp/{cfg.addr_v['6']}/{local_port}; " \
                "for i in `seq 20`; do echo a >&5; sleep 0.02; done; exec 5>&-"
    else:
        send = "for i in `seq 20`; do echo a | socat -t0.02 - UDP6:" \
              f"[{cfg.addr_v['6']}]:{local_port},sourceport={remote_port}; done"

    cpus = set()
    with bkg(send, shell=True, host=cfg.remote, exit_wait=True):
        for _ in range(20):
            fd_read_timeout(sock.fileno(), 1)
            cpu = sock.getsockopt(socket.SOL_SOCKET, socket.SO_INCOMING_CPU)
            cpus.add(cpu)

    if one_cpu:
        ksft_eq(len(cpus), 1,
                f"{one_sock=} - expected one CPU, got traffic on: {cpus=}")
    else:
        ksft_ge(len(cpus), 2,
                f"{one_sock=} - expected many CPUs, got traffic on: {cpus=}")


def test_rss_flow_label(cfg):
    """
    Test hashing on IPv6 flow label. Send traffic over a single socket
    and over multiple sockets. Depend on the remote having auto-label
    enabled so that it randomizes the label per socket.
    """

    cfg.require_ipver("6")
    cfg.require_cmd("socat", remote=True)
    _check_system(cfg)

    # Enable flow label hashing for UDP6
    initial = _ethtool_get_cfg(cfg, "udp6")
    no_lbl = initial.replace("l", "")
    if "l" not in initial:
        try:
            cmd(f"ethtool -N {cfg.ifname} rx-flow-hash udp6 l{no_lbl}")
        except CmdExitFailure as exc:
            raise KsftSkipEx("Device doesn't support Flow Label for UDP6") from exc

        defer(cmd, f"ethtool -N {cfg.ifname} rx-flow-hash udp6 {initial}")

    _traffic(cfg, one_sock=True, one_cpu=True)
    _traffic(cfg, one_sock=False, one_cpu=False)

    # Disable it, we should see no hashing (reset was already defer()ed)
    cmd(f"ethtool -N {cfg.ifname} rx-flow-hash udp6 {no_lbl}")

    _traffic(cfg, one_sock=False, one_cpu=True)


def _check_v4_flow_types(cfg):
    for fl_type in ["tcp4", "udp4", "ah4", "esp4", "sctp4"]:
        try:
            cur = cmd(f"ethtool -n {cfg.ifname} rx-flow-hash {fl_type}").stdout
            ksft_not_in("Flow Label", cur,
                        comment=f"{fl_type=} has Flow Label:" + cur)
        except CmdExitFailure:
            # Probably does not support this flow type
            pass


def test_rss_flow_label_6only(cfg):
    """
    Test interactions with IPv4 flow types. It should not be possible to set
    IPv6 Flow Label hashing for an IPv4 flow type. The Flow Label should also
    not appear in the IPv4 "current config".
    """

    with ksft_raises(CmdExitFailure) as cm:
        cmd(f"ethtool -N {cfg.ifname} rx-flow-hash tcp4 sdfnl")
    ksft_in("Invalid argument", cm.exception.cmd.stderr)

    _check_v4_flow_types(cfg)

    # Try to enable Flow Labels and check again, in case it leaks thru
    initial = _ethtool_get_cfg(cfg, "udp6")
    changed = initial.replace("l", "") if "l" in initial else initial + "l"

    cmd(f"ethtool -N {cfg.ifname} rx-flow-hash udp6 {changed}")
    restore = defer(cmd, f"ethtool -N {cfg.ifname} rx-flow-hash udp6 {initial}")

    _check_v4_flow_types(cfg)
    restore.exec()
    _check_v4_flow_types(cfg)


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        ksft_run([test_rss_flow_label,
                  test_rss_flow_label_6only],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
