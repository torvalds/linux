#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

#Introduction:
#This file has basic link layer tests for generic NIC drivers.
#The test comprises of auto-negotiation, speed and duplex checks.
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
#Required minimum ethtool version is 6.10 (supports json)
#Default values:
#time_delay = 8 #time taken to wait for transitions to happen, in seconds.

import time
import argparse
from lib.py import ksft_run, ksft_exit, ksft_pr, ksft_eq
from lib.py import KsftFailEx, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import LinkConfig

def _pre_test_checks(cfg: object, link_config: LinkConfig) -> None:
    if link_config.partner_netif is None:
        KsftSkipEx("Partner interface is not available")
    if not link_config.check_autoneg_supported() or not link_config.check_autoneg_supported(remote=True):
        KsftSkipEx(f"Auto-negotiation not supported for interface {cfg.ifname} or {link_config.partner_netif}")
    if not link_config.verify_link_up():
        raise KsftSkipEx(f"Link state of interface {cfg.ifname} is DOWN")

def verify_autonegotiation(cfg: object, expected_state: str, link_config: LinkConfig) -> None:
    if not link_config.verify_link_up():
        raise KsftSkipEx(f"Link state of interface {cfg.ifname} is DOWN")
    """Verifying the autonegotiation state in partner"""
    partner_autoneg_output = link_config.get_ethtool_field("auto-negotiation", remote=True)
    if partner_autoneg_output is None:
        KsftSkipEx(f"Auto-negotiation state not available for interface {link_config.partner_netif}")
    partner_autoneg_state = "on" if partner_autoneg_output is True else "off"

    ksft_eq(partner_autoneg_state, expected_state)

    """Verifying the autonegotiation state of local"""
    autoneg_output = link_config.get_ethtool_field("auto-negotiation")
    if autoneg_output is None:
        KsftSkipEx(f"Auto-negotiation state not available for interface {cfg.ifname}")
    actual_state = "on" if autoneg_output is True else "off"

    ksft_eq(actual_state, expected_state)

    """Verifying the link establishment"""
    link_available = link_config.get_ethtool_field("link-detected")
    if link_available is None:
        KsftSkipEx(f"Link status not available for interface {cfg.ifname}")
    if link_available != True:
        raise KsftSkipEx("Link not established at interface {cfg.ifname} after changing auto-negotiation")

def test_autonegotiation(cfg: object, link_config: LinkConfig, time_delay: int) -> None:
    _pre_test_checks(cfg, link_config)
    for state in ["off", "on"]:
        if not link_config.set_autonegotiation_state(state, remote=True):
            raise KsftSkipEx(f"Unable to set auto-negotiation state for interface {link_config.partner_netif}")
        if not link_config.set_autonegotiation_state(state):
            raise KsftSkipEx(f"Unable to set auto-negotiation state for interface {cfg.ifname}")
        time.sleep(time_delay)
        verify_autonegotiation(cfg, state, link_config)

def test_network_speed(cfg: object, link_config: LinkConfig, time_delay: int) -> None:
    _pre_test_checks(cfg, link_config)
    common_link_modes = link_config.common_link_modes
    if not common_link_modes:
        KsftSkipEx("No common link modes exist")
    speeds, duplex_modes = link_config.get_speed_duplex_values(common_link_modes)

    if speeds and duplex_modes and len(speeds) == len(duplex_modes):
        for idx in range(len(speeds)):
            speed = speeds[idx]
            duplex = duplex_modes[idx]
            if not link_config.set_speed_and_duplex(speed, duplex):
                raise KsftFailEx(f"Unable to set speed and duplex parameters for {cfg.ifname}")
            time.sleep(time_delay)
            if not link_config.verify_speed_and_duplex(speed, duplex):
                raise KsftSkipEx(f"Error occurred while verifying speed and duplex states for interface {cfg.ifname}")
    else:
        if not speeds or not duplex_modes:
            KsftSkipEx(f"No supported speeds or duplex modes found for interface {cfg.ifname}")
        else:
            KsftSkipEx("Mismatch in the number of speeds and duplex modes")

def main() -> None:
    parser = argparse.ArgumentParser(description="Run basic link layer tests for NIC driver")
    parser.add_argument('--time-delay', type=int, default=8, help='Time taken to wait for transitions to happen(in seconds). Default is 8 seconds.')
    args = parser.parse_args()
    time_delay = args.time_delay
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        link_config = LinkConfig(cfg)
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, link_config, time_delay,))
        link_config.reset_interface()
    ksft_exit()

if __name__ == "__main__":
    main()
