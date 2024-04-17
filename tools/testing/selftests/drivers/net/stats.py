#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_in, ksft_true, KsftSkipEx, KsftXfailEx
from lib.py import EthtoolFamily, NetdevFamily, RtnlFamily, NlError
from lib.py import NetDrvEnv

ethnl = EthtoolFamily()
netfam = NetdevFamily()
rtnl = RtnlFamily()


def check_pause(cfg) -> None:
    global ethnl

    try:
        ethnl.pause_get({"header": {"dev-index": cfg.ifindex}})
    except NlError as e:
        if e.error == 95:
            raise KsftXfailEx("pause not supported by the device")
        raise

    data = ethnl.pause_get({"header": {"dev-index": cfg.ifindex,
                                       "flags": {'stats'}}})
    ksft_true(data['stats'], "driver does not report stats")


def check_fec(cfg) -> None:
    global ethnl

    try:
        ethnl.fec_get({"header": {"dev-index": cfg.ifindex}})
    except NlError as e:
        if e.error == 95:
            raise KsftXfailEx("FEC not supported by the device")
        raise

    data = ethnl.fec_get({"header": {"dev-index": cfg.ifindex,
                                     "flags": {'stats'}}})
    ksft_true(data['stats'], "driver does not report stats")


def pkt_byte_sum(cfg) -> None:
    global netfam, rtnl

    def get_qstat(test):
        global netfam
        stats = netfam.qstats_get({}, dump=True)
        if stats:
            for qs in stats:
                if qs["ifindex"]== test.ifindex:
                    return qs

    qstat = get_qstat(cfg)
    if qstat is None:
        raise KsftSkipEx("qstats not supported by the device")

    for key in ['tx-packets', 'tx-bytes', 'rx-packets', 'rx-bytes']:
        ksft_in(key, qstat, "Drivers should always report basic keys")

    # Compare stats, rtnl stats and qstats must match,
    # but the interface may be up, so do a series of dumps
    # each time the more "recent" stats must be higher or same.
    def stat_cmp(rstat, qstat):
        for key in ['tx-packets', 'tx-bytes', 'rx-packets', 'rx-bytes']:
            if rstat[key] != qstat[key]:
                return rstat[key] - qstat[key]
        return 0

    for _ in range(10):
        rtstat = rtnl.getlink({"ifi-index": cfg.ifindex})['stats']
        if stat_cmp(rtstat, qstat) < 0:
            raise Exception("RTNL stats are lower, fetched later")
        qstat = get_qstat(cfg)
        if stat_cmp(rtstat, qstat) > 0:
            raise Exception("Qstats are lower, fetched later")


def main() -> None:
    with NetDrvEnv(__file__) as cfg:
        ksft_run([check_pause, check_fec, pkt_byte_sum],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
