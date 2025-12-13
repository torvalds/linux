#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests related to configuration of HW timestamping
"""

import errno
from lib.py import ksft_run, ksft_exit, ksft_ge, ksft_eq, KsftSkipEx
from lib.py import NetDrvEnv, EthtoolFamily, NlError


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


def __get_hwtimestamp_config(cfg):
    """ Retrieve current TS configuration information """

    try:
        tscfg = cfg.ethnl.tsconfig_get({'header': {'dev-name': cfg.ifname}})
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via netlink") from e
        raise
    return tscfg


def __set_hwtimestamp_config(cfg, ts):
    """ Setup new TS configuration information """

    ts['header'] = {'dev-name': cfg.ifname}
    try:
        res = cfg.ethnl.tsconfig_set(ts)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("timestamping configuration is not supported via netlink") from e
        raise
    return res


def test_hwtstamp_tx(cfg):
    """
    Test TX timestamp configuration.
    The driver should apply provided config and report back proper state.
    """

    orig_tscfg = __get_hwtimestamp_config(cfg)
    ts = __get_hwtimestamp_support(cfg)
    tx = ts['tx']
    for t in tx:
        tscfg = orig_tscfg
        tscfg['tx-types']['bits']['bit'] = [t]
        res = __set_hwtimestamp_config(cfg, tscfg)
        if res is None:
            res = __get_hwtimestamp_config(cfg)
        ksft_eq(res['tx-types']['bits']['bit'], [t])
    __set_hwtimestamp_config(cfg, orig_tscfg)


def test_hwtstamp_rx(cfg):
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
        tscfg = orig_tscfg
        tscfg['rx-filters']['bits']['bit'] = [r]
        res = __set_hwtimestamp_config(cfg, tscfg)
        if res is None:
            res = __get_hwtimestamp_config(cfg)
        if r['index'] == 0 or r['index'] == 1:
            ksft_eq(res['rx-filters']['bits']['bit'][0]['index'], r['index'])
        else:
            # the driver can fallback to some value which has higher coverage for timestamping
            ksft_ge(res['rx-filters']['bits']['bit'][0]['index'], r['index'])
    __set_hwtimestamp_config(cfg, orig_tscfg)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run([test_hwtstamp_tx, test_hwtstamp_rx], args=(cfg,))
        ksft_exit()


if __name__ == "__main__":
    main()
