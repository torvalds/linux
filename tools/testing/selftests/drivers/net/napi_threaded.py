#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Test napi threaded states.
"""

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_ne, ksft_ge
from lib.py import NetDrvEnv, NetdevFamily
from lib.py import cmd, defer, ethtool


def _assert_napi_threaded_enabled(nl, napi_id) -> None:
    napi = nl.napi_get({'id': napi_id})
    ksft_eq(napi['threaded'], 'enabled')
    ksft_ne(napi.get('pid'), None)


def _assert_napi_threaded_disabled(nl, napi_id) -> None:
    napi = nl.napi_get({'id': napi_id})
    ksft_eq(napi['threaded'], 'disabled')
    ksft_eq(napi.get('pid'), None)


def _set_threaded_state(cfg, threaded) -> None:
    with open(f"/sys/class/net/{cfg.ifname}/threaded", "wb") as fp:
        fp.write(str(threaded).encode('utf-8'))


def _setup_deferred_cleanup(cfg) -> None:
    combined = ethtool(f"-l {cfg.ifname}", json=True)[0].get("combined", 0)
    ksft_ge(combined, 2)
    defer(ethtool, f"-L {cfg.ifname} combined {combined}")

    threaded = cmd(f"cat /sys/class/net/{cfg.ifname}/threaded").stdout
    defer(_set_threaded_state, cfg, threaded)

    return combined


def napi_init(cfg, nl) -> None:
    """
    Test that threaded state (in the persistent NAPI config) gets updated
    even when NAPI with given ID is not allocated at the time.
    """

    qcnt = _setup_deferred_cleanup(cfg)

    _set_threaded_state(cfg, 1)
    cmd(f"ethtool -L {cfg.ifname} combined 1")
    _set_threaded_state(cfg, 0)
    cmd(f"ethtool -L {cfg.ifname} combined {qcnt}")

    napis = nl.napi_get({'ifindex': cfg.ifindex}, dump=True)
    for napi in napis:
        ksft_eq(napi['threaded'], 'disabled')
        ksft_eq(napi.get('pid'), None)

    cmd(f"ethtool -L {cfg.ifname} combined 1")
    _set_threaded_state(cfg, 1)
    cmd(f"ethtool -L {cfg.ifname} combined {qcnt}")

    napis = nl.napi_get({'ifindex': cfg.ifindex}, dump=True)
    for napi in napis:
        ksft_eq(napi['threaded'], 'enabled')
        ksft_ne(napi.get('pid'), None)


def enable_dev_threaded_disable_napi_threaded(cfg, nl) -> None:
    """
    Test that when napi threaded is enabled at device level and
    then disabled at napi level for one napi, the threaded state
    of all napis is preserved after a change in number of queues.
    """

    napis = nl.napi_get({'ifindex': cfg.ifindex}, dump=True)
    ksft_ge(len(napis), 2)

    napi0_id = napis[0]['id']
    napi1_id = napis[1]['id']

    qcnt = _setup_deferred_cleanup(cfg)

    # set threaded
    _set_threaded_state(cfg, 1)

    # check napi threaded is set for both napis
    _assert_napi_threaded_enabled(nl, napi0_id)
    _assert_napi_threaded_enabled(nl, napi1_id)

    # disable threaded for napi1
    nl.napi_set({'id': napi1_id, 'threaded': 'disabled'})

    cmd(f"ethtool -L {cfg.ifname} combined 1")
    cmd(f"ethtool -L {cfg.ifname} combined {qcnt}")
    _assert_napi_threaded_enabled(nl, napi0_id)
    _assert_napi_threaded_disabled(nl, napi1_id)


def change_num_queues(cfg, nl) -> None:
    """
    Test that when napi threaded is enabled at device level,
    the napi threaded state is preserved after a change in
    number of queues.
    """

    napis = nl.napi_get({'ifindex': cfg.ifindex}, dump=True)
    ksft_ge(len(napis), 2)

    napi0_id = napis[0]['id']
    napi1_id = napis[1]['id']

    qcnt = _setup_deferred_cleanup(cfg)

    # set threaded
    _set_threaded_state(cfg, 1)

    # check napi threaded is set for both napis
    _assert_napi_threaded_enabled(nl, napi0_id)
    _assert_napi_threaded_enabled(nl, napi1_id)

    cmd(f"ethtool -L {cfg.ifname} combined 1")
    cmd(f"ethtool -L {cfg.ifname} combined {qcnt}")

    # check napi threaded is set for both napis
    _assert_napi_threaded_enabled(nl, napi0_id)
    _assert_napi_threaded_enabled(nl, napi1_id)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, queue_count=2) as cfg:
        ksft_run([napi_init,
                  change_num_queues,
                  enable_dev_threaded_disable_napi_threaded],
                 args=(cfg, NetdevFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
