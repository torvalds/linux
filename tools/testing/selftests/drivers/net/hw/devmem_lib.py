# SPDX-License-Identifier: GPL-2.0
# pylint: disable=invalid-name,too-many-arguments
"""Shared helpers for devmem TCP selftests."""

import os
import re

from lib.py import (bkg, cmd, defer, ethtool, rand_port, wait_port_listen,
                    ksft_eq, KsftSkipEx, NetNSEnter, EthtoolFamily,
                    NetdevFamily)


RX_PAGE_SIZE_DEFAULT = 0
RX_PAGE_SIZE_16K = 16384

PROBE_RX_PAGE_SIZES = (RX_PAGE_SIZE_DEFAULT, RX_PAGE_SIZE_16K)

NR_HUGEPAGES_FILE = "/proc/sys/vm/nr_hugepages"


def _is_aligned(value, alignment):
    """Equivalent of the kernel IS_ALIGNED(value, alignment).

    alignment must be a power of two.
    """
    return (value & (alignment - 1)) == 0


def _restore_nr_hugepages(nr_hugepages):
    with open(NR_HUGEPAGES_FILE, 'w', encoding='utf-8') as f:
        f.write(str(nr_hugepages))


def _reserve_hugepages(want=64):
    """Raise nr_hugepages to @want and arrange for it to be restored."""
    with open(NR_HUGEPAGES_FILE, 'r+', encoding='utf-8') as f:
        nr_hugepages = int(f.read().strip())
        if nr_hugepages >= want:
            return
        f.seek(0)
        f.write(str(want))
    defer(_restore_nr_hugepages, nr_hugepages)


def _probe_devmem(cfg, rx_page_size):
    """Return True if ncdevmem can bind cfg.ifname at @rx_page_size."""
    probe_command = f"{cfg.bin_local} -f {cfg.ifname}"
    if rx_page_size != RX_PAGE_SIZE_DEFAULT:
        probe_command += f" -b {rx_page_size}"
    return cmd(probe_command, fail=False, shell=True).ret == 0


def require_devmem(cfg, rx_page_size=RX_PAGE_SIZE_DEFAULT):
    """Probe ncdevmem on cfg.ifname and SKIP the test if devmem isn't supported."""
    if rx_page_size not in PROBE_RX_PAGE_SIZES:
        raise RuntimeError(
            f"rx-page-size={rx_page_size} is missing from "
            f"PROBE_RX_PAGE_SIZES, so it was never probed.")

    if not hasattr(cfg, "devmem_supported"):
        _reserve_hugepages()
        # Probe every size upfront: in nk tests a leased queue may land in
        # ncdevmem's queue range and cause the probe to fail.
        cfg.devmem_supported = {size: _probe_devmem(cfg, size)
                                for size in PROBE_RX_PAGE_SIZES}

    if not cfg.devmem_supported[RX_PAGE_SIZE_DEFAULT]:
        raise KsftSkipEx("Test requires devmem support")

    if rx_page_size != RX_PAGE_SIZE_DEFAULT:
        page_size = os.sysconf("SC_PAGE_SIZE")
        if not _is_aligned(rx_page_size, page_size):
            raise KsftSkipEx(
                f"rx-page-size={rx_page_size} is invalid for this platform "
                f"(must be a multiple of PAGE_SIZE={page_size})")

        if not cfg.devmem_supported[rx_page_size]:
            raise KsftSkipEx(
                f"Test requires devmem rx-page-size={rx_page_size} support")


def configure_nic(cfg):
    """Channels, rings, RSS, queue lease for netkit devmem."""
    if not hasattr(cfg, "devmem_supported"):
        raise RuntimeError(
            "require_devmem() must be called before configure_nic(), which "
            "may lease a queue away and make later probes fail.")

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

    ethnl.rings_set({'header': {'dev-index': cfg.ifindex},
                     'tcp-data-split': 'enabled',
                     'hds-thresh': 0,
                     'rx': min(64, orig_rx_rings)})
    defer(ethnl.rings_set, {'header': {'dev-index': cfg.ifindex},
                            'tcp-data-split': 'unknown',
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


def ncdevmem_rx(cfg, port, verify=True, fail_on_linear=False, flow_steer=False,
                rx_page_size=RX_PAGE_SIZE_DEFAULT):
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
    if rx_page_size != RX_PAGE_SIZE_DEFAULT:
        extras.append(f"-b {rx_page_size}")

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


def run_rx_large_niov(cfg):
    """Run the devmem RX test with a large niov (rx-page-size > PAGE_SIZE).

    Sweep payload sizes that straddle the niov boundary: below, equal to,
    and above rx_page_size, to exercise sub-niov, exact-niov, and multi-niov
    RX paths.
    """
    require_devmem(cfg, rx_page_size=RX_PAGE_SIZE_16K)
    _reserve_hugepages()
    configure_nic(cfg)
    netns = getattr(cfg, "netns", None)

    for size in [1024, 4096, 8192, 16384, 32768, 65536]:
        port = rand_port()
        socat = socat_send(cfg, port)
        listen_cmd = ncdevmem_rx(cfg, port,
                                 flow_steer=not netns,
                                 rx_page_size=RX_PAGE_SIZE_16K)
        data_pipe = (f"yes $(echo -e \x01\x02\x03\x04\x05\x06) | "
                     f"head -c {size} | {socat}")
        with bkg(listen_cmd, exit_wait=True, ns=netns) as ncdevmem:
            wait_port_listen(port, proto="tcp", ns=netns)
            cmd(data_pipe, host=cfg.remote, shell=True)
        ksft_eq(ncdevmem.ret, 0,
                f"large-niov failed for payload size {size}")


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
