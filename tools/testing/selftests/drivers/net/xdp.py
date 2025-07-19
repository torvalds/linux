#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
This file contains tests to verify native XDP support in network drivers.
The tests utilize the BPF program `xdp_native.bpf.o` from the `selftests.net.lib`
directory, with each test focusing on a specific aspect of XDP functionality.
"""
import random
import string
from dataclasses import dataclass
from enum import Enum

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ne
from lib.py import KsftFailEx, NetDrvEpEnv
from lib.py import bkg, cmd, rand_port
from lib.py import ip, bpftool, defer


class TestConfig(Enum):
    """Enum for XDP configuration options."""
    MODE = 0  # Configures the BPF program for a specific test
    PORT = 1  # Port configuration to communicate with the remote host


class XDPAction(Enum):
    """Enum for XDP actions."""
    PASS = 0  # Pass the packet up to the stack
    DROP = 1  # Drop the packet
    TX = 2    # Route the packet to the remote host


class XDPStats(Enum):
    """Enum for XDP statistics."""
    RX = 0    # Count of valid packets received for testing
    PASS = 1  # Count of packets passed up to the stack
    DROP = 2  # Count of packets dropped
    TX = 3    # Count of incoming packets routed to the remote host


@dataclass
class BPFProgInfo:
    """Data class to store information about a BPF program."""
    name: str               # Name of the BPF program
    file: str               # BPF program object file
    xdp_sec: str = "xdp"    # XDP section name (e.g., "xdp" or "xdp.frags")
    mtu: int = 1500         # Maximum Transmission Unit, default is 1500


def _exchg_udp(cfg, port, test_string):
    """
    Exchanges UDP packets between a local and remote host using the socat tool.

    Args:
        cfg: Configuration object containing network settings.
        port: Port number to use for the UDP communication.
        test_string: String that the remote host will send.

    Returns:
        The string received by the test host.
    """
    cfg.require_cmd("socat", remote=True)

    rx_udp_cmd = f"socat -{cfg.addr_ipver} -T 2 -u UDP-RECV:{port},reuseport STDOUT"
    tx_udp_cmd = f"echo -n {test_string} | socat -t 2 -u STDIN UDP:{cfg.baddr}:{port}"

    with bkg(rx_udp_cmd, exit_wait=True) as nc:
        cmd(tx_udp_cmd, host=cfg.remote, shell=True)

    return nc.stdout.strip()


def _test_udp(cfg, port, size=256):
    """
    Tests UDP packet exchange between a local and remote host.

    Args:
        cfg: Configuration object containing network settings.
        port: Port number to use for the UDP communication.
        size: The length of the test string to be exchanged, default is 256 characters.

    Returns:
        bool: True if the received string matches the sent string, False otherwise.
    """
    test_str = "".join(random.choice(string.ascii_lowercase) for _ in range(size))
    recvd_str = _exchg_udp(cfg, port, test_str)

    return recvd_str == test_str


def _load_xdp_prog(cfg, bpf_info):
    """
    Loads an XDP program onto a network interface.

    Args:
        cfg: Configuration object containing network settings.
        bpf_info: BPFProgInfo object containing information about the BPF program.

    Returns:
        dict: A dictionary containing the XDP program ID, name, and associated map IDs.
    """
    abs_path = cfg.net_lib_dir / bpf_info.file
    prog_info = {}

    cmd(f"ip link set dev {cfg.remote_ifname} mtu {bpf_info.mtu}", shell=True, host=cfg.remote)
    defer(ip, f"link set dev {cfg.remote_ifname} mtu 1500", host=cfg.remote)

    cmd(
    f"ip link set dev {cfg.ifname} mtu {bpf_info.mtu} xdp obj {abs_path} sec {bpf_info.xdp_sec}",
    shell=True
    )
    defer(ip, f"link set dev {cfg.ifname} mtu 1500 xdp off")

    xdp_info = ip(f"-d link show dev {cfg.ifname}", json=True)[0]
    prog_info["id"] = xdp_info["xdp"]["prog"]["id"]
    prog_info["name"] = xdp_info["xdp"]["prog"]["name"]
    prog_id = prog_info["id"]

    map_ids = bpftool(f"prog show id {prog_id}", json=True)["map_ids"]
    prog_info["maps"] = {}
    for map_id in map_ids:
        name = bpftool(f"map show id {map_id}", json=True)["name"]
        prog_info["maps"][name] = map_id

    return prog_info


def format_hex_bytes(value):
    """
    Helper function that converts an integer into a formatted hexadecimal byte string.

    Args:
        value: An integer representing the number to be converted.

    Returns:
        A string representing hexadecimal equivalent of value, with bytes separated by spaces.
    """
    hex_str = value.to_bytes(4, byteorder='little', signed=True)
    return ' '.join(f'{byte:02x}' for byte in hex_str)


def _set_xdp_map(map_name, key, value):
    """
    Updates an XDP map with a given key-value pair using bpftool.

    Args:
        map_name: The name of the XDP map to update.
        key: The key to update in the map, formatted as a hexadecimal string.
        value: The value to associate with the key, formatted as a hexadecimal string.
    """
    key_formatted = format_hex_bytes(key)
    value_formatted = format_hex_bytes(value)
    bpftool(
        f"map update name {map_name} key hex {key_formatted} value hex {value_formatted}"
    )


def _get_stats(xdp_map_id):
    """
    Retrieves and formats statistics from an XDP map.

    Args:
        xdp_map_id: The ID of the XDP map from which to retrieve statistics.

    Returns:
        A dictionary containing formatted packet statistics for various XDP actions.
        The keys are based on the XDPStats Enum values.

    Raises:
        KsftFailEx: If the stats retrieval fails.
    """
    stats_dump = bpftool(f"map dump id {xdp_map_id}", json=True)
    if not stats_dump:
        raise KsftFailEx(f"Failed to get stats for map {xdp_map_id}")

    stats_formatted = {}
    for key in range(0, 4):
        val = stats_dump[key]["formatted"]["value"]
        if stats_dump[key]["formatted"]["key"] == XDPStats.RX.value:
            stats_formatted[XDPStats.RX.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.PASS.value:
            stats_formatted[XDPStats.PASS.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.DROP.value:
            stats_formatted[XDPStats.DROP.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.TX.value:
            stats_formatted[XDPStats.TX.value] = val

    return stats_formatted


def _test_pass(cfg, bpf_info, msg_sz):
    """
    Tests the XDP_PASS action by exchanging UDP packets.

    Args:
        cfg: Configuration object containing network settings.
        bpf_info: BPFProgInfo object containing information about the BPF program.
        msg_sz: Size of the test message to send.
    """

    prog_info = _load_xdp_prog(cfg, bpf_info)
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.PASS.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    ksft_eq(_test_udp(cfg, port, msg_sz), True, "UDP packet exchange failed")
    stats = _get_stats(prog_info["maps"]["map_xdp_stats"])

    ksft_ne(stats[XDPStats.RX.value], 0, "RX stats should not be zero")
    ksft_eq(stats[XDPStats.RX.value], stats[XDPStats.PASS.value], "RX and PASS stats mismatch")


def test_xdp_native_pass_sb(cfg):
    """
    Tests the XDP_PASS action for single buffer case.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog", "xdp_native.bpf.o", "xdp", 1500)

    _test_pass(cfg, bpf_info, 256)


