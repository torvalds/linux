#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# pylint: disable=locally-disabled, invalid-name, attribute-defined-outside-init, too-few-public-methods

"""
Tests related to configuration of HW timestamping
"""

import errno
import ctypes
import fcntl
import socket
from lib.py import ksft_run, ksft_exit, ksft_ge, ksft_eq, KsftSkipEx
from lib.py import NetDrvEnv, EthtoolFamily, NlError


SIOCSHWTSTAMP = 0x89b0
SIOCGHWTSTAMP = 0x89b1
class hwtstamp_config(ctypes.Structure):
    """ Python copy of struct hwtstamp_config """
    _fields_ = [
        ("flags", ctypes.c_int),
        ("tx_type", ctypes.c_int),
        ("rx_filter", ctypes.c_int),
    ]


class ifreq(ctypes.Structure):
    """ Python copy of struct ifreq """
    _fields_ = [
        ("ifr_name", ctypes.c_char * 16),
        ("ifr_data", ctypes.POINTER(hwtstamp_config)),
    ]


def __get_hwtimestamp_support(cfg):
    """ Retrieve supported configuration information """

    try:
        tsinfo = cfg.ethnl.tsinfo_get({'header': {'dev-name': cfg.ifname}})
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported") from e
        raise

    ctx = {}
    tx = tsinfo.get('tx-types', {})
    rx = tsinfo.get('rx-filters', {})

    bits = tx.get('bits', {})
    ctx['tx'] = bits.get('bit', [])
    bits = rx.get('bits', {})
    ctx['rx'] = bits.get('bit', [])
    return ctx


def __get_hwtimestamp_config_ioctl(cfg):
    """ Retrieve current TS configuration information (via ioctl) """

    config = hwtstamp_config()

    req = ifreq()
    req.ifr_name = cfg.ifname.encode()
    req.ifr_data = ctypes.pointer(config)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        fcntl.ioctl(sock.fileno(), SIOCGHWTSTAMP, req)
        sock.close()

    except OSError as e:
        if e.errno == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via ioctl") from e
        raise
    return config


def __get_hwtimestamp_config(cfg):
    """ Retrieve current TS configuration information (via netLink) """

    try:
        tscfg = cfg.ethnl.tsconfig_get({'header': {'dev-name': cfg.ifname}})
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via netlink") from e
        raise
    return tscfg


def __set_hwtimestamp_config_ioctl(cfg, ts):
    """ Setup new TS configuration information (via ioctl) """
    config = hwtstamp_config()
    config.rx_filter = ts['rx-filters']['bits']['bit'][0]['index']
    config.tx_type = ts['tx-types']['bits']['bit'][0]['index']
    req = ifreq()
    req.ifr_name = cfg.ifname.encode()
    req.ifr_data = ctypes.pointer(config)
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        fcntl.ioctl(sock.fileno(), SIOCSHWTSTAMP, req)
        sock.close()

    except OSError as e:
        if e.errno == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via ioctl") from e
        raise


def __set_hwtimestamp_config(cfg, ts):
    """ Setup new TS configuration information (via netlink) """

    ts['header'] = {'dev-name': cfg.ifname}
    try:
        res = cfg.ethnl.tsconfig_set(ts)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via netlink") from e
        raise
    return res


def __perform_hwtstamp_tx(cfg, is_ioctl):
    """
    Test TX timestamp configuration via either netlink or ioctl.
    The driver should apply provided config and report back proper state.
    """

    orig_tscfg = __get_hwtimestamp_config(cfg)
    ts = __get_hwtimestamp_support(cfg)
    tx = ts['tx']
    for t in tx:
        res = None
        tscfg = orig_tscfg
        tscfg['tx-types']['bits']['bit'] = [t]
        if is_ioctl:
            __set_hwtimestamp_config_ioctl(cfg, tscfg)
        else:
            res = __set_hwtimestamp_config(cfg, tscfg)
        if res is None:
            res = __get_hwtimestamp_config(cfg)
        resioctl = __get_hwtimestamp_config_ioctl(cfg)
        ksft_eq(res['tx-types']['bits']['bit'], [t])
        ksft_eq(resioctl.tx_type, t['index'])
    __set_hwtimestamp_config(cfg, orig_tscfg)

def test_hwtstamp_tx_netlink(cfg):
    """
    Test TX timestamp configuration setup via netlink.
    The driver should apply provided config and report back proper state.
    """
    __perform_hwtstamp_tx(cfg, False)


def test_hwtstamp_tx_ioctl(cfg):
    """
    Test TX timestamp configuration setup via ioctl.
    The driver should apply provided config and report back proper state.
    """
    __perform_hwtstamp_tx(cfg, True)


def __perform_hwtstamp_rx(cfg, is_ioctl):
    """
    Test RX timestamp configuration.
    The filter configuration is taken from the list of supported filters.
    The driver should apply the config without error and report back proper state.
    Some extension of the timestamping scope is allowed for PTP filters.
    """

    orig_tscfg = __get_hwtimestamp_config(cfg)
    ts = __get_hwtimestamp_support(cfg)
    rx = ts['rx']
    for r in rx:
        res = None
        tscfg = orig_tscfg
        tscfg['rx-filters']['bits']['bit'] = [r]
        if is_ioctl:
            __set_hwtimestamp_config_ioctl(cfg, tscfg)
        else:
            res = __set_hwtimestamp_config(cfg, tscfg)
        if res is None:
            res = __get_hwtimestamp_config(cfg)
        resioctl = __get_hwtimestamp_config_ioctl(cfg)
        ksft_eq(resioctl.rx_filter, res['rx-filters']['bits']['bit'][0]['index'])
        if r['index'] == 0 or r['index'] == 1:
            ksft_eq(res['rx-filters']['bits']['bit'][0]['index'], r['index'])
        else:
            # the driver can fallback to some value which has higher coverage for timestamping
            ksft_ge(res['rx-filters']['bits']['bit'][0]['index'], r['index'])
    __set_hwtimestamp_config(cfg, orig_tscfg)


def test_hwtstamp_rx_netlink(cfg):
    """
    Test RX timestamp configuration via netlink.
    The filter configuration is taken from the list of supported filters.
    The driver should apply the config without error and report back proper state.
    Some extension of the timestamping scope is allowed for PTP filters.
    """
    __perform_hwtstamp_rx(cfg, False)


def test_hwtstamp_rx_ioctl(cfg):
    """
    Test RX timestamp configuration via ioctl.
    The filter configuration is taken from the list of supported filters.
    The driver should apply the config without error and report back proper state.
    Some extension of the timestamping scope is allowed for PTP filters.
    """
    __perform_hwtstamp_rx(cfg, True)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run([test_hwtstamp_tx_ioctl, test_hwtstamp_tx_netlink,
                  test_hwtstamp_rx_ioctl, test_hwtstamp_rx_netlink],
                 args=(cfg,))
        ksft_exit()


if __name__ == "__main__":
    main()
