#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Run the tools/testing/selftests/net/csum testsuite."""

from os import path

from lib.py import ksft_run, ksft_exit, KsftSkipEx
from lib.py import EthtoolFamily, NetDrvEpEnv
from lib.py import bkg, cmd, wait_port_listen

def test_receive(cfg, ipv4=False, extra_args=None):
    """Test local nic checksum receive. Remote host sends crafted packets."""
    if not cfg.have_rx_csum:
        raise KsftSkipEx(f"Test requires rx checksum offload on {cfg.ifname}")

    if ipv4:
        ip_args = f"-4 -S {cfg.remote_v4} -D {cfg.v4}"
    else:
        ip_args = f"-6 -S {cfg.remote_v6} -D {cfg.v6}"

    rx_cmd = f"{cfg.bin_local} -i {cfg.ifname} -n 100 {ip_args} -r 1 -R {extra_args}"
    tx_cmd = f"{cfg.bin_remote} -i {cfg.ifname} -n 100 {ip_args} -r 1 -T {extra_args}"

    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(34000, proto="udp")
        cmd(tx_cmd, host=cfg.remote)


def test_transmit(cfg, ipv4=False, extra_args=None):
    """Test local nic checksum transmit. Remote host verifies packets."""
    if (not cfg.have_tx_csum_generic and
        not (cfg.have_tx_csum_ipv4 and ipv4) and
        not (cfg.have_tx_csum_ipv6 and not ipv4)):
        raise KsftSkipEx(f"Test requires tx checksum offload on {cfg.ifname}")

    if ipv4:
        ip_args = f"-4 -S {cfg.v4} -D {cfg.remote_v4}"
    else:
        ip_args = f"-6 -S {cfg.v6} -D {cfg.remote_v6}"

    # Cannot randomize input when calculating zero checksum
    if extra_args != "-U -Z":
        extra_args += " -r 1"

    rx_cmd = f"{cfg.bin_remote} -i {cfg.ifname} -L 1 -n 100 {ip_args} -R {extra_args}"
    tx_cmd = f"{cfg.bin_local} -i {cfg.ifname} -L 1 -n 100 {ip_args} -T {extra_args}"

    with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(34000, proto="udp", host=cfg.remote)
        cmd(tx_cmd)


def test_builder(name, cfg, ipv4=False, tx=False, extra_args=""):
    """Construct specific tests from the common template.

       Most tests follow the same basic pattern, differing only in
       Direction of the test and optional flags passed to csum."""
    def f(cfg):
        if ipv4:
            cfg.require_v4()
        else:
            cfg.require_v6()

        if tx:
            test_transmit(cfg, ipv4, extra_args)
        else:
            test_receive(cfg, ipv4, extra_args)

    if ipv4:
        f.__name__ = "ipv4_" + name
    else:
        f.__name__ = "ipv6_" + name
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

        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../net/lib/csum")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        cases = []
        for ipv4 in [True, False]:
            cases.append(test_builder("rx_tcp", cfg, ipv4, False, "-t"))
            cases.append(test_builder("rx_tcp_invalid", cfg, ipv4, False, "-t -E"))

            cases.append(test_builder("rx_udp", cfg, ipv4, False, ""))
            cases.append(test_builder("rx_udp_invalid", cfg, ipv4, False, "-E"))

            cases.append(test_builder("tx_udp_csum_offload", cfg, ipv4, True, "-U"))
            cases.append(test_builder("tx_udp_zero_checksum", cfg, ipv4, True, "-U -Z"))

        ksft_run(cases=cases, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
