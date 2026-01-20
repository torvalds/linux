#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Test channel and ring size configuration via ethtool (-L / -G).
"""

from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_eq
from lib.py import NetDrvEpEnv, EthtoolFamily, GenerateTraffic
from lib.py import defer, NlError


def channels(cfg) -> None:
    """
    Twiddle channel counts in various combinations of parameters.
    We're only looking for driver adhering to the requested config
    if the config is accepted and crashes.
    """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    chans = cfg.eth.channels_get(ehdr)

    all_keys = ["rx", "tx", "combined"]
    mixes = [{"combined"}, {"rx", "tx"}, {"rx", "combined"}, {"tx", "combined"},
             {"rx", "tx", "combined"},]

    # Get the set of keys that device actually supports
    restore = {}
    supported = set()
    for key in all_keys:
        if key + "-max" in chans:
            supported.add(key)
            restore |= {key + "-count": chans[key + "-count"]}

    defer(cfg.eth.channels_set, ehdr | restore)

    def test_config(config):
        try:
            cfg.eth.channels_set(ehdr | config)
            get = cfg.eth.channels_get(ehdr)
            for k, v in config.items():
                ksft_eq(get.get(k, 0), v)
        except NlError as e:
            failed.append(mix)
            ksft_pr("Can't set", config, e)
        else:
            ksft_pr("Okay", config)

    failed = []
    for mix in mixes:
        if not mix.issubset(supported):
            continue

        # Set all the values in the mix to 1, other supported to 0
        config = {}
        for key in all_keys:
            config[key + "-count"] = 1 if key in mix else 0
        test_config(config)

    for mix in mixes:
        if not mix.issubset(supported):
            continue
        if mix in failed:
            continue

        # Set all the values in the mix to max, other supported to 0
        config = {}
        for key in all_keys:
            config[key + "-count"] = chans[key + '-max'] if key in mix else 0
        test_config(config)


def _configure_min_ring_cnt(cfg) -> None:
    """ Try to configure a single Rx/Tx ring. """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    chans = cfg.eth.channels_get(ehdr)

    all_keys = ["rx-count", "tx-count", "combined-count"]
    restore = {}
    config = {}
    for key in all_keys:
        if key in chans:
            restore[key] = chans[key]
            config[key] = 0

    if chans.get('combined-count', 0) > 1:
        config['combined-count'] = 1
    elif chans.get('rx-count', 0) > 1 and chans.get('tx-count', 0) > 1:
        config['tx-count'] = 1
        config['rx-count'] = 1
    else:
        # looks like we're already on 1 channel
        return

    cfg.eth.channels_set(ehdr | config)
    defer(cfg.eth.channels_set, ehdr | restore)


def ringparam(cfg) -> None:
    """
    Tweak the ringparam configuration. Try to run some traffic over min
    ring size to make sure it actually functions.
    """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    rings = cfg.eth.rings_get(ehdr)

    restore = {}
    maxes = {}
    params = set()
    for key in rings.keys():
        if 'max' in key:
            param = key[:-4]
            maxes[param] = rings[key]
            params.add(param)
            restore[param] = rings[param]

    defer(cfg.eth.rings_set, ehdr | restore)

    # Speed up the reconfig by configuring just one ring
    _configure_min_ring_cnt(cfg)

    # Try to reach min on all settings
    for param in params:
        val = rings[param]
        while True:
            try:
                cfg.eth.rings_set({'header':{'dev-index': cfg.ifindex},
                                   param: val // 2})
                if val == 0:
                    break
                val //= 2
            except NlError:
                break

        get = cfg.eth.rings_get(ehdr)
        ksft_eq(get[param], val)

        ksft_pr(f"Reached min for '{param}' at {val} (max {rings[param]})")

    GenerateTraffic(cfg).wait_pkts_and_stop(10000)

    # Try max across all params, if the driver supports large rings
    # this may OOM so we ignore errors
    try:
        ksft_pr("Applying max settings")
        config = {p: maxes[p] for p in params}
        cfg.eth.rings_set(ehdr | config)
    except NlError as e:
        ksft_pr("Can't set max params", config, e)
    else:
        GenerateTraffic(cfg).wait_pkts_and_stop(10000)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__) as cfg:
        cfg.eth = EthtoolFamily()

        ksft_run([channels,
                  ringparam],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
