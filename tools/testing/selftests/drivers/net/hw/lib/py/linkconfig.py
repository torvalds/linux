# SPDX-License-Identifier: GPL-2.0

from lib.py import cmd, ethtool, ip
from lib.py import ksft_pr, ksft_eq, KsftSkipEx
from typing import Optional
import re
import time
import json

#The LinkConfig class is implemented to handle the link layer configurations.
#Required minimum ethtool version is 6.10

class LinkConfig:
    """Class for handling the link layer configurations"""
    def __init__(self, cfg: object) -> None:
        self.cfg = cfg
        self.partner_netif = self.get_partner_netif_name()

        """Get the initial link configuration of local interface"""
        self.common_link_modes = self.get_common_link_modes()

    def get_partner_netif_name(self) -> Optional[str]:
        partner_netif = None
        try:
            if not self.verify_link_up():
                return None
            """Get partner interface name"""
            partner_json_output = ip("addr show", json=True, host=self.cfg.remote)
            for interface in partner_json_output:
                for addr in interface.get('addr_info', []):
                    if addr.get('local') == self.cfg.remote_addr:
                        partner_netif = interface['ifname']
                        ksft_pr(f"Partner Interface name: {partner_netif}")
            if partner_netif is None:
                ksft_pr("Unable to get the partner interface name")
        except Exception as e:
            print(f"Unexpected error occurred while getting partner interface name: {e}")
        self.partner_netif = partner_netif
        return partner_netif

    def verify_link_up(self) -> bool:
        """Verify whether the local interface link is up"""
        with open(f"/sys/class/net/{self.cfg.ifname}/operstate", "r") as fp:
            link_state = fp.read().strip()

        if link_state == "down":
            ksft_pr(f"Link state of interface {self.cfg.ifname} is DOWN")
            return False
        else:
            return True

    def reset_interface(self, local: bool = True, remote: bool = True) -> bool:
        ksft_pr("Resetting interfaces in local and remote")
        if remote:
            if self.verify_link_up():
                if self.partner_netif is not None:
                    ifname = self.partner_netif
                    link_up_cmd = f"ip link set up {ifname}"
                    link_down_cmd = f"ip link set down {ifname}"
                    reset_cmd = f"{link_down_cmd} && sleep 5 && {link_up_cmd}"
                    try:
                        cmd(reset_cmd, host=self.cfg.remote)
                    except Exception as e:
                        ksft_pr(f"Unexpected error occurred while resetting remote: {e}")
                else:
                    ksft_pr("Partner interface not available")
        if local:
            ifname = self.cfg.ifname
            link_up_cmd = f"ip link set up {ifname}"
            link_down_cmd = f"ip link set down {ifname}"
            reset_cmd = f"{link_down_cmd} && sleep 5 && {link_up_cmd}"
            try:
                cmd(reset_cmd)
            except Exception as e:
                ksft_pr(f"Unexpected error occurred while resetting local: {e}")
        time.sleep(10)
        if self.verify_link_up() and self.get_ethtool_field("link-detected"):
            ksft_pr("Local and remote interfaces reset to original state")
            return True
        else:
            ksft_pr("Error occurred after resetting interfaces. Link is DOWN.")
            return False

    def set_speed_and_duplex(self, speed: str, duplex: str, autoneg: bool = True) -> bool:
        """Set the speed and duplex state for the interface"""
        autoneg_state = "on" if autoneg is True else "off"
        process = None
        try:
            process = ethtool(f"--change {self.cfg.ifname} speed {speed} duplex {duplex} autoneg {autoneg_state}")
        except Exception as e:
            ksft_pr(f"Unexpected error occurred while setting speed/duplex: {e}")
        if process is None or process.ret != 0:
            return False
        else:
            ksft_pr(f"Speed: {speed} Mbps, Duplex: {duplex} set for Interface: {self.cfg.ifname}")
            return True

    def verify_speed_and_duplex(self, expected_speed: str, expected_duplex: str) -> bool:
        if not self.verify_link_up():
            return False
        """Verifying the speed and duplex state for the interface"""
        with open(f"/sys/class/net/{self.cfg.ifname}/speed", "r") as fp:
            actual_speed = fp.read().strip()
        with open(f"/sys/class/net/{self.cfg.ifname}/duplex", "r") as fp:
            actual_duplex = fp.read().strip()

        ksft_eq(actual_speed, expected_speed)
        ksft_eq(actual_duplex, expected_duplex)
        return True

    def set_autonegotiation_state(self, state: str, remote: bool = False) -> bool:
        common_link_modes = self.common_link_modes
        speeds, duplex_modes = self.get_speed_duplex_values(self.common_link_modes)
        speed = speeds[0]
        duplex = duplex_modes[0]
        if not speed or not duplex:
            ksft_pr("No speed or duplex modes found")
            return False

        speed_duplex_cmd = f"speed {speed} duplex {duplex}" if state == "off" else ""
        if remote:
            if not self.verify_link_up():
                return False
            """Set the autonegotiation state for the partner"""
            command = f"-s {self.partner_netif} {speed_duplex_cmd} autoneg {state}"
            partner_autoneg_change = None
            """Set autonegotiation state for interface in remote pc"""
            try:
                partner_autoneg_change = ethtool(command, host=self.cfg.remote)
            except Exception as e:
                ksft_pr(f"Unexpected error occurred while changing auto-neg in remote: {e}")
            if partner_autoneg_change is None or partner_autoneg_change.ret != 0:
                ksft_pr(f"Not able to set autoneg parameter for interface {self.partner_netif}.")
                return False
            ksft_pr(f"Autoneg set as {state} for {self.partner_netif}")
        else:
            """Set the autonegotiation state for the interface"""
            try:
                process = ethtool(f"-s {self.cfg.ifname} {speed_duplex_cmd} autoneg {state}")
                if process.ret != 0:
                    ksft_pr(f"Not able to set autoneg parameter for interface {self.cfg.ifname}")
                    return False
            except Exception as e:
                ksft_pr(f"Unexpected error occurred while changing auto-neg in local: {e}")
                return False
            ksft_pr(f"Autoneg set as {state} for {self.cfg.ifname}")
        return True

    def check_autoneg_supported(self, remote: bool = False) -> bool:
        if not remote:
            local_autoneg = self.get_ethtool_field("supports-auto-negotiation")
            if local_autoneg is None:
                ksft_pr(f"Unable to fetch auto-negotiation status for interface {self.cfg.ifname}")
            """Return autoneg status of the local interface"""
            return local_autoneg
        else:
            if not self.verify_link_up():
                raise KsftSkipEx("Link is DOWN")
            """Check remote auto-negotiation support status"""
            partner_autoneg = False
            if self.partner_netif is not None:
                partner_autoneg = self.get_ethtool_field("supports-auto-negotiation", remote=True)
                if partner_autoneg is None:
                    ksft_pr(f"Unable to fetch auto-negotiation status for interface {self.partner_netif}")
            return partner_autoneg

    def get_common_link_modes(self) -> set[str]:
        common_link_modes = []
        """Populate common link modes"""
        link_modes = self.get_ethtool_field("supported-link-modes")
        partner_link_modes = self.get_ethtool_field("link-partner-advertised-link-modes")
        if link_modes is None:
            raise KsftSkipEx(f"Link modes not available for {self.cfg.ifname}")
        if partner_link_modes is None:
            raise KsftSkipEx(f"Partner link modes not available for {self.cfg.ifname}")
        common_link_modes = set(link_modes) and set(partner_link_modes)
        return common_link_modes

    def get_speed_duplex_values(self, link_modes: list[str]) -> tuple[list[str], list[str]]:
        speed = []
        duplex = []
        """Check the link modes"""
        for data in link_modes:
            parts = data.split('/')
            speed_value = re.match(r'\d+', parts[0])
            if speed_value:
                speed.append(speed_value.group())
            else:
                ksft_pr(f"No speed value found for interface {self.ifname}")
                return None, None
            duplex.append(parts[1].lower())
        return speed, duplex

    def get_ethtool_field(self, field: str, remote: bool = False) -> Optional[str]:
        process = None
        if not remote:
            """Get the ethtool field value for the local interface"""
            try:
                process = ethtool(self.cfg.ifname, json=True)
            except Exception as e:
                ksft_pr("Required minimum ethtool version is 6.10")
                ksft_pr(f"Unexpected error occurred while getting ethtool field in local: {e}")
                return None
        else:
            if not self.verify_link_up():
                return None
            """Get the ethtool field value for the remote interface"""
            self.cfg.require_cmd("ethtool", remote=True)
            if self.partner_netif is None:
                ksft_pr(f"Partner interface name is unavailable.")
                return None
            try:
                process = ethtool(self.partner_netif, json=True, host=self.cfg.remote)
            except Exception as e:
                ksft_pr("Required minimum ethtool version is 6.10")
                ksft_pr(f"Unexpected error occurred while getting ethtool field in remote: {e}")
                return None
        json_data = process[0]
        """Check if the field exist in the json data"""
        if field not in json_data:
            raise KsftSkipEx(f'Field {field} does not exist in the output of interface {json_data["ifname"]}')
        return json_data[field]
