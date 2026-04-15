#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests for XDP metadata kfuncs (e.g. bpf_xdp_metadata_rx_hash).

These tests load device-bound XDP programs from xdp_metadata.bpf.o
that call metadata kfuncs, send traffic, and verify the extracted
metadata via BPF maps.
"""
from lib.py import ksft_run, ksft_eq, ksft_exit, ksft_ge, ksft_ne, ksft_pr
from lib.py import KsftNamedVariant, ksft_variants
from lib.py import CmdExitFailure, KsftSkipEx, NetDrvEpEnv
from lib.py import NetdevFamily
from lib.py import bkg, cmd, rand_port, wait_port_listen
from lib.py import ip, bpftool, defer
from lib.py import bpf_map_set, bpf_map_dump, bpf_prog_map_ids


def _load_xdp_metadata_prog(cfg, prog_name, bpf_file="xdp_metadata.bpf.o"):
    """Load a device-bound XDP metadata program and return prog/map info.

    Returns:
        dict with 'id', 'name', and 'maps' (name -> map_id).
    """
    abs_path = cfg.net_lib_dir / bpf_file
    pin_dir = "/sys/fs/bpf/xdp_metadata_test"

    cmd(f"rm -rf {pin_dir}", shell=True, fail=False)
    cmd(f"mkdir -p {pin_dir}", shell=True)

    try:
        bpftool(f"prog loadall {abs_path} {pin_dir} type xdp "
                f"xdpmeta_dev {cfg.ifname}")
    except CmdExitFailure as e:
        cmd(f"rm -rf {pin_dir}", shell=True, fail=False)
        raise KsftSkipEx(
            f"Failed to load device-bound XDP program '{prog_name}'"
        ) from e
    defer(cmd, f"rm -rf {pin_dir}", shell=True, fail=False)

    pin_path = f"{pin_dir}/{prog_name}"
    ip(f"link set dev {cfg.ifname} xdpdrv pinned {pin_path}")
    defer(ip, f"link set dev {cfg.ifname} xdpdrv off")

    xdp_info = ip(f"-d link show dev {cfg.ifname}", json=True)[0]
    prog_id = xdp_info["xdp"]["prog"]["id"]

    return {"id": prog_id,
            "name": xdp_info["xdp"]["prog"]["name"],
            "maps": bpf_prog_map_ids(prog_id)}


def _send_probe(cfg, port, proto="tcp"):
    """Send a single payload from the remote end using socat.

    Args:
        cfg: Configuration object containing network settings.
        port: Port number for the exchange.
        proto: Protocol to use, either "tcp" or "udp".
    """
    cfg.require_cmd("socat", remote=True)

    if proto == "tcp":
        rx_cmd = f"socat -{cfg.addr_ipver} -T 2 TCP-LISTEN:{port},reuseport STDOUT"
        tx_cmd = f"echo -n rss_hash_test | socat -t 2 -u STDIN TCP:{cfg.baddr}:{port}"
    else:
        rx_cmd = f"socat -{cfg.addr_ipver} -T 2 -u UDP-RECV:{port},reuseport STDOUT"
        tx_cmd = f"echo -n rss_hash_test | socat -t 2 -u STDIN UDP:{cfg.baddr}:{port}"

    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(port, proto=proto)
        cmd(tx_cmd, host=cfg.remote, shell=True)


# BPF map keys matching the enums in xdp_metadata.bpf.c
_SETUP_KEY_PORT = 1

_RSS_KEY_HASH = 0
_RSS_KEY_TYPE = 1
_RSS_KEY_PKT_CNT = 2
_RSS_KEY_ERR_CNT = 3

XDP_RSS_L4 = 0x8  # BIT(3) from enum xdp_rss_hash_type


@ksft_variants([
    KsftNamedVariant("tcp", "tcp"),
    KsftNamedVariant("udp", "udp"),
])
def test_xdp_rss_hash(cfg, proto):
    """Test RSS hash metadata extraction via bpf_xdp_metadata_rx_hash().

    This test will only run on devices that support xdp-rx-metadata-features.

    Loads the xdp_rss_hash program from xdp_metadata, sends a packet using
    the specified protocol, and verifies that the program extracted a non-zero
    hash with an L4 hash type.
    """
    dev_info = cfg.netnl.dev_get({"ifindex": cfg.ifindex})
    rx_meta = dev_info.get("xdp-rx-metadata-features", [])
    if "hash" not in rx_meta:
        raise KsftSkipEx("device does not support XDP rx hash metadata")

    prog_info = _load_xdp_metadata_prog(cfg, "xdp_rss_hash")

    port = rand_port()
    bpf_map_set("map_xdp_setup", _SETUP_KEY_PORT, port)

    rss_map_id = prog_info["maps"]["map_rss"]

    _send_probe(cfg, port, proto=proto)

    rss = bpf_map_dump(rss_map_id)

    pkt_cnt = rss.get(_RSS_KEY_PKT_CNT, 0)
    err_cnt = rss.get(_RSS_KEY_ERR_CNT, 0)
    hash_val = rss.get(_RSS_KEY_HASH, 0)
    hash_type = rss.get(_RSS_KEY_TYPE, 0)

    ksft_ge(pkt_cnt, 1, comment="should have received at least one packet")
    ksft_eq(err_cnt, 0, comment=f"RSS hash error count: {err_cnt}")

    ksft_ne(hash_val, 0,
            f"RSS hash should be non-zero for {proto.upper()} traffic")
    ksft_pr(f"  RSS hash: {hash_val:#010x}")

    ksft_pr(f"  RSS hash type: {hash_type:#06x}")
    ksft_ne(hash_type & XDP_RSS_L4, 0,
            f"RSS hash type should include L4 for {proto.upper()} traffic")


def main():
    """Run XDP metadata kfunc tests against a real device."""
    with NetDrvEpEnv(__file__) as cfg:
        cfg.netnl = NetdevFamily()
        ksft_run(
            [
                test_xdp_rss_hash,
            ],
            args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
