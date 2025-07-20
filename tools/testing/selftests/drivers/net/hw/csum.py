#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Run the tools/testing/selftests/net/csum testsuite."""

from os import path

from lib.py import ksft_run, ksft_exit, KsftSkipEx
from lib.py import EthtoolFamily, NetDrvEpEnv
from lib.py import bkg, cmd, wait_port_listen

def test_receive(cfg, ipver="6", extra_args=None):
    """Test local nic checksum receive. Remote host sends crafted packets."""
    if not cfg.have_rx_csum:
        raise KsftSkipEx(f"Test requires rx checksum offload on {cfg.ifname}")

    ip_args = f"-{ipver} -S {cfg.remote_addr_v[ipver]} -D {cfg.addr_v[ipver]}"

    rx_cmd = f"{cfg.bin_local} -i {cfg.ifname} -n 100 {ip_args} -r 1 -R {extra_args}"
    tx_cmd = f"{cfg.bin_remote} -i {cfg.ifname} -n 100 {ip_args} -r 1 -T {extra_args}"

    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(34000, proto="udp")
        cmd(tx_cmd, host=cfg.remote)


def test_transmit(cfg, ipver="6", extra_args=None):
    """Test local nic checksum transmit. Remote host verifies packets."""
    if (not cfg.have_tx_csum_generic and
        not (cfg.have_tx_csum_ipv4 and ipver == "4") and
        not (cfg.have_tx_csum_ipv6 and ipver == "6")):
        raise KsftSkipEx(f"Test requires tx checksum offload on {cfg.ifname}")

    ip_args = f"-{ipver} -S {cfg.addr_v[ipver]} -D {cfg.remote_addr_v[ipver]}"

    # Cannot randomize input when calculating zero checksum
    if extra_args != "-U -Z":
        extra_args += " -r 1"

    rx_cmd = f"{cfg.bin_remote} -i {cfg.ifname} -L 1 -n 100 {ip_args} -R {extra_args}"
    tx_cmd = f"{cfg.bin_local} -i {cfg.ifname} -L 1 -n 100 {ip_args} -T {extra_args}"

    with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(34000, proto="udp", host=cfg.remote)
        cmd(tx_cmd)


def test_builder(name, cfg, ipver="6", tx=False, extra_args=""):
    """Construct specific tests from the common template.

       Most tests follow the same basic pattern, differing only in
       Direction of the test and optional flags passed to csum."""
    def f(cfg):
        cfg.require_ipver(ipver)

        if tx:
            test_transmit(cfg, ipver, extra_args)
        else:
            test_receive(cfg, ipver, extra_args)

    f.__name__ = f"ipv{ipver}_" + name
    return f


def check_nic_features(cfg) -> None:
    """Test whether Tx and Rx checksum offload are enabled.

       If the device under test has either off, then skip the relevant tests."""
    cfg.have_tx_csum_generic = False
    cfg.have_tx_csum_ipv4 = False
    cfg.have_tx_csum_ipv6 = False
    cfg.have_rx_csum = False

    ethnl = EthtoolFamily()
    features = ethnl.features_get({"header": {"dev-index": cfg.ifindex}})
    for f in features["active"]["bits"]["bit"]:
        if f["name"] == "tx-checksum-ip-generic":
            cfg.have_tx_csum_generic = True
        elif f["name"] == "tx-checksum-ipv4":
            cfg.have_tx_csum_ipv4 = True
        elif f["name"] == "tx-checksum-ipv6":
            cfg.have_tx_csum_ipv6 = True
        elif f["name"] == "rx-checksum":
            cfg.have_rx_csum = True


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        check_nic_features(cfg)

        cfg.bin_local = cfg.net_lib_dir / "csum"
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        cases = []
        for ipver in ["4", "6"]:
            cases.append(test_builder("rx_tcp", cfg, ipver, False, "-t"))
            cases.append(test_builder("rx_tcp_invalid", cfg, ipver, False, "-t -E"))

            cases.append(test_builder("rx_udp", cfg, ipver, False, ""))
            cases.append(test_builder("rx_udp_invalid", cfg, ipver, False, "-E"))

            cases.append(test_builder("tx_udp_csum_offload", cfg, ipver, True, "-U"))
            cases.append(test_builder("tx_udp_zero_checksum", cfg, ipver, True, "-U -Z"))

        ksft_run(cases=cases, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
