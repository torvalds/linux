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

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ge, ksft_ne, ksft_pr
from lib.py import KsftFailEx, NetDrvEpEnv
from lib.py import EthtoolFamily, NetdevFamily, NlError
from lib.py import bkg, cmd, rand_port, wait_port_listen
from lib.py import ip, bpftool, defer


class TestConfig(Enum):
    """Enum for XDP configuration options."""
    MODE = 0  # Configures the BPF program for a specific test
    PORT = 1  # Port configuration to communicate with the remote host
    ADJST_OFFSET = 2  # Tail/Head adjustment offset for extension/shrinking
    ADJST_TAG = 3  # Adjustment tag to annotate the start and end of extension


class XDPAction(Enum):
    """Enum for XDP actions."""
    PASS = 0  # Pass the packet up to the stack
    DROP = 1  # Drop the packet
    TX = 2    # Route the packet to the remote host
    TAIL_ADJST = 3  # Adjust the tail of the packet
    HEAD_ADJST = 4  # Adjust the head of the packet


class XDPStats(Enum):
    """Enum for XDP statistics."""
    RX = 0    # Count of valid packets received for testing
    PASS = 1  # Count of packets passed up to the stack
    DROP = 2  # Count of packets dropped
    TX = 3    # Count of incoming packets routed to the remote host
    ABORT = 4 # Count of packets that were aborted


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
        wait_port_listen(port, proto="udp")
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
    f"ip link set dev {cfg.ifname} mtu {bpf_info.mtu} xdpdrv obj {abs_path} sec {bpf_info.xdp_sec}",
    shell=True
    )
    defer(ip, f"link set dev {cfg.ifname} mtu 1500 xdpdrv off")

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
    for key in range(0, 5):
        val = stats_dump[key]["formatted"]["value"]
        if stats_dump[key]["formatted"]["key"] == XDPStats.RX.value:
            stats_formatted[XDPStats.RX.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.PASS.value:
            stats_formatted[XDPStats.PASS.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.DROP.value:
            stats_formatted[XDPStats.DROP.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.TX.value:
            stats_formatted[XDPStats.TX.value] = val
        elif stats_dump[key]["formatted"]["key"] == XDPStats.ABORT.value:
            stats_formatted[XDPStats.ABORT.value] = val

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


def _test_xdp_native_tx(cfg, bpf_info, payload_lens):
    """
    Tests the XDP_TX action.

    Args:
        cfg: Configuration object containing network settings.
        bpf_info: BPFProgInfo object containing the BPF program metadata.
        payload_lens: Array of packet lengths to send.
    """
    cfg.require_cmd("socat", remote=True)
    prog_info = _load_xdp_prog(cfg, bpf_info)
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.TX.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    expected_pkts = 0
    for payload_len in payload_lens:
        test_string = "".join(
            random.choice(string.ascii_lowercase) for _ in range(payload_len)
        )

        rx_udp = f"socat -{cfg.addr_ipver} -T 2 " + \
                 f"-u UDP-RECV:{port},reuseport STDOUT"

        # Writing zero bytes to stdin gets ignored by socat,
        # but with the shut-null flag socat generates a zero sized packet
        # when the socket is closed.
        tx_cmd_suffix = ",shut-null" if payload_len == 0 else ""
        tx_udp = f"echo -n {test_string} | socat -t 2 " + \
                 f"-u STDIN UDP:{cfg.baddr}:{port}{tx_cmd_suffix}"

        with bkg(rx_udp, host=cfg.remote, exit_wait=True) as rnc:
            wait_port_listen(port, proto="udp", host=cfg.remote)
            cmd(tx_udp, host=cfg.remote, shell=True)

        ksft_eq(rnc.stdout.strip(), test_string, "UDP packet exchange failed")

        expected_pkts += 1
        stats = _get_stats(prog_info["maps"]["map_xdp_stats"])
        ksft_eq(stats[XDPStats.RX.value], expected_pkts, "RX stats mismatch")
        ksft_eq(stats[XDPStats.TX.value], expected_pkts, "TX stats mismatch")


def test_xdp_native_tx_sb(cfg):
    """
    Tests the XDP_TX action for a single-buff case.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog", "xdp_native.bpf.o", "xdp", 1500)

    # Ensure there's enough room for an ETH / IP / UDP header
    pkt_hdr_len = 42 if cfg.addr_ipver == "4" else 62

    _test_xdp_native_tx(cfg, bpf_info, [0, 1500 // 2, 1500 - pkt_hdr_len])


def test_xdp_native_tx_mb(cfg):
    """
    Tests the XDP_TX action for a multi-buff case.

    Args:
        cfg: Configuration object containing network settings.
    """
    bpf_info = BPFProgInfo("xdp_prog_frags", "xdp_native.bpf.o",
                           "xdp.frags", 9000)
    # The first packet ensures we exercise the fragmented code path.
    # And the subsequent 0-sized packet ensures the driver
    # reinitializes xdp_buff correctly.
    _test_xdp_native_tx(cfg, bpf_info, [8000, 0])


def _validate_res(res, offset_lst, pkt_sz_lst):
    """
    Validates the result of a test.

    Args:
        res: The result of the test, which should be a dictionary with a "status" key.

    Raises:
        KsftFailEx: If the test fails to pass any combination of offset and packet size.
    """
    if "status" not in res:
        raise KsftFailEx("Missing 'status' key in result dictionary")

    # Validate that not a single case was successful
    if res["status"] == "fail":
        if res["offset"] == offset_lst[0] and res["pkt_sz"] == pkt_sz_lst[0]:
            raise KsftFailEx(f"{res['reason']}")

        # Get the previous offset and packet size to report the successful run
        tmp_idx = offset_lst.index(res["offset"])
        prev_offset = offset_lst[tmp_idx - 1]
        if tmp_idx == 0:
            tmp_idx = pkt_sz_lst.index(res["pkt_sz"])
            prev_pkt_sz = pkt_sz_lst[tmp_idx - 1]
        else:
            prev_pkt_sz = res["pkt_sz"]

        # Use these values for error reporting
        ksft_pr(
        f"Failed run: pkt_sz {res['pkt_sz']}, offset {res['offset']}. "
        f"Last successful run: pkt_sz {prev_pkt_sz}, offset {prev_offset}. "
        f"Reason: {res['reason']}"
        )


def _check_for_failures(recvd_str, stats):
    """
    Checks for common failures while adjusting headroom or tailroom.

    Args:
        recvd_str: The string received from the remote host after sending a test string.
        stats: A dictionary containing formatted packet statistics for various XDP actions.

    Returns:
        str: A string describing the failure reason if a failure is detected, otherwise None.
    """

    # Any adjustment failure result in an abort hence, we track this counter
    if stats[XDPStats.ABORT.value] != 0:
        return "Adjustment failed"

    # Since we are using aggregate stats for a single test across all offsets and packet sizes
    # we can't use RX stats only to track data exchange failure without taking a previous
    # snapshot. An easier way is to simply check for non-zero length of received string.
    if len(recvd_str) == 0:
        return "Data exchange failed"

    # Check for RX and PASS stats mismatch. Ideally, they should be equal for a successful run
    if stats[XDPStats.RX.value] != stats[XDPStats.PASS.value]:
        return "RX stats mismatch"

    return None


def _test_xdp_native_tail_adjst(cfg, pkt_sz_lst, offset_lst):
    """
    Tests the XDP tail adjustment functionality.

    This function loads the appropriate XDP program based on the provided
    program name and configures the XDP map for tail adjustment. It then
    validates the tail adjustment by sending and receiving UDP packets
    with specified packet sizes and offsets.

    Args:
        cfg: Configuration object containing network settings.
        prog: Name of the XDP program to load.
        pkt_sz_lst: List of packet sizes to test.
        offset_lst: List of offsets to validate support for tail adjustment.

    Returns:
        dict: A dictionary with test status and failure details if applicable.
    """
    port = rand_port()
    bpf_info = BPFProgInfo("xdp_prog_frags", "xdp_native.bpf.o", "xdp.frags", 9000)

    prog_info = _load_xdp_prog(cfg, bpf_info)

    # Configure the XDP map for tail adjustment
    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.TAIL_ADJST.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    for offset in offset_lst:
        tag = format(random.randint(65, 90), "02x")

        _set_xdp_map("map_xdp_setup", TestConfig.ADJST_OFFSET.value, offset)
        if offset > 0:
            _set_xdp_map("map_xdp_setup", TestConfig.ADJST_TAG.value, int(tag, 16))

        for pkt_sz in pkt_sz_lst:
            test_str = "".join(random.choice(string.ascii_lowercase) for _ in range(pkt_sz))
            recvd_str = _exchg_udp(cfg, port, test_str)
            stats = _get_stats(prog_info["maps"]["map_xdp_stats"])

            failure = _check_for_failures(recvd_str, stats)
            if failure is not None:
                return {
                    "status": "fail",
                    "reason": failure,
                    "offset": offset,
                    "pkt_sz": pkt_sz,
                }

            # Validate data content based on offset direction
            expected_data = None
            if offset > 0:
                expected_data = test_str + (offset * chr(int(tag, 16)))
            else:
                expected_data = test_str[0:pkt_sz + offset]

            if recvd_str != expected_data:
                return {
                    "status": "fail",
                    "reason": "Data mismatch",
                    "offset": offset,
                    "pkt_sz": pkt_sz,
                }

    return {"status": "pass"}


def test_xdp_native_adjst_tail_grow_data(cfg):
    """
    Tests the XDP tail adjustment by growing packet data.

    Args:
        cfg: Configuration object containing network settings.
    """
    pkt_sz_lst = [512, 1024, 2048]
    offset_lst = [1, 16, 32, 64, 128, 256]
    res = _test_xdp_native_tail_adjst(
        cfg,
        pkt_sz_lst,
        offset_lst,
    )

    _validate_res(res, offset_lst, pkt_sz_lst)


def test_xdp_native_adjst_tail_shrnk_data(cfg):
    """
    Tests the XDP tail adjustment by shrinking packet data.

    Args:
        cfg: Configuration object containing network settings.
    """
    pkt_sz_lst = [512, 1024, 2048]
    offset_lst = [-16, -32, -64, -128, -256]
    res = _test_xdp_native_tail_adjst(
        cfg,
        pkt_sz_lst,
        offset_lst,
    )

    _validate_res(res, offset_lst, pkt_sz_lst)


def get_hds_thresh(cfg):
    """
    Retrieves the header data split (HDS) threshold for a network interface.

    Args:
        cfg: Configuration object containing network settings.

    Returns:
        The HDS threshold value. If the threshold is not supported or an error occurs,
        a default value of 1500 is returned.
    """
    ethnl = cfg.ethnl
    hds_thresh = 1500

    try:
        rings = ethnl.rings_get({'header': {'dev-index': cfg.ifindex}})
        if 'hds-thresh' not in rings:
            ksft_pr(f'hds-thresh not supported. Using default: {hds_thresh}')
            return hds_thresh
        hds_thresh = rings['hds-thresh']
    except NlError as e:
        ksft_pr(f"Failed to get rings: {e}. Using default: {hds_thresh}")

    return hds_thresh


def _test_xdp_native_head_adjst(cfg, prog, pkt_sz_lst, offset_lst):
    """
    Tests the XDP head adjustment action for a multi-buffer case.

    Args:
        cfg: Configuration object containing network settings.
        ethnl: Network namespace or link object (not used in this function).

    This function sets up the packet size and offset lists, then performs
    the head adjustment test by sending and receiving UDP packets.
    """
    cfg.require_cmd("socat", remote=True)

    prog_info = _load_xdp_prog(cfg, BPFProgInfo(prog, "xdp_native.bpf.o", "xdp.frags", 9000))
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, XDPAction.HEAD_ADJST.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    hds_thresh = get_hds_thresh(cfg)
    for offset in offset_lst:
        for pkt_sz in pkt_sz_lst:
            # The "head" buffer must contain at least the Ethernet header
            # after we eat into it. We send large-enough packets, but if HDS
            # is enabled head will only contain headers. Don't try to eat
            # more than 28 bytes (UDPv4 + eth hdr left: (14 + 20 + 8) - 14)
            l2_cut_off = 28 if cfg.addr_ipver == 4 else 48
            if pkt_sz > hds_thresh and offset > l2_cut_off:
                ksft_pr(
                f"Failed run: pkt_sz ({pkt_sz}) > HDS threshold ({hds_thresh}) and "
                f"offset {offset} > {l2_cut_off}"
                )
                return {"status": "pass"}

            test_str = ''.join(random.choice(string.ascii_lowercase) for _ in range(pkt_sz))
            tag = format(random.randint(65, 90), '02x')

            _set_xdp_map("map_xdp_setup",
                     TestConfig.ADJST_OFFSET.value,
                     offset)
            _set_xdp_map("map_xdp_setup", TestConfig.ADJST_TAG.value, int(tag, 16))
            _set_xdp_map("map_xdp_setup", TestConfig.ADJST_OFFSET.value, offset)

            recvd_str = _exchg_udp(cfg, port, test_str)

            # Check for failures around adjustment and data exchange
            failure = _check_for_failures(recvd_str, _get_stats(prog_info['maps']['map_xdp_stats']))
            if failure is not None:
                return {
                    "status": "fail",
                    "reason": failure,
                    "offset": offset,
                    "pkt_sz": pkt_sz
                }

            # Validate data content based on offset direction
            expected_data = None
            if offset < 0:
                expected_data = chr(int(tag, 16)) * (0 - offset) + test_str
            else:
                expected_data = test_str[offset:]

            if recvd_str != expected_data:
                return {
                    "status": "fail",
                    "reason": "Data mismatch",
                    "offset": offset,
                    "pkt_sz": pkt_sz
                }

    return {"status": "pass"}


def test_xdp_native_adjst_head_grow_data(cfg):
    """
    Tests the XDP headroom growth support.

    Args:
        cfg: Configuration object containing network settings.

    This function sets up the packet size and offset lists, then calls the
    _test_xdp_native_head_adjst_mb function to perform the actual test. The
    test is passed if the headroom is successfully extended for given packet
    sizes and offsets.
    """
    pkt_sz_lst = [512, 1024, 2048]

    # Negative values result in headroom shrinking, resulting in growing of payload
    offset_lst = [-16, -32, -64, -128, -256]
    res = _test_xdp_native_head_adjst(cfg, "xdp_prog_frags", pkt_sz_lst, offset_lst)

    _validate_res(res, offset_lst, pkt_sz_lst)


def test_xdp_native_adjst_head_shrnk_data(cfg):
    """
    Tests the XDP headroom shrinking support.

    Args:
        cfg: Configuration object containing network settings.

    This function sets up the packet size and offset lists, then calls the
    _test_xdp_native_head_adjst_mb function to perform the actual test. The
    test is passed if the headroom is successfully shrunk for given packet
    sizes and offsets.
    """
    pkt_sz_lst = [512, 1024, 2048]

    # Positive values result in headroom growing, resulting in shrinking of payload
    offset_lst = [16, 32, 64, 128, 256]
    res = _test_xdp_native_head_adjst(cfg, "xdp_prog_frags", pkt_sz_lst, offset_lst)

    _validate_res(res, offset_lst, pkt_sz_lst)


def _test_xdp_native_ifc_stats(cfg, act):
    cfg.require_cmd("socat")

    bpf_info = BPFProgInfo("xdp_prog", "xdp_native.bpf.o", "xdp", 1500)
    prog_info = _load_xdp_prog(cfg, bpf_info)
    port = rand_port()

    _set_xdp_map("map_xdp_setup", TestConfig.MODE.value, act.value)
    _set_xdp_map("map_xdp_setup", TestConfig.PORT.value, port)

    # Discard the input, but we need a listener to avoid ICMP errors
    rx_udp = f"socat -{cfg.addr_ipver} -T 2 -u UDP-RECV:{port},reuseport " + \
        "/dev/null"
    # Listener runs on "remote" in case of XDP_TX
    rx_host = cfg.remote if act == XDPAction.TX else None
    # We want to spew 2000 packets quickly, bash seems to do a good enough job
    tx_udp =  f"exec 5<>/dev/udp/{cfg.addr}/{port}; " \
        "for i in `seq 2000`; do echo a >&5; done; exec 5>&-"

    cfg.wait_hw_stats_settle()
    # Qstats have more clearly defined semantics than rtnetlink.
    # XDP is the "first layer of the stack" so XDP packets should be counted
    # as received and sent as if the decision was made in the routing layer.
    before = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]

    with bkg(rx_udp, host=rx_host, exit_wait=True):
        wait_port_listen(port, proto="udp", host=rx_host)
        cmd(tx_udp, host=cfg.remote, shell=True)

    cfg.wait_hw_stats_settle()
    after = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]

    ksft_ge(after['rx-packets'] - before['rx-packets'], 2000)
    if act == XDPAction.TX:
        ksft_ge(after['tx-packets'] - before['tx-packets'], 2000)

    expected_pkts = 2000
    stats = _get_stats(prog_info["maps"]["map_xdp_stats"])
    ksft_eq(stats[XDPStats.RX.value], expected_pkts, "XDP RX stats mismatch")
    if act == XDPAction.TX:
        ksft_eq(stats[XDPStats.TX.value], expected_pkts, "XDP TX stats mismatch")

    # Flip the ring count back and forth to make sure the stats from XDP rings
    # don't get lost.
    chans = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    if chans.get('combined-count', 0) > 1:
        cfg.ethnl.channels_set({'header': {'dev-index': cfg.ifindex},
                                'combined-count': 1})
        cfg.ethnl.channels_set({'header': {'dev-index': cfg.ifindex},
                                'combined-count': chans['combined-count']})
        before = after
        after = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]

        ksft_ge(after['rx-packets'], before['rx-packets'])
        if act == XDPAction.TX:
            ksft_ge(after['tx-packets'], before['tx-packets'])


def test_xdp_native_qstats_pass(cfg):
    """
    Send 2000 messages, expect XDP_PASS, make sure the packets were counted
    to interface level qstats (Rx).
    """
    _test_xdp_native_ifc_stats(cfg, XDPAction.PASS)


def test_xdp_native_qstats_drop(cfg):
    """
    Send 2000 messages, expect XDP_DROP, make sure the packets were counted
    to interface level qstats (Rx).
    """
    _test_xdp_native_ifc_stats(cfg, XDPAction.DROP)


def test_xdp_native_qstats_tx(cfg):
    """
    Send 2000 messages, expect XDP_TX, make sure the packets were counted
    to interface level qstats (Rx and Tx)
    """
    _test_xdp_native_ifc_stats(cfg, XDPAction.TX)


def main():
    """
    Main function to execute the XDP tests.

    This function runs a series of tests to validate the XDP support for
    both the single and multi-buffer. It uses the NetDrvEpEnv context
    manager to manage the network driver environment and the ksft_run
    function to execute the tests.
    """
    with NetDrvEpEnv(__file__) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()
        ksft_run(
            [
                test_xdp_native_pass_sb,
                test_xdp_native_pass_mb,
                test_xdp_native_drop_sb,
                test_xdp_native_drop_mb,
                test_xdp_native_tx_sb,
                test_xdp_native_tx_mb,
                test_xdp_native_adjst_tail_grow_data,
                test_xdp_native_adjst_tail_shrnk_data,
                test_xdp_native_adjst_head_grow_data,
                test_xdp_native_adjst_head_shrnk_data,
                test_xdp_native_qstats_pass,
                test_xdp_native_qstats_drop,
                test_xdp_native_qstats_tx,
            ],
            args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
