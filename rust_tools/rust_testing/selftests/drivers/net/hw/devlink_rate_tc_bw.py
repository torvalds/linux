#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Devlink Rate TC Bandwidth Test Suite
===================================

This test suite verifies the functionality of devlink-rate traffic class (TC)
bandwidth distribution in a virtualized environment. The tests validate that
bandwidth can be properly allocated between different traffic classes and
that TC mapping works as expected.

Test Environment:
----------------
- Creates 1 VF
- Establishes a bridge connecting the VF representor and the uplink representor
- Sets up 2 VLAN interfaces on the VF with different VLAN IDs (101, 102)
- Configures different traffic classes (TC3 and TC4) for each VLAN

Test Cases:
----------
1. test_no_tc_mapping_bandwidth:
   - Verifies that without TC mapping, bandwidth is NOT distributed according to
     the configured 80/20 split between TC4 and TC3
   - This test should fail if bandwidth matches the 80/20 split without TC
     mapping
   - Expected: Bandwidth should NOT be distributed as 80/20

2. test_tc_mapping_bandwidth:
   - Configures TC mapping using mqprio qdisc
   - Verifies that with TC mapping, bandwidth IS distributed according to the
     configured 80/20 split between TC3 and TC4
   - Expected: Bandwidth should be distributed as 80/20

Bandwidth Distribution:
----------------------
- TC3 (VLAN 101): Configured for 80% of total bandwidth
- TC4 (VLAN 102): Configured for 20% of total bandwidth
- Total bandwidth: 1Gbps
- Tolerance: +-12%

Hardware-Specific Behavior (mlx5):
--------------------------
mlx5 hardware enforces traffic class separation by ensuring that each transmit
queue (SQ) is associated with a single TC. If a packet is sent on a queue that
doesn't match the expected TC (based on DSCP or VLAN priority and hypervisor-set
mapping), the hardware moves the queue to the correct TC scheduler to preserve
traffic isolation.

