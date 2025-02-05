#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

#Introduction:
#This file has basic performance test for generic NIC drivers.
#The test comprises of throughput check for TCP and UDP streams.
#
#Setup:
#Connect the DUT PC with NIC card to partner pc back via ethernet medium of your choice(RJ45, T1)
#
#        DUT PC                                              Partner PC
#┌───────────────────────┐                         ┌──────────────────────────┐
#│                       │                         │                          │
#│                       │                         │                          │
#│           ┌───────────┐                         │                          │
#│           │DUT NIC    │         Eth             │                          │
#│           │Interface ─┼─────────────────────────┼─    any eth Interface    │
#│           └───────────┘                         │                          │
#│                       │                         │                          │
#│                       │                         │                          │
#└───────────────────────┘                         └──────────────────────────┘
#
#Configurations:
#To prevent interruptions, Add ethtool, ip to the sudoers list in remote PC and get the ssh key from remote.
#Required minimum ethtool version is 6.10
#Change the below configuration based on your hw needs.
# """Default values"""
#time_delay = 8 #time taken to wait for transitions to happen, in seconds.
#test_duration = 10  #performance test duration for the throughput check, in seconds.
#send_throughput_threshold = 80 #percentage of send throughput required to pass the check
#receive_throughput_threshold = 50 #percentage of receive throughput required to pass the check

import time
import json
import argparse
from lib.py import ksft_run, ksft_exit, ksft_pr, ksft_true
from lib.py import KsftFailEx, KsftSkipEx, GenerateTraffic
from lib.py import NetDrvEpEnv, bkg, wait_port_listen
from lib.py import cmd
from lib.py import LinkConfig

class TestConfig:
    def __init__(self, time_delay: int, test_duration: int, send_throughput_threshold: int, receive_throughput_threshold: int) -> None:
        self.time_delay = time_delay
        self.test_duration = test_duration
        self.send_throughput_threshold = send_throughput_threshold
        self.receive_throughput_threshold = receive_throughput_threshold

def _pre_test_checks(cfg: object, link_config: LinkConfig) -> None:
    if not link_config.verify_link_up():
        KsftSkipEx(f"Link state of interface {cfg.ifname} is DOWN")
    common_link_modes = link_config.common_link_modes
    if common_link_modes is None:
        KsftSkipEx("No common link modes found")
    if link_config.partner_netif == None:
        KsftSkipEx("Partner interface is not available")
    if link_config.check_autoneg_supported():
        KsftSkipEx("Auto-negotiation not supported by local")
    if link_config.check_autoneg_supported(remote=True):
        KsftSkipEx("Auto-negotiation not supported by remote")
    cfg.require_cmd("iperf3", remote=True)

def check_throughput(cfg: object, link_config: LinkConfig, test_config: TestConfig, protocol: str, traffic: GenerateTraffic) -> None:
    common_link_modes = link_config.common_link_modes
    speeds, duplex_modes = link_config.get_speed_duplex_values(common_link_modes)
    """Test duration in seconds"""
    duration = test_config.test_duration

    ksft_pr(f"{protocol} test")
    test_type = "-u" if protocol == "UDP" else ""

    send_throughput = []
    receive_throughput = []
    for idx in range(0, len(speeds)):
        if link_config.set_speed_and_duplex(speeds[idx], duplex_modes[idx]) == False:
            raise KsftFailEx(f"Not able to set speed and duplex parameters for {cfg.ifname}")
        time.sleep(test_config.time_delay)
        if not link_config.verify_link_up():
            raise KsftSkipEx(f"Link state of interface {cfg.ifname} is DOWN")

        send_command=f"{test_type} -b 0 -t {duration} --json"
        receive_command=f"{test_type} -b 0 -t {duration} --reverse --json"

        send_result = traffic.run_remote_test(cfg, command=send_command)
        if send_result.ret != 0:
            raise KsftSkipEx("Error occurred during data transmit: {send_result.stdout}")

        send_output = send_result.stdout
        send_data = json.loads(send_output)

        """Convert throughput to Mbps"""
        send_throughput.append(round(send_data['end']['sum_sent']['bits_per_second'] / 1e6, 2))
        ksft_pr(f"{protocol}: Send throughput: {send_throughput[idx]} Mbps")

        receive_result = traffic.run_remote_test(cfg, command=receive_command)
        if receive_result.ret != 0:
            raise KsftSkipEx("Error occurred during data receive: {receive_result.stdout}")

        receive_output = receive_result.stdout
        receive_data = json.loads(receive_output)

        """Convert throughput to Mbps"""
        receive_throughput.append(round(receive_data['end']['sum_received']['bits_per_second'] / 1e6, 2))
        ksft_pr(f"{protocol}: Receive throughput: {receive_throughput[idx]} Mbps")

    """Check whether throughput is not below the threshold (default values set at start)"""
    for idx in range(0, len(speeds)):
        send_threshold = float(speeds[idx]) * float(test_config.send_throughput_threshold / 100)
        receive_threshold = float(speeds[idx]) * float(test_config.receive_throughput_threshold / 100)
        ksft_true(send_throughput[idx] >= send_threshold, f"{protocol}: Send throughput is below threshold for {speeds[idx]} Mbps in {duplex_modes[idx]} duplex")
        ksft_true(receive_throughput[idx] >= receive_threshold, f"{protocol}: Receive throughput is below threshold for {speeds[idx]} Mbps in {duplex_modes[idx]} duplex")

def test_tcp_throughput(cfg: object, link_config: LinkConfig, test_config: TestConfig, traffic: GenerateTraffic) -> None:
    _pre_test_checks(cfg, link_config)
    check_throughput(cfg, link_config, test_config, 'TCP', traffic)

def test_udp_throughput(cfg: object, link_config: LinkConfig, test_config: TestConfig, traffic: GenerateTraffic) -> None:
    _pre_test_checks(cfg, link_config)
    check_throughput(cfg, link_config, test_config, 'UDP', traffic)

def main() -> None:
    parser = argparse.ArgumentParser(description="Run basic performance test for NIC driver")
    parser.add_argument('--time-delay', type=int, default=8, help='Time taken to wait for transitions to happen(in seconds). Default is 8 seconds.')
    parser.add_argument('--test-duration', type=int, default=10, help='Performance test duration for the throughput check, in seconds. Default is 10 seconds.')
    parser.add_argument('--stt', type=int, default=80, help='Send throughput Threshold: Percentage of send throughput upon actual throughput required to pass the throughput check (in percentage). Default is 80.')
    parser.add_argument('--rtt', type=int, default=50, help='Receive throughput Threshold: Percentage of receive throughput upon actual throughput required to pass the throughput check (in percentage). Default is 50.')
    args=parser.parse_args()
    test_config = TestConfig(args.time_delay, args.test_duration, args.stt, args.rtt)
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        traffic = GenerateTraffic(cfg)
        link_config = LinkConfig(cfg)
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, link_config, test_config, traffic,  ))
        link_config.reset_interface()
    ksft_exit()

if __name__ == "__main__":
    main()