def test_xdp_native_pass_mb(cfg):
    """
    Tests the XDP_PASS action for a multi-buff size.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog_frags", "xdp_native.bpf.o", "xdp.frags", 9000)

    _test_pass(cfg, bpf_info, 8000)


def _test_drop(cfg, bpf_info, msg_sz):
    """
    Tests the XDP_DROP action by exchanging UDP packets.

    Args:
        cfg: Configuration object containing network settings.
        bpf_info: BPFProgInfo object containing information about the BPF program.
        msg_sz: Size of the test message to send.
    """

    prog_info = _load_xdp_prog(cfg, bpf_info)
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.DROP.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    ksft_eq(_test_udp(cfg, port, msg_sz), False, "UDP packet exchange should fail")
    stats = _get_stats(prog_info["maps"]["map_xdp_stats"])

    ksft_ne(stats[XDPStats.RX.value], 0, "RX stats should be zero")
    ksft_eq(stats[XDPStats.RX.value], stats[XDPStats.DROP.value], "RX and DROP stats mismatch")


def test_xdp_native_drop_sb(cfg):
    """
    Tests the XDP_DROP action for a signle-buff case.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog", "xdp_native.bpf.o", "xdp", 1500)

    _test_drop(cfg, bpf_info, 256)


def test_xdp_native_drop_mb(cfg):
    """
    Tests the XDP_DROP action for a multi-buff case.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog_frags", "xdp_native.bpf.o", "xdp.frags", 9000)

    _test_drop(cfg, bpf_info, 8000)


def test_xdp_native_tx_mb(cfg):
    """
    Tests the XDP_TX action for a multi-buff case.

    Args:
        cfg: Configuration object containing network settings.
    """
    cfg.require_cmd("socat", remote=True)

    bpf_info = BPFProgInfo("xdp_prog_frags", "xdp_native.bpf.o", "xdp.frags", 9000)
    prog_info = _load_xdp_prog(cfg, bpf_info)
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.TX.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    test_string = ''.join(random.choice(string.ascii_lowercase) for _ in range(8000))
    rx_udp = f"socat -{cfg.addr_ipver} -T 2 -u UDP-RECV:{port},reuseport STDOUT"
    tx_udp = f"echo {test_string} | socat -t 2 -u STDIN UDP:{cfg.baddr}:{port}"

    with bkg(rx_udp, host=cfg.remote, exit_wait=True) as rnc:
        cmd(tx_udp, host=cfg.remote, shell=True)

    stats = _get_stats(prog_info['maps']['map_xdp_stats'])

    ksft_eq(rnc.stdout.strip(), test_string, "UDP packet exchange failed")
    ksft_eq(stats[XDPStats.TX.value], 1, "TX stats mismatch")


def main():
    """
    Main function to execute the XDP tests.

    This function runs a series of tests to validate the XDP support for
    both the single and multi-buffer. It uses the NetDrvEpEnv context
    manager to manage the network driver environment and the ksft_run
    function to execute the tests.
    """
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(
            [
                test_xdp_native_pass_sb,
                test_xdp_native_pass_mb,
                test_xdp_native_drop_sb,
                test_xdp_native_drop_mb,
                test_xdp_native_tx_mb,
            ],
            args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