This behavior means that even without explicit TC-to-queue mapping, bandwidth
enforcement may still appear to work—because the hardware dynamically adjusts
the scheduling context. However, this can lead to performance issues in high
rates and HOL blocking if traffic from different TCs is mixed on the same queue.
"""

import json
import os
import subprocess
import threading
import time

from lib.py import ksft_pr, ksft_run, ksft_exit
from lib.py import KsftSkipEx, KsftFailEx, KsftXfailEx
from lib.py import NetDrvEpEnv, DevlinkFamily
from lib.py import NlError
from lib.py import cmd, defer, ethtool, ip


class BandwidthValidator:
    """
    Validates bandwidth totals and per-TC shares against expected values
    with a tolerance.
    """

    def __init__(self):
        self.tolerance_percent = 12
        self.expected_total_gbps = 1.0
        self.total_min_expected = self.min_expected(self.expected_total_gbps)
        self.total_max_expected = self.max_expected(self.expected_total_gbps)
        self.tc_expected_percent = {
            3: 20.0,
            4: 80.0,
        }

    def min_expected(self, value):
        """Calculates the minimum acceptable value based on tolerance."""
        return value - (value * self.tolerance_percent / 100)

    def max_expected(self, value):
        """Calculates the maximum acceptable value based on tolerance."""
        return value + (value * self.tolerance_percent / 100)

    def bound(self, expected, value):
        """Returns True if value is within expected tolerance."""
        return self.min_expected(expected) <= value <= self.max_expected(expected)

    def tc_bandwidth_bound(self, value, tc_ix):
        """
        Returns True if the given bandwidth value is within tolerance
        for the TC's expected bandwidth.
        """
        expected = self.tc_expected_percent[tc_ix]
        return self.bound(expected, value)


def setup_vf(cfg, set_tc_mapping=True):
    """
    Sets up a VF on the given network interface.

    Enables SR-IOV and switchdev mode, brings the VF interface up,
    and optionally configures TC mapping using mqprio.
    """
    try:
        cmd(f"devlink dev eswitch set pci/{cfg.pci} mode switchdev")
        defer(cmd, f"devlink dev eswitch set pci/{cfg.pci} mode legacy")
    except Exception as exc:
        raise KsftSkipEx(f"Failed to enable switchdev mode on {cfg.pci}") from exc
    try:
        cmd(f"echo 1 > /sys/class/net/{cfg.ifname}/device/sriov_numvfs")
        defer(cmd, f"echo 0 > /sys/class/net/{cfg.ifname}/device/sriov_numvfs")
    except Exception as exc:
        raise KsftSkipEx(f"Failed to enable SR-IOV on {cfg.ifname}") from exc

    time.sleep(2)
    vf_ifc = (os.listdir(
        f"/sys/class/net/{cfg.ifname}/device/virtfn0/net") or [None])[0]
    if vf_ifc:
        ip(f"link set dev {vf_ifc} up")
    else:
        raise KsftSkipEx("VF interface not found")
    if set_tc_mapping:
        cmd(f"tc qdisc add dev {vf_ifc} root handle 5 mqprio mode dcb hw 1 num_tc 8")

    return vf_ifc


def setup_vlans_on_vf(vf_ifc):
    """
    Sets up two VLAN interfaces on the given VF, each mapped to a different TC.
    """
    vlan_configs = [
        {"vlan_id": 101, "tc": 3, "ip": "198.51.100.2"},
        {"vlan_id": 102, "tc": 4, "ip": "198.51.100.10"},
    ]

    for config in vlan_configs:
        vlan_dev = f"{vf_ifc}.{config['vlan_id']}"
        ip(f"link add link {vf_ifc} name {vlan_dev} type vlan id {config['vlan_id']}")
        ip(f"addr add {config['ip']}/29 dev {vlan_dev}")
        ip(f"link set dev {vlan_dev} up")
        ip(f"link set dev {vlan_dev} type vlan egress-qos-map 0:{config['tc']}")
        ksft_pr(f"Created VLAN {vlan_dev} on {vf_ifc} with tc {config['tc']} and IP {config['ip']}")


def get_vf_info(cfg):
    """
    Finds the VF representor interface and devlink port index
    for the given PCI device used in the test environment.
    """
    cfg.vf_representor = None
    cfg.vf_port_index = None
    out = subprocess.check_output(["devlink", "-j", "port", "show"], encoding="utf-8")
    ports = json.loads(out)["port"]

    for port_name, props in ports.items():
        netdev = props.get("netdev")

        if (port_name.startswith(f"pci/{cfg.pci}/") and
            props.get("vfnum") == 0):
            cfg.vf_representor = netdev
            cfg.vf_port_index = int(port_name.split("/")[-1])
            break


def setup_bridge(cfg):
    """
    Creates and configures a Linux bridge, with both the uplink
    and VF representor interfaces attached to it.
    """
    bridge_name = f"br_{os.getpid()}"
    ip(f"link add name {bridge_name} type bridge")
    defer(cmd, f"ip link del name {bridge_name} type bridge")

    ip(f"link set dev {cfg.ifname} master {bridge_name}")

    rep_name = cfg.vf_representor
    if rep_name:
        ip(f"link set dev {rep_name} master {bridge_name}")
        ip(f"link set dev {rep_name} up")
        ksft_pr(f"Set representor {rep_name} up and added to bridge")
    else:
        raise KsftSkipEx("Could not find representor for the VF")

    ip(f"link set dev {bridge_name} up")


def setup_devlink_rate(cfg):
    """
    Configures devlink rate tx_max and traffic class bandwidth for the VF.
    """
    port_index = cfg.vf_port_index
    if port_index is None:
        raise KsftSkipEx("Could not find VF port index")
    try:
        cfg.devnl.rate_set({
            "bus-name": "pci",
            "dev-name": cfg.pci,
            "port-index": port_index,
            "rate-tx-max": 125000000,
            "rate-tc-bws": [
                {"index": 0, "bw": 0},
                {"index": 1, "bw": 0},
                {"index": 2, "bw": 0},
                {"index": 3, "bw": 20},
                {"index": 4, "bw": 80},
                {"index": 5, "bw": 0},
                {"index": 6, "bw": 0},
                {"index": 7, "bw": 0},
            ]
        })
    except NlError as exc:
        if exc.error == 95:  # EOPNOTSUPP
            raise KsftSkipEx("devlink rate configuration is not supported on the VF") from exc
        raise KsftFailEx(f"rate_set failed on VF port {port_index}") from exc


def setup_remote_server(cfg):
    """
    Sets up VLAN interfaces and starts iperf3 servers on the remote side.
    """
    remote_dev = cfg.remote_ifname
    vlan_ids = [101, 102]
    remote_ips = ["198.51.100.1", "198.51.100.9"]

    for vlan_id, ip_addr in zip(vlan_ids, remote_ips):
        vlan_dev = f"{remote_dev}.{vlan_id}"
        cmd(f"ip link add link {remote_dev} name {vlan_dev} "
            f"type vlan id {vlan_id}", host=cfg.remote)
        cmd(f"ip addr add {ip_addr}/29 dev {vlan_dev}", host=cfg.remote)
        cmd(f"ip link set dev {vlan_dev} up", host=cfg.remote)
        cmd(f"iperf3 -s -1 -B {ip_addr}",background=True, host=cfg.remote)
        defer(cmd, f"ip link del {vlan_dev}", host=cfg.remote)


def setup_test_environment(cfg, set_tc_mapping=True):
    """
    Sets up the complete test environment including VF creation, VLANs,
    bridge configuration, devlink rate setup, and the remote server.
    """
    vf_ifc = setup_vf(cfg, set_tc_mapping)
    ksft_pr(f"Created VF interface: {vf_ifc}")

    setup_vlans_on_vf(vf_ifc)

    get_vf_info(cfg)
    setup_bridge(cfg)

    setup_devlink_rate(cfg)
    setup_remote_server(cfg)
    time.sleep(2)


def run_iperf_client(server_ip, local_ip, barrier, min_expected_gbps=0.1):
    """
    Runs a single iperf3 client instance, binding to the given local IP.
    Waits on a barrier to synchronize with other threads.
    """
    try:
        barrier.wait(timeout=10)
    except Exception as exc:
        raise KsftFailEx("iperf3 barrier wait timed") from exc

    iperf_cmd = ["iperf3", "-c", server_ip, "-B", local_ip, "-J"]
    result = subprocess.run(iperf_cmd, capture_output=True, text=True,
                            check=True)

    try:
        output = json.loads(result.stdout)
        bits_per_second = output["end"]["sum_received"]["bits_per_second"]
        gbps = bits_per_second / 1e9
        if gbps < min_expected_gbps:
            ksft_pr(
                f"iperf3 bandwidth too low: {gbps:.2f} Gbps "
                f"(expected ≥ {min_expected_gbps} Gbps)"
            )
            return None
        return gbps
    except json.JSONDecodeError as exc:
        ksft_pr(f"Failed to parse iperf3 JSON output: {exc}")
        return None


def run_bandwidth_test():
    """
    Launches iperf3 client threads for each VLAN/TC pair and collects results.
    """
    def _run_iperf_client_thread(server_ip, local_ip, results, barrier, tc_ix):
        results[tc_ix] = run_iperf_client(server_ip, local_ip, barrier)

    vf_vlan_data = [
        # (local_ip, remote_ip, TC)
        ("198.51.100.2",  "198.51.100.1", 3),
        ("198.51.100.10", "198.51.100.9", 4),
    ]

    results = {}
    threads = []
    start_barrier = threading.Barrier(len(vf_vlan_data))

    for local_ip, remote_ip, tc_ix in vf_vlan_data:
        thread = threading.Thread(
            target=_run_iperf_client_thread,
            args=(remote_ip, local_ip, results, start_barrier, tc_ix)
        )
        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join()

    for tc_ix, tc_bw in results.items():
        if tc_bw is None:
            raise KsftFailEx("iperf3 client failed; cannot evaluate bandwidth")

    return results

def calculate_bandwidth_percentages(results):
    """
    Calculates the percentage of total bandwidth received by TC3 and TC4.
    """
    if 3 not in results or 4 not in results:
        raise KsftFailEx(f"Missing expected TC results in {results}")

    tc3_bw = results[3]
    tc4_bw = results[4]
    total_bw = tc3_bw + tc4_bw
    tc3_percentage = (tc3_bw / total_bw) * 100
    tc4_percentage = (tc4_bw / total_bw) * 100

    return {
        'tc3_bw': tc3_bw,
        'tc4_bw': tc4_bw,
        'tc3_percentage': tc3_percentage,
        'tc4_percentage': tc4_percentage,
        'total_bw': total_bw
    }


def print_bandwidth_results(bw_data, test_name):
    """
    Prints bandwidth measurements and TC usage summary for a given test.
    """
    ksft_pr(f"Bandwidth check results {test_name}:")
    ksft_pr(f"TC 3: {bw_data['tc3_bw']:.2f} Gbits/sec")
    ksft_pr(f"TC 4: {bw_data['tc4_bw']:.2f} Gbits/sec")
    ksft_pr(f"Total bandwidth: {bw_data['total_bw']:.2f} Gbits/sec")
    ksft_pr(f"TC 3 percentage: {bw_data['tc3_percentage']:.1f}%")
    ksft_pr(f"TC 4 percentage: {bw_data['tc4_percentage']:.1f}%")


def verify_total_bandwidth(bw_data, validator):
    """
    Ensures the total measured bandwidth falls within the acceptable tolerance.
    """
    total = bw_data['total_bw']

    if validator.bound(validator.expected_total_gbps, total):
        return

    if total < validator.total_min_expected:
        raise KsftSkipEx(
            f"Total bandwidth {total:.2f} Gbps < minimum "
            f"{validator.total_min_expected:.2f} Gbps; "
            f"parent tx_max ({validator.expected_total_gbps:.1f} G) "
            f"not reached, cannot validate share"
        )

    raise KsftFailEx(
        f"Total bandwidth {total:.2f} Gbps exceeds allowed ceiling "
        f"{validator.total_max_expected:.2f} Gbps "
        f"(VF tx_max set to {validator.expected_total_gbps:.1f} G)"
    )


def check_bandwidth_distribution(bw_data, validator):
    """
    Checks whether the measured TC3 and TC4 bandwidth percentages
    fall within their expected tolerance ranges.

    Returns:
        bool: True if both TC3 and TC4 percentages are within bounds.
    """
    tc3_valid = validator.tc_bandwidth_bound(bw_data['tc3_percentage'], 3)
    tc4_valid = validator.tc_bandwidth_bound(bw_data['tc4_percentage'], 4)

    return tc3_valid and tc4_valid


def run_bandwidth_distribution_test(cfg, set_tc_mapping):
    """
    Runs parallel iperf3 tests for both TCs and collects results.
    """
    setup_test_environment(cfg, set_tc_mapping)
    bandwidths = run_bandwidth_test()
    bw_data = calculate_bandwidth_percentages(bandwidths)
    test_name = "with TC mapping" if set_tc_mapping else "without TC mapping"
    print_bandwidth_results(bw_data, test_name)

    verify_total_bandwidth(bw_data, cfg.bw_validator)

    return check_bandwidth_distribution(bw_data, cfg.bw_validator)


def test_no_tc_mapping_bandwidth(cfg):
    """
    Verifies that bandwidth is not split 80/20 without traffic class mapping.
    """
    pass_bw_msg = "Bandwidth is NOT distributed as 80/20 without TC mapping"
    fail_bw_msg = "Bandwidth matched 80/20 split without TC mapping"
    is_mlx5 = "driver: mlx5" in ethtool(f"-i {cfg.ifname}").stdout

    if run_bandwidth_distribution_test(cfg, set_tc_mapping=False):
        if is_mlx5:
            raise KsftXfailEx(fail_bw_msg)
        raise KsftFailEx(fail_bw_msg)
    if is_mlx5:
        raise KsftFailEx("mlx5 behavior changed:" + pass_bw_msg)
    ksft_pr(pass_bw_msg)


def test_tc_mapping_bandwidth(cfg):
    """
    Verifies that bandwidth is correctly split 80/20 between TC3 and TC4
    when traffic class mapping is set.
    """
    if run_bandwidth_distribution_test(cfg, set_tc_mapping=True):
        ksft_pr("Bandwidth is distributed as 80/20 with TC mapping")
    else:
        raise KsftFailEx("Bandwidth did not match 80/20 split with TC mapping")


def main() -> None:
    """
    Main entry point for running the test cases.
    """
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.devnl = DevlinkFamily()

        cfg.pci = os.path.basename(
            os.path.realpath(f"/sys/class/net/{cfg.ifname}/device")
        )
        if not cfg.pci:
            raise KsftSkipEx("Could not get PCI address of the interface")
        cfg.require_cmd("iperf3", local=True, remote=True)

        cfg.bw_validator = BandwidthValidator()

        cases = [test_no_tc_mapping_bandwidth, test_tc_mapping_bandwidth]

        ksft_run(cases=cases, args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
