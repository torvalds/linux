#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
import time
import threading
from os import path
from lib.py import (
    ksft_run,
    ksft_exit,
    ksft_eq,
    ksft_in,
    ksft_not_in,
    ksft_raises,
)
from lib.py import (
    NetDrvContEnv,
    NetNSEnter,
    EthtoolFamily,
    NetdevFamily,
)
from lib.py import (
    bkg,
    cmd,
    defer,
    ethtool,
    ip,
    rand_port,
    wait_port_listen,
)
from lib.py import KsftSkipEx, CmdExitFailure


def set_flow_rule(cfg):
    output = ethtool(
        f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} action {cfg.src_queue}"
    ).stdout
    values = re.search(r"ID (\d+)", output).group(1)
    return int(values)


def test_iou_zcrx(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.nk_guest_ipv6} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)
        cmd(tx_cmd, host=cfg.remote)


def test_attrs(cfg) -> None:
    cfg.require_ipver("6")
    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )

    ksft_eq(queue_info["id"], cfg.src_queue)
    ksft_eq(queue_info["type"], "rx")
    ksft_eq(queue_info["ifindex"], cfg.ifindex)

    ksft_in("lease", queue_info)
    lease = queue_info["lease"]
    ksft_eq(lease["ifindex"], cfg.nk_guest_ifindex)
    ksft_eq(lease["queue"]["id"], cfg.nk_queue)
    ksft_eq(lease["queue"]["type"], "rx")
    ksft_in("netns-id", lease)


def test_attach_xdp_with_mp(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    netdevnl = NetdevFamily()

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    with bkg(rx_cmd):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

        time.sleep(0.1)
        queue_info = netdevnl.queue_get(
            {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
        )
        ksft_in("io-uring", queue_info)

        prog = cfg.net_lib_dir / "xdp_dummy.bpf.o"
        with ksft_raises(CmdExitFailure):
            ip(f"link set dev {cfg.ifname} xdp obj {prog} sec xdp.frags")

    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


def test_destroy(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    rx_proc = cmd(rx_cmd, background=True)
    wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_in("io-uring", queue_info)

    # ip link del will wait for all refs to drop first, but iou-zcrx is holding
    # onto a ref. Terminate iou-zcrx async via a thread after a delay.
    kill_timer = threading.Timer(1, rx_proc.proc.terminate)
    kill_timer.start()

    ip(f"link del dev {cfg._nk_host_ifname}")
    kill_timer.join()
    cfg._nk_host_ifname = None
    cfg._nk_guest_ifname = None

    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)

    cmd(f"tc filter del dev {cfg.ifname} ingress pref {cfg._bpf_prog_pref}")
    cfg._tc_attached = False

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.src_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)
    # Short delay since iou cleanup is async and takes a bit of time.
    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


def main() -> None:
    with NetDrvContEnv(__file__, rxqueues=2) as cfg:
        cfg.bin_local = path.abspath(
            path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx"
        )
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)
        cfg.port = rand_port()

        ethnl = EthtoolFamily()
        channels = ethnl.channels_get({"header": {"dev-index": cfg.ifindex}})
        channels = channels["combined-count"]
        if channels < 2:
            raise KsftSkipEx("Test requires NETIF with at least 2 combined channels")

        cfg.src_queue = channels - 1

        with NetNSEnter(str(cfg.netns)):
            netdevnl = NetdevFamily()
            bind_result = netdevnl.queue_create(
                {
                    "ifindex": cfg.nk_guest_ifindex,
                    "type": "rx",
                    "lease": {
                        "ifindex": cfg.ifindex,
                        "queue": {"id": cfg.src_queue, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
            cfg.nk_queue = bind_result["id"]

        # test_destroy must be last because it destroys the netkit devices
        ksft_run(
            [test_iou_zcrx, test_attrs, test_attach_xdp_with_mp, test_destroy],
            args=(cfg,),
        )
    ksft_exit()


if __name__ == "__main__":
    main()
