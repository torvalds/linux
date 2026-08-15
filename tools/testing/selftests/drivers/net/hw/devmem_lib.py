# SPDX-License-Identifier: GPL-2.0
"""Shared helpers for devmem TCP selftests."""

import re

from lib.py import (bkg, cmd, defer, ethtool, rand_port, wait_port_listen,
                    ksft_eq, KsftSkipEx, NetNSEnter, EthtoolFamily,
                    NetdevFamily)


def require_devmem(cfg):
    """Probe ncdevmem on cfg.ifname and SKIP the test if devmem isn't supported."""
    if not hasattr(cfg, "devmem_probed"):
        probe_command = f"{cfg.bin_local} -f {cfg.ifname}"
        cfg.devmem_supported = cmd(probe_command, fail=False, shell=True).ret == 0
        cfg.devmem_probed = True

    if not cfg.devmem_supported:
        raise KsftSkipEx("Test requires devmem support")


def configure_nic(cfg):
    """Channels, rings, RSS, queue lease for netkit devmem."""
    if not hasattr(cfg, 'netns'):
        return

    cfg.require_ipver('6')
    ethnl = EthtoolFamily()

    channels = ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    channels = channels['combined-count']
    if channels < 2:
        raise KsftSkipEx(
            'Test requires NETIF with at least 2 combined channels'
        )

    rings = ethnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    orig_rx_rings = rings['rx']
    orig_hds_thresh = rings.get('hds-thresh', 0)
    orig_data_split = rings.get('tcp-data-split', 'unknown')

    ethnl.rings_set({'header': {'dev-index': cfg.ifindex},
                     'tcp-data-split': 'enabled',
                     'hds-thresh': 0,
                     'rx': min(64, orig_rx_rings)})
    defer(ethnl.rings_set, {'header': {'dev-index': cfg.ifindex},
                            'tcp-data-split': orig_data_split,
                            'hds-thresh': orig_hds_thresh,
                            'rx': orig_rx_rings})

    cfg.src_queue = channels - 1
    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    if not hasattr(cfg, 'nk_queue'):
        with NetNSEnter(str(cfg.netns)):
            netdevnl = NetdevFamily()
            lease_result = netdevnl.queue_create({
                "ifindex": cfg.nk_guest_ifindex,
                "type": "rx",
                "lease": {
                    "ifindex": cfg.ifindex,
                    "queue": {"id": cfg.src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            })
            cfg.nk_queue = lease_result['id']


def set_flow_rule(cfg, port):
    """Install a flow rule steering to src_queue and return the flow rule ID."""
    output = ethtool(
        f"-N {cfg.ifname} flow-type tcp6 dst-port {port}"
        f" action {cfg.src_queue}"
    ).stdout
    return int(re.search(r'ID (\d+)', output).group(1))


def ncdevmem_rx(cfg, port, verify=True, fail_on_linear=False, flow_steer=False):
    """Build the ncdevmem RX listener command."""
    if hasattr(cfg, 'netns'):
        flow_rule_id = set_flow_rule(cfg, port)
        defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

        ifname = cfg.nk_guest_ifname
        addr = cfg.nk_guest_ipv6
        extras = [f"-t {cfg.nk_queue}", "-q 1", "-n"]
    else:
        ifname = cfg.ifname
        addr = cfg.addr
        extras = []
        if flow_steer:
            extras.append(f"-c {cfg.remote_addr}")

    if verify:
        extras.append("-v 7")
    if fail_on_linear:
        extras.append("-L")

    parts = [cfg.bin_local, "-l", f"-f {ifname}", f"-s {addr}",
             f"-p {port}", *extras]
    return " ".join(parts)


def ncdevmem_tx(cfg, port, chunk_size=0):
    """Build the ncdevmem TX send command."""
    if hasattr(cfg, 'netns'):
        ifname = cfg.nk_guest_ifname
        addr = cfg.remote_addr_v['6']
        extras = ["-t 0", "-q 1", "-n"]
    else:
        ifname = cfg.ifname
        addr = cfg.remote_addr
        extras = []

    if chunk_size:
        extras.append(f"-z {chunk_size}")

    parts = [cfg.bin_local, f"-f {ifname}", f"-s {addr}",
             f"-p {port}", *extras]
    return " ".join(parts)


def socat_send(cfg, port, buf_size=0):
    """Socat command for sending to the devmem listener.

    When buf_size > 0, force one TCP segment per write of exactly that size by
    setting socat's buffer (-b) and disabling Nagle (TCP_NODELAY).
    """
    proto = f"TCP{cfg.addr_ipver}"

    if hasattr(cfg, 'netns'):
        addr = f"[{cfg.nk_guest_ipv6}]"
    else:
        addr = cfg.baddr

    suffix = f",bind={cfg.remote_baddr}:{port}"

    buf = ""
    if buf_size:
        buf = f"-b {buf_size}"
        suffix += ",nodelay"

    return f"socat {buf} -u - {proto}:{addr}:{port}{suffix}"


def socat_listen(cfg, port):
    """Socat listen command for TX tests."""
    return f"socat -U - TCP{cfg.addr_ipver}-LISTEN:{port}"


def setup_test(cfg, bin_local):
    """Stash the local ncdevmem path on cfg and deploy it to the remote."""
    cfg.bin_local = bin_local
    cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)


def run_rx(cfg):
    """Run the devmem RX test."""
    require_devmem(cfg)
    configure_nic(cfg)
    port = rand_port()
    socat = socat_send(cfg, port)
    data_pipe = (f"yes $(echo -e \x01\x02\x03\x04\x05\x06) | head -c 1K"
                 f" | {socat}")
    netns = getattr(cfg, "netns", None)

    listen_cmd = ncdevmem_rx(cfg, port, flow_steer=not hasattr(cfg, 'netns'))
    with bkg(listen_cmd, exit_wait=True, ns=netns) as ncdevmem:
        wait_port_listen(port, proto="tcp", ns=netns)
        cmd(data_pipe, host=cfg.remote, shell=True)
    ksft_eq(ncdevmem.ret, 0)


def run_tx(cfg):
    """Run the devmem TX test."""
    require_devmem(cfg)
    configure_nic(cfg)
    netns = getattr(cfg, "netns", None)
    port = rand_port()
    tx_cmd = ncdevmem_tx(cfg, port)
    listen_cmd = socat_listen(cfg, port)

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as socat:
        wait_port_listen(port, host=cfg.remote)
        cmd(f"bash -c 'echo -e \"hello\\nworld\" | {tx_cmd}'", ns=netns, shell=True)
    ksft_eq(socat.stdout.strip(), "hello\nworld")


def run_tx_chunks(cfg):
    """Run the devmem TX chunking test."""
    require_devmem(cfg)
    configure_nic(cfg)
    netns = getattr(cfg, "netns", None)
    port = rand_port()
    tx_cmd = ncdevmem_tx(cfg, port, chunk_size=3)
    listen_cmd = socat_listen(cfg, port)

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as socat:
        wait_port_listen(port, host=cfg.remote)
        cmd(f"bash -c 'echo -e \"hello\\nworld\" | {tx_cmd}'", ns=netns, shell=True)
    ksft_eq(socat.stdout.strip(), "hello\nworld")


def run_rx_hds(cfg):
    """Run the HDS test by running devmem RX across a segment size sweep."""
    require_devmem(cfg)
    configure_nic(cfg)
    netns = getattr(cfg, "netns", None)

    for size in [1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]:
        port = rand_port()

        listen_cmd = ncdevmem_rx(cfg, port, verify=False,
                                 fail_on_linear=True)
        socat = socat_send(cfg, port, buf_size=size)

        with bkg(listen_cmd, exit_wait=True, ns=netns) as ncdevmem:
            wait_port_listen(port, proto="tcp", ns=netns)
            cmd(f"dd if=/dev/zero bs={size} count=1 2>/dev/null | "
                f"{socat}", host=cfg.remote, shell=True)
        ksft_eq(ncdevmem.ret, 0, f"HDS failed for payload size {size}")
